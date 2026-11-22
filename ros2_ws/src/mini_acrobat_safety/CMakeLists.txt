cmake_minimum_required(VERSION 3.8)
project(mini_acrobat_safety)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_srvs REQUIRED)
find_package(mini_acrobat_interfaces REQUIRED)

# ROS-free state machine (unit-tested on its own)
add_library(safety_logic STATIC src/safety_logic.cpp)
target_include_directories(safety_logic PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

add_executable(safety_node src/safety_node.cpp)
target_link_libraries(safety_node safety_logic)
ament_target_dependencies(safety_node rclcpp std_srvs mini_acrobat_interfaces)

install(TARGETS safety_node DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY launch config DESTINATION share/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_safety_logic test/test_safety_logic.cpp)
  target_link_libraries(test_safety_logic safety_logic)
endif()

ament_package()
