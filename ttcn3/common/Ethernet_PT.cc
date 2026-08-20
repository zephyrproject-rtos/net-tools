/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* See Ethernet_Port.ttcn for what this is and why it exists. */

#include "Ethernet_PT.hh"

#include <errno.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netpacket/packet.h>
#include <net/ethernet.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace Ethernet__Port {

/* An ethernet header is fourteen octets: two addresses and a type. */
static const size_t HEADER_LENGTH = 14;

/* Enough for a frame with the largest ordinary payload, plus room for one
 * that arrives longer than expected so that it can be seen to be wrong
 * rather than silently truncated.
 */
static const size_t FRAME_BUFFER = 2048;

Ethernet__PT::Ethernet__PT(const char *par_port_name)
	: Ethernet__PT_BASE(par_port_name),
	  interface_name(NULL),
	  source_address_set(false),
	  ethertype_filter(-1),
	  socket_fd(-1),
	  interface_index(-1)
{
	memset(source_address, 0, sizeof(source_address));
}

Ethernet__PT::~Ethernet__PT()
{
	Free(interface_name);
}

/* Read twelve or four hexadecimal digits into octets. Returns false on
 * anything that is not the expected number of hexadecimal digits, so that a
 * mistyped configuration is reported rather than quietly turned into zeroes.
 */
static bool parse_hex(const char *text, unsigned char *out, size_t octets)
{
	if (strlen(text) != octets * 2) {
		return false;
	}

	for (size_t i = 0; i < octets; i++) {
		unsigned int value;

		if (sscanf(text + i * 2, "%2x", &value) != 1) {
			return false;
		}

		out[i] = (unsigned char)value;
	}

	return true;
}

void Ethernet__PT::set_parameter(const char *parameter_name,
				 const char *parameter_value)
{
	if (strcmp(parameter_name, "interface") == 0) {
		Free(interface_name);
		interface_name = mcopystr(parameter_value);
	} else if (strcmp(parameter_name, "source_address") == 0) {
		if (!parse_hex(parameter_value, source_address, 6)) {
			TTCN_error("Ethernet__PT(%s): source_address has to be "
				   "twelve hexadecimal digits, got '%s'",
				   get_name(), parameter_value);
		}
		source_address_set = true;
	} else if (strcmp(parameter_name, "ethertype") == 0) {
		unsigned char type[2];

		if (!parse_hex(parameter_value, type, 2)) {
			TTCN_error("Ethernet__PT(%s): ethertype has to be four "
				   "hexadecimal digits, got '%s'",
				   get_name(), parameter_value);
		}
		ethertype_filter = (type[0] << 8) | type[1];
	} else {
		TTCN_warning("Ethernet__PT(%s): unknown parameter '%s'",
			     get_name(), parameter_name);
	}
}

void Ethernet__PT::user_map(const char *, Map_Params&)
{
	struct sockaddr_ll address;
	struct packet_mreq membership;
	struct ifreq request;

	if (interface_name == NULL) {
		TTCN_error("Ethernet__PT(%s): the interface parameter has to be set",
			   get_name());
	}

	/* Every protocol, because the point of this port is to see whatever is
	 * on the link rather than one thing on it.
	 */
	socket_fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
	if (socket_fd < 0) {
		TTCN_error("Ethernet__PT(%s): cannot open a packet socket (%s). "
			   "This port has to be run with enough privilege to do so.",
			   get_name(), strerror(errno));
	}

	interface_index = if_nametoindex(interface_name);
	if (interface_index == 0) {
		close(socket_fd);
		socket_fd = -1;
		TTCN_error("Ethernet__PT(%s): there is no interface called '%s'",
			   get_name(), interface_name);
	}

	memset(&address, 0, sizeof(address));
	address.sll_family = AF_PACKET;
	address.sll_protocol = htons(ETH_P_ALL);
	address.sll_ifindex = interface_index;

	if (bind(socket_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
		close(socket_fd);
		socket_fd = -1;
		TTCN_error("Ethernet__PT(%s): cannot bind to '%s' (%s)",
			   get_name(), interface_name, strerror(errno));
	}

	/* Frames addressed to somebody else are part of what a test looks at,
	 * so ask for them too.
	 */
	memset(&membership, 0, sizeof(membership));
	membership.mr_ifindex = interface_index;
	membership.mr_type = PACKET_MR_PROMISC;

	if (setsockopt(socket_fd, SOL_PACKET, PACKET_ADD_MEMBERSHIP,
		       &membership, sizeof(membership)) < 0) {
		TTCN_warning("Ethernet__PT(%s): cannot listen promiscuously on "
			     "'%s' (%s)", get_name(), interface_name,
			     strerror(errno));
	}

	/* Send from the interface's own address unless told otherwise, so that
	 * a suite which does not care need not say anything.
	 */
	if (!source_address_set) {
		memset(&request, 0, sizeof(request));
		strncpy(request.ifr_name, interface_name, IFNAMSIZ - 1);

		if (ioctl(socket_fd, SIOCGIFHWADDR, &request) < 0) {
			TTCN_warning("Ethernet__PT(%s): cannot read the address of "
				     "'%s' (%s), sending from all zeroes",
				     get_name(), interface_name, strerror(errno));
		} else {
			memcpy(source_address, request.ifr_hwaddr.sa_data,
			       sizeof(source_address));
		}
	}

	Handler_Add_Fd_Read(socket_fd);
}

void Ethernet__PT::user_unmap(const char *, Map_Params&)
{
	if (socket_fd >= 0) {
		Handler_Remove_Fd_Read(socket_fd);
		close(socket_fd);
		socket_fd = -1;
	}
}

void Ethernet__PT::user_start()
{
}

void Ethernet__PT::user_stop()
{
}

void Ethernet__PT::outgoing_send(const Ethernet__Frame& send_par)
{
	unsigned char frame[FRAME_BUFFER];
	struct sockaddr_ll address;
	const OCTETSTRING& payload = send_par.payload();
	int payload_length = payload.lengthof();
	int type;

	if ((size_t)payload_length + HEADER_LENGTH > sizeof(frame)) {
		TTCN_error("Ethernet__PT(%s): the frame is %d octets, which is "
			   "more than this port can send",
			   get_name(), (int)(payload_length + HEADER_LENGTH));
	}

	memcpy(frame, (const unsigned char *)send_par.destination(), 6);

	if (send_par.source().lengthof() == 6) {
		memcpy(frame + 6, (const unsigned char *)send_par.source(), 6);
	} else {
		memcpy(frame + 6, source_address, 6);
	}

	memcpy(frame + 12, (const unsigned char *)send_par.etherType(), 2);
	memcpy(frame + HEADER_LENGTH, (const unsigned char *)payload,
	       payload_length);

	type = (frame[12] << 8) | frame[13];

	memset(&address, 0, sizeof(address));
	address.sll_family = AF_PACKET;
	address.sll_protocol = htons(type);
	address.sll_ifindex = interface_index;
	address.sll_halen = 6;
	memcpy(address.sll_addr, frame, 6);

	if (sendto(socket_fd, frame, HEADER_LENGTH + payload_length, 0,
		   (struct sockaddr *)&address, sizeof(address)) < 0) {
		TTCN_error("Ethernet__PT(%s): cannot send a frame (%s)",
			   get_name(), strerror(errno));
	}
}

void Ethernet__PT::Handle_Fd_Event_Readable(int fd)
{
	unsigned char frame[FRAME_BUFFER];
	Ethernet__Frame message;
	ssize_t length;
	int type;

	length = recv(fd, frame, sizeof(frame), 0);
	if (length < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
			return;
		}

		TTCN_error("Ethernet__PT(%s): cannot read a frame (%s)",
			   get_name(), strerror(errno));
	}

	if ((size_t)length < HEADER_LENGTH) {
		TTCN_warning("Ethernet__PT(%s): a frame of %d octets is too short "
			     "to hold a header, ignoring it",
			     get_name(), (int)length);
		return;
	}

	type = (frame[12] << 8) | frame[13];
	if (ethertype_filter >= 0 && type != ethertype_filter) {
		return;
	}

	message.destination() = OCTETSTRING(6, frame);
	message.source() = OCTETSTRING(6, frame + 6);
	message.etherType() = OCTETSTRING(2, frame + 12);
	message.payload() = OCTETSTRING(length - HEADER_LENGTH,
					frame + HEADER_LENGTH);

	incoming_message(message);
}

void Ethernet__PT::Handle_Fd_Event_Error(int)
{
}

void Ethernet__PT::Handle_Fd_Event_Writable(int)
{
}

} /* end of namespace */
