cmake_minimum_required(VERSION 3.14)
project(shift_decider)

find_package(autoware_cmake REQUIRED)
autoware_package()

include_directories(include src)

# Add all relevant source files, including dzn components
ament_auto_add_library(shift_decider_node SHARED
  src/shift_decider.cpp
  src/runtime.cc
  src/shift_decider_dezyne.cc
  src/pump.cc
  # Add other necessary source files from dzn if needed
)

# Link additional libraries if required
# target_link_libraries(shift_decider_node <other_libraries_if_needed>)

rclcpp_components_register_node(shift_decider_node
  PLUGIN "ShiftDecider"
  EXECUTABLE shift_decider
)

ament_auto_package(
  INSTALL_TO_SHARE
  launch
  config
)
