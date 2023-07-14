#! /bin/sh
#
# Copyright (C) 2023, HENSOLDT Cyber GmbH
#


cmake --build build -j8
export GZ_PARTITION=relay
./build/cpp_proxy $@

