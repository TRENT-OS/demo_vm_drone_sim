#pragma once
#include <sys/types.h>
#include "OS_Error.h"

typedef struct {
	float latitude;
	float longitude;
	float altitude;
} coordinate_t;


void filter_mavlink_message(char *, size_t *, char * , size_t *);
