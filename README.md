# DPU-BENCH: A Microbenchmark Suite to Measure the Offload Efficiency of SmartNICS

## Link to Paper
[DPU-Bench](https://doi.org/10.1145/3569951.3593595)

## How to Run:
1. Compile MPI libraries (if not done) for both your host platform and your DPU platform.
2. Set up your hostfiles -- DPU-Bench assumes a block-style hostfile is used.
3. Set up your configuration files for MPMD mode.
4. Copy all necessary binaries (MPI library, DPU-Bench Executables) into a common folder on both DPU and Host, such as /tmp -- this makes writing bash scripts a LOT easier.
5. `mpirun -np <host+worker task count> --hostfile <hostfile> --configfile <configfile>`
6. Available command line arguments to place into your config file:
    1. -m: <message_size>
    2. -w <num_workers>
    3. -i <num_iterations>
    4. -v <validation>: 1 or 0
    5. -x <warmup_iteration count>

Config file example: see samples/configfiles for examples used in the paper
Hostfile example: see samples/hostfiles for examples used in the paper
Run Script example: see samples/RunScripts for an example used in the paper

7. Benchmarks Supported (Direct algorithms as of November 05, 2024):
    1. Direct Bcast (Cyclic and Block based assignment of DPU workers)
    2. Direct Gather (Cyclic and Block based assignment of DPU workers)
    3. Direct Allgather v1 (Inefficient, Cyclic and Block based assignment of
       DPU workers)
    4. Direct Allgather v2 (More efficient, Cyclic based assignment of DPU
       workers)
    
