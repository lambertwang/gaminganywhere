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
static struct bbr_btlbw_record_s bbr_btlbw[BBR_BTLBW_MAX];
static unsigned int bbr_btlbw_start = 0;
static unsigned int bbr_btlbw_head = 0;
static unsigned int last_pkt_timestamp = 0;
static struct timeval last_report_sent;

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
		unsigned int max_deliveryrate = 0;

		while (1000000 * (rcvtv.tv_sec - bbr_btlbw[bbr_btlbw_start].rcvtime.tv_sec) + 
			rcvtv.tv_usec - bbr_btlbw[bbr_btlbw_start].rcvtime.tv_usec > BBR_BTLBW_WINDOW_SIZE_US) {
			bbr_btlbw_start = (bbr_btlbw_start + 1) % BBR_BTLBW_MAX;
		}

		unsigned int seek = bbr_btlbw_start;
		int i = 0;
		while (seek != bbr_btlbw_head) {
			if (bbr_btlbw[seek].deliveryrate > max_deliveryrate) {
				max_deliveryrate = bbr_btlbw[seek].deliveryrate;
			}
			seek = (seek + 1) % BBR_BTLBW_MAX;
			i++;
		}
	}
}

void *
bitrateadaptation_thread(void *param) {
	int s = 0;
	int kbitrate[] = { 3000, 100 };
	ga_error("reconfigure thread started ...\n");
	while(1) {
		ctrlmsg_t m;
#ifdef WIN32
		Sleep(3 * 1000);
#else
		sleep(3);
#endif
		ga_error("Sending reconfiguration message\n");
		ctrlsys_reconfig(&m, 0, 0, 0, kbitrate[s%2], 0, 0);
		ctrl_client_sendmsg(&m, sizeof(ctrlmsg_system_reconfig_t));

		s = (s + 1) % 6;

		unsigned int rtprop = getRtprop();
		ga_error("rtprop: %d\n", rtprop);
	}
	return NULL;
}

typedef struct bbr_rtt_s {
	struct timeval record_time;
	unsigned int rtt;
}	bbr_rtt_t;

#define BBR_RTT_MAX 256
// #define BBR_RTT_WINDOW_SIZE_US (20 * 1000 * 1000)
#define BBR_RTT_WINDOW_SIZE 80 // A constant is probably good enough for now

static struct bbr_rtt_s bbr_rtt[BBR_RTT_MAX];
static unsigned int bbr_rtt_start = 0;
static unsigned int bbr_rtt_head = 0;

/**
 *-1 : Waiting on RTT
 *		Leaves once RTT can be acquired
 * 0 : Startup
 *		Leaves once BtlBw is found
 *		When 3 consecutive startup steps do not result in a doubled delivery rate. 
 * 1 : Drain
 *		Leaves once Excess created by startup is drained
 * 2 : Probe / steady state
 */
static int bbr_state = -1; 

// 3 round window for detecting plateaus in startup
static unsigned int bbr_startup_prev1 = 0;
static unsigned int bbr_startup_prev2 = 0;

#define BBR_PROBE_INTERVAL_US (4 * 1000 * 1000)

static struct timeval bbr_prev_probe;

#define BBR_BITRATE_MINIMUM 50
#define BBR_BITRATE_MAXIMUM 30000

static int bbr_bitrate = 200;

/*
void
handle_bbrreport(ctrlmsg_system_t *msg) {
	// Parse network properties
	ctrlmsg_system_bbrreport_t *msgn = (ctrlmsg_system_bbrreport_t*) msg;
	unsigned int latest_rtt = 0;
	m_server->ioctl(GA_IOCTL_CUSTOM, sizeof(unsigned int *), &latest_rtt);
	
	unsigned int rtProp = UINT_MAX;
	if (latest_rtt == 0) {
		return;
	} else {
		bbr_rtt[bbr_rtt_head].rtt = latest_rtt;
		bbr_rtt_head = (bbr_rtt_head + 1) % BBR_RTT_MAX;

		int seek = (bbr_rtt_head + BBR_RTT_MAX - BBR_RTT_WINDOW_SIZE) % BBR_RTT_MAX;
		while (seek != bbr_rtt_head) {
			if (bbr_rtt[bbr_rtt_head].rtt < rtProp) {
				rtProp = bbr_rtt[bbr_rtt_head].rtt;
			}
			seek = (seek + 1) % BBR_RTT_MAX;
		}

		// ga_error("RTProp: %u ms RTT: %u ms rcvrate: %d\n", rtProp * 1000 / 65536, latest_rtt * 1000 / 65536, msgn->rcvrate);
	}
	// Determine gain based on state
	float gain = 1.0;
	struct timeval now;
	switch (bbr_state) {
		case -1:
			bbr_state = 0;
			ga_error("BBR: Entering startup state\n");
			break;
		case 0:
			gain = 2; // Attempt to double delivery rate
			if (bbr_startup_prev2 != 0) {
				// Detect plateaus: If less than 25% growth in 3 rounds, leave startup state
				if (std::min(bbr_startup_prev2, bbr_startup_prev1) * 5 / 4 > msgn->rcvrate) {
					ga_error("BBR: Entering drain state\n");
					bbr_state = 1;
				}
			}
			bbr_startup_prev2 = bbr_startup_prev1;
			bbr_startup_prev1 = msgn->rcvrate;
			break;
		case 1:
			gain = .5; // Inverse of startup state gain
			// Lasts only one round
			bbr_state = 2;
			gettimeofday(&bbr_prev_probe, NULL);
			ga_error("BBR: Entering standby state\n");
			break;
		case 2:
			if (latest_rtt - rtProp > 5 * 65536 / 1000) { // 5ms
				gain = .75;
				gettimeofday(&bbr_prev_probe, NULL);
			} else {
				gettimeofday(&now, NULL);
				if (1000000 * (now.tv_sec - bbr_prev_probe.tv_sec) + 
					(now.tv_usec - bbr_prev_probe.tv_usec) >
					BBR_PROBE_INTERVAL_US) {
					ga_error("BBR: Probing bandwidth\n");
					gain = 1.25;
					gettimeofday(&bbr_prev_probe, NULL);
				}
			}
			break;
	}
	
	if (fabs(gain - 1.0) > 0.1) {
		// ga_error("Gain factor: %f\n", gain);
		bbr_bitrate *= gain;
		bbr_bitrate = std::min(std::max(BBR_BITRATE_MINIMUM, bbr_bitrate), BBR_BITRATE_MAXIMUM);
		
		ga_ioctl_reconfigure_t reconf;
		bzero(&reconf, sizeof(reconf));

		reconf.id = 0;
		reconf.framerate_n = -1;
		reconf.framerate_d = 1;
		reconf.width = -1;
		reconf.height = -1;

		reconf.crf = -1;
		reconf.bitrateKbps = bbr_bitrate;

		// encoder
		if(m_vencoder->ioctl) {
			int err = m_vencoder->ioctl(GA_IOCTL_RECONFIGURE, sizeof(reconf), &reconf);
			if(err < 0) {
				ga_error("reconfigure encoder failed, err = %d.\n", err);
			} else {
				ga_error("reconfigure encoder OK, bitrate=%d; bufsize=%d; framerate=%d/%d.\n",
						reconf.bitrateKbps, reconf.bufsize,
						reconf.framerate_n, reconf.framerate_d);
			}
		}
	}

	return;
}
*/
