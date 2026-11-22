#include <gtest/gtest.h>

#include <chrono>
#include <limits>

#include "mini_acrobat_safety/safety_logic.hpp"

using namespace std::chrono_literals;
using mini_acrobat_safety::Clock;
using mini_acrobat_safety::Params;
using mini_acrobat_safety::Reason;
using mini_acrobat_safety::SafetyLogic;
using mini_acrobat_safety::State;
using mini_acrobat_safety::Throttle;
using mini_acrobat_safety::TimePoint;

namespace
{

// Fixed origin so tests are deterministic; only differences matter.
const TimePoint kT0 = Clock::time_point{} + std::chrono::hours(1);

TimePoint at(std::chrono::milliseconds ms) {return kT0 + ms;}

// Sends `n` heartbeats 50 ms apart starting at `start`; returns the time of the last one.
std::chrono::milliseconds beat(
  SafetyLogic & logic, std::chrono::milliseconds start, int n, uint32_t first_seq = 0)
{
  auto t = start;
  for (int i = 0; i < n; ++i) {
    logic.on_heartbeat(at(t), first_seq + static_cast<uint32_t>(i));
    if (i + 1 < n) {
      t += 50ms;
    }
  }
  return t;
}

}  // namespace

TEST(SafetyLogic, BootsDisarmed)
{
  SafetyLogic logic;
  EXPECT_EQ(logic.state(), State::kDisarmed);
  EXPECT_EQ(logic.reason(), Reason::kBoot);
  const auto out = logic.tick(at(0ms));
  EXPECT_FALSE(out.armed);
  EXPECT_FLOAT_EQ(logic.heartbeat_age_ms(at(0ms)), -1.0F);
}

TEST(SafetyLogic, ArmRefusedWithoutHeartbeat)
{
  SafetyLogic logic;
  const auto r = logic.request_arm(at(0ms));
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(logic.state(), State::kDisarmed);
}

TEST(SafetyLogic, ArmRefusedUntilHeartbeatStable)
{
  SafetyLogic logic;  // needs 5 consecutive heartbeats
  const auto last = beat(logic, 0ms, 4);
  EXPECT_FALSE(logic.request_arm(at(last)).ok);
  logic.on_heartbeat(at(last + 50ms), 4);
  EXPECT_TRUE(logic.request_arm(at(last + 50ms)).ok);
  EXPECT_EQ(logic.state(), State::kArmed);
  EXPECT_EQ(logic.reason(), Reason::kNone);
}

TEST(SafetyLogic, ArmRefusedWithStaleHeartbeat)
{
  SafetyLogic logic;
  const auto last = beat(logic, 0ms, 5);
  EXPECT_FALSE(logic.request_arm(at(last + 300ms)).ok);  // exactly at the timeout
}

TEST(SafetyLogic, ArmRefusedWhenThrottleNotZero)
{
  SafetyLogic logic;
  const auto last = beat(logic, 0ms, 5);
  logic.on_setpoint(at(last), Throttle{{0.0F, 0.3F, 0.0F}});
  EXPECT_FALSE(logic.request_arm(at(last)).ok);
  logic.on_setpoint(at(last + 10ms), Throttle{{0.0F, 0.0F, 0.0F}});
  EXPECT_TRUE(logic.request_arm(at(last + 10ms)).ok);
}

TEST(SafetyLogic, StaleNonZeroSetpointDoesNotBlockArming)
{
  SafetyLogic logic;
  logic.on_setpoint(at(0ms), Throttle{{0.8F, 0.8F, 0.8F}});
  const auto last = beat(logic, 1000ms, 5);
  EXPECT_TRUE(logic.request_arm(at(last)).ok);
  // ... and the stale setpoint is never forwarded.
  const auto out = logic.tick(at(last));
  EXPECT_TRUE(out.armed);
  EXPECT_FLOAT_EQ(out.throttle[0], 0.0F);
}

TEST(SafetyLogic, DisarmsOnHeartbeatTimeout)
{
  SafetyLogic logic;
  const auto last = beat(logic, 0ms, 5);
  ASSERT_TRUE(logic.request_arm(at(last)).ok);
  logic.on_setpoint(at(last), Throttle{{0.5F, 0.5F, 0.5F}});

  auto out = logic.tick(at(last + 299ms));
  EXPECT_TRUE(out.armed);
  EXPECT_FLOAT_EQ(out.throttle[0], 0.5F);

  out = logic.tick(at(last + 300ms));
  EXPECT_FALSE(out.armed);
  EXPECT_FLOAT_EQ(out.throttle[0], 0.0F);
  EXPECT_EQ(logic.state(), State::kDisarmed);
  EXPECT_EQ(logic.reason(), Reason::kLinkLost);
}

TEST(SafetyLogic, LinkLossIsLatchedAndNeedsFreshHeartbeats)
{
  SafetyLogic logic;
  auto last = beat(logic, 0ms, 5);
  ASSERT_TRUE(logic.request_arm(at(last)).ok);
  logic.tick(at(last + 400ms));
  ASSERT_EQ(logic.state(), State::kDisarmed);

  // Link comes back: it must NOT re-arm by itself.
  last = beat(logic, last + 1000ms, 3, 100);
  EXPECT_EQ(logic.tick(at(last)).armed, false);
  // Only 3 of 5 heartbeats after the gap -> still refused.
  EXPECT_FALSE(logic.request_arm(at(last)).ok);
  logic.on_heartbeat(at(last + 50ms), 103);
  logic.on_heartbeat(at(last + 100ms), 104);
  EXPECT_TRUE(logic.request_arm(at(last + 100ms)).ok);
}

TEST(SafetyLogic, LateHeartbeatDoesNotRescueArmedSystem)
{
  // Simulates a stalled tick(): a heartbeat arrives after the timeout while
  // the watchdog has not run yet.
  SafetyLogic logic;
  const auto last = beat(logic, 0ms, 5);
  ASSERT_TRUE(logic.request_arm(at(last)).ok);
  logic.on_heartbeat(at(last + 500ms), 5);
  EXPECT_EQ(logic.state(), State::kDisarmed);
  EXPECT_EQ(logic.reason(), Reason::kLinkLost);
  EXPECT_FALSE(logic.tick(at(last + 500ms)).armed);
}

TEST(SafetyLogic, UserDisarm)
{
  SafetyLogic logic;
  const auto last = beat(logic, 0ms, 5);
  ASSERT_TRUE(logic.request_arm(at(last)).ok);
  logic.request_disarm();
  EXPECT_EQ(logic.state(), State::kDisarmed);
  EXPECT_EQ(logic.reason(), Reason::kUser);
  EXPECT_FALSE(logic.tick(at(last)).armed);
}

TEST(SafetyLogic, StaleSetpointGivesZeroThrottleButStaysArmed)
{
  SafetyLogic logic;
  auto last = beat(logic, 0ms, 5);
  ASSERT_TRUE(logic.request_arm(at(last)).ok);
  logic.on_setpoint(at(last), Throttle{{-0.4F, 0.4F, 0.2F}});

  // Keep the heartbeat alive while the setpoint goes stale.
  for (int i = 1; i <= 8; ++i) {
    logic.on_heartbeat(at(last + std::chrono::milliseconds(50 * i)), 10 + i);
  }
  const auto out = logic.tick(at(last + 400ms));
  EXPECT_TRUE(out.armed);
  EXPECT_FLOAT_EQ(out.throttle[0], 0.0F);
  EXPECT_FLOAT_EQ(out.throttle[1], 0.0F);
  EXPECT_FLOAT_EQ(out.throttle[2], 0.0F);
}

TEST(SafetyLogic, SetpointIsClampedAndNanBecomesZero)
{
  SafetyLogic logic;
  const auto last = beat(logic, 0ms, 5);
  ASSERT_TRUE(logic.request_arm(at(last)).ok);
  logic.on_setpoint(
    at(last), Throttle{{5.0F, -7.0F, std::numeric_limits<float>::quiet_NaN()}});
  const auto out = logic.tick(at(last));
  EXPECT_FLOAT_EQ(out.throttle[0], 1.0F);
  EXPECT_FLOAT_EQ(out.throttle[1], -1.0F);
  EXPECT_FLOAT_EQ(out.throttle[2], 0.0F);
}

TEST(SafetyLogic, GapResetsHeartbeatStreak)
{
  SafetyLogic logic;
  auto last = beat(logic, 0ms, 4);                  // streak 4
  logic.on_heartbeat(at(last + 350ms), 4);          // gap > timeout -> streak 1
  last += 350ms;
  logic.on_heartbeat(at(last + 50ms), 5);           // streak 2
  EXPECT_FALSE(logic.request_arm(at(last + 50ms)).ok);
}

TEST(SafetyLogic, ReportsAndResetsMaxGap)
{
  SafetyLogic logic;
  logic.on_heartbeat(at(0ms), 0);
  logic.on_heartbeat(at(50ms), 1);
  logic.on_heartbeat(at(170ms), 2);
  logic.on_heartbeat(at(220ms), 3);
  EXPECT_NEAR(logic.take_max_gap_ms(), 120.0, 0.001);
  EXPECT_NEAR(logic.take_max_gap_ms(), 0.0, 0.001);
}

TEST(SafetyLogic, CustomParamsAreRespected)
{
  Params p;
  p.heartbeat_timeout = 100ms;
  p.min_heartbeats_to_arm = 2;
  SafetyLogic logic(p);
  logic.on_heartbeat(at(0ms), 0);
  logic.on_heartbeat(at(40ms), 1);
  ASSERT_TRUE(logic.request_arm(at(40ms)).ok);
  EXPECT_TRUE(logic.tick(at(139ms)).armed);
  EXPECT_FALSE(logic.tick(at(140ms)).armed);
}
