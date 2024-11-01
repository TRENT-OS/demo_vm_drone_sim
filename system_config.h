/*
 * OS libraries configurations
 * 
 * Copyright (C) 2023-2024, HENSOLDT Cyber GmbH
 * 
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * For commercial licensing, contact: info.cyber@hensoldt.net
 */
 
#pragma once

#ifndef SYSTEM_CONFIG_H_
#define SYSTEM_CONFIG_H_

// Debug
#if !defined(NDEBUG)
#define Debug_Config_STANDARD_ASSERT
#define Debug_Config_ASSERT_SELF_PTR
#else
#define Debug_Config_DISABLE_ASSERT
#define Debug_Config_NO_ASSERT_SELF_PTR
#endif

#if !defined(Debug_Config_LOG_LEVEL)
#define Debug_Config_LOG_LEVEL                  Debug_LOG_LEVEL_DEBUG
#endif
#define Debug_Config_INCLUDE_LEVEL_IN_MSG
#define Debug_Config_LOG_WITH_FILE_LINE

// Memory
#define Memory_Config_USE_STDLIB_ALLOC


// NIC driver
#define NIC_DRIVER_RINGBUFFER_NUMBER_ELEMENTS 16
#define NIC_DRIVER_RINGBUFFER_SIZE                                             \
  (NIC_DRIVER_RINGBUFFER_NUMBER_ELEMENTS * 4096)

// Network stack

#define ETH_SUBNET_MASK "255.255.255.0"

// VM <--> TRENTOS
#define VM_LINUX_ADDR "192.168.1.1"
//VM_VM_PORT -> not defined as this is dynamic
#define VM_LINUX_PORT_SIMCOUPLER 5555

#define VM_TRENTOS_ADDR "192.168.1.2"
#define VM_TRENTOS_PORT 7000
#define VM_TRENTOS_PORT_SIMCOUPLER 5555

#define VM_GATEWAY_ADDR "192.168.1.3"

// TRENTOS <--> PX4(Linux Host)
#define PX4_TRENTOS_ADDR "10.0.0.11"
#define PX4_TRENTOS_PORT  7000
#define PX4_TRENTOS_PORT_SIMCOUPLER 5555

#define PX4_DRONE_ADDR "172.17.0.1"
#define PX4_DRONE_PORT 7000


#define PX4_GATEWAY_ADDR "10.0.0.1"

// Geofence

#define GEOFENCE_POLYGON {\
  {48.05550749800078, 11.651234342011845}, \
  {48.05580340913948, 11.653684004312566}, \
  {48.05469452629921, 11.654558805494695}, \
  {48.05404812004936, 11.652732871302717}  \
}

#define HOME_POSITION {48.05502700126609, 11.652206077452211, NAN}

#endif // SYSTEM_CONFIG_H_