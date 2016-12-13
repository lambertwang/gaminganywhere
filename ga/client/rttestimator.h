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

#ifndef __RTTESTIMATOR_H__
#define __RTTESTIMATOR_H__

#define PING_DELAY 20000 // Value in microseconds
#define RTT_STORE_SIZE 4096 // Size of RTT storing buffer
#define RTPROP_WINDOW_SIZE 20 // Value in secondss

#define PKTBUF 512 // Originally defined in ga-server-periodic.cpp
#define PKTPORT 8556 // Originally defined in ga-server-periodic.cpp

typedef struct bbr_rtt_s {
	struct timeval time_record;
	unsigned int rtt_id;
}	bbr_rtt_t;

void * rttestimator_thread(void *param);

unsigned int getRtprop();
unsigned int getMaxRecent(unsigned int timeframe); // Get the largest RTT value recorded in the current window.

#endif