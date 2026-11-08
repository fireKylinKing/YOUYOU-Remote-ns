#include "rumble_switch.hpp"
#ifdef __SWITCH__
#include "diagnostics.hpp"
#include <chrono>
#include <cstdio>

namespace uu {
SwitchRumble::SwitchRumble(std::shared_ptr<RumbleInbox> inbox,RumbleOptions options):
    inbox_(std::move(inbox)),options_(options) {
    inbox_->setTracing(options_.trace);
    worker_=std::thread([this]{run();});
}
SwitchRumble::~SwitchRumble() {
    stop();quit_=true;worker_.join();
    if(options_.trace)flushRumbleLog();
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
    RumbleAmplitude previous{};
    RumbleShaper shaper(options_);
    uint64_t previousAt=rumbleNow(),activeEpoch=0;
    unsigned previousSides=0;
    if(options_.trace) {
        char line[192];
        std::snprintf(line,sizeof(line),"rumble-config strength_pct=%d low_pct=%d high_pct=%d smooth_ms=%d tick_ms=5",
            options_.strength,options_.lowGain,options_.highGain,options_.smoothMs);
        rumbleDiagnostic(line);
    }
    auto send=[&](RumbleAmplitude value,unsigned sides) {
        HidVibrationValue values[2]{};
        for(int i=0;i<count;++i) {
            values[i].freq_low=160.f;values[i].freq_high=320.f;
            if(sides & (1u<<i)) {
                values[i].amp_low=value.low;
                values[i].amp_high=value.high;
            }
        }
        return hidSendVibrationValues(handles,values,count);
    };
    auto stopOutput=[&](const char *reason) {
        if(ready && applied) {
            // Try each handle even if a paired send failed during hot-unplug.
            HidVibrationValue zero{};zero.freq_low=160.f;zero.freq_high=320.f;
            for(int i=0;i<count;++i)hidSendVibrationValue(handles[i],&zero);
            if(options_.trace) {
                char line[192];
                std::snprintf(line,sizeof(line),"rumble-stop-attempt t_ms=%llu out_low=0 out_high=0 reason=%s",
                    static_cast<unsigned long long>(rumbleNow()),reason);
                rumbleDiagnostic(line);
            }
        }
        applied=false;previous={};previousSides=0;shaper.reset();
    };
    while(!quit_) {
        char outputLine[256]{};
        {
            // Serializes activation, local tests and output with stop().
            std::lock_guard lock(mutex_);
            const auto now=rumbleNow();
            const auto elapsedMs=now-previousAt;previousAt=now;
            if(now-updatedAt_>500) {testAt_=0;inbox_->disable();}
            auto sample=inbox_->sample(now);
            if(sample.epoch!=activeEpoch) {shaper.reset();activeEpoch=sample.epoch;}
            if(activeId!=id_ || activeStyle!=style_) {
                stopOutput("route");ready=false;activeId=id_;activeStyle=style_;
                count=(style_==HidNpadStyleTag_NpadJoyLeft || style_==HidNpadStyleTag_NpadJoyRight)?1:2;
                if(style_) {
                    ready=R_SUCCEEDED(hidInitializeVibrationDevices(handles,count,HidNpadIdType(id_),HidNpadStyleTag(style_)));
                    diagnostic(ready?"rumble-output-ready":"rumble-output-unavailable");
                }
            }
            RumbleValue desired=sample.enabled?sample.value:RumbleValue{};
            unsigned sides=3;
            bool localTest=false;
            if(testAt_) {
                const auto elapsed=now-testAt_;
                if(sample.epoch!=testEpoch_ || elapsed>=1100) {testAt_=0;desired={};}
                else if(elapsed<350) {desired={96,96};sides=1;}
                else if(elapsed>=600 && elapsed<950) {desired={96,96};sides=count==1?1:2;}
                else desired={};
                localTest=testAt_!=0;
            }
            RumbleAmplitude amplitude;
            if(localTest) {
                shaper.reset();amplitude={float(desired.low)/255.f,float(desired.high)/255.f};
            } else amplitude=shaper.advance(desired,elapsedMs);
            if(ready && (amplitude.low!=previous.low || amplitude.high!=previous.high || sides!=previousSides)) {
                // Set before sending so failure cleanup also attempts stop.
                applied=true;
                if(R_FAILED(send(amplitude,sides))) {
                    stopOutput("send-failed");ready=false;inbox_->disable();
                    diagnostic("rumble-output-failed");
                } else {
                    if(desired!=RumbleValue{} && previous.low==0 && previous.high==0)diagnostic("rumble-output-applied");
                    previous=amplitude;previousSides=sides;
                    applied=desired!=RumbleValue{};
                    if(options_.trace)std::snprintf(outputLine,sizeof(outputLine),
                        "rumble-out t_ms=%llu raw_low=%u raw_high=%u out_low=%.4f out_high=%.4f sides=%u mode=%s",
                        static_cast<unsigned long long>(now),unsigned(desired.low),unsigned(desired.high),
                        double(amplitude.low),double(amplitude.high),sides,localTest?"test":"remote");
                }
            }
        }
        // The callback only appends numeric records to a fixed ring. Formatting
        // and bounded async logging happen after HID output, outside its lock.
        if(options_.trace) {
            auto trace=inbox_->takeTrace();
            char line[256];
            if(trace.dropped) {
                std::snprintf(line,sizeof(line),"rumble-rx-dropped count=%llu",static_cast<unsigned long long>(trace.dropped));
                rumbleDiagnostic(line);
            }
            for(const auto &event:trace.feedback) {
                std::snprintf(line,sizeof(line),
                    "rumble-rx t_ms=%llu seq=%llu epoch=%llu low=%u high=%u dt_ms=%llu replaced=%d",
                    static_cast<unsigned long long>(event.at),static_cast<unsigned long long>(event.sequence),
                    static_cast<unsigned long long>(event.epoch),unsigned(event.value.low),unsigned(event.value.high),
                    static_cast<unsigned long long>(event.interval),event.replacedPending?1:0);
                rumbleDiagnostic(line);
            }
            if(outputLine[0])rumbleDiagnostic(outputLine);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    stopOutput("shutdown");
}
} // namespace uu
#endif
