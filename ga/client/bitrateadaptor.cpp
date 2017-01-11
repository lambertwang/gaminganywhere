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
#include "bitrateadaptor.h"

static unsigned int latest_throughput; // Not thread safe.
static unsigned int bbr_btlbw_start = 0;
static unsigned int bbr_btlbw_head = 0;
static unsigned int last_pkt_timestamp = 0;

void
bbr_update(unsigned int ssrc, unsigned int seq, struct timeval rcvtv, unsigned int timestamp, unsigned int pktsize) {
	// assume ssrc is always video source.
	static struct timeval last_report_sent;
	static struct bbr_btlbw_record_s bbr_btlbw[BBR_BTLBW_MAX];

	// Same frame?
	if(timestamp == last_pkt_timestamp) {
		int prev_index = (bbr_btlbw_head + BBR_BTLBW_MAX - 1) % BBR_BTLBW_MAX;
		bbr_btlbw[prev_index].pktsize += pktsize;
		if (bbr_btlbw[prev_index].timeelapsed == 0) {
			bbr_btlbw[prev_index].deliveryrate = 0;
		} else {
			bbr_btlbw[prev_index].deliveryrate = 1000000 * pktsize / bbr_btlbw[prev_index].timeelapsed;
		}

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
			seek = (seek + 1) % BBR_BTLBW_MAX;
		}
	}
}

float bbr_gain(bbr_state_t *state) {
	float gain = GAIN_MAINTAIN;
	struct timeval now;
	switch (state->stage) {
		case WAITING:
			state->stage = STARTUP;
			ga_error("BBR: Entering startup state\n");
			break;
		case STARTUP:
			// TODO: Startup state seems to end too quickly
			gain = GAIN_INCREASE; // Attempt to double delivery rate
			if (state->start_1 != 0) {
				// Detect plateaus: If less than 25% growth in 3 rounds, leave startup state
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
				if (min(state->start_1, state->start_0) * 5 / 4 > latest_throughput) {
#else
				if (std::min(state->start_1, state->start_0) * 5 / 4 > latest_throughput) {
#endif
					ga_error("BBR: Entering drain state\n");
					state->stage = DRAIN;
				}
			}
			state->start_1 = state->start_0;
			state->start_0 = latest_throughput;
		case DRAIN:
			gain = GAIN_DRAIN; // Inverse of startup state gain
			// Lasts only one round
			state->stage = STANDBY;
			gettimeofday(&state->prev_probe, NULL);
			ga_error("BBR: Entering standby state\n");
			break;
		case STANDBY:
			/**
			 * TODO: Standby state should stop revert probe if the probe
			 * results in no significant increase in latest throughput.
			 */
			if (state->latest_rtt - state->rtprop > 5000) { // 5ms
				gain = GAIN_STANDBY;
				gettimeofday(&state->prev_probe, NULL);
			} else {
				gettimeofday(&now, NULL);
				if (1000000 * (now.tv_sec - state->prev_probe.tv_sec) + 
					(now.tv_usec - state->prev_probe.tv_usec) >
					BBR_PROBE_INTERVAL_US) {
					ga_error("BBR: Probing bandwidth\n");
					gain = GAIN_PROBE;
					gettimeofday(&state->prev_probe, NULL);
				}
			}
			break;
	}
	return gain;
}

void *
bitrateadaptor_thread(void *param) {
	struct timeval now, prev_ping, prev_bbr_cycle;
	bbr_state_t state; 
	// Initial values
	state.stage = WAITING;
	state.start_0 = 0;
	state.start_1 = 0;
	state.bitrate = 1000; // TODO: Read from a conf file

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
			prev_bbr_cycle = now;
			state.rtprop = getRtprop();
			state.latest_rtt = getMaxRecent(BBR_CYCLE_DELAY);
			ga_error("Bitrate adaptor cycke: rtprop: %d us, latest_rtt: %d, latest_throughput: %d (some kind of units)\n",
				state.rtprop, state.latest_rtt, latest_throughput);

			float gain = bbr_gain(&state);
			if (fabs(gain - 1.0) > 0.1) {
				// ga_error("Gain factor: %f\n", gain);
				state.bitrate *= gain;
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
				state.bitrate = min(max(BBR_BITRATE_MINIMUM, state.bitrate), BBR_BITRATE_MAXIMUM);
#else
				state.bitrate = std::min(std::max(BBR_BITRATE_MINIMUM, state.bitrate), BBR_BITRATE_MAXIMUM);
#endif
				
				ctrlmsg_t m_reconf;
				ctrlsys_reconfig(&m_reconf, 0, 0, 0, state.bitrate, 0, 0);
				ctrl_client_sendmsg(&m_reconf, sizeof(ctrlmsg_system_reconfig_t));
			}
		}
	}
	return NULL;
}
