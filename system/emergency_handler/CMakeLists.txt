cmake_minimum_required(VERSION 3.14)
project(emergency_handler)

find_package(autoware_cmake REQUIRED)
autoware_package()

include_directories(include src)
include_directories(
  include
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/src/emergency_handler
)

ament_auto_add_executable(emergency_handler
  src/emergency_handler/emergency_handler_node.cpp
  src/emergency_handler/emergency_handler_core.cpp
)

ament_auto_add_library(emergency_handler_lib SHARED
  src/emergency_handler/emergency_handler_node.cpp
  src/emergency_handler/emergency_handler_core.cpp
  src/emergency_handler/runtime.cc
  src/emergency_handler/emergency_handler_dezyne.cc
  src/emergency_handler/pump.cc
  # Add any other missing implementations here
)


ament_auto_package(INSTALL_TO_SHARE
  launch
  config
)
