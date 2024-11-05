#ifndef DPUBENCH_COMPUTE_UTILS_H
#define DPUBENCH_COMPUTE_UTILS_H
#include "common.h"
#include <stdlib.h>

/* Dummy compute things */
#define DIM 25 /* Matrix Dimension for Dummy Compute */

int min(int a, int b);
int max(int a, int b);
double min_d(double a, double b);
double max_d(double a, double b);


void do_compute_on_host(void);
void time_compute_on_host(double target_seconds);
void init_arrays();



#endif /* DPUBENCH_COMPUTE_UTILS_H */
