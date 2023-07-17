#include <stdio.h>
#include <sys/types.h>
#include "lib_debug/Debug.h"

#include "common/mavlink.h"

#include "mavlink_filter.h"
#include "geofence.h"

mavlink_status_t status;
mavlink_message_t msg;
uint8_t chan = MAVLINK_COMM_0;


bool check_coordinates(coordinate_t *cord) {
	if (isnan(cord->latitude) || isnan(cord->longitude)) {
		puts("Invalid Coordinate: NaN, NaN\n");
		return false; // TODO: decide if packet needs to be dropped
	}

	//Check if coordinates are within the borders
	printf("Target Coordinate:\n %f, %f, %f\n", cord->latitude, cord->longitude, cord->altitude);

	point_t target = {.x = cord->latitude, .y = cord->longitude};

	if (!inside_geofence(target)) {
		// Illegal move outside the fence -> land the drone

		puts("Target coordinates outside of geofence!\n");
		// land
		coordinate_t home_cord = HOME_POSITION;
		memcpy(cord, &home_cord, sizeof(coordinate_t));
		return true;
	}
	puts("Coordinate is valid!");
	return false;
}

bool handle_mavlink_command_long() {
	mavlink_command_long_t cmd_long;
	mavlink_msg_command_long_decode(&msg, &cmd_long);
	printf("Command long: %u\n", cmd_long.command );
	//test_geofence();
	coordinate_t cord;
	//Command codes can be found under https://mavlink.io/en/messages/common.html#MAV_CMD
	switch(cmd_long.command) {
		case 21: //MAV_CMD_NAV_LAND
			puts("Land");
			cord = (coordinate_t) {
				.latitude = cmd_long.param5,
				.longitude = cmd_long.param6,
				.altitude = cmd_long.param7
			};
			return check_coordinates(&cord);
		case 22: //MAV_CMD_NAV_TAKEOFF
			puts("Takeoff");
			cord = (coordinate_t) {
				.latitude = cmd_long.param5,
				.longitude = cmd_long.param6,
				.altitude = cmd_long.param7
			};
			return check_coordinates(&cord);
		case 176: //MAV_CMD_DO_SET_MODE
			puts("Do set mode");
			printf("Do set mode: 1: %f, 2: %f, 3: %f\n", cmd_long.param1, cmd_long.param2, cmd_long.param3);
			break;
		case 400: //MAV_CMD_COMPONENT_ARM_DISARM
			puts("Arm Disarm\n");
			break;
		case 511: //MAV_CMD_SET_MESSAGE_INTERVAL
			puts("set message interval\n");
			printf("Interval: %f\n", cmd_long.param2);
			break;
		case 512: //MAV_CMD_REQUEST_MESSAGE
			puts("request message\n");
			break;
		default:
			printf("Unknown MAV CMD: %u\n", cmd_long.command);
			return true;
	}
	return false;
}

bool handle_mavlink_command_int() {
	mavlink_command_int_t cmd_int;
	mavlink_msg_command_int_decode(&msg, &cmd_int);
	printf("Command int: %u\n", cmd_int.command);
	coordinate_t cord = {
		.latitude = ((double) cmd_int.x)* 0.0000001,
		.longitude = ((double) cmd_int.y) * 0.0000001,
		.altitude = cmd_int.z
	};
	return check_coordinates(&cord);

	/*coordinate_t home_pos = HOME_POSITION;
	puts("Sending back to home position\n");
	cmd_int.x = (int)(home_pos.latitude * 10000000);
	cmd_int.y = (int)(home_pos.longitude * 10000000);
	cmd_int.z = home_pos.altitude + 1000.0;
	mavlink_msg_command_int_encode(
		msg.sysid,
		msg.compid,
		&msg,
		&cmd_int);
	*/
	
}


void handle_mavlink_package(char *buf, size_t* len, char * ret_buf, size_t * ret_len) {
	switch(msg.msgid) {
		case MAVLINK_MSG_ID_HEARTBEAT: //ID 0
			puts("Heartbeat\n");
			break;
		case MAVLINK_MSG_ID_PING: //ID 4
			puts("Ping\n");
			break;
		case MAVLINK_MSG_ID_COMMAND_LONG:
			puts("Command Long\n");
			if (handle_mavlink_command_long()) { 
				Debug_LOG_ERROR("Packet is malicous and will be dropped");
				return; 
			}
			break;
		case MAVLINK_MSG_ID_COMMAND_INT:
			puts("Command int");
			if (handle_mavlink_command_int()) { 
				Debug_LOG_ERROR("Packet is malicous and will be dropped");
				return; 
			}
			break;
		case MAVLINK_MSG_ID_PARAM_REQUEST_READ:
			puts("Param request read\n");
			break;
		default:
			printf("Error: Unknown MAVLINK MSG ID %i\n Message Dropped", msg.msgid);
			return;
	}

	size_t added_len = mavlink_msg_to_send_buffer((uint8_t*) &ret_buf[*ret_len], &msg);
	*ret_len += added_len;
}

void filter_mavlink_message(char * message, size_t * nread, char * ret_buf, size_t * ret_len) {
	for(int i=0; i < *nread; i++) {
		uint8_t byte = message[i];

		//this has state -> return 1 if package decoding is complete
		if (mavlink_parse_char(chan, byte, &msg, &status)) {
			printf("Received message with ID %d, sequence: %d from component %d of system %d\n", msg.msgid, msg.seq, msg.compid, msg.sysid);
			handle_mavlink_package(message, nread, ret_buf, ret_len);
		}
	}
}