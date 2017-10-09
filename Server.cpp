#include "Server.h"
#include <thread>
#include "lib/rapidjson/document.h"
#include "lib/rapidjson/stringbuffer.h"
#include "lib/main.h"
#include <iostream>
#include <fstream>
using namespace rapidjson;

extern std::ofstream debugfile;
void wait(int);

Server::Server(unsigned int port) {
	struct sockaddr_in server;
	freopen("deepgtav.log", "w", stdout);

	printf("\nInitializing Winsock...");
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		printf("Failed. Error Code: %d", WSAGetLastError());
	}
	printf("Initialized.\n");

	if ((ServerSocket = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
		printf("Could not create socket: %d", WSAGetLastError());
	}
	printf("Socket created.\n");

	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons(port);

	if (bind(ServerSocket, (struct sockaddr *)&server, sizeof(server)) == SOCKET_ERROR) {
		printf("Bind failed with error code: %d", WSAGetLastError());
	}
	printf("Bind done.\n");

	printf("Listening...\n");
	if (listen(ServerSocket, 1) == SOCKET_ERROR) {
		printf("Could not listen: %d", WSAGetLastError());
	}

	if (ioctlsocket(ServerSocket, FIONBIO, &iMode) != NO_ERROR) {
		printf("Server ioctlsocket failed");
	}
	scenario.server = this;

}

void Server::checkClient(){
	//debugfile << "Inside checkClient()" << std::endl;
	SOCKET tmpSocket = SOCKET_ERROR;
	tmpSocket = accept(ServerSocket, NULL, NULL);
	if (tmpSocket != SOCKET_ERROR) {
		printf("Connection accepted.\n");
		ClientSocket = tmpSocket;
		if (ioctlsocket(ClientSocket, FIONBIO, &iMode) != NO_ERROR) {
			printf("Client ioctlsocket failed");
			return;
		}
		clientConnected = true;
	}
}

void Server::checkRecvMessage() {
	int result;
	Document d;
	int error;
	
	if (recvMessageLen == 0) {
		recv(ClientSocket, (char*)&recvMessageLen, 4, 0); //Receive message len first

		error = WSAGetLastError();
		if (error == WSAEWOULDBLOCK) return;
		if (error != 0) {
			printf("\nError receiving message length: %d", error);
			resetState();
			return;
		}
	}

	while (bytesRead < recvMessageLen){

		result = recv(ClientSocket, json + bytesRead, recvMessageLen - bytesRead, 0);

		error = WSAGetLastError();
		if (error == WSAEWOULDBLOCK) return;
		if (error != 0 || result < 1) {
			printf("\nError receiving message: %d", error);
			resetState();
			return;
		}
		bytesRead = bytesRead + result;
	}

	json[bytesRead] = '\0';
	bytesRead = 0;
	recvMessageLen = 0;

	d.Parse(json);

	if (d.HasMember("commands")) {
		printf("Commands received\n");
		const Value& commands = d["commands"];
		scenario.setCommands(commands["throttle"].GetFloat(), commands["brake"].GetFloat(), commands["steering"].GetFloat());
	}
	else if (d.HasMember("config")) {
		//Change the message values and keep the others the same
		printf("Config received\n");
		const Value& config = d["config"];
		const Value& sc = config["scenario"];
		const Value& dc = config["dataset"];
		scenario.config(sc, dc);
	}
	else if (d.HasMember("start")) {
		//Set the message values and randomize the others. Start sending the messages
		printf("Start received\n");
		const Value& config = d["start"];
		const Value& sc = config["scenario"];
		const Value& dc = config["dataset"];
		scenario.start(sc, dc);
		sendOutputs = true;
	}
	else if (d.HasMember("stop")) {
		//Stop sendig messages, keep client connected
		printf("Stop received\n");
		sendOutputs = false;
		scenario.stop();
	}
	else if (d.HasMember("formal_configs")) {
		const Value& cfgs = d["formal_configs"];

		scenario.screenCapturer = new ScreenCapturer(1920, 1200);
		isFormalScenarios = true;
		scenario.buildFormalScenarios(cfgs, this);
		isFormalScenarios = false;
	}
	else {
		return; //Invalid message
	}
}

void Server::checkSendMessage() {
	int error;
	int r;
	
	//if (isFormalScenarios) {
	//	debugfile << "Capturing ..." << std::endl;
	//	wait(10);
	//	scenario.screenCapturer->capture();
	//	//wait(10);
	//}

	if (isFormalScenarios || (sendOutputs && (((float)(std::clock() - lastSentMessage) / CLOCKS_PER_SEC) > (1.0 / scenario.rate)))) {
		if (messageSize == 0) {			
			message = scenario.generateMessage();
			chmessage = message.GetString();
			messageSize = message.GetSize();
			debugfile << "message size is .." << messageSize << std::endl;
			debugfile << "The message is .." << chmessage << std::endl;
		}

		debugfile << "AA" << std::endl;
		if (!frameSent) {
			debugfile << "BB" << std::endl;

			if (!readyToSend) {
				debugfile << "CC" << std::endl;
				debugfile << "Sending frame..." << std::endl;
				send(ClientSocket, (const char*)&scenario.screenCapturer->length, sizeof(scenario.screenCapturer->length), 0);
				error = WSAGetLastError();
				debugfile << "DD" << std::endl;
				if (error == WSAEWOULDBLOCK) return;
				debugfile << "EE" << std::endl;

				if (error != 0) {
					printf("\nError sending frame length: %d", error);
					resetState();
					return;
				}
				
				debugfile << "FF" << std::endl;
				readyToSend = true;
				sendMessageLen = 0;
			}

			while (readyToSend && (sendMessageLen < scenario.screenCapturer->length)) {
				r = send(ClientSocket, (const char*)(scenario.screenCapturer->pixels + sendMessageLen), scenario.screenCapturer->length - sendMessageLen, 0);
				error = WSAGetLastError();
				if (error == WSAEWOULDBLOCK) return;
				if (error != 0 || r <= 1) {
					printf("\nError sending frame: %d", error);
					resetState();
					return;
				}
				sendMessageLen = sendMessageLen + r;
			}
			debugfile << "GG" << std::endl;
			readyToSend = false;
			frameSent = true;
		}
		debugfile << "HH" << std::endl;
		if (frameSent) {
			debugfile << "II" << std::endl;
			if (!readyToSend) {
				debugfile << "JJ" << std::endl;
				send(ClientSocket, (const char*)&messageSize, sizeof(messageSize), 0);
				error = WSAGetLastError();
				if (error == WSAEWOULDBLOCK) return;
				if (error != 0) {
					printf("\nError sending message length: %d", error);
					resetState();
					return;
				}
				debugfile << "KK" << std::endl;
				readyToSend = true;
				sendMessageLen = 0;
			}

			while (readyToSend && (sendMessageLen < messageSize)) {
				debugfile << "LL" << std::endl;
				debugfile << chmessage << std::endl;
				do {
					debugfile << "AAA" << std::endl;
					r = send(ClientSocket, (const char*)(chmessage + sendMessageLen), messageSize - sendMessageLen, 0);
					debugfile << "MM" << std::endl;
					error = WSAGetLastError();
					debugfile << "OO" << std::endl;				
				} while (error == WSAEWOULDBLOCK);
				//if (error == WSAEWOULDBLOCK) return;
				debugfile << "PP" << std::endl;
				if (error != 0 || r <= 1) {
					printf("\nError sending message: %d", error);
					resetState();
					return;
				}
				debugfile << "NN" << std::endl;
				sendMessageLen = sendMessageLen + r;
			}
			readyToSend = false;
			messageSize = 0;
			frameSent = false;
		}
		lastSentMessage = std::clock();
	}
}

void Server::resetState() {
	shutdown(ClientSocket, SD_SEND);
	closesocket(ClientSocket);

	clientConnected = false;
	sendOutputs = false;
	bytesRead = 0;
	recvMessageLen = 0;
	sendMessageLen = 0;
	readyToSend = false;
	frameSent = false;
	messageSize = 0;

	scenario.stop();
}