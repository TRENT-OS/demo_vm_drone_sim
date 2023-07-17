#include <stdbool.h>

#include "system_config.h"

typedef struct {
    float x;
    float y;
} point_t;

bool inside_geofence(point_t p);

void test_geofence();