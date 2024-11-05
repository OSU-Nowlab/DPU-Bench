#include "common.h"
#include "compute_utils.h"
int main(int argc, char **argv)
{
    int i, peer = 0;
    int j = 0;
    int itercount = 0;
    int rank, size;
    int iters  = 10000;
    int warm   = 100;
    int batch  = 0;
    int num_workers = 1;
    int validate = 0;

    int division = 0;
    
    init_arrays();

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    char *buf = NULL;
    size_t msg_len = 4;

    int opt;
    while((opt = getopt(argc, argv, "i:x:m:b:w:v:h")) != -1) {
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
            case 'w':
                num_workers = atoi(optarg);
                assert(num_workers >= 0);
                assert(num_workers <= size/2);
                break;
            case 'v':
                validate = !!atoi(optarg);
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
        printf("#[block-worker-assignment-staged-bcast] ranks: %d (%d of which are workers), iters: %d, bytes: %ld",
                size, num_workers, iters, msg_len);
    }

    size_t buf_len = 2 * msg_len;
    buf = malloc(buf_len);
    assert(buf);

    char * sbuf = buf;
  

    global g = {0};
    g.comm  = MPI_COMM_WORLD;
    g.proto = RC;
    g.buf   = buf;
    g.len   = buf_len;
    g.port  = 1;

    setup(&g);
    rank = g.rank;
    size = g.size;
 
    MPI_Barrier(MPI_COMM_WORLD);
       int wr;
    int *data3, *data = (int*) sbuf;
    if (rank == 0){
        for (i = 0 ; i < msg_len/sizeof(int); i++){
            data[i] = (rank + 1);
        }
    }

    MPI_Comm Host_comm;
    MPI_Comm_split(MPI_COMM_WORLD, rank <(size-num_workers), rank, &Host_comm);
    MPI_Barrier(MPI_COMM_WORLD);
    if (rank == 0){
        peer = 0;
        fprintf(stdout, "Beginning timing loop\n");
        for (i = 1; i < (size - num_workers); i++) {
            peer = i;
            g.create_rdma_write(&g, peer, sbuf, ((void*)(g.buf_addr[peer])), msg_len);
        }
    }
    for (itercount = 0; itercount < iters + warm ; itercount++){
        if (itercount == warm) { 
            g.start_ref = MPI_Wtime();
        }
        wr = 0;
        if(rank == 0){ 
            for (i = 1; i < (size-num_workers); i++) {
                peer = i;
                g.post_send(&g, peer, &g.swr[wr]);
                wr++;
            }
            g.poll_cqe(&g, wr, 0);
           
        }
        MPI_Barrier(Host_comm);
    }
 
    MPI_Barrier(MPI_COMM_WORLD);
    g.end_ref = MPI_Wtime();

    g.ref_time = (g.end_ref - g.start_ref) * 1e6 / (iters); /* Calculate average latency for bcast over a number of iterations */
 
    if(rank == 0 ){
        fprintf(stdout, "[Rank %d] Reference latency: %lf us\n", rank, g.ref_time);
    }
    
    g.is_dpu = (rank >= (size - num_workers));      
    wr = 0; 
    division = ((size-num_workers)/num_workers); /* Obviously can't do decimals, so the last block will end up having the remaining items */

    if (rank == 0){
        for (i = 0 ; i < msg_len/sizeof(int); i++){
            data[i] = (i + sizeof(int)) % sizeof(int);
        }
    }
    
    int temp_size = size-num_workers;
    int starting_size = 1;
    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Comm worker_comm;
    MPI_Comm_split(MPI_COMM_WORLD, (rank >=size-num_workers), rank, &worker_comm);
    
    if (g.is_dpu) {
        peer = 0; //root 
        g.base = size-num_workers; 
        g.create_rdma_read(&g, peer, sbuf, (((void*)(g.buf_addr[peer]))), msg_len);
        if (num_workers == 1){

            for (i = 1; i < (size-num_workers); i++) {
                peer = i;
                g.create_rdma_write(&g, peer, sbuf, ((void*)(g.buf_addr[peer])), msg_len);
            }
        }else{
            for(j = g.base; j < size; j++){
                if (rank == j){
                    for (i = starting_size; i < min(starting_size+division, temp_size); i++) { 
                        peer = i;
                        g.create_rdma_write(&g, peer, sbuf, (((void*)(g.buf_addr[peer]))), msg_len);

                    }
                }
                MPI_Barrier(worker_comm);
                starting_size += division;
                if(starting_size<temp_size && j == size-1){
                    j = g.base-1;
                }
            } 
        }
    }
    
    MPI_Barrier(MPI_COMM_WORLD);
    
    for (itercount = 0; itercount < iters + warm; itercount ++){
        if (itercount == warm) { 
            g.start = MPI_Wtime();
            g.comm_start = MPI_Wtime();
        }
        starting_size = 1;

        if (g.is_dpu){
            peer = 0;
            wr = 0;
            g.post_send(&g, peer, &g.swr[peer]);
            g.poll_cqe(&g, 1, 0);
            starting_size = 1;


            if(num_workers == 1){
                for (i = 1; i < (size-num_workers); i++) { 
                    peer = i;
                    g.post_send(&g, peer, &g.swr[peer]);
                    wr++;
                }
                g.poll_cqe(&g, wr, 0);
            }else{
                for(j = g.base; j < size; j++){

                    if(rank == j){
                        wr=1;
                        for(i = starting_size; i<min(starting_size+division, temp_size); i++){
                            peer = i;
                            g.post_send(&g, peer, &g.swr[wr]);
                            wr++;      
                        }

                    }
                    MPI_Barrier(worker_comm);
                    starting_size += division;
                    if(starting_size<temp_size && j == size-1){
                        j = g.base-1;
                    }
                }

                g.poll_cqe(&g, wr-1, 0);
            }
            g.comm_end = MPI_Wtime();
            g.comm_time = (g.comm_end - g.comm_start);
        }else{
            if(itercount == warm){
                g.comp_start = MPI_Wtime();
            }
            time_compute_on_host(g.ref_time/(1e6));
            g.comp_end = MPI_Wtime();
            g.comp_time = (g.comp_end - g.comp_start);
        }

        /* Max(latency, compute) */
        MPI_Barrier(MPI_COMM_WORLD);
    }
    g.end = MPI_Wtime();
    MPI_Barrier(MPI_COMM_WORLD);
    g.total_time = (g.end - g.start) * 1e6 / iters; 
    g.comm_time = (g.comm_time * 1e6)/iters;
    g.comp_time = (g.comp_time * 1e6)/iters;

     if(validate != 0){ 
        if(rank == 0){
            fprintf(stderr, "Verification time\n");
        }

        for (i = 1; i< size-num_workers; i++){ 
            if(rank == i){
                data3 = (int*)(((void*)(g.buf_addr[i]))); 

                fprintf(stderr, "rank [%d], data: [", i);
                for (j = 0; j<msg_len/sizeof(int); j++){
                    fprintf(stderr, "%d, ", data3[j]);
                }
                fprintf(stderr, "]\n");
            }
            MPI_Barrier(Host_comm);
        } 
    }

    if(rank == size-num_workers) {
        MPI_Send(&(g.comm_time), 1, MPI_DOUBLE, size-num_workers-1,size-num_workers-1, MPI_COMM_WORLD);
    } 

    if (rank == size-num_workers-1){
        if(num_workers > 0){
            MPI_Recv(&(g.comm_time_2), 1, MPI_DOUBLE, size-num_workers, size-num_workers-1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }else{
            fprintf(stderr, "Warning: Using 0 workers = no offloaded communication. Using reference time as comm_time\n");
            g.comm_time_2 = g.ref_time;
        }
        print_stats(&g);
    }

    MPI_Comm_free(&worker_comm);
    MPI_Comm_free(&Host_comm);
    teardown(&g);
    free(buf);
    MPI_Finalize();
    return 0;
}

