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
#ifndef WIN32
#include <unistd.h>
#endif

#include "ga-common.h"
#include "controller.h"
#include "rttserver.h"
#include "bitrateadaptation.h"

typedef struct bbr_btlbw_record_s {
	struct timeval rcvtime;
	unsigned int pktsize;
	unsigned int timeelapsed; // Time in usec
	unsigned int deliveryrate; // In bytes per sec
}	bbr_record_t;

// Record windowed maximum of delivery rate
#define BBR_BTLBW_MAX 256
#define BBR_BTLBW_WINDOW_SIZE_US (1000 * 1000)
#define BBR_BTLBW_REPORT_PERIOD_US (500 * 1000)
struct bbr_btlbw_record_s bbr_btlbw[BBR_BTLBW_MAX];
unsigned int bbr_btlbw_start = 0;
unsigned int bbr_btlbw_head = 0;
unsigned int last_pkt_timestamp = 0;
struct timeval last_report_sent;
unsigned int latest_throughput; // Not thread safe.

void
bbr_update(unsigned int ssrc, unsigned int seq, struct timeval rcvtv, unsigned int timestamp, unsigned int pktsize) {
	// assume ssrc is always video source.

	// Same frame?
	if(timestamp == last_pkt_timestamp) {
		int prev_index = (bbr_btlbw_head + BBR_BTLBW_MAX - 1) % BBR_BTLBW_MAX;
		bbr_btlbw[prev_index].pktsize += pktsize;
		if (bbr_btlbw[prev_index].timeelapsed == 0) {
			bbr_btlbw[prev_index].deliveryrate = 0;
		} else {
			bbr_btlbw[prev_index].deliveryrate = 1000000 * pktsize / bbr_btlbw[prev_index].timeelapsed;
		}
		// ga_error("Updated Frame Size: %u Elapsed: %u Rate: %u\n", 
			// pktsize, 
			// bbr_btlbw[bbr_btlbw_head].timeelapsed, 
			// bbr_btlbw[bbr_btlbw_head].deliveryrate);
		return;
	}

	last_pkt_timestamp = timestamp;

	bbr_btlbw[bbr_btlbw_head].rcvtime = rcvtv;
	bbr_btlbw[bbr_btlbw_head].pktsize = pktsize;
	if (bbr_btlbw_start != bbr_btlbw_head) {
		int prev_index = (bbr_btlbw_head + BBR_BTLBW_MAX - 1) % BBR_BTLBW_MAX;
		bbr_btlbw[bbr_btlbw_head].timeelapsed = 
			1000000 * (rcvtv.tv_sec - bbr_btlbw[prev_index].rcvtime.tv_sec) + 
			rcvtv.tv_usec - bbr_btlbw[prev_index].rcvtime.tv_usec;
		if (bbr_btlbw[bbr_btlbw_head].timeelapsed == 0) {
			bbr_btlbw[bbr_btlbw_head].deliveryrate = 0;
		} else {
			bbr_btlbw[bbr_btlbw_head].deliveryrate = 1000000 * pktsize / bbr_btlbw[bbr_btlbw_head].timeelapsed;
		}
		// ga_error("New Frame Size: %u Elapsed: %u Rate: %u\n", 
		// 	pktsize, 
		// 	bbr_btlbw[bbr_btlbw_head].timeelapsed, 
		// 	bbr_btlbw[bbr_btlbw_head].deliveryrate);
	} else {
		// Should only occur on first packet received
		bbr_btlbw[bbr_btlbw_head].timeelapsed = 0;
		bbr_btlbw[bbr_btlbw_head].deliveryrate = 0;
		last_report_sent = rcvtv;
	}

	bbr_btlbw_head = (bbr_btlbw_head + 1) % BBR_BTLBW_MAX;
	if (bbr_btlbw_head == bbr_btlbw_start) {
		bbr_btlbw_start = (bbr_btlbw_start + 1) % BBR_BTLBW_MAX;
	}


	if (1000000 * (rcvtv.tv_sec - last_report_sent.tv_sec) + 
		rcvtv.tv_usec - last_report_sent.tv_usec > BBR_BTLBW_REPORT_PERIOD_US) {
		last_report_sent = rcvtv;

		// unsigned int max_deliveryrate = 0;

		while (1000000 * (rcvtv.tv_sec - bbr_btlbw[bbr_btlbw_start].rcvtime.tv_sec) + 
			rcvtv.tv_usec - bbr_btlbw[bbr_btlbw_start].rcvtime.tv_usec > BBR_BTLBW_WINDOW_SIZE_US) {
			bbr_btlbw_start = (bbr_btlbw_start + 1) % BBR_BTLBW_MAX;
		}

		unsigned int seek = bbr_btlbw_start;
		latest_throughput = 0;
		while (seek != bbr_btlbw_head) {
			latest_throughput += bbr_btlbw[seek].pktsize;
			// if (bbr_btlbw[seek].deliveryrate > max_deliveryrate) {
			// 	max_deliveryrate = bbr_btlbw[seek].deliveryrate;
			// }
			seek = (seek + 1) % BBR_BTLBW_MAX;
		}
	}
}

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
#define BBR_PROBE_INTERVAL_US (4 * 1000 * 1000) // 4 seconds

typedef struct bbr_state_s {
	int stage;
	unsigned int start_0;
	unsigned int start_1;
	struct timeval prev_probe;
	int rtprop; // Time delta values in microseconds
	int latest_rtt;
	int bitrate;
} bbr_state_t;

float bbr_gain(bbr_state_t *state) {
	float gain = 1.0;
	struct timeval now;
	switch (state->stage) {
		case -1:
			state->stage = 0;
			ga_error("BBR: Entering startup state\n");
			break;
		case 0:
			gain = 2; // Attempt to double delivery rate
			if (state->start_1 != 0) {
				// Detect plateaus: If less than 25% growth in 3 rounds, leave startup state
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
				if (min(state->start_1, state->start_0) * 5 / 4 > latest_throughput) {
#else
				if (std::min(state->start_1, state->start_0) * 5 / 4 > latest_throughput) {
#endif
					ga_error("BBR: Entering drain state\n");
					state->stage = 1;
				}
			}
			state->start_1 = state->start_0;
			state->start_0 = latest_throughput;
		case 1:
			gain = .5; // Inverse of startup state gain
			// Lasts only one round
			state->stage = 2;
			gettimeofday(&state->prev_probe, NULL);
			ga_error("BBR: Entering standby state\n");
			break;
		case 2:
			if (state->latest_rtt - state->rtprop > 5000) { // 5ms
				gain = .75;
				gettimeofday(&state->prev_probe, NULL);
			} else {
				gettimeofday(&now, NULL);
				if (1000000 * (now.tv_sec - state->prev_probe.tv_sec) + 
					(now.tv_usec - state->prev_probe.tv_usec) >
					BBR_PROBE_INTERVAL_US) {
					ga_error("BBR: Probing bandwidth\n");
					gain = 1.25;
					gettimeofday(&state->prev_probe, NULL);
				}
			}
			break;
	}
	return gain;
}

void *
bitrateadaptation_thread(void *param) {
	struct timeval now, prev_ping, prev_bbr_cycle;
	bbr_state_t state; 
	// Initial values
	state.stage = -1;
	state.start_0 = 0;
	state.start_1 = 0;
	state.bitrate = 1000; // Should probably read a conf file or something

	ga_error("reconfigure thread started ...\n");
	unsigned int ping_count = 0;
	gettimeofday(&prev_ping, NULL);
	gettimeofday(&prev_bbr_cycle, NULL);
	while(1) {
		gettimeofday(&now, NULL);
		long delta = (now.tv_sec - prev_ping.tv_sec) * 1000000 + (now.tv_usec - prev_ping.tv_usec);
		if (delta >= PING_DELAY) {
			ctrlmsg_t m_ping;
			ctrlsys_ping(&m_ping, ping_count, now);
			ping_count = (ping_count + 1) % RTT_STORE_SIZE;
			ctrl_client_sendmsg(&m_ping, sizeof(ctrlmsg_system_ping_t));
			prev_ping = now;
		}

		delta = (now.tv_sec - prev_bbr_cycle.tv_sec) * 1000000 + (now.tv_usec - prev_bbr_cycle.tv_usec);
		if (delta >= BBR_CYCLE_DELAY) {
			state.rtprop = getRtprop();
			state.latest_rtt = getMaxRecent(BBR_CYCLE_DELAY);

			float gain = bbr_gain(&state);
			if (fabs(gain - 1.0) > 0.1) {
				// ga_error("Gain factor: %f\n", gain);
				state.bitrate *= gain;
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
				state.bitrate = min(max(BBR_BITRATE_MINIMUM, state.bitrate), BBR_BITRATE_MAXIMUM);
#else
				state.bitrate = std::min(std::max(BBR_BITRATE_MINIMUM, state.bitrate), BBR_BITRATE_MAXIMUM);
#endif
				
				// ga_error("Sending reconfiguration message\n");
				ctrlmsg_t m_reconf;
				ctrlsys_reconfig(&m_reconf, 0, 0, 0, state.bitrate, 0, 0);
				ctrl_client_sendmsg(&m_reconf, sizeof(ctrlmsg_system_reconfig_t));

				// reconf.id = 0;
				// reconf.framerate_n = -1;
				// reconf.framerate_d = 1;
				// reconf.width = -1;
				// reconf.height = -1;

				// reconf.crf = -1;
				// reconf.bitrateKbps = state.bitrate;

				// encoder
				// if(m_vencoder->ioctl) {
				// 	int err = m_vencoder->ioctl(GA_IOCTL_RECONFIGURE, sizeof(reconf), &reconf);
				// 	if(err < 0) {
				// 		ga_error("reconfigure encoder failed, err = %d.\n", err);
				// 	} else {
				// 		ga_error("reconfigure encoder OK, bitrate=%d; bufsize=%d; framerate=%d/%d.\n",
				// 				reconf.bitrateKbps, reconf.bufsize,
				// 				reconf.framerate_n, reconf.framerate_d);
				// 	}
				// }
			}
		}
	}
	return NULL;
}
