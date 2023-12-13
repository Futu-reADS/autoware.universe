// Copyright 2020 Tier IV, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.



#include <rclcpp/timer.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <utility>
#include "shift_decider/shift_decider.hpp"

#include "shift_decider_dezyne.hh"

#include <dzn/locator.hh>
#include <dzn/runtime.hh>



ShiftDecider::ShiftDecider(const rclcpp::NodeOptions & node_options)
: Node("shift_decider", node_options), shift_decider_instance(locator.set(runtime))
{
  using std::placeholders::_1;

  static constexpr std::size_t queue_size = 1;
  rclcpp::QoS durable_qos(queue_size);
  durable_qos.transient_local();

  park_on_goal_ = declare_parameter<bool>("park_on_goal");
  static constexpr double vel_threshold = 0.01;  // to prevent chattering

  pub_shift_cmd_ =
    create_publisher<autoware_auto_vehicle_msgs::msg::GearCommand>("output/gear_cmd", durable_qos);
  sub_control_cmd_ = create_subscription<autoware_auto_control_msgs::msg::AckermannControlCommand>(
    "input/control_cmd", queue_size, std::bind(&ShiftDecider::onControlCmd, this, _1));
  sub_autoware_state_ = create_subscription<autoware_auto_system_msgs::msg::AutowareState>(
    "input/state", queue_size, std::bind(&ShiftDecider::onAutowareState, this, _1));

  initTimer(0.1);

  shift_decider_instance.evaluate_longitudinal_speed.in.request = [this](){
    // when desired velocity is positive and we are standing still => select drive
    // when desired velocity is negative and we are standing still => select reverse
    // when desired velocity is zero and we are standing still => select neutral
    if (control_cmd_->longitudinal.speed > vel_threshold) {
      return Sign::POSITIVE;
    }
    if (control_cmd_->longitudinal.speed < -vel_threshold) {
      return Sign::NEGATIVE;
    }
    return Sign::ZERO;
  };

  shift_decider_instance.get_params.in.park_on_goal = [this](){
    return static_cast<bool>(park_on_goal_);
  };
  shift_decider_instance.sub_current_gear.in.gear_cmd = [this](){
    return static_cast<GearCommand>(shift_cmd_.command);
  };
  shift_decider_instance.sub_autoware_state.in.state = [this](){
    return static_cast<AutowareState>(autoware_state_->state);
  };
}

void ShiftDecider::onControlCmd(
  autoware_auto_control_msgs::msg::AckermannControlCommand::SharedPtr msg)
{
  control_cmd_ = msg;
}

void ShiftDecider::onAutowareState(autoware_auto_system_msgs::msg::AutowareState::SharedPtr msg)
{
  autoware_state_ = msg;
}

void ShiftDecider::onTimer()
{
  if (!autoware_state_ || !control_cmd_) {
    return;
  }

  updateCurrentShiftCmd();
  pub_shift_cmd_->publish(shift_cmd_);
}

void ShiftDecider::updateCurrentShiftCmd()
{
  using autoware_auto_system_msgs::msg::AutowareState;
  using autoware_auto_vehicle_msgs::msg::GearCommand;

  shift_cmd_.stamp = now();
  shift_cmd_.command = static_cast<decltype(shift_cmd_.command)>(shift_decider_instance.pub_shift_cmd.in.gear_cmd());
}

void ShiftDecider::initTimer(double period_s)
{
  const auto period_ns =
    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(period_s));
  timer_ =
    rclcpp::create_timer(this, get_clock(), period_ns, std::bind(&ShiftDecider::onTimer, this));
}

#include <rclcpp_components/register_node_macro.hpp>
RCLCPP_COMPONENTS_REGISTER_NODE(ShiftDecider)