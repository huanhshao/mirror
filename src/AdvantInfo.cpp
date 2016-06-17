#include <vector>
#include <iostream>
#include <process.h>
using namespace std;
#include <stdio.h>
#include <math.h>
#include "AdvantInfo.h"

AdvInfo::AdvInfo(){
	globle_mem_out_handle_=NULL;
	globle_mem_out_=nullptr;
	event_thread_handle_=NULL;
	upside_rate_=0.8;
	DEVLIST dev_list;
	SHORT dev_num;
	if (DRV_DeviceGetList(&dev_list,1,&dev_num)!=SUCCESS||dev_num==0){
		cout<<"Can't Find Device!"<<endl;
		exit(1);
	}
	cout<<"Device:"<<dev_list.dwDeviceNum<<' '<<dev_list.szDeviceName<<endl;
	ULONG used_dev_num=dev_list.dwDeviceNum;
	DWORD dwErrorCode;
	if ((dwErrorCode = DRV_DeviceOpen(used_dev_num, &driver_handle_)) != SUCCESS){
		ErrorHandler(dwErrorCode);
		exit(1);
	}
	SetInitState();
	GenerateBufferData();
	PT_EnableEvent ptEnableEvent;
	ptEnableEvent.EventType = 
		ADS_EVT_BUFCHANGE  |
		ADS_EVT_TERMINATED |
		ADS_EVT_OVERRUN;
	ptEnableEvent.Enabled = 1;
	ptEnableEvent.Count   = 1;
	if (((dwErrorCode = DRV_EnableEvent(driver_handle_,(LPT_EnableEvent)&ptEnableEvent))) != 0){
		ErrorHandler(dwErrorCode);
		DRV_DeviceClose((LONG far *)&driver_handle_);
		exit(0);
	}
	ep.driver_handle_=driver_handle_;
}
void AdvInfo::EnableSignal(){
	DWORD dwErrorCode;
	byte tmp=0x80;		//bit 7(DIO7) = 1 : enable
	if ((dwErrorCode=AdxDioWriteDoPorts(driver_handle_,0,1,&tmp))!=SUCCESS){
		ErrorHandler(dwErrorCode);
	}
}
void AdvInfo::DisableSignal(){
	DWORD dwErrorCode;
	byte tmp=0x00;		//bit 7(DIO7) = 1 : enable
	if ((dwErrorCode=AdxDioWriteDoPorts(driver_handle_,0,1,&tmp))!=SUCCESS){
		ErrorHandler(dwErrorCode);
	}
}
void AdvInfo::SetInitState(){
	DWORD dwErrorCode;
	DisableSignal();
	tagPT_AOVoltageOut tAOVoltageOut;
	for (int i=0;i<4;i++){
		tAOVoltageOut.chan=i;
		tAOVoltageOut.OutputValue=0.0;
		if((dwErrorCode = DRV_AOVoltageOut(driver_handle_, &tAOVoltageOut))!=SUCCESS){
			ErrorHandler(dwErrorCode);
		}
	}
	wave_channel_.resize(4);
	wave_channel_[0].Set(SMALL_SQUARE,2,2,480);
	wave_channel_[0].circle_period=4;
	wave_channel_[1].Set(WAVE_TRIANGLE,4,0,480);
	wave_channel_[2].Set(WAVE_SINE,0,0,480);	//Only test x-axis, put y to zero
	wave_channel_[3].Set(NO_WAVE,0,0,0);		//DO NOT enable channel 3 unless you know what you're doing
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
	//NOTE: For this unique usage, period=480, cir=4, channel count =3;
	pt_FAO_waveform_start_.TrigSrc			= 0;
	pt_FAO_waveform_start_.SampleRate		= 144000;
	pt_FAO_waveform_start_.Count			= wave_channel_[0].period*enalbed_channel_count_;
	pt_FAO_waveform_start_.WaveCount		= 0xFFFFFFFF;	// infinite
	pt_FAO_waveform_start_.Buffer			= (USHORT far *)globle_mem_out_;
	pt_FAO_waveform_start_.EnabledChannel	= enabled_channel_;
	if (event_thread_handle_!=NULL){
		cout<<"It's streaming!!! Can't open another stream!"<<endl;
		return false;
	}
	DWORD dwErrorCode= DRV_FAOWaveFormStart(driver_handle_,(LPT_FAOWaveFormStart)&pt_FAO_waveform_start_);
	if (dwErrorCode != SUCCESS){
		ErrorHandler(dwErrorCode);
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
	cout<<"Not Supported!!"<<endl;
	return;
	wave_channel_[channel].Set(WAVE_TRIANGLE,mag,off,pd);
	GenerateBufferData();
}
void AdvInfo::GenerateBufferData(){
	vector<float> voltage_buffer;
	vector<vector<USHORT>> binary_buffer(4);
	enabled_channel_=ADV_CHANNEL0|ADV_CHANNEL1|ADV_CHANNEL2;
	enalbed_channel_count_=3;
	for (int i=0;i<3;i++){
		SetRealBuffer(wave_channel_[i],voltage_buffer);
		ScaleVoltage(voltage_buffer,binary_buffer[i],i);
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
		exit(1);
	}
}
void AdvInfo::SetRealBuffer(const SWAVE& sw,vector<float>& buf){
	buf.resize(sw.period);
	switch (sw.waveform){
	case WAVE_SINE://to test use
		{
			for (int i = 0; i < sw.period; i++){
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
	case SMALL_SQUARE:
		{
			float low=sw.offset+sw.magnitude;
			float high=sw.offset-sw.magnitude;
			for (int i=0;i<sw.period;i+=sw.circle_period){
				for (int j=0;(j<sw.circle_period)&&(i+j<sw.period);j++){
					buf[i+j]=j >= sw.circle_period/2 ? low : high;
				}
			}
			break;
		}
	case WAVE_SQUARE:
	case SINE_TRIANGLE:
	//I don't support these waveform in my program
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
	//NOTE: if you use Multi channel, their period MUST BE THE SAME!!!
	int data_size=0;
	for (int i=0;i<bufs.size();i++){
		data_size=data_size<bufs[i].size() ? bufs[i].size() : data_size;
	}
	data_size*=enalbed_channel_count_;
	size_t mem_size=data_size*sizeof(USHORT);
	if (mem_size==0){
		cout<<"Unexpected Error, GenBuffer Failure!"<<endl;
		return;
	}
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
		if(enabled_channel_ & ADV_CHANNEL2)
		{
			globle_mem_out_[count++] = (bufs[2][i] | ( 0x02 << 12)) & 0x3fff;
		}
		if(enabled_channel_ & ADV_CHANNEL3)
		{
			globle_mem_out_[count++] = (bufs[3][i] | ( 0x03 << 12)) & 0x3fff;
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
