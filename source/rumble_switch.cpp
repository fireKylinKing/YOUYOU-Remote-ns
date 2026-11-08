#include "rumble_switch.hpp"
#ifdef __SWITCH__
#include "diagnostics.hpp"
#include <chrono>

namespace uu {
SwitchRumble::SwitchRumble(std::shared_ptr<RumbleInbox> inbox):inbox_(std::move(inbox)) {
    worker_=std::thread([this]{run();});
}
SwitchRumble::~SwitchRumble() {
    stop();quit_=true;worker_.join();
}
void SwitchRumble::stop() {
    std::lock_guard lock(mutex_);
    testAt_=0;inbox_->disable();
}
void SwitchRumble::update(const PadState &pad,bool remoteAllowed,bool testRequested,bool localAllowed) {
    const auto now=rumbleNow();
    uint32_t id=padIsHandheld(&pad)?HidNpadIdType_Handheld:HidNpadIdType_No1;
    uint32_t style=padIsConnected(&pad)?hidGetNpadStyleSet(HidNpadIdType(id)):0;
    // Only styles with documented vibration devices. Switch Lite's built-in
    // controls are rejected by HID; an external supported controller can work.
    style&=HidNpadStyleTag_NpadHandheld|HidNpadStyleTag_NpadJoyDual|
           HidNpadStyleTag_NpadJoyLeft|HidNpadStyleTag_NpadJoyRight|HidNpadStyleTag_NpadFullKey;
    std::lock_guard lock(mutex_);
    if(id_!=id || style_!=style) {inbox_->disable();testAt_=0;id_=id;style_=style;}
    updatedAt_=now;
    inbox_->heartbeat(remoteAllowed && style!=0,now);
    if(!localAllowed)testAt_=0;
    if(testRequested && localAllowed && style) {
        inbox_->disable();testEpoch_=inbox_->sample(now).epoch;testAt_=now;
        diagnostic("rumble-local-test-start");
    }
}
void SwitchRumble::run() {
    HidVibrationDeviceHandle handles[2]{};
    uint32_t activeId=0,activeStyle=0;
    int count=0;
    bool ready=false,applied=false;
    RumbleValue previous{};
    unsigned previousSides=0;
    auto send=[&](RumbleValue value,unsigned sides) {
        HidVibrationValue values[2]{};
        for(int i=0;i<count;++i) {
            values[i].freq_low=160.f;values[i].freq_high=320.f;
            if(sides & (1u<<i)) {
                values[i].amp_low=float(value.low)/255.f;
                values[i].amp_high=float(value.high)/255.f;
            }
        }
        return hidSendVibrationValues(handles,values,count);
    };
    auto stopOutput=[&] {
        if(ready && applied) {
            // Try each handle even if a paired send failed during hot-unplug.
            HidVibrationValue zero{};zero.freq_low=160.f;zero.freq_high=320.f;
            for(int i=0;i<count;++i)hidSendVibrationValue(handles[i],&zero);
        }
        applied=false;previous={};previousSides=0;
    };
    while(!quit_) {
        {
            // Serializes activation, local tests and output with stop().
            std::lock_guard lock(mutex_);
            const auto now=rumbleNow();
            if(now-updatedAt_>500) {testAt_=0;inbox_->disable();}
            auto sample=inbox_->sample(now);
            if(activeId!=id_ || activeStyle!=style_) {
                stopOutput();ready=false;activeId=id_;activeStyle=style_;
                count=(style_==HidNpadStyleTag_NpadJoyLeft || style_==HidNpadStyleTag_NpadJoyRight)?1:2;
                if(style_) {
                    ready=R_SUCCEEDED(hidInitializeVibrationDevices(handles,count,HidNpadIdType(id_),HidNpadStyleTag(style_)));
                    diagnostic(ready?"rumble-output-ready":"rumble-output-unavailable");
                }
            }
            RumbleValue desired=sample.enabled?sample.value:RumbleValue{};
            unsigned sides=3;
            if(testAt_) {
                const auto elapsed=now-testAt_;
                if(sample.epoch!=testEpoch_ || elapsed>=1100) {testAt_=0;desired={};}
                else if(elapsed<350) {desired={96,96};sides=1;}
                else if(elapsed>=600 && elapsed<950) {desired={96,96};sides=count==1?1:2;}
                else desired={};
            }
            if(ready && (desired!=previous || sides!=previousSides)) {
                // Set before sending so failure cleanup also attempts stop.
                applied=true;
                if(R_FAILED(send(desired,sides))) {
                    stopOutput();ready=false;inbox_->disable();
                    diagnostic("rumble-output-failed");
                } else {
                    if(desired!=RumbleValue{} && previous==RumbleValue{})diagnostic("rumble-output-applied");
                    previous=desired;previousSides=sides;
                    applied=desired!=RumbleValue{};
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    stopOutput();
}
} // namespace uu
#endif
