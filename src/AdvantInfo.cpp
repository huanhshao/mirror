#include <vector>
#include <iostream>
#include <process.h>
using namespace std;
#include <stdio.h>
#include <math.h>
#include "AdvantInfo.h"

namespace OCTProgram{
	AdvInfo::AdvInfo(){
		globle_mem_out_handle_=NULL;
		globle_mem_out_=nullptr;
		event_thread_handle_=NULL;
		upside_rate_=0.9;
		DEVLIST dev_list;
		SHORT dev_num;
		if (DRV_DeviceGetList(&dev_list,1,&dev_num)!=SUCCESS||dev_num==0){
			cout<<"Can't Find Device!"<<endl;
			exit(0);
		}
		cout<<"Device:"<<dev_list.dwDeviceNum<<' '<<dev_list.szDeviceName<<endl;
		ULONG used_dev_num=dev_list.dwDeviceNum;
		DWORD dwErrCde;
		if ((dwErrCde = DRV_DeviceOpen(used_dev_num, &driver_handle_)) != SUCCESS){
			ErrorHandler(dwErrCde);
			exit(0);
		}
		GenerateBufferData();
		PT_EnableEvent ptEnableEvent;
		ptEnableEvent.EventType = 
			ADS_EVT_BUFCHANGE  |
			ADS_EVT_TERMINATED |
			ADS_EVT_OVERRUN;
		ptEnableEvent.Enabled = 1;
		ptEnableEvent.Count   = 1;
		if (((dwErrCde = DRV_EnableEvent(driver_handle_,(LPT_EnableEvent)&ptEnableEvent))) != 0){
			ErrorHandler(dwErrCde);
			DRV_DeviceClose((LONG far *)&driver_handle_);
			exit(0);
		}
		ep.driver_handle_=driver_handle_;
	}
	AdvInfo::~AdvInfo(){
		StopWaveOut();
		if (globle_mem_out_handle_!=NULL){
			GlobalUnlock(globle_mem_out_handle_);
			GlobalFree(globle_mem_out_handle_);
			globle_mem_out_handle_=NULL;
		}
		if (driver_handle_){
			DRV_DeviceClose((LONG far *)&driver_handle_);
		}
	}
	bool AdvInfo::StartWaveOut(){
		//return true;
		if (globle_mem_out_handle_==NULL||globle_mem_out_==nullptr){
			cout<<"ERROR!!Can't Find Data Buffer!!"<<endl;
			return false;
		}
		pt_FAO_waveform_start_.TrigSrc			= 1;			// external trigger
		pt_FAO_waveform_start_.SampleRate		= 100000;		// pacer rate I think is useless
		pt_FAO_waveform_start_.Count			= wave_channel_x_.period*enalbed_channel_count_;
		pt_FAO_waveform_start_.WaveCount		= 0xFFFFFFFF;	// infinite
		pt_FAO_waveform_start_.Buffer			= (USHORT far *)globle_mem_out_;
		pt_FAO_waveform_start_.EnabledChannel	= enabled_channel_;
		if (event_thread_handle_!=NULL){
			cout<<"It's streaming!!! Can't open another stream!"<<endl;
			return false;
		}
		DWORD dwErrCde= DRV_FAOWaveFormStart(driver_handle_,(LPT_FAOWaveFormStart)&pt_FAO_waveform_start_);
		if (dwErrCde != SUCCESS){
			ErrorHandler(dwErrCde);
			return false;
		}
		event_thread_handle_ = (HANDLE)_beginthreadex(NULL, 0, &EVENTPROCESSOR, &ep, 0, NULL);
		return true;
	}
	bool AdvInfo::StopWaveOut(){
		if (event_thread_handle_==NULL){
			return true;
		}
		bool success=true;
		DWORD dwErrCde = DRV_FAOTerminate(driver_handle_);
		if (dwErrCde!=SUCCESS){
			ErrorHandler(dwErrCde);
			success=false;
		}
		WaitForSingleObject(event_thread_handle_, INFINITE);
		CloseHandle(event_thread_handle_);   // close thread handle
		event_thread_handle_ = NULL;
		return success;
	}
	void AdvInfo::SetSignal(double mag,double off,int pd,int channel){
		switch(channel){
		case 0:
			wave_channel_x_.Set(WAVE_TRIANGLE,mag,off,pd*2);
			break;
		case 1:
			wave_channel_y_.Set(WAVE_TRIANGLE,mag,off,pd);
			break;
		default:
			break;
		}
		GenerateBufferData();
	}
	void AdvInfo::GenerateBufferData(){
		enabled_channel_=0;
		enalbed_channel_count_=0;
		vector<float> voltage_buffer;
		vector<vector<USHORT>> binary_buffer(2);
		if(wave_channel_x_.waveform!= NO_WAVE){
			enabled_channel_ |= ADV_CHANNEL0;
			enalbed_channel_count_++;
			SetRealBuffer(wave_channel_x_,voltage_buffer);
			ScaleVoltage(voltage_buffer,binary_buffer[0],0);
		}
		if(wave_channel_y_.waveform!=NO_WAVE){
			enabled_channel_ |= ADV_CHANNEL1;
			enalbed_channel_count_++;
			SetRealBuffer(wave_channel_y_,voltage_buffer);
			ScaleVoltage(voltage_buffer,binary_buffer[1],1);
		}
		SetMultiToOneBuffer(binary_buffer);
	}
	void AdvInfo::ScaleVoltage(const vector<float>& vol_buf,vector<USHORT>& bny_buf, int channel){
		PT_FAOScale pt_FAO_scale;
		bny_buf.resize(vol_buf.size());
		pt_FAO_scale.VoltArray = const_cast<float*>(&(vol_buf[0]));
		pt_FAO_scale.BinArray = const_cast<USHORT*>(&(bny_buf[0]));
		pt_FAO_scale.chan = channel;
		pt_FAO_scale.count = vol_buf.size();
		DWORD dwErrCde;
		if ((dwErrCde=DRV_FAOScale(driver_handle_,(LPT_FAOScale)&pt_FAO_scale))!=SUCCESS){
			ErrorHandler(dwErrCde);
			DRV_DeviceClose(&driver_handle_);
			exit(0);
		}
	}
	void AdvInfo::SetRealBuffer(const SWAVE& sw,vector<float>& buf){
		buf.resize(sw.period);
		switch (sw.waveform){
		case WAVE_SINE://to test use
			{
				for (int i = 0; i < sw.period; i ++){
					buf[i] = static_cast<float>(sw.magnitude
						* sin(2*PI*(double)i/(double)sw.period)
						+ sw.offset);
				}
				break;
			}
		case WAVE_TRIANGLE:
			{
				int upside_time = static_cast<int>(sw.period*upside_rate_);
				float upslope = sw.magnitude * 2.0f / (float)(upside_time);
				float dnslope = sw.magnitude * 2.0f / (float)(sw.period-upside_time);
				for (int i = 0; i < sw.period; i++){
					if (i <= upside_time){
						buf[i] = upslope * i - sw.magnitude + sw.offset;
					}
					else{
						buf[i] = dnslope * (sw.period-i) - sw.magnitude + sw.offset;
					}
				}
				break;
			}
		case WAVE_SQUARE:
		case SINE_TRIANGLE:
		//I don't support this two waveform in my program
			break;
		default:
			break;
		}
	}
	void AdvInfo::SetMultiToOneBuffer(const vector<vector<USHORT>>& bufs){
		if (globle_mem_out_handle_!=NULL){
			GlobalUnlock(globle_mem_out_handle_);
			GlobalFree(globle_mem_out_handle_);
			globle_mem_out_handle_=NULL;
		}
		//NOTE: if you use two channel, their period must be the same!!!
		int data_size=max(bufs[0].size(),bufs[1].size())*enalbed_channel_count_;
		size_t mem_size=data_size*sizeof(USHORT);
		if (mem_size==0) return;
		if ((globle_mem_out_handle_ = (USHORT far *)GlobalAlloc(GHND, mem_size)) == NULL){
			cout<<"Unable to alloc memery!!"<<endl;
		}
		if((globle_mem_out_ = (USHORT far *)GlobalLock(globle_mem_out_handle_)) == nullptr){
			cout<<"Unable to alloc memery!!"<<endl;
			GlobalFree(globle_mem_out_handle_);
			globle_mem_out_handle_=NULL;
		}
		int count=0;
		for (int i=0;i<bufs[0].size();i++){
			if(enabled_channel_ & ADV_CHANNEL0){
				globle_mem_out_[count++] = bufs[0][i] & 0x0fff;
			}
			if(enabled_channel_ & ADV_CHANNEL1){
				globle_mem_out_[count++] = (bufs[1][i] | ( 0x01 << 12)) & 0x3fff;
			}
		}
		if (count!=data_size){
			cout<<"There must be something wrong in writing memery!!"<<endl;
		}
	}
	void AdvInfo::ErrorHandler(DWORD dwErrCde){
		char szErrMsg[180];
		DRV_GetErrorMessage(dwErrCde, szErrMsg);
		cout<<"Error("<<(dwErrCde & 0xffff)<<"):"<<szErrMsg<<endl;
	}
	unsigned __stdcall EVENTPROCESSOR(void* lpParam){
		EventPack* ep=reinterpret_cast<EventPack*>(lpParam);
		USHORT usEventType;
		PT_CheckEvent ptCheckEvent;
		while(true){
			// Check message
			ptCheckEvent.EventType = &usEventType;
			ptCheckEvent.Milliseconds = 100;
			DRV_CheckEvent(ep->driver_handle_, (LPT_CheckEvent)&ptCheckEvent);
			// Process buffer change event
			if (usEventType & ADS_EVT_BUFCHANGE){
			}
			// Process overrun event
			if (usEventType & ADS_EVT_OVERRUN){
			}
			// Process terminate event
			if (usEventType == ADS_EVT_TERMINATED){
				break;
			}
		}
		_endthreadex(0);
		return 0;
	}
}
