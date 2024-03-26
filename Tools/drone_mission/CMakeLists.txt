#
# Copyright (C) 2023-2024, HENSOLDT Cyber GmbH
# 
# SPDX-License-Identifier: GPL-2.0-or-later
#
# For commercial licensing, contact: info.cyber@hensoldt.net
#

cmake_minimum_required(VERSION 3.10.2)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(drone_mission)

add_executable(drone_mission
    drone_mission.cpp
)

find_package(MAVSDK REQUIRED)

target_link_libraries(drone_mission
    MAVSDK::mavsdk
)

add_compile_options(drone_mission PRIVATE -Wall -Wextra)

