/*
 * Copyright (C) 2023, HENSOLDT Cyber GmbH
 */

#include <chrono>
#include <mavsdk/mavsdk.h>
#include <mavsdk/plugins/action/action.h>
#include <mavsdk/plugins/telemetry/telemetry.h>
#include <iostream>
#include <future>
#include <memory>
#include <cstdint>
#include <thread>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <unistd.h>
//#include <fstream>
#include <string>
#include <mutex>
#include <condition_variable>
#include <string.h>

#include "json/json.hpp"

using namespace mavsdk;
using std::chrono::seconds;
using std::this_thread::sleep_for;
using json = nlohmann::json;


void usage(const std::string& bin_name)
{
    std::cerr << "Usage : " << bin_name << " <connection_url>\n"
              << "Connection URL format should be :\n"
              << " For TCP : tcp://[server_host][:server_port]\n"
              << " For UDP : udp://[bind_host][:bind_port]\n"
              << " For Serial : serial:///path/to/serial/dev[:baudrate]\n"
              << "For example, to connect to the simulator use URL: udp://:14540\n";
}


bool slice_input_json(std::string &input, std::string &json_str) {
    std::size_t eof_pos = input.find_first_of('\x07');
    if (eof_pos == std::string::npos) {
        return false;
    }
    json_str = input.substr(0, eof_pos);
    input.erase(0, eof_pos +1 );
    return true;
}


void clean_input(std::string &input) {
    std::size_t eof_pos = input.find_first_of('\x07');
    if (eof_pos < std::string::npos) {
      eof_pos = input.size();  
    }
    input.erase(0, eof_pos);
}


int get_socket(std::string addr, uint16_t port) {
	struct sockaddr_in server_addr = {
		.sin_family = AF_INET,
		.sin_port = htons(port),
		.sin_addr = {
			.s_addr = inet_addr(addr.c_str())
		}
	};

	int sock_client;
	if ((sock_client = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		std::cerr << "Error: socket creation failed" << std::endl;
		exit(-1);
	}

	while (connect(sock_client, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
	{
		std::cerr << "Error: connection to server with IP: "\
			<< addr << " Port: "\
			<< port << " failed." << std::endl;
		sleep(1);
	}

	std::cout << "Connection to SimCoupler with IP: "\
		<< addr << " Port: "\
		<< port << " established." << std::endl;

	return sock_client;
}


bool check_if_json(std::string input) {
    return input.find('\x07') != std::string::npos;
}


void print_current_output(int sockfd, std::string &raw_json_buf) {
    
    if (!check_if_json(raw_json_buf)) {
        char buf[1500] = { 0 };
        int n = recv(sockfd, buf, sizeof(buf), 0);
        raw_json_buf += buf;
    }
    std::string json_str;
    if (slice_input_json(raw_json_buf, json_str)) {
        json data;
        data = json::parse(json_str);
        std::cout << "GPS position\n" 
            << "lat: " << data["latitudeDeg"] 
            << "\nlon: " << data["longitudeDeg"]
            << "\nalt: " << data["altitude"]
            << "\n" << std::endl;
    }
}


int main(int argc, char** argv)
{
    if (argc != 2) {
        usage(argv[0]);
        return 1;
    }
    std::cout << "SimCoupler connection initiated" << std::endl;
    int sockfd = get_socket("192.168.1.2", 5555);

    std::string raw_json_buf;
  
    {
        char buf[1500] = { 0 };
        int n = recv(sockfd, buf, sizeof(buf), 0);
        raw_json_buf += buf;
        clean_input(raw_json_buf);
    }
    
    print_current_output(sockfd, raw_json_buf);
    Mavsdk mavsdk;
	double timeout = 10.0;
	mavsdk.set_timeout_s(timeout);
	std::cout << "Set timeout to: " << timeout << '\n';
    ConnectionResult connection_result = mavsdk.add_any_connection(argv[1]);

    if (connection_result != ConnectionResult::Success) {
        std::cerr << "Connection failed: " << connection_result << '\n';
        return 1;
    }

    auto system = mavsdk.first_autopilot(3.0);
    if (!system) {
        std::cerr << "Timed out waiting for system\n";
        return 1;
    }

    print_current_output(sockfd, raw_json_buf);
    // Instantiate plugins.
    auto telemetry = Telemetry{system.value()};
    auto action = Action{system.value()};

    // We want to listen to the altitude of the drone at 1 Hz.
    const auto set_rate_result = telemetry.set_rate_position(1.0);
    if (set_rate_result != Telemetry::Result::Success) {
        std::cerr << "Setting rate failed: " << set_rate_result << '\n';
        return 1;
    }

    print_current_output(sockfd, raw_json_buf);
    // Set up callback to monitor altitude while the vehicle is in flight
    telemetry.subscribe_position([](Telemetry::Position position) {
        std::cout << "Altitude: " << position.relative_altitude_m << " m\n";
    });

    print_current_output(sockfd, raw_json_buf);
    // Check until vehicle is ready to arm
    while (telemetry.health_all_ok() != true) {
        std::cout << "Vehicle is getting ready to arm\n";
        sleep_for(seconds(1));
    }

    print_current_output(sockfd, raw_json_buf);
    // Arm vehicle
    std::cout << "Arming...\n";
    const Action::Result arm_result = action.arm();

    if (arm_result != Action::Result::Success) {
        std::cerr << "Arming failed: " << arm_result << '\n';
        return 1;
    }

    print_current_output(sockfd, raw_json_buf);
    // Take off
    std::cout << "Taking off...\n";
    const Action::Result takeoff_result = action.takeoff();
    if (takeoff_result != Action::Result::Success) {
        std::cerr << "Takeoff failed: " << takeoff_result << '\n';
        return 1;
    }

    print_current_output(sockfd, raw_json_buf);
    // Let it hover for a bit before landing again.
    sleep_for(seconds(15));
	std::cout << "Flying to valid location 48.05529681783492, 11.65173378612393" << std::endl;
	action.goto_location(48.05529681783492, 11.65173378612393, NAN, NAN);
    sleep_for(seconds(10));

    print_current_output(sockfd, raw_json_buf);
	std::cout << "Flying to invalid location 48.056529056548406, 11.652396728102497" << std::endl;
	action.goto_location(48.056529056548406, 11.652396728102497, NAN, NAN);
	sleep_for(seconds(10));

    print_current_output(sockfd, raw_json_buf);
	std::cout << "Returning to home" << std::endl;
	action.return_to_launch();
	/*
	action.goto_location(48.05502700126609, 11.652206077452211, NAN, NAN);
	sleep_for(seconds(30));
    const Action::Result land_result = action.land();
    if (land_result != Action::Result::Success) {
        std::cerr << "Land failed: " << land_result << '\n';
        return 1;
    }*/

    // Check if vehicle is still in air
    while (telemetry.in_air()) {
        std::cout << "Vehicle is landing...\n";
        print_current_output(sockfd, raw_json_buf);
        sleep_for(seconds(2));
    }
    std::cout << "Landed!\n";

    // We are relying on auto-disarming but let's keep watching the telemetry for a bit longer.
    sleep_for(seconds(3));
    std::cout << "Finished...\n";
    print_current_output(sockfd, raw_json_buf);

    return 0;
}
