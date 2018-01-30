/*
 * Copyright (c) 2018 CPqD Foundation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* generate random numbers for the nodes */
float random_nodes(void)
{
	float random_number = ((float)rand()) / ((float)RAND_MAX);
	return random_number;
}

/* free the memory */
void free_memory(float **m, int length)
{
	for (int i = 0; i < length; i++) {
		free(m[i]);
	}
	free(m);
}

/* create a dinamic matrix */
float **create_matrix(int n)
{
	float **matrix = (float **)malloc(n * sizeof(float *));

	for (int i = 0; i < n; i++) {
		matrix[i] = (float *)malloc(n * sizeof(float));
		for (int j = 0; j < n; j++) {
			matrix[i][j] = 0.0;
		}
	}

	return matrix;
}

/* read the number of lines of .csv files */
int read_lines(char *file)
{
	FILE *fd;
	int lines = 0;
	char c;

	fd = fopen(file, "r");

	while ((c = getc(fd)) != '\n') {
		if (c == ',') {
			lines++;
		}
	}
	lines++;
	return lines;
}

/* print the matrix */
void print_matrix(float **m, int n)
{
	for (int i = 0; i < n; i++) {
		for (int j = 0; j < n; j++) {
			printf(" %1.2f ", m[i][j]);
		}
		printf("\n");
	}
}

/* read .csv files containing only float contents */
float **read_csv(char *file)
{
	int file_lines = read_lines(file);
	FILE *fd;
	float **m = create_matrix(file_lines);
	int lines = 0, cols = 0;
	float num;

	fd = fopen(file, "r");

	while (fscanf(fd, "%f", &m[lines][cols++]) != EOF) {
		char separator = getc(fd);

		if (separator == '\n') {
			lines++;
			cols = 0;
		} else if (separator != ',') {
			printf("Unexpected char received: %c\n", separator);
			return NULL;
		}
	}
	fclose(fd);
	return m;
}
