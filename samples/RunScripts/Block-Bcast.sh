#!/bin/bash

source ~/.bashrc

#echo "staged proof-of-concept-bcast with 1 DPU worker"

parent_dir=/tmp/benjaminm


export MV2_USE_RDMA_CM=0
export MV2_IBA_HCA=mlx5_0
export MV2_SUPPRESS_JOB_STARTUP_PERFORMANCE_WARNING=1  
num_iters=3
nnodes=8
host_ppn=8
host_ranks=$(($nnodes*$host_ppn))
dpu_ppn=8


for msg_size in 262144 524288 1048576 2097152 4194304; do
    for num_workers in  1 2 4 8 16 32 64
         do 
        filename=$PWD/block-staged-bcast-results-$nnodes-nodes-$host_ppn-host-ppn-$num_workers-workers-$msg_size-bytes.txt
        rm -f $filename
        touch $filename
        date >> $filename
        #  rm -f $hostfile
        #  echo "$(scontrol show hostname | head -n 1):$i" > $hostfile
        #  echo "$(scontrol show hostname | tail -n 1)" >> $hostfile
        #  echo "Running with $i procs on the host, 1 proc on the DPU (single node)
        #  using a 256KB message size "
        #cat hostfile
        #mpirun -np 4 --hostfile $PWD/hostfile-test `which mpispawn`
        exp_clang $parent_dir/MV2
        which mpispawn
        which hydra_mpi_proxy
        for j in `seq 1 $num_iters`; do
            echo "iteration $j"
            set -x
            if [[ $num_workers -le 8 ]]; then
                echo "1 worker per DPU" >> $filename
                date >> $filename
                mpirun_rsh --export-all -np $(($host_ranks +  $num_workers)) --hostfile \
                    $PWD/hostfile-1-pDPU  --config $PWD/bcast-config-$msg_size-$num_workers-worker \
                    $parent_dir/bcast/m-w-bcast-block.exe >> $filename 
            elif [[ $num_workers -le 16 ]]; then
                echo "2 workers per DPU" >> $filename
                date >> $filename
                $parent_dir/MV2/bin/mpirun_rsh --export-all -np $(($host_ranks +  $num_workers)) --hostfile \
                    $PWD/hostfile-2-pDPU  --config $PWD/bcast-config-$msg_size-$num_workers-worker \
                    $parent_dir/bcast/m-w-bcast-block.exe >> $filename 
            elif [[ $num_workers == 32 ]]; then
                echo "4 workers per DPU" >> $filename
                date >> $filename
                $parent_dir/MV2/bin/mpirun_rsh --export-all -np $(($host_ranks +  $num_workers)) --hostfile \
                    $PWD/hostfile-4-pDPU  --config $PWD/bcast-config-$msg_size-$num_workers-worker \
                    $parent_dir/bcast/m-w-bcast-block.exe >> $filename 
            else
                echo "8 workers per DPU" >> $filename
                date >> $filename
                $parent_dir/MV2/bin/mpirun_rsh --export-all -np $(($host_ranks +  $num_workers)) --hostfile \
                    $PWD/hostfile-8-pDPU  --config $PWD/bcast-config-$msg_size-$num_workers-worker \
                    $parent_dir/bcast/m-w-bcast-block.exe >> $filename 
            fi
            set +x
            echo ""
            echo ""
            echo "==========================================================================="
        done
        echo "================================================================================="
        echo "" 
    done
done


