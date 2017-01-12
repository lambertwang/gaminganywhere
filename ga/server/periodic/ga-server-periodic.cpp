/*
 * Copyright (c) 2013 Chun-Ying Huang
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

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <stdlib.h>

#ifdef _WIN32
#include <winsock2.h>
#include <Ws2tcpip.h>
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#endif

#ifndef WIN32
#include <unistd.h>
#endif

#include "ga-common.h"
#include "ga-conf.h"
#include "ga-module.h"
#include "rtspconf.h"
#include "controller.h"
#include "encoder-common.h"
#include "../../client/rttserver.h"

// image source pipeline:
//	vsource -- [vsource-%d] --> filter -- [filter-%d] --> encoder

// configurations:
static char *imagepipefmt = "video-%d";
static char *filterpipefmt = "filter-%d";
static char *imagepipe0 = "video-0";
static char *filterpipe0 = "filter-0";
static char *filter_param[] = { imagepipefmt, filterpipefmt };
static char *video_encoder_param = filterpipefmt;
static void *audio_encoder_param = NULL;
static bool rttThreadStarted = false;

static struct gaRect *prect = NULL;
static struct gaRect rect;

static ga_module_t *m_vsource, *m_filter, *m_vencoder, *m_asource, *m_aencoder, *m_ctrl, *m_server;

int
load_modules() {
	if((m_vsource = ga_load_module("mod/vsource-desktop", "vsource_")) == NULL)
		return -1;
	if((m_filter = ga_load_module("mod/filter-rgb2yuv", "filter_RGB2YUV_")) == NULL)
		return -1;
	if((m_vencoder = ga_load_module("mod/encoder-video", "vencoder_")) == NULL)
		return -1;
	if(ga_conf_readbool("enable-audio", 1) != 0) {
	//////////////////////////
#ifndef __APPLE__
	if((m_asource = ga_load_module("mod/asource-system", "asource_")) == NULL)
		return -1;
#endif
	if((m_aencoder = ga_load_module("mod/encoder-audio", "aencoder_")) == NULL)
		return -1;
	//////////////////////////
	}
	if((m_ctrl = ga_load_module("mod/ctrl-sdl", "sdlmsg_replay_")) == NULL)
		return -1;
	if((m_server = ga_load_module("mod/server-live555", "live_")) == NULL)
		return -1;
	return 0;
}

int
init_modules() {
	struct RTSPConf *conf = rtspconf_global();
	//static const char *filterpipe[] = { imagepipe0, filterpipe0 };
	if(conf->ctrlenable) {
		ga_init_single_module_or_quit("controller", m_ctrl, (void *) prect);
	}
	// controller server is built-in - no need to init
	// note the order of the two modules ...
	ga_init_single_module_or_quit("video-source", m_vsource, (void*) prect);
	ga_init_single_module_or_quit("filter", m_filter, (void*) filter_param);
	//
	ga_init_single_module_or_quit("video-encoder", m_vencoder, filterpipefmt);
	if(ga_conf_readbool("enable-audio", 1) != 0) {
	//////////////////////////
#ifndef __APPLE__
	ga_init_single_module_or_quit("audio-source", m_asource, NULL);
#endif
	ga_init_single_module_or_quit("audio-encoder", m_aencoder, NULL);
	//////////////////////////
	}
	//
	ga_init_single_module_or_quit("server-live555", m_server, NULL);
	//
	return 0;
}

int
run_modules() {
	struct RTSPConf *conf = rtspconf_global();
	static const char *filterpipe[] =  { imagepipe0, filterpipe0 };
	// controller server is built-in, but replay is a module
	if(conf->ctrlenable) {
		ga_run_single_module_or_quit("control server", ctrl_server_thread, conf);
		// XXX: safe to comment out?
		//ga_run_single_module_or_quit("control replayer", m_ctrl->threadproc, conf);
	}
	// video
	//ga_run_single_module_or_quit("image source", m_vsource->threadproc, (void*) imagepipefmt);
	if(m_vsource->start(prect) < 0)		exit(-1);
	//ga_run_single_module_or_quit("filter 0", m_filter->threadproc, (void*) filterpipe);
	if(m_filter->start(filter_param) < 0)	exit(-1);
	encoder_register_vencoder(m_vencoder, video_encoder_param);
	// audio
	if(ga_conf_readbool("enable-audio", 1) != 0) {
	//////////////////////////
#ifndef __APPLE__
	//ga_run_single_module_or_quit("audio source", m_asource->threadproc, NULL);
	if(m_asource->start(NULL) < 0)		exit(-1);
#endif
	encoder_register_aencoder(m_aencoder, audio_encoder_param);
	//////////////////////////
	}
	// server
	if(m_server->start(NULL) < 0)		exit(-1);
	//
	return 0;
}

#ifdef TEST_RECONFIGURE
static void *
test_reconfig(void *) {
	int s = 0, err;
	int kbitrate[] = { 3000, 100 };
	int framerate[][2] = { { 12, 1 }, {30, 1}, {24, 1} };
	ga_error("reconfigure thread started ...\n");
	while(1) {
		ga_ioctl_reconfigure_t reconf;
		if(encoder_running() == 0) {
#ifdef WIN32
			Sleep(1);
#else
			sleep(1);
#endif
			continue;
		}
#ifdef WIN32
		Sleep(20 * 1000);
#else
		sleep(100);
#endif
		bzero(&reconf, sizeof(reconf));
		reconf.id = 0;
		reconf.bitrateKbps = kbitrate[s%2];
#if 0
		reconf.bufsize = 5 * kbitrate[s%2] / 24;
#endif
		// encoder
		if(m_vencoder->ioctl) {
			err = m_vencoder->ioctl(GA_IOCTL_RECONFIGURE, sizeof(reconf), &reconf);
			if(err < 0) {
				ga_error("reconfigure encoder failed, err = %d.\n", err);
			} else {
				ga_error("reconfigure encoder OK, bitrate=%d; bufsize=%d; framerate=%d/%d.\n",
						reconf.bitrateKbps, reconf.bufsize,
						reconf.framerate_n, reconf.framerate_d);
			}
		}
		s = (s + 1) % 6;
	}
	return NULL;
}
#endif

void
handle_netreport(ctrlmsg_system_t *msg) {
	ctrlmsg_system_netreport_t *msgn = (ctrlmsg_system_netreport_t*) msg;
	ga_error("net-report: capacity=%.3f Kbps; loss-rate=%.2f%% (%u/%u); overhead=%.2f [%u KB received in %.3fs (%.2fKB/s)]\n",
		msgn->capacity / 1024.0,
		100.0 * msgn->pktloss / msgn->pktcount,
		msgn->pktloss, msgn->pktcount,
		1.0 * msgn->pktcount / msgn->framecount,
		msgn->bytecount / 1024,
		msgn->duration / 1000000.0,
		msgn->bytecount / 1024.0 / (msgn->duration / 1000000.0));
	return;
}

// Handle a reconfig ctrlmsg to reconfigure the encoder
void
handle_reconfig(ctrlmsg_system_t *msg){
	ctrlmsg_system_reconfig_t *msgn = (ctrlmsg_system_reconfig_t*) msg;

	// Create reconfigure struct
	ga_ioctl_reconfigure_t reconf;
	bzero(&reconf, sizeof(reconf));

	// Copy values from msg to msgn
	reconf.id = msgn->reconfId;
	reconf.crf = msgn->crf;
	reconf.framerate_n = msgn->framerate;
	reconf.framerate_d = 1;
	reconf.bitrateKbps = msgn->bitrate;
	reconf.width = msgn->width;
	reconf.height = msgn->height;


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

	return;
}

static pthread_mutex_t rttserver_mutex;
// Socket descriptors
#ifdef _WIN32
static SOCKET rtt_server_sock;
#else
static int rtt_server_sock;
#endif

// Initializes the network connection for the RTT Server
void
init_rttserver() {
	if(!rttThreadStarted){
		rttThreadStarted = true;
	}
#ifdef _WIN32
	WSADATA wsa;
#endif
	struct sockaddr_in myaddr;
	int recvlen;

	// Initialize socket
#ifdef _WIN32
	// Initialize winsock
	if(WSAStartup(MAKEWORD(2,2),&wsa) != 0){
		ga_error("rtt_handler: Winsock failed to initialize\n");
		exit(EXIT_FAILURE);
	}
	// Create socket
	if((rtt_server_sock = socket(AF_INET, SOCK_DGRAM, 0)) == SOCKET_ERROR){
#else
	// Create socket
	if((rtt_server_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0){
#endif
		ga_error("rtt_handler: Failed to create socket\n");
		return;
	}

	// Set address structure
	memset((char *)&myaddr, 0, sizeof(myaddr));
	myaddr.sin_family = AF_INET;
	myaddr.sin_addr.s_addr = INADDR_ANY;
	myaddr.sin_port = htons(PKTPORT);
}

// Clean up connection and close sockets for the RTT Server
void
close_rttserver() {
	if(!rttThreadStarted){
		return;
	}

#ifdef _WIN32
	WSACleanup();
#endif

	int status = 0;

#ifdef _WIN32
	status = shutdown(rtt_server_sock, SD_BOTH);
	if(status == 0){ status = closesocket(rtt_server_sock); }
#else
	status = shutdown(rtt_server_sock, SHUT_RDWR);
	if(status == 0){ status = close(rtt_server_sock); }
#endif

	return;
}

// Handles the RTT Server ctrlmsg
void
handle_rttserver(ctrlmsg_system_t *msg){
	// Only start the handler once, in case multiple ctrlmsg signals are sent.
	pthread_mutex_lock(&rttserver_mutex);
	init_rttserver();
	pthread_mutex_unlock(&rttserver_mutex);

	return;
}

// Handles ping ctrlmsg
void
handle_ping(ctrlmsg_system_t *msg) {
	if(!rttThreadStarted){
		return;
	}
	pthread_mutex_lock(&rttserver_mutex);
	ctrlmsg_system_ping_t *msgn = (ctrlmsg_system_ping_t*) msg;

	bbr_rtt_t buf;
	buf.rtt_id = msgn->ping_id;
	buf.time_record.tv_sec = msgn->tv_sec;
	buf.time_record.tv_usec = msgn->tv_usec;

	// Format destination address for packet
	struct sockaddr_in toaddr;
	memset((char *)&toaddr, 0, sizeof(toaddr));
	toaddr.sin_family = AF_INET;
	toaddr.sin_addr.s_addr = inet_addr("127.0.0.1"); // TODO: Ensure we have correct IP
	toaddr.sin_port = htons(PKTPORT);
	
	// struct RTSPConf *conf = rtspconf_global();
	// Send response to client
	sendto(rtt_server_sock, (char *) &buf, sizeof(bbr_rtt_t), 0, (struct sockaddr *) &(toaddr), sizeof(toaddr));
	pthread_mutex_unlock(&rttserver_mutex);

	return;
}

int
main(int argc, char *argv[]) {
#ifdef WIN32
	if(CoInitializeEx(NULL, COINIT_MULTITHREADED) < 0) {
		fprintf(stderr, "cannot initialize COM.\n");
		return -1;
	}
#endif
	//
	if(argc < 2) {
		fprintf(stderr, "usage: %s config-file\n", argv[0]);
		return -1;
	}
	//
	if(ga_init(argv[1], NULL) < 0)	{ return -1; }
	//
	ga_openlog();
	//
	if(rtspconf_parse(rtspconf_global()) < 0)
					{ return -1; }
	//
	prect = NULL;
	//
	if(ga_crop_window(&rect, &prect) < 0) {
		return -1;
	} else if(prect == NULL) {
		ga_error("*** Crop disabled.\n");
	} else if(prect != NULL) {
		ga_error("*** Crop enabled: (%d,%d)-(%d,%d)\n", 
			prect->left, prect->top,
			prect->right, prect->bottom);
	}
	//
	if(load_modules() < 0)	 	{ return -1; }
	if(init_modules() < 0)	 	{ return -1; }
	if(run_modules() < 0)	 	{ return -1; }

	// Init static mutex
	pthread_mutex_init(&rttserver_mutex, NULL);

	// enable handler to monitored network status
	ctrlsys_set_handler(CTRL_MSGSYS_SUBTYPE_NETREPORT, handle_netreport);
	ctrlsys_set_handler(CTRL_MSGSYS_SUBTYPE_RECONFIG, handle_reconfig);
	ctrlsys_set_handler(CTRL_MSGSYS_SUBTYPE_INIT_RTTSERVER, handle_rttserver);
	ctrlsys_set_handler(CTRL_MSGSYS_SUBTYPE_PING, handle_ping);
	//
#ifdef TEST_RECONFIGURE
	pthread_t t;
	pthread_create(&t, NULL, test_reconfig, NULL);
#endif
	while(1) {
		usleep(5000000);
	}
	// alternatively, it is able to create a thread to run rtspserver_main:
	//	pthread_create(&t, NULL, rtspserver_main, NULL);
	// Close rtt server
	pthread_mutex_lock(&rttserver_mutex);
	close_rttserver();
	pthread_mutex_unlock(&rttserver_mutex);
	//
	ga_deinit();
	//
	return 0;
}

