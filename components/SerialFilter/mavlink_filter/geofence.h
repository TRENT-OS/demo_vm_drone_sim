/*
 * Copyright (C) 2023-2024, HENSOLDT Cyber GmbH
 * 
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * For commercial licensing, contact: info.cyber@hensoldt.net
 */

#include <stdbool.h>

#include "system_config.h"

typedef struct {
    float x;
    float y;
} point_t;

bool inside_geofence(point_t p);

void test_geofence();