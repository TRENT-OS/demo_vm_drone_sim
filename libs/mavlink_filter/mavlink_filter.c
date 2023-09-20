#include <stdio.h>
#include <sys/types.h>
#include "OS_Error.h"


#include "common/mavlink.h"

#include "mavlink_filter.h"
#include "geofence.h"

mavlink_status_t status;
mavlink_message_t msg;
uint8_t chan = MAVLINK_COMM_0;

void check_coordinates(coordinate_t *cord) {
	if (isnan(cord->latitude) || isnan(cord->longitude)) {
		puts("Coordinate in mesg not set \n");
		return; // TODO: decide if packet needs to be dropped
	}

	//Check if coordinates are within the borders
	printf("Target Coordinate:\n%f, %f\n", cord->latitude, cord->longitude);

	point_t target = {.x = cord->latitude, .y = cord->longitude};

	if (!inside_geofence(target)) {
		// Illegal move outside the fence -> land the drone

		// land
	}
}


int handle_mavlink_command_long(mavlink_command_long_t *cmd_long) {
	//test_geofence();
	coordinate_t cord;
	//Command codes can be found under https://mavlink.io/en/messages/common.html#MAV_CMD
	switch(cmd_long->command) {
		case 21: //MAV_CMD_NAV_LAND
			puts("Land");
			cord = (coordinate_t) {
				.latitude = cmd_long->param5,
				.longitude = cmd_long->param6,
				.altitude = cmd_long->param7
			};
			check_coordinates(&cord);
			break;
		case 22: //MAV_CMD_NAV_TAKEOFF
			puts("Takeoff");
			cord = (coordinate_t) {
				.latitude = cmd_long->param5,
				.longitude = cmd_long->param6,
				.altitude = cmd_long->param7
			};
			check_coordinates(&cord);
			break;
		case 400: //MAV_CMD_COMPONENT_ARM_DISARM
			puts("Arm Disarm\n");
			break;
		case 511: //MAV_CMD_SET_MESSAGE_INTERVAL
			puts("set message interval\n");
			printf("Interval: %f\n", cmd_long->param2);
			break;
		case 512: //MAV_CMD_REQUEST_MESSAGE
			puts("request message\n");
			break;
		default:
			printf("Unknown MAV CMD: %u\n", cmd_long->command);
			break;
	}
	return 0;
}

int handle_mavlink_package() {
	switch(msg.msgid) {
		case MAVLINK_MSG_ID_HEARTBEAT: //ID 0
			puts("Heartbeat\n");
			break;
		case MAVLINK_MSG_ID_PING: //ID 4
			puts("Ping\n");
			break;
		case MAVLINK_MSG_ID_COMMAND_LONG:
			puts("Command Long\n");
			mavlink_command_long_t cmd_long;
			mavlink_msg_command_long_decode(&msg, &cmd_long);
			printf("Command: %u\n", cmd_long.command );
			handle_mavlink_command_long(&cmd_long);
			break;
		case MAVLINK_MSG_ID_PARAM_REQUEST_READ:
			puts("Param request read\n");
			break;
		default:
			printf("Error: Unknown MAVLINK MSG ID %i\n", msg.msgid);
			break;
	}
	return 0;
}

OS_Error_t filter_mavlink_message(char * message, size_t nread) {
	for(int i=0; i < nread; i++) {
		uint8_t byte = message[i];
		
		//this has state -> return 1 if package decoding is complete
		if (mavlink_parse_char(chan, byte, &msg, &status)) {
			printf("Received message with ID %d, sequence: %d from component %d of system %d\n", msg.msgid, msg.seq, msg.compid, msg.sysid);
			handle_mavlink_package();
		}
	}
	return 0;
}