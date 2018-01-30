/*
 * Copyright (c) 2018 CPqD Foundation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <linux/if_ether.h>
#include <pcap/pcap.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signalfd.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <syslog.h>
#include <unistd.h>

#include "graphs.h"

#define PIPE_IN ".in"
#define PIPE_OUT ".out"

#define PACKET_START 0
#define FRAME_SIZE 1
#define PACKET_PSDU 2

static GMainLoop *main_loop;
static char **pipe_in;
static char **pipe_out;
static int *fd_in;
static int fdSize;
unsigned char **buffer;
int number_nodes;
float **m;

/*semaphore variables */
sem_t *sem;
int *buffer_index;
int *packet_state;
int *packet_index;
int *packet_len;
bool **send;
float channel_statistic_send;

/* allocate size for arrays */
void alloc_size(void)
{
	fd_in = malloc(number_nodes * sizeof(int));
	buffer_index = malloc(number_nodes * sizeof(int));
	packet_state = malloc(number_nodes * sizeof(int));
	packet_index = malloc(number_nodes * sizeof(int));
	packet_len = malloc(number_nodes * sizeof(int));

	buffer = (unsigned char **) malloc(
		number_nodes * sizeof(unsigned char *));

	for (int lines = 0; lines < number_nodes; lines++) {
		buffer[lines] = (unsigned char *) malloc(
			10000 * sizeof(unsigned char));
	}

	send = (bool **) malloc(number_nodes * sizeof(bool *));
	for (int lines = 0; lines < number_nodes; lines++) {
		send[lines] = (bool *) malloc(number_nodes * sizeof(bool));
	}

	pipe_in = (char **) malloc(number_nodes * sizeof(char *));
	pipe_out = (char **) malloc(number_nodes * sizeof(char *));
	for (int lines = 0; lines < number_nodes; lines++) {
		pipe_in[lines] = (char *) malloc(40 * sizeof(char));
		pipe_out[lines] = (char *) malloc(40 * sizeof(char));
	}
}

/* free space */
void dealloc_size(void)
{
	free(fd_in);
	free(buffer_index);
	free(packet_state);
	free(packet_index);
	free(packet_len);

	for (int i = 0; i < number_nodes; i++) {
		free(buffer[i]);
		free(send[i]);
		free(pipe_in[i]);
		free(pipe_out[i]);
	}
	free(buffer);
	free(send);
	free(pipe_in);
	free(pipe_out);
}

void setup_reachable_nodes(int pos)
{
	/*set randomly probability to send the packet */
	channel_statistic_send = random_nodes();

	for (int i = 0; i < number_nodes; i++) {
		if (m[pos][i] > channel_statistic_send) {
			send[pos][i] = TRUE;
		}
	}
}

void reset_reachable_nodes(int pos)
{
	for (int i = 0; i < number_nodes; i++) {
		send[pos][i] = FALSE;
	}
}

void broadcast_data(int pos, unsigned char *buf)
{
	for (int i = 0; i < fdSize; i++) {
		if (send[pos][i] == TRUE) {
			write(fd_in[i], buf, 1);
		}
	}
}

static gboolean fifo_handler(GIOChannel *channel,
			     GIOCondition cond,
			     gpointer user_data)
{
	unsigned char buf[1];
	ssize_t result;
	int fd;
	int pos = GPOINTER_TO_INT(user_data);

	if (cond & (G_IO_NVAL | G_IO_ERR | G_IO_HUP)) {
		printf("Pipe closed");
		return FALSE;
	}

	memset(buf, 0, sizeof(buf));
	fd = g_io_channel_unix_get_fd(channel);

	result = read(fd, buf, 1);

	if (result != 1) {
		printf("Failed to read %lu", result);
		return FALSE;
	}

	if (packet_state[pos] == PACKET_START) {
		int ret = sem_trywait(sem);

		if (ret != 0) {
			buffer[pos][buffer_index[pos]] = *buf;
			buffer_index[pos] += 1;
			return TRUE;
		}
	}

	if (buffer_index[pos] == 0) {
		/* We have no buffer do the regular process */

		if (packet_state[pos] == PACKET_START) {
			/* packet not started */
			if (*buf == 0xF0) {
				setup_reachable_nodes(pos);

				/* Packet started */
				broadcast_data(pos, buf);
				packet_state[pos] = FRAME_SIZE;

			} else {
				/* Wrong byte of start */
				printf("Wrong byte for pkt start %d\n", *buf);
				sem_post(sem);
				return TRUE;
			}
		} else if (packet_state[pos] == FRAME_SIZE) {
			packet_len[pos] = (int)*buf;
			broadcast_data(pos, buf);
			packet_state[pos] = PACKET_PSDU;
		} else if (packet_state[pos] == PACKET_PSDU) {
			broadcast_data(pos, buf);
			packet_index[pos] += 1;

			if (packet_index[pos] == packet_len[pos]) {
				packet_state[pos] = 0;
				packet_index[pos] = 0;
				packet_len[pos] = 0;
				reset_reachable_nodes(pos);
				printf("Unlock packet %d\n", pos);
				sem_post(sem);
			}
		}
	} else {
		/* We do have buffer. Go for bulk send */
		buffer[pos][buffer_index[pos]] = *buf;
		buffer_index[pos] += 1;

		for (int i = 0; i < buffer_index[pos]; ++i) {
			*buf = buffer[pos][i];
			buffer[pos][i] = 0xFF;

			if (packet_state[pos] == PACKET_START) {
				/* packet not started */
				if (*buf == 0xF0) {
					setup_reachable_nodes(pos);

					/* Packet started */
					broadcast_data(pos, buf);
					packet_state[pos] = FRAME_SIZE;

				} else {
					/* Wrong byte of start */
					printf("Wrong byte for pkt start %d\n",
						*buf);
				}
			} else if (packet_state[pos] == FRAME_SIZE) {
				packet_len[pos] = (int)*buf;
				broadcast_data(pos, buf);
				packet_state[pos] = PACKET_PSDU;
			} else if (packet_state[pos] == PACKET_PSDU) {
				broadcast_data(pos, buf);
				packet_index[pos] += 1;

				if (packet_index[pos] == packet_len[pos]) {
					packet_state[pos] = 0;
					packet_index[pos] = 0;
					packet_len[pos] = 0;
					reset_reachable_nodes(pos);
				}
			}
		}

		buffer_index[pos] = 0;
		if (packet_state[pos] == PACKET_START) {
			printf("Unlock buffer %d\n", pos);
			sem_post(sem);
		}
	}

finish:
	return TRUE;
}

static int setup_fifofd(char *pipe, int pos)
{
	GIOChannel *channel;
	guint source;
	int fd;
	char *pipe_new = pipe;

	printf("%s\n", pipe_new);

	fd = open(pipe_new, O_RDONLY);
	if (fd < 0) {
		return fd;
	}

	channel = g_io_channel_unix_new(fd);
	g_io_channel_set_close_on_unref(channel, TRUE);

	source = g_io_add_watch(channel,
			G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
			fifo_handler, GINT_TO_POINTER(pos));

	g_io_channel_unref(channel);
	return source;
}

int main(int argc, char *argv[])
{
	int ret;
	char str_num[4];
	int currentIndex;

	srand((unsigned int) time(NULL));

	/* verify if the hub is using graph's */
	if (argc == 2) {
		m = read_csv(argv[1]);

		if (m == NULL) {
			printf("Error reading csv file.\n");
			return 1;
		}

		number_nodes = read_lines(argv[1]);
	} else {
		printf("Wrong input. Expecting: ./hub <graph.csv>");
	}

	/* alocate the size */
	alloc_size();

	int fifoTrain[number_nodes];
	int open_fifo;

	/* initializing variables */
	for (int i = 0; i < number_nodes; i++) {
		buffer_index[i] = 0;
		packet_state[i] = 0;
		packet_index[i] = 0;
		packet_len[i] = 0;
	}

	/* Initialize reachable nodes*/
	for (int i = 0; i < number_nodes; i++) {
		for (int j = 0; j < number_nodes; j++) {
			send[i][j] = FALSE;
		}
	}

	sem = (sem_t *) malloc(sizeof(sem_t));
	sem_init(sem, 0, 1);

	fdSize = number_nodes;

	main_loop = g_main_loop_new(NULL, FALSE);

	for (int i = 0; i < fdSize; i++) {
		snprintf(str_num, sizeof(str_num), "%d", (i + 1));

		pipe_in[i] = g_strconcat("/tmp/hub/ip-stack-node",
			g_strconcat(str_num, PIPE_IN, NULL), NULL);
		pipe_out[i] = g_strconcat("/tmp/hub/ip-stack-node",
			g_strconcat(str_num, PIPE_OUT, NULL), NULL);

		fifoTrain[i] = setup_fifofd(pipe_out[i], i);

		if (fifoTrain[i] > 0) {
			open_fifo++;
		} else {
			printf("Failed to open the fifo node %d\n", (i + 1));
			printf("Probably inserted wrong number of nodes"
				" or this node doesn't exists\n");
			printf("******** Closing virtual-hub **************\n");
			ret = -EINVAL;
			goto exit;
		}
	}

	if (open_fifo == fdSize) {
		printf("-------------------------------\n");
		printf("All fifo opened with sucess!!\n");
		printf("-------------------------------\n");
	}

	for (int i = 0; i < fdSize; i++) {
		currentIndex = i;
		fd_in[i] = open(pipe_in[i], O_WRONLY);

		if (fd_in[i] < 0) {
			ret = -EINVAL;
			goto exit;
		}
	}

	g_main_loop_run(main_loop);
	ret = 0;

exit:
	for (int i = 0; i < currentIndex; i++) {
		if (fifoTrain[i] >= 0) {
			g_source_remove(fifoTrain[i]);
		}

		if (fd_in[i] >= 0) {
			close(fd_in[i]);
		}

		g_free(pipe_in[i]);
		g_free(pipe_out[i]);
	}

	sem_destroy(sem);
	free_memory(m, number_nodes);
	g_main_loop_unref(main_loop);
	dealloc_size();

	exit(ret);
}
