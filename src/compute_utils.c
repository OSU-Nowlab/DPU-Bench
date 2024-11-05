#include "compute_utils.h"

static float **a, *x, *y;

inline void time_compute_on_host(double target_seconds){
    double t1 = 0.0, t2 = 0.0;
    double time_elapsed = 0.0;

    while (time_elapsed < target_seconds) {
        t1 = MPI_Wtime(); 
        do_compute_on_host();
        t2 = MPI_Wtime();
        time_elapsed += (t2-t1);
    }

}

inline void do_compute_on_host(void){
    int i = 0, j = 0;
    for (i = 0; i < DIM; i++)
        for (j = 0; j < DIM; j++)
            x[i] = x[i] + a[i][j]*a[j][i] + y[j];
}

void init_arrays(){

    int i = 0, j = 0;

    a = malloc(DIM * sizeof(float *));

    for (i = 0; i < DIM; i++)
        a[i] = malloc(DIM * sizeof(float));

    x = malloc(DIM * sizeof(float));
    y = malloc(DIM * sizeof(float));

    for (i = 0; i < DIM; i++) {
        x[i] = y[i] = 1.0f;
        for (j = 0; j < DIM; j++) {
            a[i][j] = 2.0f;
        }
    }
}
