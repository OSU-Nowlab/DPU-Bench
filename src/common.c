#include "common.h"

extern void rc_create_send     (global *g, int dst, void *buf, size_t len);
extern void rc_create_recv     (global *g, int src, void *buf, size_t len);
extern void rc_post_send       (global *g, int dst, struct ibv_send_wr* wr);
extern void rc_post_recv       (global *g, int src, struct ibv_recv_wr* wr);
extern void rc_create_rdma_read     (global *g, int src, void *sbuf, void *rbuf, size_t len);
extern void rc_create_rdma_write    (global *g, int dst, void *sbuf, void *rbuf, size_t len);

void combine_sends   (global *g, int nsend);
void combine_recvs   (global *g, int nrecv);
void poll_cqe        (global *g, int nsend, int nrecv);


int max(int x, int y){
    return x>y?x:y;
}
int min(int x, int y){
    return x<y?x:y;
}

double max_d(double x, double y){
    return x>y?x:y;
}
double min_d(double x, double y){
    return x<y?x:y;
}


void print_help(){
    fprintf(stdout, "How to use: mpirun -np <host_procs + worker_procs> --hostfile </path/to/hostfile> --configfile </path/to/configfile>, where:\n");
    fprintf(stdout, "\t<hostfile> is a block-style hostfile and <configfile> contains only two (2) lines:\n");
    fprintf(stdout, "\t\t -n <host_procs> : /path/to/x86_impl/bin/app <flags>\n");
    fprintf(stdout, "\t\t -n <worker_procs> : /path/to/arm_impl/bin/app <flags>\n");
    fprintf(stdout, "\nFlags involved:\n");
    fprintf(stdout, "-i <iteration_count>\n");
    fprintf(stdout, "-x <warmup_iteration_count>\n");
    fprintf(stdout, "-w <num_workers -- must be > 0>\n");
    fprintf(stdout, "-v <0 or 1> for data validation\n");
    fprintf(stdout, "-h: prints this message and exits\n");
}

void setup(global *g)
{
    int i, rc; 
    /* MPI Comm */
    {
        MPI_Comm_rank(g->comm, &g->rank);
        MPI_Comm_size(g->comm, &g->size);
        if (!g->rank) {
            debug1("[%d] ranks: %d, protocol: %d, port: %d\n",
                    g->rank, g->size, g->proto, g->port);
        }
    }

    /* Open Device */
    {
        int nd;
        g->dvl = ibv_get_device_list(&nd);
        assert(nd > 0);
        assert(g->dvl);

        g->dev = g->dvl[0];
        assert(g->dev);

        g->ctx = ibv_open_device(g->dev);
        assert(g->ctx);

        g->pd  = ibv_alloc_pd(g->ctx);
        assert(g->pd);

        g->mr  = ibv_reg_mr(g->pd, g->buf, g->len,
                IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ);
        assert(g->mr);

        g->scq = ibv_create_cq(g->ctx, TX_DEPTH, NULL, NULL, 0);
        assert(g->scq);

        g->rcq = ibv_create_cq(g->ctx, RX_DEPTH, NULL, NULL, 0);
        assert(g->rcq);
    }

    /* Get Local Info */
    {
        struct ibv_port_attr attr = {0};
        rc = ibv_query_port(g->ctx, 1, &attr);
        assert(!rc);
        g->my_lid  = attr.lid;
        g->my_psn  = lrand48() & 0xffffff;
        g->my_lkey = g->mr->lkey; 
        g->my_rkey = g->mr->rkey;
        g->my_buf_addr =(uintptr_t) g->buf;
        debug1("[%d] my_lid: %d, my_psn: %d, my_lkey: %u, my_rkey: %u\n",
                g->rank, g->my_lid, g->my_psn, g->my_lkey, g->my_rkey);
    }

    /* 
     * Create QPs
     * RC requires N QPs
     * UD requires 1 QP
     * DC requires 1 QP
     * But we create N QPs anyway
     */
    {
        struct ibv_qp_init_attr attr = {0};
        attr.send_cq            = g->scq;
        attr.recv_cq            = g->rcq;
        attr.cap.max_send_wr    = TX_DEPTH;
        attr.cap.max_recv_wr    = RX_DEPTH;
        attr.cap.max_send_sge   = 1;
        attr.cap.max_recv_sge   = 1;
        attr.cap.max_inline_data = MAX_INLINE;
        attr.sq_sig_all = 0;

        if (g->proto == RC) {
            attr.qp_type = IBV_QPT_RC;
        } else {
            /* Not yet implemented: UD AND DC */
            assert(0);
        }

        for (i=0; i<g->size; i++) {
            g->qp[i] = ibv_create_qp(g->pd, &attr);
            assert(g->qp[i]);
            g->my_qpn[i] = g->qp[i]->qp_num;
        }
        debug2("[%d] my_qpn: %d %d ...\n",
                g->rank, g->my_qpn[0], g->my_qpn[1]);
    }

    /* Init QPs */
    {
        struct ibv_qp_attr attr = {0};
        attr.qp_state   = IBV_QPS_INIT;
        attr.pkey_index = 0;
        attr.port_num   = g->port;
        attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ;

        for (i=0; i<g->size; i++) {
            rc = ibv_modify_qp(g->qp[i], &attr,
                    IBV_QP_STATE              |
                    IBV_QP_PKEY_INDEX         |
                    IBV_QP_PORT               |
                    IBV_QP_ACCESS_FLAGS);
            assert(!rc);
        }
    }
    
    /* Address Exchange */
    {
        MPI_Allgather(&g->my_lid,  1, MPI_INT, g->lid,  1, MPI_INT, g->comm);
        MPI_Allgather(&g->my_psn,  1, MPI_INT, g->psn,  1, MPI_INT, g->comm);
        MPI_Allgather(&g->my_rkey, 1, MPI_INT, g->rkey, 1, MPI_INT, g->comm);
        MPI_Allgather(&g->my_buf_addr, sizeof(g->my_buf_addr), MPI_BYTE, g->buf_addr, sizeof(g->my_buf_addr), MPI_BYTE, g->comm);
        MPI_Alltoall (g->my_qpn,   1, MPI_INT, g->qpn,  1, MPI_INT, g->comm);    
    }

    /* Init -> RTR */
    if (g->proto == RC) {
        for (i=0; i<g->size; i++) {
            if (i == g->rank)   continue;
            struct ibv_qp_attr attr = {0};
            attr.qp_state = IBV_QPS_RTR;
            attr.path_mtu = IBV_MTU_4096;
            attr.max_dest_rd_atomic = 1;
            attr.min_rnr_timer = 12;

            attr.ah_attr.is_global      = 0;
            attr.ah_attr.sl             = 0;
            attr.ah_attr.src_path_bits  = 0;
            attr.ah_attr.port_num       = g->port;
            attr.ah_attr.dlid   = g->lid[i];
            attr.dest_qp_num    = g->qpn[i];
            attr.rq_psn         = g->psn[i];

            debug2("[%d] dst: %d, lid: %d, qpn: %d, psn: %d\n",
                    g->rank, i, g->lid[i], g->qpn[i], g->psn[i]);

            rc = ibv_modify_qp(g->qp[i], &attr,
                    IBV_QP_STATE                |
                    IBV_QP_AV                   |
                    IBV_QP_PATH_MTU             |
                    IBV_QP_DEST_QPN             |
                    IBV_QP_RQ_PSN               |
                    IBV_QP_MAX_DEST_RD_ATOMIC   |
                    IBV_QP_MIN_RNR_TIMER
                    );
            assert(!rc);
        }
    } else {
        /* Not yet implemented: UD and DC */
        assert(0);
    }

    MPI_Barrier(g->comm);

    /* RTR -> RTS */
    if (g->proto == RC) {
        for (i=0; i<g->size; i++) {
            if (i == g->rank)   continue;
            struct ibv_qp_attr attr = {0};
            attr.qp_state = IBV_QPS_RTS;
            attr.sq_psn   = g->my_psn;
            attr.timeout        = 14;
            attr.retry_cnt      = 7;
            attr.rnr_retry      = 7;
            attr.max_rd_atomic  = 1;

            rc = ibv_modify_qp(g->qp[i], &attr,
                    IBV_QP_STATE                |
                    IBV_QP_SQ_PSN               |
                    IBV_QP_TIMEOUT              |
                    IBV_QP_RETRY_CNT            |
                    IBV_QP_RNR_RETRY            |
                    IBV_QP_MAX_QP_RD_ATOMIC
                    );
            assert(!rc);
        }
    } else {
        /* Not yet implemented: UD and DC */
        assert(0);
    }

    /* Assign Function Pointers */
    if (g->proto == RC) {
        g->create_send  = rc_create_send;
        g->create_recv  = rc_create_recv;
        g->create_rdma_read = rc_create_rdma_read;
        g->create_rdma_write = rc_create_rdma_write;
        g->post_send    = rc_post_send;
        g->post_recv    = rc_post_recv;
    } else {
        /* Not yet implemented: UD and DC */
        assert(0);
    }
    g->combine_sends = combine_sends;
    g->combine_recvs = combine_recvs;
    g->poll_cqe      = poll_cqe;

    g->comm_start = g->comm_end = g->comm_time = 0;
    g->comp_start = g->comp_end = g->comp_time = 0;
    g->start = g->end = g->total_time = 0;
    g->comm_time_2 = 0;
    g->overlap = g->offload_efficiency = 0;
    g->is_dpu = g->base = 0;
    g->start_ref = g->end_ref = g->ref_time = 0;
    
}

double compute_overlap(double comm_time, double comp_time, double total_time){
    return 100.0 * max_d(0, min_d(1, (comm_time + comp_time - total_time)/min_d(comm_time, comp_time)));
}

double compute_offload_efficiency(double ref_time, double total_time){
    double oe = (ref_time/total_time)*100;
    return ( oe > 100 ? 100 : oe);
}

void print_stats (global *g){

    double true_total_time = max_d(g->total_time, max_d(g->comm_time_2,g->comp_time));

    fprintf(stdout, "Injected work time: %.2lf us\n", g->comp_time);
    fprintf(stdout, "Pure_comm_time = %.2lf\n", g->comm_time_2);
    g->overlap = compute_overlap(g->comm_time_2, g->comp_time, true_total_time); 
    fprintf(stdout, "Measured percent overlap: %.2lf\n", g->overlap);
    g->offload_efficiency =  compute_offload_efficiency(g->ref_time, g->total_time);
    fprintf(stdout, "Offload Efficiency (Scale of 1 to 100): %.2lf\n", g->offload_efficiency);
    fprintf(stdout, "Total runtime: %.2lf us\n", true_total_time);

}

void combine_sends(global *g, int nsend)
{
    int i;
    assert(nsend <= g->nswr);

    for (i=0; i<nsend-1; i++) {
        g->swr[i].send_flags &= ~IBV_SEND_SIGNALED; 
        g->swr[i].next = &g->swr[i+1];
    }
    g->swr[i].send_flags |= IBV_SEND_SIGNALED;
    g->swr[i].next = NULL;
}

void combine_recvs(global *g, int nrecv)
{
    int i;
    assert(nrecv <= g->nrwr);
    
    for (i=0; i<nrecv-1; i++) {
        g->rwr[i].next = &g->rwr[i+1];
    }
    g->rwr[i].next = NULL;
}

void poll_cqe(global *g, int nsend, int nrecv)
{
    int rc = 0;

    while(nsend) {
        rc = ibv_poll_cq(g->scq, nsend, g->wc);
        if (rc > 0) {
            if(g->wc[0].status != IBV_WC_SUCCESS) {
               fprintf(stderr, "[rank %d], nsend %d, nrecv %d, status %s \n",g->rank, nsend, nrecv, ibv_wc_status_str(g->wc[0].status));
            }
            nsend -= rc;
            assert(!g->wc[0].status);
        }
    }

    while(nrecv) {
        rc = ibv_poll_cq(g->rcq, nrecv, g->wc);
        if (rc > 0) {
            if(g->wc[0].status != IBV_WC_SUCCESS) {
                fprintf(stderr, "[rank %d], nsend %d, nrecv %d, status %s \n",g->rank, nsend, nrecv, ibv_wc_status_str(g->wc[0].status));
            }
            nrecv -= rc;
            assert(!g->wc[0].status);
        }
    }
}

void teardown(global *g)
{
    int i = 0;
    for (i=0; i<g->size; i++) {
        if (g->ah[i]) { ibv_destroy_ah(g->ah[i]); }
        if (g->qp[i]) { ibv_destroy_qp(g->qp[i]); }
    }
    if (g->scq) { ibv_destroy_cq    (g->scq); }
    if (g->rcq) { ibv_destroy_cq    (g->rcq); }
    if (g->mr)  { ibv_dereg_mr      (g->mr);  }
    if (g->remote_mr)   { ibv_dereg_mr  (g->remote_mr); }
    if (g->pd)  { ibv_dealloc_pd    (g->pd);  }
    if (g->ctx) { ibv_close_device  (g->ctx); }
    if (g->dvl) { ibv_free_device_list(g->dvl); }
}

