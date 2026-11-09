#include "rumble_switch.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace uu;
namespace {
std::mutex outputMutex;
struct Output {uint32_t handle;HidVibrationValue value;};
std::vector<Output> outputs;
std::atomic<u32> style{HidNpadStyleTag_NpadJoyDual};
std::atomic<bool> fail{false};
std::atomic<int> attempts{0},initializedCount{0};
void check(bool ok,const char *m){if(!ok)throw std::runtime_error(m);}
template<class F> void waitFor(F f,const char *m) {
    auto until=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(!f() && std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(5));
    check(f(),m);
}
bool isLast(float low,float high) {
    std::lock_guard lock(outputMutex);
    return !outputs.empty() && std::abs(outputs.back().value.amp_low-low)<.001f &&
        std::abs(outputs.back().value.amp_high-high)<.001f;
}
bool isPair(float leftLow,float leftHigh,float rightLow,float rightHigh) {
    std::lock_guard lock(outputMutex);
    if(outputs.size()<2)return false;
    const auto &left=outputs[outputs.size()-2],&right=outputs.back();
    return left.handle%10==0 && right.handle%10==1 &&
        std::abs(left.value.amp_low-leftLow)<.001f && std::abs(left.value.amp_high-leftHigh)<.001f &&
        std::abs(right.value.amp_low-rightLow)<.001f && std::abs(right.value.amp_high-rightHigh)<.001f &&
        left.value.freq_low==160.f && left.value.freq_high==320.f &&
        right.value.freq_low==160.f && right.value.freq_high==320.f;
}
bool sawTestSide(bool right) {
    std::lock_guard lock(outputMutex);
    for(auto &o:outputs)if((o.handle%10==(right?1u:0u)) && o.value.amp_low>0)return true;
    return false;
}
}
namespace uu {
void diagnostic(const char *) noexcept {}
void rumbleDiagnostic(const char *) noexcept {}
bool flushRumbleLog() noexcept {return true;}
}
u32 hidGetNpadStyleSet(HidNpadIdType){return style;}
Result hidInitializeVibrationDevices(HidVibrationDeviceHandle *h,int n,HidNpadIdType id,HidNpadStyleTag) {
    initializedCount=n;
    for(int i=0;i<n;++i)h[i].value=uint32_t(id)*10+i;
    return 0;
}
Result hidSendVibrationValue(HidVibrationDeviceHandle h,const HidVibrationValue *v) {
    std::lock_guard lock(outputMutex);outputs.push_back({h.value,*v});return 0;
}
Result hidSendVibrationValues(const HidVibrationDeviceHandle *h,const HidVibrationValue *v,int n) {
    ++attempts;
    if(fail)return 1;
    for(int i=0;i<n;++i)hidSendVibrationValue(h[i],&v[i]);return 0;
}
int main() {
    try {
        auto inbox=std::make_shared<RumbleInbox>();inbox->connect(1);PadState pad;
        {
            SwitchRumble output(inbox);
            output.update(pad,true);inbox->receive(1,{255,128},rumbleNow());
            waitFor([&]{return isLast(.75f,128.f/255*.6f);},"comfort amplitude conversion");
            inbox->receive(1,{},rumbleNow());waitFor([&]{return isLast(0,0);},"zero stop");
            output.update(pad,true);inbox->receive(1,{255,0},rumbleNow());
            waitFor([&]{return isLast(.75f,0);},"nonzero before pause");
            output.stop();waitFor([&]{return isLast(0,0);},"focus/menu stop");
            output.update(pad,true);inbox->receive(1,{0,255},rumbleNow());
            waitFor([&]{return isLast(0,.6f);},"high channel");
            // Deliberately stop renewing the UI lease; worker must stop itself.
            waitFor([&]{return isLast(0,0);},"UI stall watchdog");
            output.update(pad,true);check(inbox->sample(rumbleNow()).value==RumbleValue{},"stall resume replay");
            inbox->receive(1,{255,255},rumbleNow());
            waitFor([&]{return isLast(.75f,.6f);},"pre-unplug output");
            pad.connected=false;output.update(pad,true);
            waitFor([&]{return isLast(0,0);},"unplug stop");
            pad.connected=true;pad.handheld=true;style=HidNpadStyleTag_NpadHandheld;
            output.update(pad,true);inbox->receive(1,{255,0},rumbleNow());
            waitFor([&]{return isLast(.75f,0);},"handheld rediscovery");
            output.update(pad,true);fail=true;inbox->receive(1,{128,128},rumbleNow());
            waitFor([&]{return isLast(0,0);},"output failure stop");
            const auto failedAttempts=attempts.load();fail=false;
            for(int i=0;i<5;++i){output.update(pad,true);std::this_thread::sleep_for(std::chrono::milliseconds(10));}
            check(attempts==failedAttempts,"output failure retry storm");
            pad.connected=false;output.update(pad,false);std::this_thread::sleep_for(std::chrono::milliseconds(30));
            {std::lock_guard lock(outputMutex);outputs.clear();}
            pad.connected=true;output.update(pad,false,true,true);
            for(int i=0;i<24;++i){output.update(pad,false,false,true);std::this_thread::sleep_for(std::chrono::milliseconds(50));}
            check(sawTestSide(false) && sawTestSide(true),"left/right local self-test");
            check(isLast(0,0),"self-test completion stop");
            pad.handheld=false;style=HidNpadStyleTag_NpadJoyRight;
            output.update(pad,true);inbox->receive(1,{255,0},rumbleNow());
            waitFor([&]{return isLast(.75f,0);},"single Joy-Con output");
            check(initializedCount==1,"single Joy-Con handle count");
        }
        check(isLast(0,0),"destructor stop and join");
        // Remapping must submit new device values even when the incoming
        // strengths have not changed, and must clear the other band's output.
        style=HidNpadStyleTag_NpadJoyDual;pad.handheld=false;pad.connected=true;
        inbox=std::make_shared<RumbleInbox>();inbox->connect(2);
        {
            RumbleOptions options;options.smoothMs=0;
            SwitchRumble output(inbox,options);
            auto update=[&](RumbleMapping mapping,bool allowed=true){output.update(pad,allowed,false,false,mapping);};
            update(RumbleMapping::Mixed);inbox->receive(2,{255,128},rumbleNow());
            const float high=128.f/255*.6f;
            waitFor([&]{return isPair(.75f,high,.75f,high);},"mixed sends both bands to both devices");
            update(RumbleMapping::Split);
            waitFor([&]{return isPair(.75f,0,0,high);},"live split remaps unchanged feedback and clears cross bands");
            update(RumbleMapping::Mixed);
            waitFor([&]{return isPair(.75f,high,.75f,high);},"live mixed remaps unchanged feedback");
            update(RumbleMapping::Split);inbox->receive(2,{0,255},rumbleNow());
            waitFor([&]{return isPair(0,0,0,.6f);},"high-only split vibrates right device");
            inbox->receive(2,{255,0},rumbleNow());
            waitFor([&]{return isPair(.75f,0,0,0);},"low-only split vibrates left device");
            inbox->receive(2,{},rumbleNow());
            waitFor([&]{return isPair(0,0,0,0);},"split zero stops both devices");
            inbox->receive(2,{255,255},rumbleNow());
            waitFor([&]{return isPair(.75f,0,0,.6f);},"split before disable");
            update(RumbleMapping::Mixed,false);
            waitFor([&]{return isPair(0,0,0,0);},"disabled remapping stays silent");
            check(!inbox->receive(2,{255,255},rumbleNow()),"disabled feedback rejected");
            update(RumbleMapping::Split);
            check(inbox->sample(rumbleNow()).value==RumbleValue{},"reenable after mapping change does not replay old feedback");
            for(auto controllerStyle:{HidNpadStyleTag_NpadHandheld,HidNpadStyleTag_NpadFullKey,HidNpadStyleTag_NpadJoyDual}) {
                style=controllerStyle;pad.handheld=controllerStyle==HidNpadStyleTag_NpadHandheld;
                update(RumbleMapping::Split);inbox->receive(2,{255,255},rumbleNow());
                waitFor([&]{return isPair(.75f,0,0,.6f);},"split handheld/Pro/dual devices");
            }
            pad.handheld=false;
            for(auto controllerStyle:{HidNpadStyleTag_NpadJoyLeft,HidNpadStyleTag_NpadJoyRight}) {
                style=controllerStyle;update(RumbleMapping::Split);inbox->receive(2,{255,255},rumbleNow());
                waitFor([&]{return initializedCount==1 && isLast(.75f,.6f);},"single Joy-Con retains both bands in split setting");
            }
            style=HidNpadStyleTag_NpadJoyDual;
            output.update(pad,false,true,true,RumbleMapping::Split);
            waitFor([&]{return isPair(96.f/255,96.f/255,0,0);},"local side test bypasses split mapping");
            output.stop();waitFor([&]{return isPair(0,0,0,0);},"split self-test stop");
        }
        std::cout<<"Switch rumble output lifecycle tests passed (simulated HID)\n";
    } catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
