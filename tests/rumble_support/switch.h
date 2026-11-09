#pragma once
#include <cstdint>
// Host test double for the libnx signatures used by rumble_switch.cpp.
using u32=uint32_t;
using Result=uint32_t;
enum HidNpadIdType { HidNpadIdType_No1=0,HidNpadIdType_Handheld=0x20 };
enum HidNpadStyleTag {
    HidNpadStyleTag_NpadFullKey=1,HidNpadStyleTag_NpadHandheld=2,
    HidNpadStyleTag_NpadJoyDual=4,HidNpadStyleTag_NpadJoyLeft=8,HidNpadStyleTag_NpadJoyRight=16
};
struct HidVibrationDeviceHandle {uint32_t value;};
struct HidVibrationValue {float amp_low,freq_low,amp_high,freq_high;};
struct PadState {bool handheld=false,connected=true;};
inline bool padIsHandheld(const PadState *p) {return p->handheld;}
inline bool padIsConnected(const PadState *p) {return p->connected;}
inline bool R_SUCCEEDED(Result r) {return r==0;}
inline bool R_FAILED(Result r) {return r!=0;}
u32 hidGetNpadStyleSet(HidNpadIdType);
Result hidInitializeVibrationDevices(HidVibrationDeviceHandle *,int,HidNpadIdType,HidNpadStyleTag);
Result hidSendVibrationValue(HidVibrationDeviceHandle,const HidVibrationValue *);
Result hidSendVibrationValues(const HidVibrationDeviceHandle *,const HidVibrationValue *,int);
