/*
 * Copyright (c) 2018 CPqD Foundation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GRAPH_H_
#define GRAPH_H_

float random_nodes(void);
void free_memory(float **m, int length);
float **create_matrix(int n);
int read_lines(char *file);
void print_matrix(float **m, int n);
float **read_csv(char *file);

#endif
