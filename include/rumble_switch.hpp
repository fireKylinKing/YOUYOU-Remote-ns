#pragma once
#ifdef __SWITCH__
#include "rumble.hpp"
#include <switch.h>
#include <atomic>
#include <memory>
#include <thread>

namespace uu {
// HID calls belong exclusively to this worker. A 500 ms UI lease prevents a
// stalled renderer from leaving an effect on indefinitely (not a network timer).
class SwitchRumble {
    std::shared_ptr<RumbleInbox> inbox_;
    RumbleOptions options_;
    RumbleMapping mapping_=RumbleMapping::Mixed;
    std::mutex mutex_;
    uint32_t id_=0,style_=0;
    uint64_t updatedAt_=0,testAt_=0,testEpoch_=0;
    std::atomic<bool> quit_{false};
    std::thread worker_;
    void run();
  public:
    explicit SwitchRumble(std::shared_ptr<RumbleInbox> inbox,RumbleOptions options={});
    ~SwitchRumble();
    void update(const PadState &pad,bool remoteAllowed,bool testRequested=false,bool localAllowed=false,
                RumbleMapping mapping=RumbleMapping::Mixed);
    void stop();
};
} // namespace uu
#endif
