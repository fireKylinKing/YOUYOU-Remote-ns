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
bool sawTestSide(bool right) {
    std::lock_guard lock(outputMutex);
    for(auto &o:outputs)if((o.handle%10==(right?1u:0u)) && o.value.amp_low>0)return true;
    return false;
}
}
namespace uu {void diagnostic(const char *) noexcept {}}
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
            waitFor([&]{return isLast(1.f,128.f/255);},"amplitude conversion");
            inbox->receive(1,{},rumbleNow());waitFor([&]{return isLast(0,0);},"zero stop");
            output.update(pad,true);inbox->receive(1,{255,0},rumbleNow());
            waitFor([&]{return isLast(1,0);},"nonzero before pause");
            output.stop();waitFor([&]{return isLast(0,0);},"focus/menu stop");
            output.update(pad,true);inbox->receive(1,{0,255},rumbleNow());
            waitFor([&]{return isLast(0,1);},"high channel");
            // Deliberately stop renewing the UI lease; worker must stop itself.
            waitFor([&]{return isLast(0,0);},"UI stall watchdog");
            output.update(pad,true);check(inbox->sample(rumbleNow()).value==RumbleValue{},"stall resume replay");
            inbox->receive(1,{255,255},rumbleNow());
            waitFor([&]{return isLast(1,1);},"pre-unplug output");
            pad.connected=false;output.update(pad,true);
            waitFor([&]{return isLast(0,0);},"unplug stop");
            pad.connected=true;pad.handheld=true;style=HidNpadStyleTag_NpadHandheld;
            output.update(pad,true);inbox->receive(1,{255,0},rumbleNow());
            waitFor([&]{return isLast(1,0);},"handheld rediscovery");
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
            waitFor([&]{return isLast(1,0);},"single Joy-Con output");
            check(initializedCount==1,"single Joy-Con handle count");
        }
        check(isLast(0,0),"destructor stop and join");
        std::cout<<"Switch rumble output lifecycle tests passed (simulated HID)\n";
    } catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
