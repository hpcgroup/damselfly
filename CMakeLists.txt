cmake_minimum_required(VERSION 2.6)
project(ariesModeling)

include_directories("${PROJECT_BINARY_DIR}")

option (JOB_TRAFFIC "Write link traffic for each job" OFF)

if (JOB_TRAFFIC)
  set (JOB_SPECIFIC_TRAFFIC 1)
  message("Write link traffic for each job is ON")
else (JOB_TRAFFIC)
  set (JOB_SPECIFIC_TRAFFIC 0)
  message("Write link traffic for each job is OFF")
endif (JOB_TRAFFIC)

configure_file (
      "${PROJECT_SOURCE_DIR}/src/Config.h.in"
      "${PROJECT_BINARY_DIR}/Config.h"
)

add_executable(ariesModeling src/ariesModeling.C)
