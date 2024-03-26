#
# Network API Test System, ZynqMP board configuration
#
# Copyright (C) 2023-2024, HENSOLDT Cyber GmbH
# 
# SPDX-License-Identifier: GPL-2.0-or-later
#
# For commercial licensing, contact: info.cyber@hensoldt.net
#

cmake_minimum_required(VERSION 3.7.2)

set(LibEthdriverNumPreallocatedBuffers 32 CACHE STRING "" FORCE)

NIC_ZYNQ_DeclareCAmkESComponents_for_NICs()
