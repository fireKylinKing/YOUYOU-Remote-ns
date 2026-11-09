#include "rumble.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <cmath>
using namespace uu;
void check(bool ok,const char *message) {if(!ok)throw std::runtime_error(message);}
std::optional<RumbleValue> parse(const std::string &s) {
    return parseRumble(reinterpret_cast<const uint8_t *>(s.data()),s.size());
}
int main() {
    try {
        RumbleShaper shaper;
        auto amplitude=shaper.advance({255,255},5);
        check(std::abs(amplitude.low-.25f)<.0001f && std::abs(amplitude.high-.25f)<.0001f,"bounded attack");
        amplitude=shaper.advance({255,255},15);
        check(std::abs(amplitude.low-.75f)<.0001f && std::abs(amplitude.high-.6f)<.0001f,"comfort gains");
        amplitude=shaper.advance({128,255},1);
        check(std::abs(amplitude.low-.7f)<.0001f,"nonzero transition slew");
        amplitude=shaper.advance({0,255},0);
        check(amplitude.low==0 && std::abs(amplitude.high-.6f)<.0001f,"per-channel zero bypasses smoothing");
        amplitude=shaper.advance({},0);
        check(amplitude.low==0 && amplitude.high==0,"zero stop has no tail");
        RumbleShaper original({100,100,100,0,false});
        amplitude=original.advance({255,128},0);
        check(amplitude.low==1 && std::abs(amplitude.high-128.f/255)<.0001f,"original mapping configurable");
        RumbleShaper clamped({500,-10,500,-1,false});
        amplitude=clamped.advance({255,255},5);
        check(amplitude.low==0 && amplitude.high==1,"options bounds");
        shaper.advance({255,255},100);shaper.reset();
        amplitude=shaper.advance({255,255},0);
        check(amplitude.low==0 && amplitude.high==0,"reset cannot replay amplitude");
        RumbleInbox traced;traced.connect(8);traced.setTracing(true);traced.heartbeat(true,100);
        traced.receive(8,{7,9},101);traced.receive(8,{},109);
        traced.sample(109);traced.receive(8,{11,13},111);
        check(!traced.receive(7,{255,255},112),"trace rejects stale session");
        auto trace=traced.takeTrace();
        check(trace.feedback.size()==3 && trace.dropped==0,"all received values traced");
        check(trace.feedback[0].interval==0 && trace.feedback[1].interval==8 && trace.feedback[2].interval==2,"receive intervals");
        check(!trace.feedback[0].replacedPending && trace.feedback[1].replacedPending && !trace.feedback[2].replacedPending,"short pulse overwrite observable");
        check(trace.feedback[1].value==RumbleValue{},"zero feedback traced");
        check(traced.takeTrace().feedback.empty(),"trace drain");
        for(int i=0;i<1000;++i)traced.receive(8,{uint8_t(i%256),42},120);
        traced.disable();trace=traced.takeTrace();
        check(trace.feedback.size()==512 && trace.dropped==488 && trace.feedback.back().sequence==1003,"bounded trace and explicit drops");
        traced.setTracing(false);traced.heartbeat(true,130);traced.receive(8,{1,2},131);
        check(traced.takeTrace().feedback.empty(),"trace off");
        const std::string valid=R"({"action":"gpd_vibration","index":0,"left_motor":255,"right_motor":90})";
        auto v=parse(valid);check(v && *v==RumbleValue{255,90},"wire values");
        check(parse(R"({"right_motor":0,"left_motor":0,"index":0,"action":"gpd_vibration"})")->low==0,"zero/order");
        for(const auto &bad:{"", "null", "[]", "{}", "true",
            R"({"action":"gpd_vibration","index":1,"left_motor":0,"right_motor":0})",
            R"({"action":"gpd_vibration","index":0,"left_motor":256,"right_motor":0})",
            R"({"action":"gpd_vibration","index":0,"left_motor":-1,"right_motor":0})",
            R"({"action":"gpd_vibration","index":0,"left_motor":1.0,"right_motor":0})",
            R"({"action":"gpd_vibration","index":false,"left_motor":1,"right_motor":0})",
            R"({"action":"gpd_vibration","index":0,"left_motor":"1","right_motor":0})",
            R"({"action":"gpd_vibration","index":0,"left_motor":18446744073709551615,"right_motor":0})",
            R"({"action":"gpd_vibration","index":0,"left_motor":1e100,"right_motor":0})",
            R"({"action":"gpd_vibration","index":0,"left_motor":0,"right_motor":null})",
            R"({"action":"gpd_vibration","index":0,"left_motor":{},"right_motor":0})",
            R"({"action":"gpd_vibration","index":0,"left_motor":[],"right_motor":0})",
            R"({"action":"gpd_vibration","index":0,"left_motor":1,"left_motor":2,"right_motor":0})",
            R"({"action":"gpd_vibration","index":0,"left_motor":1,"right_motor":0,"extra":0})",
            R"({"action":"gpd_update","index":0,"left_motor":0,"right_motor":0})"})check(!parse(bad),"malformed accepted");
        check(!parse(valid+" trailing"),"trailing bytes");
        check(!parse(valid+std::string(513,' ')),"oversize");
        check(!parse(valid+std::string(1,'\0')+"garbage"),"embedded null");
        check(!parseRumble(nullptr,12),"null buffer");
        RumbleInbox inbox;
        check(!inbox.receive(1,{1,2},100),"inactive peer");
        inbox.connect(1);
        check(!inbox.receive(1,{1,2},100),"local gate");
        inbox.heartbeat(true,100);
        check(!inbox.receive(2,{1,2},100),"wrong generation");
        for(int i=0;i<10000;++i)check(inbox.receive(1,{uint8_t(i%256),42},101),"flood");
        auto s=inbox.sample(102);check(s.enabled && s.value==RumbleValue{15,42},"latest only");
        // Hosts may send only changes: UI heartbeats preserve sustained effects.
        for(uint64_t t=200;t<=3000;t+=100){inbox.heartbeat(true,t);check(inbox.sample(t).value==s.value,"sustained effect");}
        check(inbox.receive(1,{},3001),"zero feedback");
        check(inbox.sample(3001).value==RumbleValue{},"stop immediately");
        inbox.receive(1,{255,255},3002);inbox.disable();
        check(!inbox.sample(3002).enabled,"pause");
        inbox.heartbeat(true,3003);check(inbox.sample(3003).value==RumbleValue{},"pause replay");
        inbox.receive(1,{42,42},3004);inbox.disconnect();inbox.connect(2);inbox.heartbeat(true,3005);
        check(!inbox.receive(1,{255,255},3005),"old session callback");
        inbox.disconnect(1);check(inbox.sample(3005).enabled,"old close killed new session");
        inbox.receive(2,{255,0},3006);inbox.sample(3006);
        check(!inbox.sample(3506).enabled,"UI watchdog");
        inbox.heartbeat(true,3507);check(inbox.sample(3507).value==RumbleValue{},"watchdog replay");
        inbox.receive(2,{1,1},3508);inbox.heartbeat(true,4000);
        check(inbox.sample(4010).value==RumbleValue{},"stale pending command");
        // Network/UI/worker race under the same mutex; final close must win.
        std::thread producer([&]{for(int i=0;i<20000;++i)inbox.receive(2,{1,2},4100);});
        for(int i=0;i<20000;++i){inbox.heartbeat(i%3!=0,4100);inbox.sample(4100);}
        producer.join();inbox.disconnect();check(!inbox.sample(4100).enabled,"final close");
        std::cout<<"Rumble protocol and lifecycle tests passed\n";
    } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
