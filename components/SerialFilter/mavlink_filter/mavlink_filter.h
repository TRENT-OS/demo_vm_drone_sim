/*
 * Copyright (C) 2023-2024, HENSOLDT Cyber GmbH
 * 
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * For commercial licensing, contact: info.cyber@hensoldt.net
 */

#pragma once
#include <sys/types.h>
#include "OS_Error.h"

typedef struct {
	float latitude;
	float longitude;
	float altitude;
} coordinate_t;


void filter_mavlink_message(char *, size_t *, char * , size_t *);
