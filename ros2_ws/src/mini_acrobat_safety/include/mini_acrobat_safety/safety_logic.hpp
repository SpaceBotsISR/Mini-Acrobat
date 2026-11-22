// Pure C++ safety state machine (no ROS dependency, so it can be unit-tested
// on any machine). The ROS node in safety_node.cpp is a thin wrapper around it.
//
//   DISARMED --(arm request, link healthy, throttle ~0)--> ARMED
//   ARMED    --(heartbeat timeout)------------------------> DISARMED (LINK_LOST)
//   ARMED    --(disarm request)---------------------------> DISARMED (USER)
//
// A link loss is latched: re-arming is always a manual action, and needs a
// fresh run of consecutive heartbeats first.
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <string>

namespace mini_acrobat_safety
{

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Throttle = std::array<float, 3>;

enum class State : uint8_t { kDisarmed = 0, kArmed = 1 };
enum class Reason : uint8_t { kNone = 0, kBoot = 1, kUser = 2, kLinkLost = 3 };

struct Params
{
  // Disarm when no heartbeat arrives for this long.
  std::chrono::milliseconds heartbeat_timeout{300};
  // While armed, a setpoint older than this is replaced by zero throttle
  // (the node stays armed; only a lost heartbeat disarms).
  std::chrono::milliseconds setpoint_timeout{300};
  // Consecutive healthy heartbeats required before arming is accepted.
  int min_heartbeats_to_arm{5};
  // Arming is refused if any |throttle| is above this.
  float arm_zero_threshold{0.02F};
};

struct Output
{
  bool armed{false};
  Throttle throttle{{0.0F, 0.0F, 0.0F}};
};

struct ArmResult
{
  bool ok{false};
  std::string message;
};

class SafetyLogic
{
public:
  explicit SafetyLogic(const Params & params = Params{});

  void on_heartbeat(TimePoint now, uint32_t seq);
  void on_setpoint(TimePoint now, const Throttle & throttle);

  ArmResult request_arm(TimePoint now);
  void request_disarm();

  // Call periodically. Applies the watchdog and returns what to send to the motors.
  Output tick(TimePoint now);

  State state() const {return state_;}
  Reason reason() const {return reason_;}
  uint32_t last_seq() const {return last_seq_;}
  bool link_ok(TimePoint now) const;
  // Milliseconds since the last heartbeat, or -1 if none was received yet.
  double heartbeat_age_ms(TimePoint now) const;
  // Largest heartbeat gap since the previous call, in ms; resets the counter.
  double take_max_gap_ms();

private:
  void disarm(Reason reason);

  Params params_;
  State state_{State::kDisarmed};
  Reason reason_{Reason::kBoot};

  bool have_heartbeat_{false};
  TimePoint last_heartbeat_{};
  uint32_t last_seq_{0};
  int streak_{0};
  Clock::duration max_gap_{Clock::duration::zero()};

  bool have_setpoint_{false};
  TimePoint last_setpoint_{};
  Throttle setpoint_{{0.0F, 0.0F, 0.0F}};
};

}  // namespace mini_acrobat_safety
