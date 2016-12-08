/*
 * Copyright (c) 2016 Chun-Ying Huang
 *
 * This file is part of GamingAnywhere (GA).
 *
 * GA is free software; you can redistribute it and/or modify it
 * under the terms of the 3-clause BSD License as published by the
 * Free Software Foundation: http://directory.fsf.org/wiki/License:BSD_3Clause
 *
 * GA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the 3-clause BSD License along with GA;
 * if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <Ws2tcpip.h>
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#endif

#include "ga-common.h"
#include "controller.h"

#define PORT 8556
#define BUFSIZE 512
#define WINDOWNUM 5000

void *
udpping_thread(void *param) {
	char *ipaddr = (char *)param;
	int counter = 0;
	int recvlen;
	struct timeval begin, end;
	double startTime[WINDOWNUM]; // Starting times, placed in the index of the counter
	double respTime[WINDOWNUM]; // RTT values

	// Initialize socket
	#ifdef _WIN32
		WSADATA wsa;
		SOCKET sock;
		// Initialize winsock
		if(WSAStartup(MAKEWORD(2,2),&wsa) != 0){
			ga_error("Winsock failed to initialize\n");
			exit(EXIT_FAILURE);
		}
		// Create socket
		if((sock = socket(AF_INET, SOCK_DGRAM, 0)) == SOCKET_ERROR){
			ga_error("Failed to create socket\n");
			return NULL;
		}
	#else
		int sock;
		// Create socket
		if((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0){
			ga_error("Failed to create socket\n");
			return NULL;
		}
	#endif

	// Set address structure
	struct sockaddr_in clientaddr;
	memset((char *)&clientaddr, 0, sizeof(clientaddr));
	clientaddr.sin_family = AF_INET;
	clientaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	clientaddr.sin_port = htons(PORT);

	// Bind socket
	if(bind(sock, (struct sockaddr *)&clientaddr, sizeof(clientaddr)) < 0){
		ga_error("Failed to bind\n");
		return NULL;
	}

	// Set server information
	struct sockaddr_in servaddr;
	memset((char *)&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(PORT);
	socklen_t addrlen = sizeof(servaddr);
	//servaddr.sin_addr.s_addr = [IP address, pull from main]

	char *buf;
	buf = (char *) malloc(BUFSIZE);

	while(1){
		// Convert to string
		char str[5];
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
		sprintf_s(str, 5, "%d", counter);
#else
		sprintf(str, "%d", counter);
#endif

		// Get start time and store for packet value
		gettimeofday(&begin, NULL);
		startTime[counter] = ((begin.tv_sec * 1000.0) + (begin.tv_usec / 1000.0));

		// Send a packet
		if(sendto(sock, str, strlen(str), 0, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0){
			ga_error("Sendto failed\n");
			break;
		}

		// Wait to receive a packet
		recvlen = recvfrom(sock, buf, BUFSIZE, 0, (struct sockaddr *)&servaddr, &addrlen);
		if(recvlen > 0){
			buf[recvlen] = 0;
			// Record time of value and compute RTT
			// TODO: pull integer from message, use that counter value
			gettimeofday(&end, NULL);
			double endTime = ((end.tv_sec * 1000.0) + (end.tv_usec / 1000.0));
			respTime[counter] = endTime - startTime[counter];

			#ifdef _WIN32
				Sleep(1);
			#else
				sleep(1);
			#endif
		}

		// Increment counter: only the previous 100 entries are tracked.
		counter++;
		if(counter >= WINDOWNUM){
			counter = 0;
		}
	}
	free(buf);


	#ifdef _WIN32
		WSACleanup();
	#endif

	int status = 0;
	#ifdef _WIN32
		status = shutdown(sock, SD_BOTH);
		if(status == 0){ status = closesocket(sock); }
	#else
		status = shutdown(sock, SHUT_RDWR);
		if(status == 0){ status = close(sock); }
	#endif
    
	return NULL;
}
