#pragma once
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>

namespace uu {
struct RumbleValue {
    uint8_t low=0, high=0;
    bool operator==(const RumbleValue &v) const { return low==v.low && high==v.high; }
    bool operator!=(const RumbleValue &v) const { return !(*this==v); }
};
// UU's left/right motor fields are low/high frequency, not physical Joy-Con sides.
std::optional<RumbleValue> parseRumble(const uint8_t *bytes,size_t size);
uint64_t rumbleNow();
struct RumbleSample { RumbleValue value; uint64_t epoch=0; bool enabled=false; };

// Peer-local, bounded feedback storage. Network callbacks never call HID or
// capture Client. Session generation and local activation are separate gates.
class RumbleInbox {
    std::mutex mutex_;
    uint64_t generation_=0, epoch_=0, heartbeat_=0, receivedAt_=0;
    bool connected_=false, enabled_=false, pending_=false;
    RumbleValue desired_{};
    void clear();
  public:
    void connect(uint64_t generation);
    void disconnect(uint64_t generation=0);
    void disable();
    void heartbeat(bool enabled,uint64_t now);
    bool receive(uint64_t generation,RumbleValue value,uint64_t now);
    RumbleSample sample(uint64_t now);
};
} // namespace uu
