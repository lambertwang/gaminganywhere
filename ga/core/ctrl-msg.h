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

#ifndef __CTRL_MSG_H__
#define	__CTRL_MSG_H__

#include "ga-common.h"

#define	CTRL_MSGTYPE_NULL	0xff	/* system control message starting from 0xff - reserved */
#define	CTRL_MSGTYPE_SYSTEM	0xfe	/* system control message type */

#define	CTRL_MSGSYS_SUBTYPE_NULL	0	/* system control message: NULL */
#define	CTRL_MSGSYS_SUBTYPE_SHUTDOWN	1	/* system control message: shutdown */
#define	CTRL_MSGSYS_SUBTYPE_NETREPORT	2	/* system control message: report networking */
#define CTRL_MSGSYS_SUBTYPE_RECONFIG	3	/* system control message: reconfigure */
#define CTRL_MSGSYS_SUBTYPE_RTTESTIMATOR	4	/* system control message: UDP ping handler */
#define CTRL_MSGSYS_SUBTYPE_PING	5	/* system control message: Ping */
#define	CTRL_MSGSYS_SUBTYPE_MAX		5	/* must equal to the last sub message type */

#ifdef WIN32
#define	BEGIN_CTRL_MESSAGE_STRUCT	__pragma(pack(push, 1))	/* equal to #pragma pack(push, 1) */
#define END_CTRL_MESSAGE_STRUCT		; \
					__pragma(pack(pop))	/* equal to #pragma pack(pop) */ 
#else
#define	BEGIN_CTRL_MESSAGE_STRUCT
#define END_CTRL_MESSAGE_STRUCT		__attribute__((__packed__));
#endif

BEGIN_CTRL_MESSAGE_STRUCT
/**
 * Generic (minimal) control message. This is compatible with that sdlmsg_t.
 */
struct ctrlmsg_s {
	unsigned short msgsize;		/*< size of this message, including msgsize */
	unsigned char msgtype;		/*< message type */
	unsigned char which;		/*< unused */
	unsigned char padding[124];	/*< a sufficient large buffer to fit all types of message */
}
END_CTRL_MESSAGE_STRUCT
typedef struct ctrlmsg_s ctrlmsg_t;

////////////////////////////////////////////////////////////////////////////

BEGIN_CTRL_MESSAGE_STRUCT
/**
 * General system control message structure.
 */
struct ctrlmsg_system_s {
	unsigned short msgsize;		/*< size of this message, including msgsize */
	unsigned char msgtype;		/*< message type */
	unsigned char subtype;		/*< system command message subtype */
}
END_CTRL_MESSAGE_STRUCT
typedef struct ctrlmsg_system_s ctrlmsg_system_t;

////////////////////////////////////////////////////////////////////////////

BEGIN_CTRL_MESSAGE_STRUCT
struct ctrlmsg_system_netreport_s {
	unsigned short msgsize;		/*< size of this message, including this field */
	unsigned char msgtype;		/*< must be CTRL_MSGTYPE_SYSTEM */
	unsigned char subtype;		/*< must be CTRL_MSGSYS_SUBTYPE_NETREPORT */
	unsigned int duration;		/*< sample collection duration (in microseconds) */
	unsigned int framecount;	/*< number of frames */
	unsigned int pktcount;		/*< packet count (including lost packets) */
	unsigned int pktloss;		/*< packet loss count */
	unsigned int bytecount;		/*< total received amunt of data (in bytes) */
	unsigned int capacity;		/*< measured capacity (in bits per second) */
}
END_CTRL_MESSAGE_STRUCT
typedef struct ctrlmsg_system_netreport_s ctrlmsg_system_netreport_t;

////////////////////////////////////////////////////////////////////////////

BEGIN_CTRL_MESSAGE_STRUCT
struct ctrlmsg_system_reconfig_s {
	unsigned short msgsize;		/*< size of this message, including this field */
	unsigned char msgtype;		/*< must be CTRL_MSGTYPE_SYSTEM */
	unsigned char subtype;		/*< must be CTRL_MSGSYS_SUBTYPE_RECONFIG */
	int reconfId;				/*< ID number of the reconfigure message */
	int crf;
	int framerate;
	int bitrate;				/*< Bitrate MUST be in Kbps */
	int width;
	int height;
}
END_CTRL_MESSAGE_STRUCT
typedef struct ctrlmsg_system_reconfig_s ctrlmsg_system_reconfig_t;

////////////////////////////////////////////////////////////////////////////

BEGIN_CTRL_MESSAGE_STRUCT
struct ctrlmsg_system_rttestimator_s {
	unsigned short msgsize;		/*< size of this message, including msgsize */
	unsigned char msgtype;		/*< must be CTRL_MSGTYPE_SYSTEM */
	unsigned char subtype;		/*< must be CTRL_MSGSYS_SUBTYPE_RTTESTIMATOR */
	// short sin_family;		 	/*< Client socket descriptor information */
	// unsigned short sin_port;
	// unsigned long s_addr;
}
END_CTRL_MESSAGE_STRUCT
typedef struct ctrlmsg_system_rttestimator_s ctrlmsg_system_rttestimator_t;

////////////////////////////////////////////////////////////////////////////

BEGIN_CTRL_MESSAGE_STRUCT
struct ctrlmsg_system_ping_s {
	unsigned short msgsize;		/*< size of this message, including msgsize */
	unsigned char msgtype;		/*< must be CTRL_MSGTYPE_SYSTEM */
	unsigned char subtype;		/*< must be CTRL_MSGSYS_SUBTYPE_PING */
	unsigned int ping_id;		/*< Id of the ping */
	long tv_sec;				/*< timeval record */
	long tv_usec;
}
END_CTRL_MESSAGE_STRUCT
typedef struct ctrlmsg_system_ping_s ctrlmsg_system_ping_t;

////////////////////////////////////////////////////////////////////////////

typedef void (*ctrlsys_handler_t)(ctrlmsg_system_t *);

EXPORT int ctrlsys_handle_message(unsigned char *buf, unsigned int size);
EXPORT	ctrlsys_handler_t ctrlsys_set_handler(unsigned char subtype, ctrlsys_handler_t handler);

/* functions for building message data structure */
EXPORT ctrlmsg_t * ctrlsys_netreport(ctrlmsg_t *msg, unsigned int duration, unsigned int framecount, unsigned int pktcount, unsigned int pktloss, unsigned int bytecount, unsigned int capacity);
EXPORT ctrlmsg_t * ctrlsys_reconfig(ctrlmsg_t *msg, int reconfId, int crf, int framerate, int bitrate, int width, int height);
EXPORT ctrlmsg_t * ctrlsys_bbrreport(ctrlmsg_t *msg, unsigned int framecount, unsigned int duration, unsigned int bytecount, unsigned int rcvrate);
// EXPORT ctrlmsg_t * ctrlsys_rttestimator(ctrlmsg_t *msg, short sin_family, unsigned short sin_port, unsigned long s_addr);
EXPORT ctrlmsg_t * ctrlsys_rttestimator(ctrlmsg_t *msg);
EXPORT ctrlmsg_t * ctrlsys_ping(ctrlmsg_t *msg, unsigned int id, struct timeval time_record);

#endif	/* __CTRL_MSG_H__ */
