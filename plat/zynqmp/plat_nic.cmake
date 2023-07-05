#
# Network API Test System, ZynqMP board configuration
#
# Copyright (C) 2023, HENSOLDT Cyber GmbH
#

cmake_minimum_required(VERSION 3.7.2)

set(LibEthdriverNumPreallocatedBuffers 32 CACHE STRING "" FORCE)

NIC_ZYNQ_DeclareCAmkESComponents_for_NICs()
