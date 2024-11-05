#include "common.h"
#include "compute_utils.h"

int main(int argc, char **argv)
{
    int i, peer = 0;;
    int itercount = 0;
    int rank, size;
    int iters  = 10000;
    int warm   = 100;
    int batch  = 0;
    int validate = 0;
    int j = 0;

    int num_workers = 0;
    init_arrays();

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    char *buf = NULL,
         *sbuf = NULL,
         *rbuf = NULL;
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
        printf("#[cyclic-assigned-staged-allgather: ranks: %d (%d of which are workers), iters: %d, bytes: %ld",
                size, num_workers, iters, msg_len);
    }

    size_t buf_len = 2 * msg_len * size;
    buf = malloc(buf_len);
    assert(buf);

    sbuf = buf;
    rbuf = (buf+(buf_len/2));


    global g = {0};
    g.comm  = MPI_COMM_WORLD;
    g.proto = RC;
    g.buf   = buf;
    g.len   = buf_len;
    g.port  = 1;
    int offset = msg_len*size;

    setup(&g);
    rank = g.rank;
    size = g.size;
    g.buf_addr[rank] = (uintptr_t)((void*)(rbuf));


    MPI_Barrier(MPI_COMM_WORLD);
    int wr;
    int *data3;
    int *data = (int*)sbuf;

    if(rank < size-num_workers){
        for (i = 0 ; i< (msg_len)/(sizeof(int)); i++){
            data[i] = rank;
        }
    }

    MPI_Comm Host_comm;
    MPI_Comm_split(MPI_COMM_WORLD, rank <(size-num_workers), rank, &Host_comm);

    MPI_Barrier(MPI_COMM_WORLD);    
    if (rank < size-num_workers){
        peer = 0;
        if (rank == 0 ){
            fprintf(stdout, "(rank %d) Beginning timing loop\n", rank);
        }
        for (i = 0; i < (size - num_workers); i++) {
            peer = i;
            if (peer == rank){
                memcpy(((void*)(g.buf_addr[peer]))+(rank*msg_len), sbuf, msg_len);
            }else{
                g.create_rdma_write(&g, peer, sbuf, ((void*)(g.buf_addr[peer]))+offset+(rank*msg_len), msg_len);
            }
        }
    }
    for (itercount = 0; itercount < iters + warm ; itercount++){
        if (itercount == warm) { 
            g.start_ref = MPI_Wtime();
        }
        wr = 0;
        if(rank < size-num_workers){

            for (i = 0; i < (size-num_workers); i++) {
                peer = i;
                if(peer == rank ) 
                   continue; 
                g.post_send(&g, peer, &g.swr[wr]);
                wr++;
                    
                
            }
            g.poll_cqe(&g, wr, 0);

        }
        MPI_Barrier(Host_comm);
    }

    MPI_Barrier(MPI_COMM_WORLD);
    g.end_ref = MPI_Wtime();

    if(validate == 1){
        if(rank ==0 ){
            fprintf(stderr, "Reference validation data:\n");
        }
        for (i = 0; i< size-num_workers; i++){ 
            if (rank == i){
                data3 = (int*)((void*)(g.buf_addr[i])); 

                fprintf(stderr, "rank [%d], data: [", i);
                for (j = 0; j<(msg_len*(size-num_workers))/sizeof(int); j++){
                    fprintf(stderr, "%d, ", data3[j]);
                }
                fprintf(stderr, "]\n");
            }
            sleep(1);
            MPI_Barrier(Host_comm);
        }
    }
   
    MPI_Barrier(MPI_COMM_WORLD);

    g.ref_time = (g.end_ref - g.start_ref) * 1e6 / (iters); 

    if(rank == 0 ){
        fprintf(stdout, "[Rank %d] Reference latency: %lf us\n", rank, g.ref_time);
    }

     data = (int*)sbuf;

    if(rank < size-num_workers){
        for (i = 0 ; i< (msg_len)/sizeof(int); i++){
            data[i] = rank;
        }
        data = (int*)((void*) g.buf_addr[rank]);
        for (i = 0 ; i< (msg_len*size)/sizeof(int); i++){
            data[i] = -1;
        }
        for (i = 0; i <msg_len/sizeof(int); i++){
            data[i] = rank;
        }
        if(validate == 1){
            for(i = 0; i < size-num_workers; i++){
                if(rank == i){
                    fprintf(stderr, "\nrank [%d], data: [", i);
                    for (j = 0; j<(msg_len/sizeof(int)); j++){
                        fprintf(stderr, "%d, ", data[j]);
                    }
                    fprintf(stderr, "]\n");
                }
                MPI_Barrier(Host_comm);
                sleep(1);
            }
        }
    }else{
        for(i = 0;  i< (msg_len*(size-num_workers))/sizeof(int); i++){
            data[i] = -1;
        }
    }
    MPI_Barrier(MPI_COMM_WORLD);

    g.base = size-num_workers;
    g.is_dpu = (rank >= g.base);      

    wr = 0;
   
    int counter = g.base;
    MPI_Comm worker_comm;
    MPI_Comm_split(MPI_COMM_WORLD, (rank >= g.base), rank, &worker_comm);
    fflush(stdout);

    MPI_Barrier(MPI_COMM_WORLD);
    if (g.is_dpu) {
        peer = 0; //root 
        g.base = size-num_workers;
        counter = size-num_workers;

        for (i = 0; i < g.base; i++) {
            if(num_workers == 1){
                peer = i;
                g.create_rdma_read(
                        &g, peer, sbuf+(peer*msg_len), 
                        ((void*)(g.buf_addr[peer])), msg_len);
            }else{
                if(rank == counter){
                    peer = i;
                    g.create_rdma_read(
                            &g, peer,
                            sbuf+(peer*msg_len),
                            ((void*)(g.buf_addr[peer])), msg_len);
                }
                counter++;
                if (counter > size-1) {
                    counter = g.base;
                }
            }
        }
        MPI_Barrier(worker_comm);
        if(num_workers==1){
            for (i = 0; i < g.base; i++){
                peer = i;
                g.create_rdma_write(&g, peer, sbuf, ((void*)(g.buf_addr[peer]))+offset, msg_len*g.base);
            }
        }else{
            counter = g.base;
            for (i = 0; i< g.base; i++){
                peer = i;
                if(counter == rank && g.base == counter){
                    
                }else if(rank == counter){
                    peer = i;
                    g.create_rdma_write(&g, g.base, sbuf+(peer *msg_len), 
                            ((void*)(g.buf_addr[g.base]))+(peer*msg_len),
                            msg_len);
                }
                counter++;
                if(counter > size-1){
                    counter = g.base;
                }
            }
            if(rank == g.base){
                for(i = 0; i<g.base; i++){
                    peer = i;
                    g.create_rdma_write(&g, peer, sbuf, ((void*)(g.buf_addr[peer]))+offset, msg_len*g.base);
                }
            }
            MPI_Barrier(worker_comm);
        }
        MPI_Barrier(worker_comm);
    }

    MPI_Barrier(MPI_COMM_WORLD);
    int wr1 = 0;

    for (itercount = 0; itercount < iters + warm; itercount ++){
        if (itercount == warm) { 
            g.start = MPI_Wtime();
            g.comm_start = MPI_Wtime();
        }

        if (g.is_dpu){
            wr = 0;
            wr1 = 0;
            counter = g.base;
            if(num_workers == 1){
                for (i = 0; i < (size-num_workers); i++) { 
                    peer = i;
                    g.post_send(&g, peer, &g.swr[wr++]); //RDMA read
                                  
                }
                g.poll_cqe(&g, wr, 0);
                for(i = 0 ; i<g.base; i++){
                    g.post_send(&g, i, &g.swr[wr++]);
     
                }
                g.poll_cqe(&g, wr-g.base, 0);
            }else{
                wr = 0;
                wr1 = 0;
                counter = g.base;
                for (i = 0; i < g.base; i++) { 
                    if(counter == rank){
                        peer = i;
                        g.post_send(&g, peer, &g.swr[wr++]); //RDMA read
                    }
                    counter++;
                    if(counter > size-1){
                        counter = g.base;
                    }            
                }
               
                g.poll_cqe(&g, wr, 0);
                wr1=wr;
                counter = g.base;
                for (i = 0; i < g.base; i++) {
                    peer = i;
                    if(rank == counter && counter==g.base){}else
                    if(counter == rank){
                        peer = i;
                        g.post_send(&g, g.base, &g.swr[wr++]); //RDMA read
                    }
                    counter++;
                    if(counter > size-1){
                        counter = g.base;
                    }            
                }
                MPI_Barrier(worker_comm);

                g.poll_cqe(&g, wr-wr1, 0);
                wr1 = wr;
                if(rank == g.base){
                    for(i = 0 ; i<g.base; i++){
                        g.post_send(&g, i, &g.swr[wr++]);

                    }
                    g.poll_cqe(&g, wr-wr1, 0);
                }
                MPI_Barrier(worker_comm);
            }

            counter = g.base;
            wr = wr1;
            
            wr = 0;
            g.comm_end = MPI_Wtime();
            g.comm_time = (g.comm_end - g.comm_start);
        }else{
            if(itercount == warm){
                g.comp_start = MPI_Wtime();
            }
            time_compute_on_host(g.ref_time/(1e6));
            g.comp_end = MPI_Wtime();
            g.comp_time = g.comp_end - g.comp_start;
        }

        /* Max(latency, compute) */
        MPI_Barrier(MPI_COMM_WORLD);
    }
    g.end = MPI_Wtime();
       
    g.total_time = ((g.end - g.start)*1e6);
    g.total_time /= iters;
    g.comm_time = (g.comm_time * 1e6) / iters;
    g.comp_time = (g.comp_time * 1e6) / iters;
   

    if(rank == g.base){
        MPI_Send(&(g.comm_time), 1, MPI_DOUBLE, size-num_workers-1, size-num_workers-1, MPI_COMM_WORLD);
    }

    if(validate != 0){
        if (rank == 0){
            fprintf(stderr, "Verification time\n");
        }
        for (i = 0; i< size-num_workers; i++){ 
            if (rank == i){
                data3 = (int*)((void*)(g.buf_addr[i])); 

                fprintf(stderr, "\nrank [%d], data: [", i);
                for (j = 0; j<(msg_len/sizeof(int))*(size-num_workers); j++){
                    fprintf(stderr, "%d, ", data3[j]);
                }
                fprintf(stderr, "]\n");
            }
            MPI_Barrier(Host_comm);
            sleep(1);
        }

        fflush(stdout);
        sleep(1);
    }


    MPI_Barrier(MPI_COMM_WORLD);

    if (rank == size-num_workers-1){
        if(num_workers > 0){ 
            MPI_Recv(&(g.comm_time_2), 1, MPI_DOUBLE, g.base, g.base-1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        } else{
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

