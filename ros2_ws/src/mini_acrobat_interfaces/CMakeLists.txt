cmake_minimum_required(VERSION 3.8)
project(mini_acrobat_interfaces)

find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)

rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/Heartbeat.msg"
  "msg/MotorSetpoint.msg"
  "msg/MotorCommand.msg"
  "msg/SafetyState.msg"
)

ament_export_dependencies(rosidl_default_runtime)
ament_package()
