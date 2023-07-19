/*
 * Copyright (C) 2023, HENSOLDT Cyber GmbH
 */


#include <google/protobuf/message.h>
#include <google/protobuf/util/json_util.h>
#include <iostream>
#include <vector>
#include <string>
#include <gz/transport.hh>
#include <cstdint>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "CmdOptions.hh"

using namespace std;
using namespace google;

protobuf::util::JsonPrintOptions JsonPrintOptions_ctx;

struct CppProxy {
	struct CmdOptions *cmd_options;
	int socket;
};

CppProxy cpp_proxy;

int get_socket();

void callback(const protobuf::Message &_msg,
        const gz::transport::MessageInfo &_info)
{
	string json_string;
	protobuf::util::MessageToJsonString(_msg, &json_string, JsonPrintOptions_ctx);
	if (cpp_proxy.cmd_options->verbose) {
		cout << "Topic: [" << _info.Topic() << "]" << endl;
		cout << _msg.DebugString() << endl;
	}

	json_string.push_back((char) 7);

	if (send(cpp_proxy.socket, json_string.c_str(), json_string.size(), 0) < 0) {
		cerr << "Error: Socket send failed. Closing the socket." << endl;
		close(cpp_proxy.socket);
		cout << "Trying to establish a new connection" << endl;
		cpp_proxy.socket = get_socket();
	}
}


void register_nodes() {
	gz::transport::Node node;

	for (string topic: cpp_proxy.cmd_options->topics) {
		cout << topic << endl;
  		if (!node.Subscribe(topic, callback))
  		{
    		cerr << "Error subscribing to topic [" << topic << "]" << endl;
    		exit(-1);
 		}
	}
	gz::transport::waitForShutdown();
	close(cpp_proxy.socket);
}


int get_socket() {
	struct sockaddr_in server_addr = {
		.sin_family = AF_INET,
		.sin_port = htons(cpp_proxy.cmd_options->port),
		.sin_addr = {
			.s_addr = inet_addr(cpp_proxy.cmd_options->ip.c_str())
		}
	};

	int sock_client;
	if ((sock_client = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		cerr << "Error: socket creation failed" << endl;
		exit(-1);
	}

	while (connect(sock_client, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
	{
		cerr << "Error: connection to server with IP: "\
			<< cpp_proxy.cmd_options->ip << " Port: "\
			<< cpp_proxy.cmd_options->port << " failed." << endl;
		//exit(-1);
		sleep(1);
	}

	cout << "Connection to server with IP: "\
		<< cpp_proxy.cmd_options->ip << " Port: "\
		<< cpp_proxy.cmd_options->port << " established." << endl;

	return sock_client;
}


int main(int argc, char **argv)
{
	CmdOptions cmd_o(argc, argv);
	cpp_proxy.cmd_options = &cmd_o;
	cpp_proxy.socket = get_socket();
	register_nodes();
	return 0;
}
