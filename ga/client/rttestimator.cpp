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
#include <pthread.h>

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
#include "rttestimator.h"

#define PORT 8556
#define BUFSIZE 512

static pthread_mutex_t rtt_mutex;
static unsigned int rtt_store[RTT_STORE_SIZE]; // RTT values in microseconds
static unsigned int ping_id_iterator;

int rttestimator_init(in_addr ipaddr, void *p_sock, struct sockaddr_in servaddr) {
	// Initialize socket
	ga_error("rttestimator: Initializing socket\n");
#ifdef _WIN32
	WSADATA wsa;
	SOCKET *sock = ((SOCKET *) p_sock);
	// Initialize winsock
	if(WSAStartup(MAKEWORD(2,2),&wsa) != 0){
		ga_error("rttestimator: Winsock failed to initialize\n");
		exit(EXIT_FAILURE);
	}
	// Create socket
	if((*sock = socket(AF_INET, SOCK_DGRAM, 0)) == SOCKET_ERROR){
#else
	int *sock = ((int *) p_sock);
	// Create socket
	if((*sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0){
#endif
		ga_error("rttestimator: Failed to create socket\n");
		return -1;
	}

	// Bind socket
	ga_error("rttestimator: Binding to socket\n");
	if(bind(*sock, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0){
		char *errmsg;
		errmsg = strerror(errno);
		ga_error("rttestimator: Failed to bind - %s\n", errmsg);
		return -1;
	}

	return 0;
}

// Update the RTT array given the specified index and value.
// Stores calculated response times returned from the UDP connection.
void rtt_update(unsigned int index, unsigned int rtt_val) {
	// Write to array
	pthread_mutex_lock(&rtt_mutex);
	rtt_store[index] = rtt_val;
	pthread_mutex_unlock(&rtt_mutex);
}

// Primary thread for RTT estimation:
// The client opens a connection with the server, and sends an RTT ctrlmsg.
// It then listens for responses from the server.
void *
rttestimator_thread(void *param) {
	in_addr ipaddr = *((in_addr *) param);
	unsigned int recvlen;
	struct timeval end;

	// Initialize socket descriptor
#ifdef _WIN32
	SOCKET sock;
#else
	int sock;
#endif

	struct sockaddr_in servaddr;
	// Set server information
	memset(&servaddr, 0, sizeof(struct sockaddr_in));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(PORT);

	if (rttestimator_init(ipaddr, (void *) &sock, servaddr)) {
		return NULL;
	}

	// Send ctrlmsg to server to initiate server messages
	ga_error("rttestimator_thread: Sending rttestimator ctrl message\n");
	ctrlmsg_t msg;
	ctrlsys_rttestimator(&msg);
	ctrl_client_sendmsg(&msg, sizeof(ctrlmsg_system_rttestimator_t));

	ping_id_iterator = 0;
	// Zero rtt_store
	for (int i = 0; i < RTT_STORE_SIZE; i++) {
		rtt_store[i] = 0;
	}
	pthread_mutex_init(&rtt_mutex, NULL);

	char *buf;
	buf = (char *) malloc(BUFSIZE);

	// Loop forever
	while(1){
		// Wait to receive a packet
		recvlen = recvfrom(sock, buf, BUFSIZE, 0, NULL, NULL);

		if (recvlen >= sizeof(bbr_rtt_t)) {
			char *read_head = buf;
			bbr_rtt_t read_data;

			while (read_head - buf < recvlen) {
				memcpy(&read_data, read_head, sizeof(bbr_rtt_t));
				// Record time of value and compute RTT
				gettimeofday(&end, NULL);
				unsigned int rtt_val = (end.tv_sec - read_data.time_record.tv_sec) * 1000000.0 + 
					end.tv_usec - read_data.time_record.tv_usec;
				
				// TODO: Take into account dropped packets or skipped replies.
				rtt_update(read_data.rtt_id, rtt_val);

				read_head += sizeof(bbr_rtt_t);
			}
		}
	}
	free(buf);

// Clean up sockets and shut down the connection
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

unsigned int getRtprop() {
	unsigned int ret = UINT_MAX;
	int rtprop_window_size = RTPROP_WINDOW_SIZE * 1000000 / PING_DELAY;
	
	pthread_mutex_lock(&rtt_mutex);
	for (unsigned int i = ping_id_iterator; 
		i != (ping_id_iterator + RTT_STORE_SIZE - rtprop_window_size) % RTT_STORE_SIZE; 
		i = (i + RTT_STORE_SIZE - 1) % RTT_STORE_SIZE) {
		if (rtt_store[i] != 0 && rtt_store[i] < ret) {
			ret = rtt_store[i];
		}
	}
	pthread_mutex_unlock(&rtt_mutex);

	return ret;
}

// Get the largest RTT value recorded in the current window.
unsigned int getMaxRecent(unsigned int timeframe) {
	unsigned int ret = 0;
	int rtt_window_size = timeframe / PING_DELAY;
	
	pthread_mutex_lock(&rtt_mutex);
	for (unsigned int i = ping_id_iterator; 
		i != (ping_id_iterator + RTT_STORE_SIZE - rtt_window_size) % RTT_STORE_SIZE; 
		i = (i + RTT_STORE_SIZE - 1) % RTT_STORE_SIZE) {
		if (rtt_store[i] > ret) {
			ret = rtt_store[i];
		}
	}
	pthread_mutex_unlock(&rtt_mutex);

	return ret;
}


