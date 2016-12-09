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

#define PORT 8556
#define BUFSIZE 512
#define RTT_STORE_SIZE 4096

#define PING_DELAY 100000 // Value in microseconds
#define RTPROP_WINDOW_SIZE 20 // Value in seconds

static pthread_mutex_t rtt_mutex;
static unsigned int rtt_store[RTT_STORE_SIZE]; // RTT values in microseconds
static unsigned int ping_id_iterator;

int rttserver_init(in_addr ipaddr, void *p_sock, struct sockaddr_in *servaddr) {
	// Initialize socket
	ga_error("rttserver: Initializing socket\n");
#ifdef _WIN32
	WSADATA wsa;
	SOCKET *sock = ((SOCKET *) p_sock);
	// Initialize winsock
	if(WSAStartup(MAKEWORD(2,2),&wsa) != 0){
		ga_error("rttserver: Winsock failed to initialize\n");
		exit(EXIT_FAILURE);
	}
	// Create socket
	if((*sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == SOCKET_ERROR){
#else
	int *sock = ((int *) p_sock);
	// Create socket
	if((*sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0){
#endif
		ga_error("rttserver: Failed to create socket\n");
		return -1;
	}

	// Set address structure
	// struct sockaddr_in clientaddr;
	// memset((char *)&clientaddr, 0, sizeof(clientaddr));
	// clientaddr.sin_family = AF_INET;
	// // clientaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	// // clientaddr.sin_port = htons(PORT);
	// clientaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	// clientaddr.sin_port = htons(9876);

	// Bind socket
	// ga_error("rttserver: Binding to socket\n");
	// if(bind(*sock, (struct sockaddr *)&clientaddr, sizeof(clientaddr)) < 0){
	// 	ga_error("rttserver: Failed to bind\n");
	// 	return -1;
	// }

#ifdef _WIN32
	// Set blocking mode of socket
	unsigned long nMode = 1; // 1: NON-BLOCKIN
	ga_error("rttserver: Setting io mode of socket to non-blocking\n");
	if (ioctlsocket(*sock, FIONBIO, &nMode) == SOCKET_ERROR) {
		int status = shutdown(sock, SD_BOTH);
		if(status == 0){ status = closesocket(sock); }
		WSACleanup();
		ga_error("rttserver: Failed to set io mode of socket\n");
		return -1;
	}
#else
	struct timeval read_timeout;
	read_timeout.tv_sec = 0;
	read_timeout.tv_usec = 100;
	setsockopt(*sock, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof(read_timeout));
#endif

	// Set server information
	memset((char *) servaddr, 0, sizeof(*servaddr));
	servaddr->sin_family = AF_INET;
	servaddr->sin_port = htons(PORT);
	servaddr->sin_addr = ipaddr;

	return 0;
}

void rtt_update(unsigned int index, unsigned int rtt_val) {
	// Write to array
	pthread_mutex_lock(&rtt_mutex);
	rtt_store[index] = rtt_val;
	pthread_mutex_unlock(&rtt_mutex);
}

void *
rttserver_thread(void *param) {
	in_addr ipaddr = *((in_addr *) param);
	int recvlen;
	struct timeval start, end;
	struct timeval startTime[RTT_STORE_SIZE]; // Starting times, placed in the index of the ping_id_iterator

	// Initialize socket descriptor
#ifdef _WIN32
	SOCKET sock;
#else
	int sock;
#endif
	struct sockaddr_in servaddr;

	if (rttserver_init(ipaddr, (void *) &sock, &servaddr)) {
		return NULL;
	}

	ctrlmsg_t msg;
	ctrlsys_rttserver(&msg);
	ctrl_client_sendmsg(&msg, sizeof(ctrlmsg_system_rttserver_t));

	socklen_t addrlen = sizeof(servaddr);

	ping_id_iterator = 0;
	// Zero rtt_store
	for (int i = 0; i < RTT_STORE_SIZE; i++) {
		rtt_store[i] = 0;
	}
	pthread_mutex_init(&rtt_mutex, NULL);

	char *buf;
	buf = (char *) malloc(BUFSIZE);
	char *id_buf;
	id_buf = (char *) malloc(9);
	id_buf[8] = 0;

	bool first_ping_sent = false;

	while(1){
		// Buffer ping requests (max 50 per sec for now)
		gettimeofday(&start, NULL);
		unsigned int prev_ping_id = (ping_id_iterator + RTT_STORE_SIZE - 1) % RTT_STORE_SIZE;
		int prev_ping_diff = (start.tv_sec - startTime[prev_ping_id].tv_sec) * 1000000 +
			 (start.tv_usec - startTime[prev_ping_id].tv_usec);

		if (prev_ping_diff >= PING_DELAY || !first_ping_sent) {
			first_ping_sent = true;

			// Convert ping id to string
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
			sprintf_s(id_buf, 9, "%8d", ping_id_iterator);
#else
			sprintf(id_buf, "%8d", ping_id_iterator);
#endif
			ga_error("Sending ping id %d\n", ping_id_iterator);

			// Get start time
			gettimeofday(&startTime[ping_id_iterator], NULL);

			// Send a packet
			if(sendto(sock, id_buf, strlen(id_buf), 0, (struct sockaddr *)&servaddr, addrlen) < 0){
				ga_error("Sendto failed\n");
				break;
			}

			// Increment ping_id_iterator: only the previous RTT_STORE_SIZE entries are tracked.
			ping_id_iterator ++;
			ping_id_iterator %= RTT_STORE_SIZE;
		}

		// Wait to receive a packet
		recvlen = recvfrom(sock, buf, BUFSIZE, 0, NULL, NULL);
		recvlen = 0;

		if (recvlen > 0) {
			// ga_error("rttserver: Received %d bytes\n", recvlen);
			char *read_head = buf;
			buf[recvlen] = 0;

			while (read_head - buf < recvlen) {
				memcpy(id_buf, read_head, 8);
				unsigned int rec_id = atoi(id_buf);
				// Record time of value and compute RTT
				gettimeofday(&end, NULL);
				unsigned int rtt_val = (end.tv_sec - startTime[rec_id].tv_sec) * 1000000.0 + 
					end.tv_usec - startTime[rec_id].tv_usec;

				ga_error("rttserver: ping id = %d, rtt = %d us\n", rec_id, rtt_val);
				
				rtt_update(rec_id, rtt_val);

				read_head += 8;
			}
		}

		// #ifdef _WIN32
		// 	Sleep(10);
		// #else
		// 	sleep(1);
		// #endif
	}
	free(buf);
	free(id_buf);


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


