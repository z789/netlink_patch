// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * 	ucon.c
 *
 * Copyright (c) 2004+ Evgeniy Polyakov <zbr@ioremap.net>
 */

#include <asm/types.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/poll.h>

#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include <arpa/inet.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <getopt.h>

#include <linux/connector.h>

#define DEBUG
#define NETLINK_CONNECTOR 	11
#define MAX_LEN_BUFFER 4096

/* Hopefully your userspace connector.h matches this kernel */
#define CN_TEST_IDX		CN_NETLINK_USERS + 3
#define CN_TEST_VAL		0x456

#ifdef DEBUG
#define ulog(f, a...) fprintf(stdout, f, ##a)
#else
#define ulog(f, a...) do {} while (0)
#endif

static int need_exit;
static __u32 seq;

static int netlink_send(int s, struct cn_msg *msg)
{
	struct nlmsghdr *nlh;
	unsigned int size;
	int err;
	char buf[128];
	struct cn_msg *m;

	size = NLMSG_SPACE(sizeof(struct cn_msg) + msg->len);

	nlh = (struct nlmsghdr *)buf;
	nlh->nlmsg_seq = seq++;
	nlh->nlmsg_pid = getpid();
	nlh->nlmsg_type = NLMSG_DONE;
	nlh->nlmsg_len = size;
	nlh->nlmsg_flags = 0;

	m = NLMSG_DATA(nlh);
#if 0
	ulog("%s: [%08x.%08x] len=%u, seq=%u, ack=%u.\n",
	       __func__, msg->id.idx, msg->id.val, msg->len, msg->seq, msg->ack);
#endif
	memcpy(m, msg, sizeof(*m) + msg->len);

	err = send(s, nlh, size, 0);
	if (err == -1)
		ulog("Failed to send: %s [%d].\n",
			strerror(errno), errno);

	return err;
}

static void usage(void)
{
	printf(
		"Usage: ucon [options] [output file]\n"
		"\n"
		"\t-h\tthis help screen\n"
		"\t-s\tsend buffers to the test module\n"
		"\n"
		"The default behavior of ucon is to subscribe to the test module\n"
		"and wait for state messages.  Any ones received are dumped to the\n"
		"specified output file (or stdout).  The test module is assumed to\n"
		"have an id of {%u.%u}\n"
		"\n"
		"If you get no output, then verify the cn_test module id matches\n"
		"the expected id above.\n"
		, CN_TEST_IDX, CN_TEST_VAL
	);
}

static int open_netlink_bind(void)
{
	int s = -1;
	char buf[MAX_LEN_BUFFER];
	struct sockaddr_nl l_local;

	memset(buf, 0, sizeof(buf));

	s = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_CONNECTOR);
	if (s == -1) {
		perror("socket");
		goto end;
	}

	l_local.nl_family = AF_NETLINK;
	l_local.nl_groups = (1 << (CN_TEST_IDX-1));	/* bitmask of requested groups */
//	l_local.nl_groups = -1;	/* bitmask of requested groups */
	l_local.nl_pid = getpid();

	ulog("subscribing to %u.%u\n", CN_TEST_IDX, CN_TEST_VAL);

	if (bind(s, (struct sockaddr *)&l_local, sizeof(struct sockaddr_nl)) ==
	    -1) {
		perror("bind");
		close(s);
		s = -1;
		goto end;
	}

 end:
	return s;
}

static int open_netlink_connect(void)
{
	int s = -1;
	char buf[MAX_LEN_BUFFER];
	struct sockaddr_nl l_local;
	int opt =CN_TEST_IDX;
	int ret = -1;

	memset(buf, 0, sizeof(buf));

	s = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_CONNECTOR);
	if (s == -1) {
		perror("socket");
		goto end;
	}

	l_local.nl_family = AF_NETLINK;
	l_local.nl_groups = (1 << (CN_TEST_IDX-1));	/* bitmask of requested groups */
	l_local.nl_pid = 0;             /* */

	ulog("subscribing to %u.%u\n", CN_TEST_IDX, CN_TEST_VAL);

	if (connect(s, (struct sockaddr *)&l_local, sizeof(struct sockaddr_nl))
	    == -1) {
		perror("connect");
		close(s);
		s = -1;
		goto end;
	}

	ret = setsockopt(s, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP, &opt, sizeof(opt));
	if (ret < 0) {
		ulog("setockeopt fail\n");
		close(s);
		s = -1;
		goto end;
	}

 end:
	return s;
}

int main(int argc, char *argv[])
{
	int s;
	char buf[1024];
	int len;
	struct nlmsghdr *reply;
	struct sockaddr_nl l_local;
	struct cn_msg *data;
	FILE *out;
	time_t tm;
	struct pollfd pfd;
	bool send_msgs = false;
	int connect_mode = 0;

	while ((s = getopt(argc, argv, "hsc")) != -1) {
		switch (s) {
		case 's':
			send_msgs = true;
			break;
		case 'c':
			connect_mode = 1;
			break;

		case 'h':
			usage();
			return 0;

		default:
			/* getopt() outputs an error for us */
			usage();
			return 1;
		}
	}

	if (argc != optind) {
		out = fopen(argv[optind], "a+");
		if (!out) {
			ulog("Unable to open %s for writing: %s\n",
				argv[1], strerror(errno));
			out = stdout;
		}
	} else
		out = stdout;

	memset(buf, 0, sizeof(buf));

#if 0
	s = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_CONNECTOR);
	if (s == -1) {
		perror("socket");
		return -1;
	}

	l_local.nl_family = AF_NETLINK;
	l_local.nl_groups = -1; /* bitmask of requested groups */
	l_local.nl_pid = 0;

	ulog("subscribing to %u.%u\n", CN_TEST_IDX, CN_TEST_VAL);

	if (bind(s, (struct sockaddr *)&l_local, sizeof(struct sockaddr_nl)) == -1) {
		perror("bind");
		close(s);
		return -1;
	}

#if 0
	{
		int on = 0x57; /* Additional group number */
		setsockopt(s, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP, &on, sizeof(on));
	}
#endif
#endif
	if (connect_mode)
		s = open_netlink_connect(); 
	else
		s = open_netlink_bind(); 
	if (s < 0) {
		perror("open_netlink");
		return -1;
	}
	if (send_msgs) {
		int i, j;

		memset(buf, 0, sizeof(buf));

		data = (struct cn_msg *)buf;

		data->id.idx = CN_TEST_IDX;
		data->id.val = CN_TEST_VAL;
		data->seq = seq++;
		data->ack = 0;
		data->len = 0;

		for (j=0; j<10; ++j) {
			for (i=0; i<1000; ++i) {
				len = netlink_send(s, data);
				if (len < 0) 
					goto send_end;
				
				ulog("%d messages have been sent to %08x.%08x.\n", i, data->id.idx, data->id.val);
				sleep(1);
			}

			ulog("%d messages have been sent to %08x.%08x.\n", i, data->id.idx, data->id.val);
		}
send_end:

		return 0;
	}


	pfd.fd = s;

	while (!need_exit) {
		pfd.events = POLLIN;
		pfd.revents = 0;
		switch (poll(&pfd, 1, -1)) {
			case 0:
				need_exit = 1;
				break;
			case -1:
				if (errno != EINTR) {
					need_exit = 1;
					break;
				}
				continue;
		}
		if (need_exit)
			break;

		memset(buf, 0, sizeof(buf));
		len = recv(s, buf, sizeof(buf), 0);
		if (len == -1) {
			perror("recv buf");
			close(s);
			return -1;
		}
		reply = (struct nlmsghdr *)buf;

		switch (reply->nlmsg_type) {
		case NLMSG_ERROR:
			fprintf(out, "Error message received.\n");
			fflush(out);
			break;
		case NLMSG_DONE:
			data = (struct cn_msg *)NLMSG_DATA(reply);

			time(&tm);
			data->data[data->len] = '\0';
			fprintf(out, "%.24s : [%x.%x] [%08u.%08u]: %s\n",
				ctime(&tm), data->id.idx, data->id.val, data->seq, data->ack, data->data);
			fflush(out);
			break;
		default:
			break;
		}
	}

	close(s);
	return 0;
}
