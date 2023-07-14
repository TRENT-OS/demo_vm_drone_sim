/*
 * Copyright (C) 2023, HENSOLDT Cyber GmbH
 */


#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <algorithm>
#include <sstream>

#include "CmdOptions.hh"

using namespace std;

CmdOptions::CmdOptions(int argc, char **argv) {
	argc = argc;
	vector<string> arguments_tmp(argv + 1, argv + argc);
	arguments = arguments_tmp;
	verbose = false;
	parse_args();
}


int CmdOptions::parse_args() {
	if(arguments.empty()) {
		cerr << "Error: no arguments provided" << endl;
		print_help();
		exit(-1);
	}

	string help_str = "-h";
	if (find(arguments.begin(), arguments.end(), help_str) != arguments.end()) {
		print_help();
		exit(0);
	}

	if (parse_ip_port(arguments[0])) {
		cerr << "Error: Could not parse PORT:IP argument" << endl;
		print_help();
		exit(-1);
	}
	arguments.erase(arguments.begin());

	string option_str = "-v";
	if (auto pos = find(arguments.begin(), arguments.end(), option_str);
			pos != arguments.end()) {
		verbose = true;
		arguments.erase(pos);
	}

	if (!arguments.size()) {
		cerr << "Error: no topic given" << endl;
		print_help();
		exit(-1);
	}

	for (string topic: arguments) {
		if (topic[0] != '/') {
			cerr << "Error: invalid topic " << topic << endl;
			print_help();
			exit(-1);
		}
		topics.push_back(topic);
	}

	return 0;
}


int CmdOptions::parse_ip_port(string ip_port) {
	size_t colonPos = ip_port.find(':');
	ip = ip_port.substr(0, colonPos);
	string port_str = ip_port.substr(colonPos+1);

	stringstream parser(port_str);
	uint16_t port_tmp;
	if (parser >> port_tmp) {
		port = port_tmp;
		return 0;
	}
	return -1;
}


void CmdOptions::print_help() {
	cout << "Usage: cpp_proxy IP:PORT [OPTIONS...] TOPIC [TOPIC...]\n" \
		 "Example: cpp_proxy 172.17.0.2:5555 -v /navsat\n" \
		 "cpp_proxy -- send gazebo topic information to a server\n" \
		 "-v            Print topic messages\n" \
		 "-h            Show this help message\n" \
		 << endl;
}
