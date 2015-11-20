##############################################################################
# Copyright (c) 2014, Lawrence Livermore National Security, LLC.
# Produced at the Lawrence Livermore National Laboratory.
#
# Written by:
#     Nikhil Jain <nikhil.jain@acm.org>
#     Abhinav Bhatele <bhatele@llnl.gov>
#     Peer-Timo Bremer <ptbremer@llnl.gov>
#
# LLNL-CODE-678961. All rights reserved.
#
# This file is part of Damselfly. For details, see:
# https://github.com/scalability-llnl/damselfly
# Please also read the LICENSE file for our notice and the LGPL.
##############################################################################

project(damselfly)
cmake_minimum_required(VERSION 2.6)

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

set(CMAKE_C_COMPILER mpicc)
set(CMAKE_CXX_COMPILER mpicxx)

add_executable(damselfly src/ariesModeling.C)

install(TARGETS damselfly DESTINATION bin)
