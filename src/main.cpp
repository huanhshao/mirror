#include <stdio.h>
#include <iostream>
using namespace std;
#include <process.h>
#include "AdvantInfo.h"
void WaitForKeyPress(){
	char tmp;
	tmp=cin.get();
}
int main(){
	AdvInfo adv;
	adv.StartWaveOut();
	cout<<"Prepare Over."<<endl;
	WaitForKeyPress();
	adv.EnableSignal();
	cout<<"Signal Enabled."<<endl;
	WaitForKeyPress();
	adv.DisableSignal();
	adv.StopWaveOut();
	cout<<"Stopped."<<endl;
	return 0;
}