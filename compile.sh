#qsub -A Aurora_deployment -q workq -l select=2 -l walltime=02:00:00,filesystems=home -I
module load frameworks
icpx src/init_cxi.cpp src/collectives/allreduce.cpp src/tests/test_allreduce.cpp -o test_compile  -I src -I src/collectives -I /opt/cray/libfabric/1.20.1/include -L /opt/cray/libfabric/1.20.1/lib64 -lfabric -lmpi -qopenmp -O3 -mavx2 -march=native
#export CCL_ALLREDUCE=topo
#export CCL_ALLREDUCE_SCALEOUT=rabenseifner
#mpirun -n 2 -ppn 1 --cpu-bind depth -- ./test_dev
#mpirun -n 24 -ppn 12 --cpu-bind depth -- ./test_dev
echo "compile done"