/*
 * Copyright (C) 2023, HENSOLDT Cyber GmbH
 */


#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <cstdint>

using namespace std;

struct CmdOptions {
	bool verbose;
	uint16_t port;
	string ip;
	vector<string> topics;
	CmdOptions(int, char **);
	private:
		int argc;
		vector<string> arguments;
		int parse_args();
		void print_help();
		int parse_ip_port(string);
};
