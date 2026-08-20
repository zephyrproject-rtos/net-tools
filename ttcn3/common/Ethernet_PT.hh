/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef Ethernet__PT_HH
#define Ethernet__PT_HH

#include "Ethernet_Port.hh"

namespace Ethernet__Port {

class Ethernet__PT : public Ethernet__PT_BASE {
public:
	Ethernet__PT(const char *par_port_name = NULL);
	~Ethernet__PT();

	void set_parameter(const char *parameter_name, const char *parameter_value);

private:
	void Handle_Fd_Event_Error(int fd);
	void Handle_Fd_Event_Writable(int fd);
	void Handle_Fd_Event_Readable(int fd);

protected:
	void user_map(const char *system_port, Map_Params& params);
	void user_unmap(const char *system_port, Map_Params& params);

	void user_start();
	void user_stop();

	void outgoing_send(const Ethernet__Frame& send_par);

private:
	char *interface_name;
	unsigned char source_address[6];
	bool source_address_set;
	int ethertype_filter;   /* -1 when every frame is handed up */
	int socket_fd;
	int interface_index;
};

} /* end of namespace */

#endif
