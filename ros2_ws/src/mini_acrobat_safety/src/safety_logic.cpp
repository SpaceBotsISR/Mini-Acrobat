#include "mini_acrobat_safety/safety_logic.hpp"

#include <algorithm>
#include <cmath>

namespace mini_acrobat_safety
{

namespace
{

// NaN -> 0, then clamp to [-1, 1].
float sanitize(float v)
{
  if (std::isnan(v)) {
    return 0.0F;
  }
  return std::clamp(v, -1.0F, 1.0F);
}

double to_ms(Clock::duration d)
{
  return std::chrono::duration<double, std::milli>(d).count();
}

}  // namespace

SafetyLogic::SafetyLogic(const Params & params)
: params_(params) {}

void SafetyLogic::on_heartbeat(TimePoint now, uint32_t seq)
{
  if (have_heartbeat_) {
    const auto gap = now - last_heartbeat_;
    max_gap_ = std::max(max_gap_, gap);
    if (gap < params_.heartbeat_timeout) {
      ++streak_;
    } else {
      streak_ = 1;
      // A late heartbeat must not "save" an armed system whose watchdog
      // has not been evaluated yet.
      if (state_ == State::kArmed) {
        disarm(Reason::kLinkLost);
      }
    }
  } else {
    have_heartbeat_ = true;
    streak_ = 1;
  }
  last_heartbeat_ = now;
  last_seq_ = seq;
}

void SafetyLogic::on_setpoint(TimePoint now, const Throttle & throttle)
{
  for (size_t i = 0; i < throttle.size(); ++i) {
    setpoint_[i] = sanitize(throttle[i]);
  }
  last_setpoint_ = now;
  have_setpoint_ = true;
}

bool SafetyLogic::link_ok(TimePoint now) const
{
  return have_heartbeat_ && (now - last_heartbeat_) < params_.heartbeat_timeout;
}

double SafetyLogic::heartbeat_age_ms(TimePoint now) const
{
  if (!have_heartbeat_) {
    return -1.0;
  }
  return to_ms(now - last_heartbeat_);
}

double SafetyLogic::take_max_gap_ms()
{
  const double ms = to_ms(max_gap_);
  max_gap_ = Clock::duration::zero();
  return ms;
}

ArmResult SafetyLogic::request_arm(TimePoint now)
{
  if (state_ == State::kArmed) {
    return {true, "already armed"};
  }
  if (!link_ok(now)) {
    return {false, "refused: no fresh heartbeat from the GCS"};
  }
  if (streak_ < params_.min_heartbeats_to_arm) {
    return {false, "refused: heartbeat not stable yet (" + std::to_string(streak_) + "/" +
           std::to_string(params_.min_heartbeats_to_arm) + ")"};
  }
  const bool setpoint_fresh =
    have_setpoint_ && (now - last_setpoint_) < params_.setpoint_timeout;
  if (setpoint_fresh) {
    for (const float v : setpoint_) {
      if (std::fabs(v) > params_.arm_zero_threshold) {
        return {false, "refused: throttle is not zero"};
      }
    }
  }
  state_ = State::kArmed;
  reason_ = Reason::kNone;
  return {true, "armed"};
}

void SafetyLogic::request_disarm()
{
  if (state_ == State::kArmed) {
    disarm(Reason::kUser);
  }
}

void SafetyLogic::disarm(Reason reason)
{
  state_ = State::kDisarmed;
  reason_ = reason;
}

Output SafetyLogic::tick(TimePoint now)
{
  if (state_ == State::kArmed && !link_ok(now)) {
    disarm(Reason::kLinkLost);
  }

  Output out;
  out.armed = (state_ == State::kArmed);
  if (out.armed && have_setpoint_ && (now - last_setpoint_) < params_.setpoint_timeout) {
    out.throttle = setpoint_;
  }
  return out;
}

}  // namespace mini_acrobat_safety
