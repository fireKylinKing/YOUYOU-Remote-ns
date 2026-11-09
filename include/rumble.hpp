#pragma once
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <array>
#include <vector>

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

struct RumbleOptions {
    int strength=75,lowGain=100,highGain=80,smoothMs=20;
    bool trace=true;
};
struct RumbleAmplitude { float low=0,high=0; };
enum class RumbleMapping { Mixed=0, Split=1 };
// Slew nonzero amplitudes; zero on either channel always stops that channel.
class RumbleShaper {
    RumbleOptions options_;
    RumbleAmplitude current_{};
  public:
    explicit RumbleShaper(RumbleOptions options={});
    RumbleAmplitude advance(RumbleValue value,uint64_t elapsedMs);
    void reset() { current_={}; }
};
struct RumbleFeedback {
    uint64_t sequence=0,at=0,interval=0,epoch=0;
    RumbleValue value;
    bool replacedPending=false;
};
struct RumbleTraceBatch {
    std::vector<RumbleFeedback> feedback;
    uint64_t dropped=0;
};

// Peer-local, bounded feedback storage. Network callbacks never call HID or
// capture Client. Session generation and local activation are separate gates.
class RumbleInbox {
    std::mutex mutex_;
    uint64_t generation_=0, epoch_=0, heartbeat_=0, receivedAt_=0;
    bool connected_=false, enabled_=false, pending_=false;
    RumbleValue desired_{};
    bool tracing_=false,havePreviousReceive_=false;
    uint64_t traceSequence_=0,previousReceive_=0,traceDropped_=0;
    std::array<RumbleFeedback,512> trace_{};
    size_t traceHead_=0,traceSize_=0;
    void clear();
  public:
    void connect(uint64_t generation);
    void disconnect(uint64_t generation=0);
    void disable();
    void heartbeat(bool enabled,uint64_t now);
    bool receive(uint64_t generation,RumbleValue value,uint64_t now);
    RumbleSample sample(uint64_t now);
    void setTracing(bool enabled);
    RumbleTraceBatch takeTrace();
};
} // namespace uu
