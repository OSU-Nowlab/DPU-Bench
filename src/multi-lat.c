#include "common.h"
#include "compute_utils.h"

void create_rdma_write(global *g, struct ibv_mr *mr, int dst, void *sendbuf, void *recvbuf, size_t len){
    static int id;
    struct ibv_send_wr *wr = &g->swr[g->nswr++];
    struct ibv_sge    *sgl = &g->sge[g->nsge++];

//   fprintf(stderr, "[%d][rc_create_rdma_write] dst: %d, sbuf: %p, rbuf %p, len: %lu\n",
 //           g->rank, dst, sbuf, rbuf, len);

    sgl->addr   = (uintptr_t)sendbuf;
    sgl->length = len;
    sgl->lkey   = mr->lkey;

    wr->wr_id      = id++;
    wr->sg_list    = sgl;
    wr->num_sge    = 1;
    wr->opcode     = IBV_WR_RDMA_WRITE;
    wr->send_flags = IBV_SEND_SIGNALED;
    wr->next       = NULL;
    wr->wr.rdma.remote_addr = (uintptr_t)recvbuf;
    wr->wr.rdma.rkey = mr->rkey; 


    if (!len) {
        /* Unnecessary */
        wr->sg_list = NULL;
        wr->num_sge = 0;
    }
}
    

int main(int argc, char **argv)
{
    int itercount = 0;
    int rank, size;
    int iters  = 10000;
    int warm   = 100;
    int batch  = 0;
    int is_host = 0;
    int *message_buf = NULL;
    struct ibv_mr *mr = NULL;
    int rank_offset = 0;
    buff_info *binfo = NULL;
    int *recv_buf = NULL;

    init_arrays();

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    
    if (size < 2){
        fprintf(stderr, "Need at least 2 MPI ranks\n");
        MPI_Finalize();
        exit(-1);
    }

    if (size % 2 == 1){
        fprintf(stderr, "Need an even number of ranks (requested amount: %d)\n",
                size);
        MPI_Finalize();
        exit(-1);
    }

    rank_offset = size/2;
    is_host = rank < rank_offset;

    char *buf = NULL;
    size_t msg_len = 4;

    int opt;
    while((opt = getopt(argc, argv, "i:x:m:b:h")) != -1) {
        switch(opt) {
            case 'i':
                iters  = atoi(optarg);
                assert(iters > 0);
                break;
            case 'x':
                warm  = atoi(optarg);
                assert(warm >= 0);
                break;
            case 'm':
                msg_len  = atoi(optarg);
                assert(msg_len <= max_msg_len);
                break;
            case 'b':
                batch  = atoi(optarg);
                assert(batch == 0 || batch == 1);
                break; 
            case 'h':
                if (!rank){
                    print_help();
                }
                MPI_Finalize();
                exit(0);
                break;
            default:
                break;

        }
    }

    if (!rank) {
        printf("#[multi-pair-lat] ranks: %d, iters: %d, bytes: %ld\n",
                size, iters, msg_len);
    }

    size_t buf_len = 2 * msg_len;
    buf = malloc(buf_len);
    assert(buf);  

    global g = {0};
    g.comm  = MPI_COMM_WORLD;
    g.proto = RC;
    g.buf   = buf;
    g.len   = buf_len;
    g.port  = 1;


    setup(&g);
    rank = g.rank;
    size = g.size;
 
    g.start = 0.0;
    MPI_Barrier(MPI_COMM_WORLD);

    MPI_Barrier(MPI_COMM_WORLD);
    for (itercount = 0; itercount < iters + warm; itercount ++){
        if (itercount == warm) { 
            g.start = MPI_Wtime();
        }

        if (is_host){
            if (!message_buf) {
                message_buf = malloc(sizeof(int));
                assert(message_buf);
                mr = ibv_reg_mr(g.pd, message_buf, sizeof(int),
                        IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ);
                assert(mr);
                binfo = malloc(sizeof(buff_info));
                assert (binfo);
            }
            binfo->addr = message_buf;
            binfo->rkey = mr->rkey;
            MPI_Send(message_buf, 1, MPI_INT, rank + rank_offset, 101, MPI_COMM_WORLD);
            while(*message_buf != 1);
        }else{
            if(!recv_buf){
                recv_buf = malloc(sizeof(int));
                assert(recv_buf);
            }
            if (!message_buf) {
                message_buf = malloc(sizeof(int));
                assert(message_buf);
                mr = ibv_reg_mr(g.pd, recv_buf, sizeof(int),
                        IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ);
                assert(mr);
                binfo = malloc(sizeof(buff_info));
                assert(binfo);
            }

            MPI_Recv(recv_buf, 1, MPI_INT, rank - rank_offset, 101, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            binfo->addr = recv_buf;
            *recv_buf = 1;
            binfo->rkey = mr->rkey;
            if(itercount == 0){
                create_rdma_write(&g, mr, rank - rank_offset, recv_buf, message_buf, sizeof(int));
            }
            g.post_send(&g, rank-rank_offset, &g.swr[0]);

        }
    }
    g.end = MPI_Wtime(); 
    g.total_time = ((g.end - g.start) * 1e6) / iters; 

    if(rank == 0){
        fprintf(stderr, "Multi-pair latency (us): %.3f\n", g.total_time);
    }

    teardown(&g);
    free(buf);
    MPI_Finalize();
    return 0;
}

