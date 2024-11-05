#include "common.h"

void rc_create_rdma_read(global *g, int src,
        void *sbuf, void *rbuf, size_t len)
{
    static int id;
    struct ibv_send_wr *wr = &g->swr[g->nswr++];
    struct ibv_sge    *sgl = &g->sge[g->nsge++];

 

    sgl->addr   = (uintptr_t)sbuf;
    sgl->length = len;
    sgl->lkey   = g->my_lkey;

    wr->wr_id      = id++;
    wr->sg_list    = sgl;
    wr->num_sge    = 1;
    wr->opcode     = IBV_WR_RDMA_READ;
    wr->send_flags = IBV_SEND_SIGNALED;
    wr->next       = NULL;
    wr->wr.rdma.remote_addr = (uintptr_t)rbuf;
    wr->wr.rdma.rkey = g->rkey[src]; 

    if (!len) {
        /* Unnecessary */
        wr->sg_list = NULL;
        wr->num_sge = 0;
    }
  //  if (len < MAX_INLINE) {
  //      wr->send_flags |= IBV_SEND_INLINE;
  //  }
}


void rc_create_rdma_write(global *g, int dst,
        void *sbuf, void *rbuf, size_t len)
{
    static int id;
    struct ibv_send_wr *wr = &g->swr[g->nswr++];
    struct ibv_sge    *sgl = &g->sge[g->nsge++]; 

    sgl->addr   = (uintptr_t)sbuf;
    sgl->length = len;
    sgl->lkey   = g->my_lkey;

    wr->wr_id      = id++;
    wr->sg_list    = sgl;
    wr->num_sge    = 1;
    wr->opcode     = IBV_WR_RDMA_WRITE;
    wr->send_flags = IBV_SEND_SIGNALED;
    wr->next       = NULL;
    wr->wr.rdma.remote_addr = (uintptr_t)rbuf;
    wr->wr.rdma.rkey = g->rkey[dst]; 


    if (!len) {
        /* Unnecessary */
        wr->sg_list = NULL;
        wr->num_sge = 0;
    }
  //  if (len < MAX_INLINE) {
 //       wr->send_flags |= IBV_SEND_INLINE;
 //   }
}


void rc_create_send(global *g, int dst,
        void *buf, size_t len)
{
    static int id;
    struct ibv_send_wr *wr = &g->swr[g->nswr++];
    struct ibv_sge    *sgl = &g->sge[g->nsge++];

    debug2("[%d][rc_create_send] dst: %d, buf: %p, len: %u\n",
            g->rank, dst, buf, len);

    sgl->addr   = (uintptr_t)buf;
    sgl->length = len;
    sgl->lkey   = g->my_lkey;

    wr->wr_id      = id++;
    wr->sg_list    = sgl;
    wr->num_sge    = 1;
    wr->opcode     = IBV_WR_SEND;
    wr->send_flags = IBV_SEND_SIGNALED;
    wr->next       = NULL;

    if (!len) {
        /* Unnecessary */
        wr->sg_list = NULL;
        wr->num_sge = 0;
    }
    if (len < MAX_INLINE) {
        wr->send_flags |= IBV_SEND_INLINE;
    }
}

void rc_create_recv(global *g, int src,
        void *buf, size_t len)
{
    static int id;
    struct ibv_recv_wr *wr = &g->rwr[g->nrwr++];
    struct ibv_sge    *sgl = &g->sge[g->nsge++];

    debug2("[%d][rc_create_recv] src: %d, buf: %p, len: %u\n",
            g->rank, src, buf, len);

    sgl->addr   = (uintptr_t)buf;
    sgl->length = len;
    sgl->lkey   = g->my_lkey;

    wr->wr_id      = id++;
    wr->sg_list    = sgl;
    wr->num_sge    = 1;
    wr->next       = NULL;

    if (!len) {
        /* Unnecessary */
        wr->sg_list = NULL;
        wr->num_sge = 0;
    }
}

void rc_post_send(global *g, int dst, struct ibv_send_wr* wr)
{ 
    struct ibv_send_wr *bad_wr;
    int rc = ibv_post_send(g->qp[dst], wr, &bad_wr);
    if(rc){
        fprintf(stderr, "[%d][rc_send] error: %s\n", g->rank, ibv_wc_status_str(rc));
    }

    assert(!rc);
}

void rc_post_recv(global *g, int src, struct ibv_recv_wr* wr)
{
    struct ibv_recv_wr *bad_wr;
    int rc = ibv_post_recv(g->qp[src], wr, &bad_wr);
    if(rc){
        fprintf(stderr, "[%d][rc_recv] error: %s\n", g->rank, ibv_wc_status_str(rc));
    }

    assert(!rc);
}

