#include "rumble.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <algorithm>
#include <set>
#include <string>

namespace uu {
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
} // namespace uu
