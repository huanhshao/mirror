#pragma once
#include <vector>
#include <Windows.h>
#include "api/Advantech/Driver.h"
#define PI 3.1415926

enum WAVE_TYPE { WAVE_SINE = 0, WAVE_TRIANGLE, WAVE_SQUARE, SINE_TRIANGLE, NO_WAVE, SMALL_SQUARE};
typedef struct _SWAVE{
	WAVE_TYPE waveform;
	float magnitude;
	float offset;
	int period;
	int circle_period;
	_SWAVE():waveform(NO_WAVE),magnitude(0),offset(0),period(2048),circle_period(0){}
	_SWAVE(WAVE_TYPE wf,float mag,float off,int pd){Set(wf,mag,off,pd);}
	void Set(WAVE_TYPE wf,float mag,float off,int pd){
		waveform=wf;
		magnitude=mag;
		offset=off;
		period=pd;
	}
} SWAVE, FAR *LPSWAVE;
struct EventPack{
	long driver_handle_;
};
class AdvInfo{
public:
	AdvInfo();
	~AdvInfo();
	void SetSignal(double mag,double off,int pd,int channel=0);
	bool StartWaveOut();
	bool StopWaveOut();
	void EnableSignal();
	void DisableSignal();
private:
	void SetRealBuffer(const SWAVE& sw,std::vector<float>& buf);
	void ScaleVoltage(const std::vector<float>& vol_buf,std::vector<USHORT>& bny_buf,int channel);
	void SetMultiToOneBuffer(const std::vector<vector<USHORT>>& bufs);
	void ErrorHandler(DWORD dwErrCde);
	void GenerateBufferData();
	void SetInitState();
private:
	//I just use x, but I leaved a y for further use!
	std::vector<SWAVE> wave_channel_;
	long driver_handle_;
	long signal_period_;
	double upside_rate_;
	USHORT enabled_channel_;
	USHORT enalbed_channel_count_;
	PT_FAOWaveFormStart pt_FAO_waveform_start_;
	HGLOBAL globle_mem_out_handle_;
	USHORT far* globle_mem_out_;
	HANDLE event_thread_handle_;
	EventPack ep;
};
unsigned __stdcall EVENTPROCESSOR(void* lpParam);
