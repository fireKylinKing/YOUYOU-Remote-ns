#include "rumble.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <algorithm>
#include <set>
#include <string>

namespace uu {
RumbleShaper::RumbleShaper(RumbleOptions options):options_(options) {
    options_.strength=std::clamp(options_.strength,0,100);
    options_.lowGain=std::clamp(options_.lowGain,0,100);
    options_.highGain=std::clamp(options_.highGain,0,100);
    options_.smoothMs=std::clamp(options_.smoothMs,0,100);
}
RumbleAmplitude RumbleShaper::advance(RumbleValue value,uint64_t elapsedMs) {
    const float gain=float(options_.strength)/100.f;
    const float step=options_.smoothMs?std::min(1.f,float(elapsedMs)/options_.smoothMs):1.f;
    auto channel=[&](uint8_t raw,int channelGain,float current) {
        const float target=float(raw)/255.f*gain*float(channelGain)/100.f;
        if(target==0.f)return 0.f;
        return current+std::clamp(target-current,-step,step);
    };
    current_={channel(value.low,options_.lowGain,current_.low),
              channel(value.high,options_.highGain,current_.high)};
    return current_;
}
std::optional<RumbleValue> parseRumble(const uint8_t *bytes,size_t size) {
    if(!bytes || !size || size>512)return std::nullopt;
    if(std::find(bytes,bytes+size,uint8_t(0))!=bytes+size)return std::nullopt;
    try {
        // Reject nested containers and duplicate keys before DOM overwrites them.
        bool invalid=false;
        std::set<std::string> keys;
        using Json=nlohmann::json;
        auto callback=[&](int depth,Json::parse_event_t event,Json &value){
            if(event==Json::parse_event_t::object_start && depth!=0)invalid=true;
            if(event==Json::parse_event_t::array_start)invalid=true;
            if(event==Json::parse_event_t::key && !keys.insert(value.get<std::string>()).second)invalid=true;
            return true;
        };
        auto j=Json::parse(bytes,bytes+size,callback,false);
        if(invalid || !j.is_object() || j.size()!=4 ||
           !j.contains("action") || !j["action"].is_string() || j["action"]!="gpd_vibration")return std::nullopt;
        auto byte=[&](const char *key)->std::optional<uint8_t>{
            if(!j.contains(key) || !j[key].is_number_integer())return std::nullopt;
            const auto &v=j[key];
            if(v.is_number_unsigned()) {
                auto n=v.get<uint64_t>();if(n>255)return std::nullopt;
                return uint8_t(n);
            }
            auto n=v.get<int64_t>();if(n<0 || n>255)return std::nullopt;
            return uint8_t(n);
        };
        auto index=byte("index"),low=byte("left_motor"),high=byte("right_motor");
        if(!index || *index!=0 || !low || !high)return std::nullopt;
        return RumbleValue{*low,*high};
    } catch(...) { return std::nullopt; }
}
uint64_t rumbleNow() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
void RumbleInbox::clear() {
    desired_={};pending_=false;enabled_=false;++epoch_;
    havePreviousReceive_=false;
}
void RumbleInbox::connect(uint64_t generation) {
    std::lock_guard lock(mutex_);clear();generation_=generation;connected_=true;
}
void RumbleInbox::disconnect(uint64_t generation) {
    std::lock_guard lock(mutex_);
    if(generation && generation!=generation_)return;
    connected_=false;clear();
}
void RumbleInbox::disable() { std::lock_guard lock(mutex_);clear(); }
void RumbleInbox::heartbeat(bool enabled,uint64_t now) {
    std::lock_guard lock(mutex_);
    if(!enabled || !connected_) {if(enabled_)clear();return;}
    // A stalled UI cannot resume a previously active effect.
    if(enabled_ && now-heartbeat_>500)clear();
    enabled_=true;heartbeat_=now;
}
bool RumbleInbox::receive(uint64_t generation,RumbleValue value,uint64_t now) {
    std::lock_guard lock(mutex_);
    if(!connected_ || !enabled_ || generation!=generation_ || now-heartbeat_>500)return false;
    if(tracing_) {
        if(traceSize_==trace_.size()) {
            traceHead_=(traceHead_+1)%trace_.size();--traceSize_;++traceDropped_;
        }
        trace_[(traceHead_+traceSize_)%trace_.size()]={++traceSequence_,now,
            havePreviousReceive_?now-previousReceive_:0,epoch_,value,pending_};
        ++traceSize_;
    }
    previousReceive_=now;havePreviousReceive_=true;
    desired_=value;receivedAt_=now;pending_=true;
    return true;
}
RumbleSample RumbleInbox::sample(uint64_t now) {
    std::lock_guard lock(mutex_);
    if(enabled_ && now-heartbeat_>500)clear();
    if(pending_) {
        // Stop messages always take effect. Do not replay delayed nonzero input.
        if(now-receivedAt_>500 && desired_!=RumbleValue{})desired_={};
        pending_=false;
    }
    return {desired_,epoch_,connected_ && enabled_};
}
void RumbleInbox::setTracing(bool enabled) {
    std::lock_guard lock(mutex_);
    tracing_=enabled;traceHead_=traceSize_=0;traceDropped_=traceSequence_=0;
    havePreviousReceive_=false;
}
RumbleTraceBatch RumbleInbox::takeTrace() {
    std::lock_guard lock(mutex_);
    RumbleTraceBatch batch;batch.dropped=traceDropped_;
    batch.feedback.reserve(traceSize_);
    for(size_t i=0;i<traceSize_;++i)batch.feedback.push_back(trace_[(traceHead_+i)%trace_.size()]);
    traceHead_=traceSize_=0;traceDropped_=0;
    return batch;
}
} // namespace uu
