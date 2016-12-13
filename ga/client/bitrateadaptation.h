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

#ifndef __BITRATEADAPTATION_H__
#define __BITRATEADAPTATION_H__

// Record windowed maximum of delivery rate
#define BBR_BTLBW_MAX 256
#define BBR_BTLBW_WINDOW_SIZE_US (1000 * 1000)
#define BBR_BTLBW_REPORT_PERIOD_US (500 * 1000)

// Set minimum and maximum bitrates for BBR
#define BBR_BITRATE_MINIMUM 50
#define BBR_BITRATE_MAXIMUM 30000

#define BBR_CYCLE_DELAY 500000 // Value in microseconds
#define BBR_PROBE_INTERVAL_US (4 * 1000 * 1000) // 4 seconds

/**
 * BBR Constants
 * The following variables are constants defined within BBR:
 * Contestion-Based Control, a publication posted in Volume 14, Issue 5
 * of the acm queue.
 *
 * Cardwell, N., Cheng, Y., Gunn, S., Jacobson, V., & Yeganeh, S. (2016, September/October). 
 * BBR Congestion-Based Congestion Control. ACMQueue, 20-53. Retrieved December 13, 2016, 
 * from http://queue.acm.org/app/
 */
// Needs to be filled in: 5/4, .5, .75

/**
 * BBR State table
 *-1 : Waiting on RTT
 *		Leaves once RTT can be acquired
 * 0 : Startup
 *		Leaves once BtlBw is found
 *		When 3 consecutive startup steps do not result in a doubled delivery rate. 
 * 1 : Drain
 *		Leaves once Excess created by startup is drained
 * 2 : Probe / steady state
 */
enum BBR_State{
	waiting,
	startup,
	drain,
	standby
};

typedef struct bbr_state_s {
	enum BBR_State stage;	// Current BBR stage: One of Waiting, Startup, Drain, or Standby
	unsigned int start_0;	// Previous throughput value
	unsigned int start_1;	// Newest throughput value
	struct timeval prev_probe;	// Time that the last probe took place
	int rtprop; // Time delta values in microseconds
	int latest_rtt;	// Set by getMaxRecent, equal to the max RTT in the window.
	int bitrate;	// Bitrate of system
} bbr_state_t;

void bbr_update(
    unsigned int ssrc, 
    unsigned int seq, 
    struct timeval rcvtv, 
    unsigned int timestamp, 
    unsigned int pktsize
    );

void * bitrateadaptation_thread(void *param);

#endif