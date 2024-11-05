#ifndef DPUBENCH_MPI_COMMON_H
#define DPUBENCH_MPI_COMMON_H

#include <mpi.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <verbs.h>
#include <getopt.h>
#include <mlx5dv.h>
#include <errno.h>
#include <unistd.h>

#define MAX_INLINE  256
#define TX_DEPTH    256
#define RX_DEPTH    256

#define MAX_PEERS   512
#define MAX_WRS     8192
#define MAX_SGE     8192


#if 0
#define debug1      printf
#define debug2      printf
#else
#define debug1(...)
#define debug2(...)
#endif


#define min_msg_len 1
#define max_msg_len 16777216


typedef struct buff_info {
    void *addr;
    uint32_t rkey;
} buff_info;


typedef struct global global;
typedef enum protocol {
    RC, UD, DC
} protocol;

typedef struct global {
    /* User Defined */
    MPI_Comm    comm;
    protocol    proto;
    void*       buf;
    void*       remote_buf;
    size_t      len;
    size_t      remote_len;

    /* IB Context */
    struct ibv_device**     dvl;
    struct ibv_device*      dev;
    struct ibv_context*     ctx;
    struct ibv_pd*          pd;
    struct ibv_mr*          mr;
    struct ibv_mr*          remote_mr;
    struct ibv_qp*          qp[MAX_PEERS];
    struct ibv_cq*          scq;
    struct ibv_cq*          rcq;
    struct ibv_send_wr      swr[MAX_WRS];
    struct ibv_send_wr      rdma_swr[MAX_WRS];
    struct ibv_recv_wr      rwr[MAX_WRS];
    struct ibv_sge          sge[MAX_SGE];
    struct ibv_ah*          ah[MAX_PEERS];
    struct ibv_wc           wc[RX_DEPTH];
    int                     port;

    /* Connection Data */
    int         lid[MAX_PEERS];
    int         qpn[MAX_PEERS];
    int         psn[MAX_PEERS];
    unsigned    rkey[MAX_PEERS];
    /* RDMA Connection_Data */
    uintptr_t   buf_addr[MAX_PEERS];

    /* Private Data */
    int         rank;
    int         size;
    int         my_lid;
    int         my_qpn[MAX_PEERS];
    int         my_psn;
    unsigned    my_lkey;
    unsigned    my_remote_lkey;
    unsigned    my_rkey;
    unsigned    my_remote_rkey;
    uintptr_t   my_buf_addr;

    /* Extra-data for RDMA_Write/Read */
    unsigned    your_lkey;
    unsigned    your_rkey;

    /* Consumed Resources */
    int         nswr;
    /* RDMA_RESOURCES */
    int         rdma_nswr;
    int         nrwr;
    int         nsge;

    /* Function Pointers */
    void (*create_send)         (global *g, int dst, void *buf, size_t len);
    void (*create_recv)         (global *g, int src, void *buf, size_t len);
    void (*create_rdma_read)    (global *g, int src, void *sbuf, void *rbuf, size_t len);
    void (*create_rdma_write)   (global *g, int dst, void *sbuf, void *rbuf, size_t len);
    void (*combine_sends)       (global *g, int nsend);
    void (*combine_recvs)       (global *g, int nrecv);

    void (*post_send)       (global *g, int dst, struct ibv_send_wr* wr);
    void (*post_recv)       (global *g, int src, struct ibv_recv_wr* wr);
    void (*poll_cqe)        (global *g, int nsend, int nrecv);

    /* Benchmarking values */
    int is_dpu, 
        base;
    double start, 
           comm_start,
           comm_end,
           comm_time,
           comp_start,
           comp_end,
           comp_time,
           end, total_time;
    double comm_time_2;
    double overlap;
    double offload_efficiency;
    double start_ref,
           end_ref,
           ref_time;


} global;

double compute_overlap(double comm_time, double comp_time, double total_time);
double compute_offload_efficiency(double ref_time, double total_time);
void print_stats(global* g);

void setup    (global *g);
void teardown (global *g);
void print_help();

#endif //RDMABENCH_MPI_COMMON_H

