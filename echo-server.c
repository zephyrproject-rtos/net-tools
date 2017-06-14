/*
 * Copyright (c) 2015 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* Listen UDP (unicast or multicast) and TCP messages from tun device and send
 * them back to client that is running in qemu.
 */

#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>
#include <errno.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/ipv6.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <fcntl.h>

#define SERVER_PORT  4242
#define MAX_BUF_SIZE 1280	/* min IPv6 MTU, the actual data is smaller */
#define MAX_TIMEOUT  3		/* in seconds */

static bool do_reverse;

static inline void reverse(unsigned char *buf, int len)
{
	int i, last = len - 1;

	for(i = 0; i < len/2; i++) {
		unsigned char tmp = buf[i];
		buf[i] = buf[last - i];
		buf[last - i] = tmp;
	}
}

static int get_ifindex(const char *name)
{
	struct ifreq ifr;
	int sk, err;

	if (!name)
		return -1;

	sk = socket(PF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (sk < 0)
		return -1;

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, name, sizeof(ifr.ifr_name) - 1);

	err = ioctl(sk, SIOCGIFINDEX, &ifr);

	close(sk);

	if (err < 0)
		return -1;

	return ifr.ifr_ifindex;
}

static int get_socket(int family, int proto)
{
	int fd;

	fd = socket(family, proto == IPPROTO_TCP ? SOCK_STREAM : SOCK_DGRAM,
		    proto);
	if (fd < 0) {
		perror("socket");
		exit(-errno);
	}
	return fd;
}

static int bind_device(int fd, const char *interface, void *addr, int len,
		       int family)
{
	struct ifreq ifr;
	int ret, val = 1;

	memset(&ifr, 0, sizeof(ifr));
	snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", interface);

	if (setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE,
		       (void *)&ifr, sizeof(ifr)) < 0) {
		perror("SO_BINDTODEVICE");
		exit(-errno);
	}

	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

	switch (family) {
		struct sockaddr_in6 *addr6;
		struct sockaddr_in *addr4;
		char addr_buf[INET6_ADDRSTRLEN];

	case AF_INET:
		addr4 = ((struct sockaddr_in *)addr);
		printf("Socket %d binding to %s\n", fd,
		       inet_ntop(AF_INET, &addr4->sin_addr,
				 addr_buf, sizeof(addr_buf)));
		break;
	case AF_INET6:
		addr6 = ((struct sockaddr_in6 *)addr);
		printf("Socket %d binding to %s\n", fd,
		       inet_ntop(AF_INET6, &addr6->sin6_addr,
				 addr_buf, sizeof(addr_buf)));
		break;
	}

	ret = bind(fd, (struct sockaddr *)addr, len);
	if (ret < 0) {
		perror("bind");
	}
}

static int receive(int fd, unsigned char *buf, int buflen,
		   struct sockaddr *addr, socklen_t *addrlen, int proto)
{
	int ret;

	if (proto == IPPROTO_UDP) {
		ret = recvfrom(fd, buf, buflen, 0, addr, addrlen);
		if (ret < 0) {
			perror("recv");
			return ret;
		}
	} else if (proto == IPPROTO_TCP) {
		ret = read(fd, buf, buflen);
		if (ret < 0) {
			perror("read");
			return ret;
		}
	} else {
		printf("Invalid protocol %d\n", proto);
		return -EPROTONOSUPPORT;
	}

	return ret;
}

static int reply(int fd, unsigned char *buf, int buflen,
		 struct sockaddr *addr, socklen_t addrlen, int proto)
{
	int ret;

	if (proto == IPPROTO_UDP) {
		ret = sendto(fd, buf, buflen, 0, addr, addrlen);
		if (ret < 0)
			perror("send");

	} else if (proto == IPPROTO_TCP) {
		int sent = 0;

		do {
			ret = write(fd, buf + sent, buflen - sent);
			if (ret <= 0)
				break;

			sent += ret;
		} while (sent < buflen);

	} else {
		printf("Invalid protocol %d\n", proto);
		return -EPROTONOSUPPORT;
	}

	return ret;
}

static int udp_receive_and_reply(fd_set *rfds, int fd_recv, int fd_send,
				 unsigned char *buf, int buflen, int proto,
				 bool do_reverse)
{
	if (FD_ISSET(fd_recv, rfds)) {
		struct sockaddr_in6 from = { 0 };
		socklen_t fromlen = sizeof(from);
		int ret;

		ret = receive(fd_recv, buf, buflen,
			      (struct sockaddr *)&from, &fromlen, proto);
		if (ret < 0)
			return ret;

		if (do_reverse)
			reverse(buf, ret);

		ret = reply(fd_send, buf, ret,
			    (struct sockaddr *)&from, fromlen, proto);
		if (ret < 0)
			return ret;

		fprintf(stderr, ".");
	}

	return 0;
}

static int tcp_receive_and_reply(fd_set *rfds, fd_set *errfds,
				 int fd_recv, int fd_send,
				 unsigned char *buf, int buflen, int proto)
{
	struct sockaddr_in6 from = { 0 };
	socklen_t fromlen = sizeof(from);
	int ret = 0;

	if (FD_ISSET(fd_recv, rfds)) {
		ret = receive(fd_recv, buf, buflen,
			      (struct sockaddr *)&from, &fromlen, proto);
		if (ret < 0)
			return ret;

		ret = reply(fd_send, buf, ret,
			    (struct sockaddr *)&from, fromlen, proto);
		if (ret < 0) {
			return ret;
		} else if (ret == 0) {
			close(fd_recv);
			printf("Connection closed fd %d\n", fd_recv);
		}

		fprintf(stderr, ".");
	}

	return ret;
}

#define MY_MCAST_ADDR6 \
	{ { { 0xff,0x84,0,0,0,0,0,0,0,0,0,0,0,0,0,0x2 } } } /* ff84::2 */

#define MY_MCAST_ADDR4 "239.192.0.2"

int family_to_level(int family)
{
	switch (family) {
	case AF_INET:
		return IPPROTO_IP;
	case AF_INET6:
		return IPPROTO_IPV6;
	default:
		return -1;
	}
}

static int join_mc_group(int sock, int ifindex, int family, void *addr,
		       int addr_len)
{
	struct group_req req;
	int ret, off = 0;

	memset(&req, 0, sizeof(req));

	req.gr_interface = ifindex;
	memcpy(&req.gr_group, addr, addr_len);

	ret = setsockopt(sock, family_to_level(family), MCAST_JOIN_GROUP,
			 &req, sizeof(req));
	if (ret < 0)
		perror("setsockopt(MCAST_JOIN_GROUP)");

	switch (family) {
	case AF_INET:
		ret = setsockopt(sock, family_to_level(family),
				 IP_MULTICAST_LOOP, &off, sizeof(off));
		break;
	case AF_INET6:
		ret = setsockopt(sock, family_to_level(family),
				 IPV6_MULTICAST_LOOP, &off, sizeof(off));
		break;
	}
	return ret;
}

static int find_address(int family, struct ifaddrs *if_address,
			const char *if_name, void *address)
{
	struct ifaddrs *tmp;
	int error = -ENOENT;

	for (tmp = if_address; tmp; tmp = tmp->ifa_next) {
		if (tmp->ifa_addr &&
		    !strncmp(tmp->ifa_name, if_name, IF_NAMESIZE) &&
		    tmp->ifa_addr->sa_family == family) {

			switch (family) {
			case AF_INET: {
				struct sockaddr_in *in4 =
					(struct sockaddr_in *)tmp->ifa_addr;
				if (in4->sin_addr.s_addr == INADDR_ANY)
					continue;
				if ((in4->sin_addr.s_addr & IN_CLASSB_NET) ==
						((in_addr_t)0xa9fe0000))
					continue;
				memcpy(address, &in4->sin_addr,
				       sizeof(struct in_addr));
				error = 0;
				goto out;
			}
			case AF_INET6: {
				struct sockaddr_in6 *in6 =
					(struct sockaddr_in6 *)tmp->ifa_addr;
				if (!memcmp(&in6->sin6_addr, &in6addr_any,
					    sizeof(struct in6_addr)))
					continue;
				if (IN6_IS_ADDR_LINKLOCAL(&in6->sin6_addr))
					continue;

				memcpy(address, &in6->sin6_addr,
				       sizeof(struct in6_addr));
				error = 0;
				goto out;
			}
			default:
				error = -EINVAL;
				goto out;
			}
		}
	}

out:
	return error;
}

static int get_address(const char *if_name, int family, void *address)
{
	struct ifaddrs *if_address;
	int err;

	if (getifaddrs(&if_address) < 0) {
		err = -errno;
		fprintf(stderr, "Cannot get interface addresses for "
			"interface %s error %d/%s",
			if_name, err, strerror(-err));
		return err;
	}

	err = find_address(family, if_address, if_name, address);

	freeifaddrs(if_address);

	return err;
}

extern int optind, opterr, optopt;
extern char *optarg;

/* The application returns:
 *    < 0 : connection or similar error
 *      0 : no errors, all tests passed
 *    > 0 : could not send all the data to server
 */
int main(int argc, char**argv)
{
	int c, ret, fd4 = 0, fd6 = 0, fd4m = 0, fd6m = 0, tcp4 = 0, tcp6 = 0,
		i = 0, timeout = 0;
	int port = SERVER_PORT;
	int accepted4 = -1, accepted6 = -1;
	struct sockaddr_in6 addr6_recv = { 0 }, maddr6 = { 0 };
	struct in6_addr mcast6_addr = MY_MCAST_ADDR6;
	struct in_addr mcast4_addr = { 0 };
	struct sockaddr_in addr4_recv = { 0 }, maddr4 = { 0 };
	int family;
	unsigned char buf[MAX_BUF_SIZE];
	char addr_buf[INET6_ADDRSTRLEN];
	const struct in6_addr any = IN6ADDR_ANY_INIT;
	const char *interface = NULL;
	fd_set rfds, errfds;
	struct timeval tv = {};
	int ifindex = -1;
	int opt = 1;

	opterr = 0;

	while ((c = getopt(argc, argv, "i:p:r")) != -1) {
		switch (c) {
		case 'i':
			interface = optarg;
			break;
		case 'p':
			port = atoi(optarg);
			break;
		case 'r':
			do_reverse = true;
			break;
		}
	}

	if (!interface) {
		printf("usage: %s [-r] -i <iface> [-p <port>]\n", argv[0]);
		printf("\t-r Reverse the sent UDP data.\n");
		printf("\t-i Use this network interface.\n");
		printf("\t-p Use this port (default is %d)\n", SERVER_PORT);
		exit(-EINVAL);
	}

	ifindex = get_ifindex(interface);
	if (ifindex < 0) {
		printf("Invalid interface %s\n", interface);
		exit(-EINVAL);
	}

	addr4_recv.sin_family = AF_INET;
	addr4_recv.sin_port = htons(port);

	/* We want to bind to global unicast address here so that
	 * we can listen correct addresses. We do not want to listen
	 * link local addresses in this test.
	 */
	get_address(interface, AF_INET, &addr4_recv.sin_addr);
	printf("IPv4: binding to %s\n",
	       inet_ntop(AF_INET, &addr4_recv.sin_addr,
			 addr_buf, sizeof(addr_buf)));

	addr6_recv.sin6_family = AF_INET6;
	addr6_recv.sin6_port = htons(port);

	/* Bind to global unicast address instead of ll address */
	get_address(interface, AF_INET6, &addr6_recv.sin6_addr);
	printf("IPv6: binding to %s\n",
	       inet_ntop(AF_INET6, &addr6_recv.sin6_addr,
			 addr_buf, sizeof(addr_buf)));

	memcpy(&maddr6.sin6_addr, &mcast6_addr, sizeof(struct in6_addr));
	maddr6.sin6_family = AF_INET6;
	maddr6.sin6_port = htons(port);

	mcast4_addr.s_addr = inet_addr(MY_MCAST_ADDR4);
	memcpy(&maddr4.sin_addr, &mcast4_addr, sizeof(struct in_addr));
	maddr4.sin_family = AF_INET;
	maddr4.sin_port = htons(port);

restart:
	if (fd4)
		close(fd4);
	if (fd6)
		close(fd6);
	if (fd4m)
		close(fd4m);
	if (fd6m)
		close(fd6m);
	if (tcp4)
		close(tcp4);
	if (tcp6)
		close(tcp6);

	fd4 = get_socket(AF_INET, IPPROTO_UDP);
	fd6 = get_socket(AF_INET6, IPPROTO_UDP);
	fd4m = get_socket(AF_INET, IPPROTO_UDP);
	fd6m = get_socket(AF_INET6, IPPROTO_UDP);
	tcp4 = get_socket(AF_INET, IPPROTO_TCP);
	tcp6 = get_socket(AF_INET6, IPPROTO_TCP);

	printf("Sockets: UPD IPv4 %d IPv6 %d, mcast IPv4 %d IPv6 %d, "
	       "TCP IPv4 %d IPv6 %d\n",
	       fd4, fd6, fd4m, fd6m, tcp4, tcp6);

	bind_device(fd4, interface, &addr4_recv, sizeof(addr4_recv), AF_INET);
	bind_device(fd6, interface, &addr6_recv, sizeof(addr6_recv), AF_INET6);

	bind_device(fd4m, interface, &maddr4, sizeof(maddr4), AF_INET);
	bind_device(fd6m, interface, &maddr6, sizeof(maddr6), AF_INET6);

	join_mc_group(fd4m, ifindex, AF_INET, &maddr4, sizeof(maddr4));
	join_mc_group(fd6m, ifindex, AF_INET6, &maddr6, sizeof(maddr6));

	ret = setsockopt(tcp4, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
	if (ret < 0) {
		perror("setsockopt TCP v4");
	}
	ret = setsockopt(tcp6, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
	if (ret < 0) {
		perror("setsockopt TCP v6");
	}
	bind_device(tcp4, interface, &addr4_recv, sizeof(addr4_recv), AF_INET);
	bind_device(tcp6, interface, &addr6_recv, sizeof(addr6_recv), AF_INET6);

	if (listen(tcp4, 0) < 0) {
		perror("IPv4 TCP listen");
	}
	if (listen(tcp6, 0) < 0) {
		perror("IPv6 TCP listen");
	}

	if (fcntl(tcp4, F_SETFL, O_NONBLOCK) < 0) {
		perror("IPv4 TCP non blocking");
	}

	if (fcntl(tcp6, F_SETFL, O_NONBLOCK) < 0) {
		perror("IPv6 TCP non blocking");
	}

#define MAX(a,b) ((a) > (b) ? (a) : (b))

	while (1) {
		int addr4len = sizeof(addr4_recv);
		int addr6len = sizeof(addr6_recv);
		int fd;

		FD_ZERO(&rfds);
		FD_SET(fd4, &rfds);
		FD_SET(fd6, &rfds);
		FD_SET(fd4m, &rfds);
		FD_SET(fd6m, &rfds);
		FD_SET(tcp4, &rfds);
		FD_SET(tcp6, &rfds);

		FD_ZERO(&errfds);
		FD_SET(tcp4, &errfds);
		FD_SET(tcp6, &errfds);

		fd = MAX(tcp4, tcp6);

		if (accepted4 >= 0) {
			FD_SET(accepted4, &rfds);
			FD_SET(accepted4, &errfds);
			fd = MAX(fd, accepted4);
		}
		if (accepted6 >= 0) {
			FD_SET(accepted6, &rfds);
			FD_SET(accepted6, &errfds);
			fd = MAX(fd, accepted6);
		}

		ret = select(fd + 1, &rfds, NULL,
			     &errfds, NULL);
		if (ret < 0) {
			perror("select");
			break;
		} else if (ret == 0) {
			continue;
		}

		if (accepted4 < 0) {
			accepted4 = accept(tcp4, (struct sockaddr *)&addr4_recv,
					   &addr4len);
			if (accepted4 < 0 &&
			    (errno != EAGAIN && errno != EWOULDBLOCK)) {
				perror("accept IPv4");
				break;
			} else if (accepted4 >= 0) {
				FD_SET(accepted4, &errfds);
				FD_SET(accepted4, &rfds);
			}
		}

		if (accepted6 < 0) {
			accepted6 = accept(tcp6, (struct sockaddr *)&addr6_recv,
					   &addr6len);
			if (accepted6 < 0 &&
			    (errno != EAGAIN && errno != EWOULDBLOCK)) {
				perror("accept IPv6");
				break;
			} else if (accepted6 >= 0) {
				FD_SET(accepted6, &errfds);
				FD_SET(accepted6, &rfds);

				printf("New connection fd %d\n", accepted6);
			}
		}

		/* Unicast IPv4 */
		if (udp_receive_and_reply(&rfds, fd4, fd4, buf, sizeof(buf),
					  IPPROTO_UDP, do_reverse) < 0)
			goto restart;

		/* Unicast IPv6 */
		if (udp_receive_and_reply(&rfds, fd6, fd6, buf, sizeof(buf),
					  IPPROTO_UDP, do_reverse) < 0)
			goto restart;

		/* Multicast IPv4 */
		if (udp_receive_and_reply(&rfds, fd4m, fd4, buf, sizeof(buf),
					  IPPROTO_UDP, do_reverse) < 0)
			goto restart;

		/* Multicast IPv6 */
		if (udp_receive_and_reply(&rfds, fd6m, fd6, buf, sizeof(buf),
					  IPPROTO_UDP, do_reverse) < 0)
			goto restart;

		/* TCP IPv4 */
		ret = tcp_receive_and_reply(&rfds, &errfds,
					    accepted4, accepted4,
					    buf, sizeof(buf),
					    IPPROTO_TCP);
		if (ret < 0)
			goto restart;
		else if (ret == 0)
			accepted4 = -1;

		/* TCP IPv6 */
		ret = tcp_receive_and_reply(&rfds, &errfds,
					    accepted6, accepted6,
					    buf, sizeof(buf),
					    IPPROTO_TCP);
		if (ret < 0)
			goto restart;
		else if (ret == 0)
			accepted6 = -1;
	}

	close(fd4);
	close(fd6);
	close(fd4m);
	close(fd6m);
	close(tcp4);
	close(tcp6);

	printf("\n");

	exit(0);
}
