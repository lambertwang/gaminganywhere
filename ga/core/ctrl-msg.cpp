/*
 * Copyright (c) 2012-2015 Chun-Ying Huang
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

/**
 * @file
 * ctrl-msg: generic message and system message handler
 */

#include <stdio.h>
#ifndef WIN32
#include <arpa/inet.h>
#endif

#include "ga-common.h"
#include "ctrl-msg.h"

static ctrlsys_handler_t ctrlsys_handler_list[] = {
	NULL,	/* 0 = CTRL_MSGSYS_SUBTYPE_NULL */
	NULL,	/* 1 = CTRL_MSGSYS_SUBTYPE_SHUTDOWN */
	NULL,	/* 2 = CTRL_MSGSYS_SUBTYPE_NETREPORT */
	NULL,	/* 3 = CTRL_MSGSYS_SUBTYPE_RECONFIG */
	NULL,	/* 4 = CTRL_MSGSYS_SUBTYPE_INIT_RTTSERVER */
	NULL	/* 5 = CTRL_MSGSYS_SUBTYPE_PING */
};

ctrlsys_handler_t
ctrlsys_set_handler(unsigned char subtype, ctrlsys_handler_t handler) {
	ctrlsys_handler_t old_handler = NULL;
	if(subtype > CTRL_MSGSYS_SUBTYPE_MAX)
		return NULL;
	old_handler = ctrlsys_handler_list[subtype];
	ctrlsys_handler_list[subtype] = handler;
	return old_handler;
}

/**
 * Convert fields in a controller system message to
 * host byte-order.
 *
 * @param [in] msg Pointer to the message
 * @return 0 if no error, or -1 if \a msg cannot be processed.
 *
 * This function also checks if the size of the message is correct.
 * It returns -1 if the size for the message is incorrect.
 */
static int 
ctrlsys_ntoh(ctrlmsg_system_t *msg) {
	ctrlmsg_system_netreport_t *netreport;
	ctrlmsg_system_reconfig_t *reconf;
	ctrlmsg_system_init_rttserver_t *init_rttserver;
	ctrlmsg_system_ping_t *ping;
	msg->msgsize = ntohs(msg->msgsize);
	switch(msg->subtype) {
	/* no conversion needed, and no size checking */
	case CTRL_MSGSYS_SUBTYPE_NULL:
	case CTRL_MSGSYS_SUBTYPE_SHUTDOWN:
		return 0;
	case CTRL_MSGSYS_SUBTYPE_NETREPORT:
		if(msg->msgsize != sizeof(ctrlmsg_system_netreport_t))
			return -1;
		netreport = (ctrlmsg_system_netreport_t*) msg;
		netreport->duration = htonl(netreport->duration);
		netreport->framecount = htonl(netreport->framecount);
		netreport->pktcount = htonl(netreport->pktcount);
		netreport->pktloss  = htonl(netreport->pktloss);
		netreport->bytecount = htonl(netreport->bytecount);
		netreport->capacity = htonl(netreport->capacity);
		break;
	case CTRL_MSGSYS_SUBTYPE_RECONFIG:
		if(msg->msgsize != sizeof(ctrlmsg_system_reconfig_t))
			return -1;
		reconf = (ctrlmsg_system_reconfig_t*) msg;
		reconf->reconfId = htonl(reconf->reconfId);
		reconf->crf = htonl(reconf->crf);
		reconf->framerate = htonl(reconf->framerate);
		reconf->bitrate = htonl(reconf->bitrate);
		reconf->width = htonl(reconf->width);
		reconf->height = htonl(reconf->height);
		break;
	case CTRL_MSGSYS_SUBTYPE_INIT_RTTSERVER:
		if(msg->msgsize != sizeof(ctrlmsg_system_init_rttserver_t))
			return -1;
		init_rttserver = (ctrlmsg_system_init_rttserver_t*) msg;
		// init_rttserver->sin_family = htons(sin_family);
		// init_rttserver->sin_port = htons(sin_port);
		// init_rttserver->s_addr = htonl(s_addr);
		break;
	case CTRL_MSGSYS_SUBTYPE_PING:
		if(msg->msgsize != sizeof(ctrlmsg_system_ping_t))
			return -1;
		ping = (ctrlmsg_system_ping_t*) msg;
		ping->ping_id = htonl(ping->ping_id);
		ping->tv_sec = htonl(ping->tv_sec);
		ping->tv_usec = htonl(ping->tv_usec);
		break;
	default:
		return -1;
	}
	return 0;
}

/**
 * Handle controller system messages.
 *
 * @param [in] msg Pointer to the message.
 * @return 1 if the message is a known control message, or 0 if it is not.
 */
int
ctrlsys_handle_message(unsigned char *buf, unsigned int size) {
	ctrlmsg_system_t *msg = (ctrlmsg_system_t*) buf;
	if(msg == NULL)
		return 1;
	if(size < sizeof(ctrlmsg_system_t))
		return 0;
	if(msg->msgtype != CTRL_MSGTYPE_SYSTEM)
		return 0;
	if(msg->subtype > CTRL_MSGSYS_SUBTYPE_MAX) {
		ga_error("system-message: unknown subtype (%02x)\n", msg->subtype);
		return 1;
	}
	if(ctrlsys_handler_list[msg->subtype] == NULL)
		return 1;
	if(ctrlsys_ntoh(msg) != 0)
		return 1;
	ctrlsys_handler_list[msg->subtype](msg);
	return 1;
}

/**
 * Build a network statistics report message, which is sent from a client to a server
 *
 * @param msg [in]	The structure to store the built message.
 *			The size of the structure must be at least \a sizeof(ctrlmsg_system_netreport_t)
 * @param duration [in]	The duration of monitored numbers (in microseconds).
 * @param framecount [in] Number of received frames in \a duration.
 * @param pktcount [in] Number of all packets in \a duration (including lost packets).
 * @param pktloss [in] Number of lost packets in \a duration.
 * @param bytecount [in] Number of received payload size in \a duration (in bytes).
 * @param capacity [in] Estimated network capacity (in bits per second).
 *
 */
ctrlmsg_t *
ctrlsys_netreport(ctrlmsg_t *msg, unsigned int duration,
		unsigned int framecount, unsigned int pktcount,
		unsigned int pktloss, unsigned int bytecount,
		unsigned int capacity) {
	ctrlmsg_system_netreport_t *msgn = (ctrlmsg_system_netreport_t*) msg;
	bzero(msg, sizeof(ctrlmsg_system_netreport_t));
	msgn->msgsize = htons(sizeof(ctrlmsg_system_netreport_t));
	msgn->msgtype = CTRL_MSGTYPE_SYSTEM;
	msgn->subtype = CTRL_MSGSYS_SUBTYPE_NETREPORT;
	msgn->duration = htonl(duration);
	msgn->framecount = htonl(framecount);
	msgn->pktcount = htonl(pktcount);
	msgn->pktloss = htonl(pktloss);
	msgn->bytecount = htonl(bytecount);
	msgn->capacity = htonl(capacity);
	return msg;
}

/**
 * Sends a ctrlsys_reconfig message to reconfigure the server
 * @param msg
 * 	Pointer to reconfig struct to fill in
 * @param crf
 *	Constant Rate Factor
 * @param framerate
 *	Framerate for reconfiguration. -1 to leave unchanged. This value defaults to unchanged.
 * @param bitrate
 *	Bitrate for reconfiguration. -1 to leave unchanged.
 * @param width
 *	Width of the frame
 * @param height
 *	Height of the frame
 */
ctrlmsg_t * 
ctrlsys_reconfig(ctrlmsg_t *msg, 
		int reconfId, int crf, int framerate, 
		int bitrate, int width, int height) {
	ctrlmsg_system_reconfig_t *msgn = (ctrlmsg_system_reconfig_t*) msg;
	bzero(msg, sizeof(ctrlmsg_system_reconfig_t));
	msgn->msgsize = htons(sizeof(ctrlmsg_system_reconfig_t));
	msgn->msgtype = CTRL_MSGTYPE_SYSTEM;
	msgn->subtype = CTRL_MSGSYS_SUBTYPE_RECONFIG;
	msgn->crf = htonl(crf);
	msgn->framerate = htonl(framerate);
	msgn->bitrate = htonl(bitrate);
	msgn->width = htonl(width);
	msgn->height = htonl(height);
	return msg;
}

/**
 * Send a signal to the server to start the UDP ping handler
 *
 * @param msg [in]	The structure to store the built message.
 *			The size of the structure must be at least \a sizeof(ctrlmsg_system_init_rttserver_t)
 */
ctrlmsg_t *
//  ctrlsys_init_rttserver(ctrlmsg_t *msg, short sin_family, unsigned short sin_port, unsigned long s_addr) {
ctrlsys_init_rttserver(ctrlmsg_t *msg) {
	ctrlmsg_system_init_rttserver_t *msgn = (ctrlmsg_system_init_rttserver_t*) msg;
	bzero(msg, sizeof(ctrlmsg_system_init_rttserver_t));
	msgn->msgsize = htons(sizeof(ctrlmsg_system_init_rttserver_t));
	msgn->msgtype = CTRL_MSGTYPE_SYSTEM;
	msgn->subtype = CTRL_MSGSYS_SUBTYPE_INIT_RTTSERVER;
	// msgn->sin_family = htons(sin_family);
	// msgn->sin_port = htons(sin_port);
	// msgn->s_addr = htonl(s_addr);
	return msg;
}

/**
 * Handles ping ctrlmsg
 *
 * @param msg [in]	The structure to store the built message.
 *			The size of the structure must be at least \a sizeof(ctrlmsg_system_ping_t)
 * @param ping_id
 *	Ping identifier to determine order in the sequence
 * @param tv_sec
 *	Time stamp in seconds since ping was sent
 * @param tv_usec
 *	Time stamp in remaining microseconds since ping was sent
 */
ctrlmsg_t *
ctrlsys_ping(ctrlmsg_t *msg, unsigned int id, struct timeval time_record) {
	ctrlmsg_system_ping_t *msgn = (ctrlmsg_system_ping_t*) msg;
	bzero(msg, sizeof(ctrlmsg_system_ping_t));
	msgn->msgsize = htons(sizeof(ctrlmsg_system_ping_t));
	msgn->msgtype = CTRL_MSGTYPE_SYSTEM;
	msgn->subtype = CTRL_MSGSYS_SUBTYPE_PING;
	msgn->ping_id = htonl(id);
	msgn->tv_sec = htonl(time_record.tv_sec);
	msgn->tv_usec = htonl(time_record.tv_usec);
	return msg;
}
