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

#include "ga-common.h"
#include "ctrl-msg.h"

static void *
test_reconfig(void *) {
	int s = 0, err;
	int kbitrate[] = { 3000, 100 };
	ga_error("reconfigure thread started ...\n");
	while(1) {
		ctrlmsg_t m;
#ifdef WIN32
		Sleep(20 * 1000);
#else
		sleep(100);
#endif
		ctrlsys_reconfig(&m, 0, 0, 0, kbitrate[s%2], 0, 0);
		ctrl_client_sendmsg(&m, sizeof(ctrlmsg_ssytem_reconfig_t));

		s = (s + 1) % 6;
	}
	return NULL;
}