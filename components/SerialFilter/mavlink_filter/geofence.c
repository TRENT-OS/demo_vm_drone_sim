#include <stdio.h>

#include "mavlink_filter.h"
#include "geofence.h"

/*Raycasting Algorithm to check wether a given point is inside the area of the polygon*/
bool inside_geofence(point_t p) {
    point_t polygon[] = GEOFENCE_POLYGON;
    int n = sizeof(polygon) / sizeof(point_t);
    
    bool c = false;
    for (int i = 0, j = n -1; i < n; j = i++) {
        if (((polygon[i].y > p.y) != (polygon[j].y > p.y)) &&
            (p.x < (polygon[j].x - polygon[i].x)
            * (p.y - polygon[i].y) 
            / (polygon[j].y - polygon[i].y) + polygon[i].x)) {
                c = !c;
        }
    }
    return c;
}

void test_geofence() {
	point_t a[] = {
		{48.05501151866626, 11.652647249479202}, //Inside the fence
		{48.255665901125575, 11.586522668102866} //Outside the fence
		};
	for (int i =0; i < 2; i++)
	if (inside_geofence(a[i])) {
		puts("Inside Geofence\n");
	} else {
		puts("Outside geofence");
	}
}