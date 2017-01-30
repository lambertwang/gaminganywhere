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

#define BBR_GRAPH

#include <stdlib.h>
#ifndef WIN32
#include <unistd.h>
#endif

#include "ga-common.h"
#include "ga-conf.h"
#include "controller.h"
#include "rttserver.h"
#include "bitrateadaptor.h"

static struct bbr_btlbw_record_s reported_bbr_record; // Not thread safe.
static struct bbr_state_s bbr_state;

void
bbr_update(unsigned int ssrc, unsigned int seq, struct timeval rcvtv, unsigned int timestamp, unsigned int pktsize) {
	// assume ssrc is always video source.
	static struct timeval last_report_sent;
	static struct bbr_btlbw_record_s bbr_btlbw[BBR_BTLBW_MAX];
	static unsigned int bbr_btlbw_start = 0;
	static unsigned int bbr_btlbw_head = 0;
	static unsigned int last_pkt_timestamp = 0;


	// Same frame?
	int prev_index = (bbr_btlbw_head + BBR_BTLBW_MAX - 1) % BBR_BTLBW_MAX;
	if(timestamp == last_pkt_timestamp) {
		bbr_btlbw[prev_index].pktsize += pktsize;
	} else {
		// New frame

#ifdef BBR_GRAPH
		static FILE* bbr_graph_csv = NULL;
		static unsigned long init_time = 0;

		// Write previous bbr record data to csv
		char buffer[1024];

		if (bbr_graph_csv != NULL) {
			struct bbr_btlbw_record_s prev_record = bbr_btlbw[prev_index];
			if (prev_record.rtprop != UINT_MAX) {
				if (init_time == 0) {
					init_time = prev_record.rcvtime.tv_sec * 1000000 + prev_record.rcvtime.tv_usec;
				}
				int char_written = sprintf(buffer, "%.3f, %u, %d, %u, %u, %d\n", 
					(float) (prev_record.rcvtime.tv_sec * 1000000 + prev_record.rcvtime.tv_usec - init_time) / 1000000.0,
					prev_record.pktsize,
					reported_bbr_record.throughput,
					prev_record.latest_rtt,
					prev_record.rtprop,
					bbr_state.bitrate);
				fwrite(buffer, sizeof(char), char_written, bbr_graph_csv);
			}
		} else {
			// Open log file
			bbr_graph_csv = fopen("bbr_graph.csv", "w");
			int char_written = sprintf(buffer, "rcvtime, pktsize, throughput, rtt, rtprop, bitrate\n");
			fwrite(buffer, sizeof(char), char_written, bbr_graph_csv);
		}
#endif

		// Otherwise create new frame
		last_pkt_timestamp = timestamp;

		bbr_btlbw[bbr_btlbw_head].rcvtime = rcvtv;
		bbr_btlbw[bbr_btlbw_head].pktsize = pktsize;
		bbr_btlbw[bbr_btlbw_head].rtprop = getRtprop();
		bbr_btlbw[bbr_btlbw_head].latest_rtt = getMaxRecent(BBR_CYCLE_DELAY);

		if (bbr_btlbw_start != bbr_btlbw_head) {
			int prev_index = (bbr_btlbw_head + BBR_BTLBW_MAX - 1) % BBR_BTLBW_MAX;
			bbr_btlbw[bbr_btlbw_head].timeelapsed = 
				1000000 * (rcvtv.tv_sec - bbr_btlbw[prev_index].rcvtime.tv_sec) + 
				rcvtv.tv_usec - bbr_btlbw[prev_index].rcvtime.tv_usec;
		} else {
			// Should only occur on first packet received
			bbr_btlbw[bbr_btlbw_head].timeelapsed = 0;
			last_report_sent = rcvtv;
		}

		bbr_btlbw_head = (bbr_btlbw_head + 1) % BBR_BTLBW_MAX;
		if (bbr_btlbw_head == bbr_btlbw_start) {
			bbr_btlbw_start = (bbr_btlbw_start + 1) % BBR_BTLBW_MAX;
		}
	}


	// Update latest throughput
	if (1000000 * (rcvtv.tv_sec - last_report_sent.tv_sec) + 
		rcvtv.tv_usec - last_report_sent.tv_usec > BBR_BTLBW_REPORT_PERIOD_US) {
		last_report_sent = rcvtv;

		while (1000000 * (rcvtv.tv_sec - bbr_btlbw[bbr_btlbw_start].rcvtime.tv_sec) + 
			rcvtv.tv_usec - bbr_btlbw[bbr_btlbw_start].rcvtime.tv_usec > BBR_CYCLE_DELAY) {
			bbr_btlbw_start = (bbr_btlbw_start + 1) % BBR_BTLBW_MAX;
		}

		// Sum size of packets received to return throughput
		unsigned int seek = bbr_btlbw_start;
		reported_bbr_record.throughput = 0;
		while (seek != bbr_btlbw_head) {
			reported_bbr_record.throughput += bbr_btlbw[seek].pktsize;
			seek = (seek + 1) % BBR_BTLBW_MAX;
		}

		// Report latest rt information received
		prev_index = (bbr_btlbw_head + BBR_BTLBW_MAX - 1) % BBR_BTLBW_MAX;
		reported_bbr_record.rtprop = bbr_btlbw[prev_index].rtprop;
		reported_bbr_record.latest_rtt = bbr_btlbw[prev_index].latest_rtt;
	}
}

float bbr_gain(bbr_state_t *state) {
	float gain = GAIN_MAINTAIN;
	struct timeval now;

	// RTT will be UINT_MAX while the server is uninitialized
	if (reported_bbr_record.rtprop == UINT_MAX) {
		return gain;
	}

	switch (state->stage) {
		case WAITING:
			state->stage = STARTUP;
			ga_error("BBR: Entering startup state\n");
			break;
		case STARTUP:
			if (state->start_1 != 0) {
				// Attempt to double delivery rate
				gain = GAIN_INCREASE;
				if (state->gain > GAIN_MAINTAIN) {
					// Detect plateaus: If less than 25% growth in 3 rounds, leave startup state
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
					int plateau_thresh = min(state->start_1, state->start_0) * PLATEAU_GROWTH;
#else
					int plateau_thresh = std::min(state->start_1, state->start_0) * PLATEAU_GROWTH;
#endif	
					// Check for plateau and bottleneck. When the rtt is 5ms greater than rtprop, a queue was created
					// Also leave startup rate if bitrate has reached maximum
					if (plateau_thresh > reported_bbr_record.throughput ||
						state->latest_rtt - state->rtprop > 5000 ||
						state->bitrate == BBR_BITRATE_MAXIMUM) {
						ga_error("BBR: Plateau detected, leaving startup state.\n");
						state->stage = STANDBY;
						gettimeofday(&state->prev_probe, NULL);
					}
					// Only drain if a queue was detected created
					if (state->latest_rtt - state->rtprop > 5000) {
						ga_error("BBR: Beginning startup drain\n");
						gain = GAIN_DRAIN;
					}
					ga_error("BBR: plateau_thresh: %d, throughput: %d\n", plateau_thresh, reported_bbr_record.throughput);
				}
			}
			state->start_1 = state->start_0;
			state->start_0 = reported_bbr_record.throughput;
			break;
		case STANDBY:
			/**
			 * TODO: Standby state should revert probe if the probe
			 * results in no significant increase in latest throughput.
			 */
			// Detect queues in bottleneck
			if (state->latest_rtt - state->rtprop > 5000) { // 5ms
				gain = GAIN_STANDBY;
				gettimeofday(&state->prev_probe, NULL);
			} else {
				gettimeofday(&now, NULL);
				int timediff = 1000000 * (now.tv_sec - state->prev_probe.tv_sec) + 
					(now.tv_usec - state->prev_probe.tv_usec);
				if (timediff > BBR_PROBE_INTERVAL_US) {
					// Do not probe if already at max
					if (state->bitrate < BBR_BITRATE_MAXIMUM) {
						ga_error("BBR: Probing bandwidth\n");
						gain = GAIN_PROBE;
					}
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
	// Initial state
	bbr_state.stage = WAITING;
	bbr_state.start_0 = 0;
	bbr_state.start_1 = 0;
	bbr_state.bitrate = ga_conf_readint("bitrate-initial");
	if (bbr_state.bitrate <= 0) {
		bbr_state.bitrate = BBR_BITRATE_INIT_DEFAULT;
	}

	// Sleep until window is started
#ifdef WIN32
		Sleep(3000);
#else
		sleep(3);
#endif

	ctrlmsg_t m_reconf;
	// Initialize encoder bitrate
	ctrlsys_reconfig(&m_reconf, 0, 0, 0, bbr_state.bitrate, 0, 0);
	ctrl_client_sendmsg(&m_reconf, sizeof(ctrlmsg_system_reconfig_t));
	
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

			// Update state
			bbr_state.rtprop = reported_bbr_record.rtprop;
			bbr_state.latest_rtt = reported_bbr_record.latest_rtt;

			bbr_state.gain = bbr_gain(&bbr_state);
			ga_error("BBR cycle: gain: %.2f, rtprop: %u us, rtt: %u us, bitrate: %d MBps\n",
				bbr_state.gain, bbr_state.rtprop, bbr_state.latest_rtt, bbr_state.bitrate);
			if (fabs(bbr_state.gain - 1.0) > 0.1) {
				bbr_state.bitrate *= bbr_state.gain;
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
				bbr_state.bitrate = min(max(BBR_BITRATE_MINIMUM, bbr_state.bitrate), BBR_BITRATE_MAXIMUM);
#else
				bbr_state.bitrate = std::min(std::max(BBR_BITRATE_MINIMUM, bbr_state.bitrate), BBR_BITRATE_MAXIMUM);
#endif
				ctrlsys_reconfig(&m_reconf, 0, 0, 0, bbr_state.bitrate, 0, 0);
				ctrl_client_sendmsg(&m_reconf, sizeof(ctrlmsg_system_reconfig_t));
			}
		}
	}

	return NULL;
}
