// ROS 2 wrapper around SafetyLogic.
//
//   gcs/heartbeat          (Heartbeat)      in  - from the GCS at ~20 Hz (via rosbridge)
//   control/motor_setpoint (MotorSetpoint)  in  - from the control node
//   safety/arm             (Trigger)        srv
//   safety/disarm          (Trigger)        srv
//   motor_cmd              (MotorCommand)   out - fixed rate, consumed by the hardware node
//   safety/state           (SafetyState)    out - ~10 Hz and on every transition
//
// Everything (subscriptions, services, timers) runs on a single-threaded
// executor, so SafetyLogic needs no locking.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

#include "mini_acrobat_interfaces/msg/heartbeat.hpp"
#include "mini_acrobat_interfaces/msg/motor_command.hpp"
#include "mini_acrobat_interfaces/msg/motor_setpoint.hpp"
#include "mini_acrobat_interfaces/msg/safety_state.hpp"
#include "mini_acrobat_safety/safety_logic.hpp"
#include "rclcpp/rclcpp.hpp"
#include "std_srvs/srv/trigger.hpp"

namespace mini_acrobat_safety
{

using mini_acrobat_interfaces::msg::Heartbeat;
using mini_acrobat_interfaces::msg::MotorCommand;
using mini_acrobat_interfaces::msg::MotorSetpoint;
using mini_acrobat_interfaces::msg::SafetyState;
using std_srvs::srv::Trigger;

// The message constants and the logic enums must stay in sync.
static_assert(static_cast<uint8_t>(State::kDisarmed) == SafetyState::STATE_DISARMED);
static_assert(static_cast<uint8_t>(State::kArmed) == SafetyState::STATE_ARMED);
static_assert(static_cast<uint8_t>(Reason::kNone) == SafetyState::REASON_NONE);
static_assert(static_cast<uint8_t>(Reason::kBoot) == SafetyState::REASON_BOOT);
static_assert(static_cast<uint8_t>(Reason::kUser) == SafetyState::REASON_USER);
static_assert(static_cast<uint8_t>(Reason::kLinkLost) == SafetyState::REASON_LINK_LOST);

class SafetyNode : public rclcpp::Node
{
public:
  SafetyNode()
  : rclcpp::Node("safety_node")
  {
    using namespace std::chrono_literals;

    Params p;
    heartbeat_timeout_ms_ = int_param("heartbeat_timeout_ms", 300, 50, 5000);
    p.heartbeat_timeout = std::chrono::milliseconds(heartbeat_timeout_ms_);
    p.setpoint_timeout =
      std::chrono::milliseconds(int_param("setpoint_timeout_ms", 300, 50, 5000));
    p.min_heartbeats_to_arm = static_cast<int>(int_param("min_heartbeats_to_arm", 5, 1, 100));
    p.arm_zero_threshold = static_cast<float>(double_param("arm_zero_threshold", 0.02, 0.0, 0.5));
    const double rate_hz = double_param("output_rate_hz", 50.0, 10.0, 200.0);
    logic_ = SafetyLogic(p);

    const auto period = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::duration<double>(1.0 / rate_hz));
    if (period * 3 > p.heartbeat_timeout) {
      RCLCPP_WARN(
        get_logger(),
        "output_rate_hz=%.1f is slow compared to heartbeat_timeout_ms=%lld: "
        "the watchdog is only evaluated once per period", rate_hz,
        static_cast<long long>(heartbeat_timeout_ms_));
    }
    state_every_n_ = std::max(1, static_cast<int>(rate_hz / 10.0));

    motor_pub_ = create_publisher<MotorCommand>("motor_cmd", rclcpp::QoS(1));
    state_pub_ = create_publisher<SafetyState>("safety/state", rclcpp::QoS(1));

    hb_sub_ = create_subscription<Heartbeat>(
      "gcs/heartbeat", rclcpp::QoS(10),
      [this](const Heartbeat & msg) {logic_.on_heartbeat(Clock::now(), msg.seq);});

    setpoint_sub_ = create_subscription<MotorSetpoint>(
      "control/motor_setpoint", rclcpp::QoS(1),
      [this](const MotorSetpoint & msg) {logic_.on_setpoint(Clock::now(), msg.throttle);});

    arm_srv_ = create_service<Trigger>(
      "safety/arm",
      [this](const std::shared_ptr<Trigger::Request>, std::shared_ptr<Trigger::Response> res) {
        const ArmResult r = logic_.request_arm(Clock::now());
        res->success = r.ok;
        res->message = r.message;
        if (r.ok) {
          RCLCPP_INFO(get_logger(), "arm request: %s", r.message.c_str());
        } else {
          RCLCPP_WARN(get_logger(), "arm request %s", r.message.c_str());
        }
        on_tick();  // reflect the change on the outputs immediately
      });

    disarm_srv_ = create_service<Trigger>(
      "safety/disarm",
      [this](const std::shared_ptr<Trigger::Request>, std::shared_ptr<Trigger::Response> res) {
        logic_.request_disarm();
        res->success = true;
        res->message = "disarmed";
        on_tick();  // motors are told to stop right now, not at the next period
      });

    tick_timer_ = create_wall_timer(period, [this]() {on_tick();});
    jitter_timer_ = create_wall_timer(1s, [this]() {on_jitter_timer();});

    RCLCPP_INFO(
      get_logger(),
      "safety node up: DISARMED, heartbeat_timeout=%lld ms, output %.0f Hz",
      static_cast<long long>(heartbeat_timeout_ms_), rate_hz);
  }

private:
  int64_t int_param(const std::string & name, int64_t def, int64_t lo, int64_t hi)
  {
    const int64_t v = declare_parameter<int64_t>(name, def);
    if (v < lo || v > hi) {
      throw std::invalid_argument(
              "parameter '" + name + "' = " + std::to_string(v) + " is outside [" +
              std::to_string(lo) + ", " + std::to_string(hi) + "]");
    }
    return v;
  }

  double double_param(const std::string & name, double def, double lo, double hi)
  {
    const double v = declare_parameter<double>(name, def);
    if (!(v >= lo && v <= hi)) {
      throw std::invalid_argument(
              "parameter '" + name + "' = " + std::to_string(v) + " is outside [" +
              std::to_string(lo) + ", " + std::to_string(hi) + "]");
    }
    return v;
  }

  void on_tick()
  {
    const TimePoint now = Clock::now();
    const Output out = logic_.tick(now);

    MotorCommand cmd;
    cmd.armed = out.armed;
    cmd.throttle = out.throttle;
    motor_pub_->publish(cmd);

    const State st = logic_.state();
    if (st != last_state_) {
      last_state_ = st;
      if (st == State::kArmed) {
        RCLCPP_WARN(get_logger(), "state -> ARMED");
      } else {
        switch (logic_.reason()) {
          case Reason::kLinkLost:
            RCLCPP_ERROR(
              get_logger(),
              "LINK LOST: no GCS heartbeat for >= %lld ms -> DISARMED (re-arm is manual)",
              static_cast<long long>(heartbeat_timeout_ms_));
            break;
          case Reason::kUser:
            RCLCPP_INFO(get_logger(), "state -> DISARMED (user request)");
            break;
          default:
            RCLCPP_INFO(get_logger(), "state -> DISARMED");
            break;
        }
      }
      publish_state(now);
    } else if (++ticks_since_state_ >= state_every_n_) {
      publish_state(now);
    }
  }

  void publish_state(TimePoint now)
  {
    SafetyState msg;
    msg.state = static_cast<uint8_t>(logic_.state());
    msg.reason = static_cast<uint8_t>(logic_.reason());
    msg.link_ok = logic_.link_ok(now);
    msg.heartbeat_age_ms = static_cast<float>(logic_.heartbeat_age_ms(now));
    msg.heartbeat_max_gap_ms = static_cast<float>(window_max_gap_ms_);
    msg.last_heartbeat_seq = logic_.last_seq();
    state_pub_->publish(msg);
    ticks_since_state_ = 0;
  }

  // Once per second: roll the max-gap window; every 5 s log it so the timeout
  // can be tuned against the real link (e.g. the phone hotspot).
  void on_jitter_timer()
  {
    window_max_gap_ms_ = logic_.take_max_gap_ms();
    max_gap_5s_ms_ = std::max(max_gap_5s_ms_, window_max_gap_ms_);
    if (++jitter_seconds_ >= 5) {
      if (max_gap_5s_ms_ > 0.0) {
        const bool close = max_gap_5s_ms_ > 0.5 * static_cast<double>(heartbeat_timeout_ms_);
        if (close) {
          RCLCPP_WARN(
            get_logger(), "heartbeat max gap over last 5 s: %.0f ms (timeout %lld ms)",
            max_gap_5s_ms_, static_cast<long long>(heartbeat_timeout_ms_));
        } else {
          RCLCPP_INFO(
            get_logger(), "heartbeat max gap over last 5 s: %.0f ms (timeout %lld ms)",
            max_gap_5s_ms_, static_cast<long long>(heartbeat_timeout_ms_));
        }
      }
      max_gap_5s_ms_ = 0.0;
      jitter_seconds_ = 0;
    }
  }

  SafetyLogic logic_;
  State last_state_{State::kDisarmed};
  int64_t heartbeat_timeout_ms_{300};
  int state_every_n_{5};
  int ticks_since_state_{0};
  double window_max_gap_ms_{0.0};
  double max_gap_5s_ms_{0.0};
  int jitter_seconds_{0};

  rclcpp::Publisher<MotorCommand>::SharedPtr motor_pub_;
  rclcpp::Publisher<SafetyState>::SharedPtr state_pub_;
  rclcpp::Subscription<Heartbeat>::SharedPtr hb_sub_;
  rclcpp::Subscription<MotorSetpoint>::SharedPtr setpoint_sub_;
  rclcpp::Service<Trigger>::SharedPtr arm_srv_;
  rclcpp::Service<Trigger>::SharedPtr disarm_srv_;
  rclcpp::TimerBase::SharedPtr tick_timer_;
  rclcpp::TimerBase::SharedPtr jitter_timer_;
};

}  // namespace mini_acrobat_safety

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  int rc = 0;
  try {
    rclcpp::spin(std::make_shared<mini_acrobat_safety::SafetyNode>());
  } catch (const std::exception & e) {
    RCLCPP_FATAL(rclcpp::get_logger("safety_node"), "%s", e.what());
    rc = 1;
  }
  rclcpp::shutdown();
  return rc;
}
