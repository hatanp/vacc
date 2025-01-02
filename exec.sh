export CPU_BIND="verbose,list:0-12,13-25,26-38,39-51,52-64,65-77,78-90,91-103"

export FI_CXI_RX_MATCH_MODE=hardware
export FI_CXI_RDZV_THRESHOLD=50000000000
export FI_CXI_DISABLE_HOST_REGISTER=1
#export FI_CXI_DEFAULT_CQ_SIZE=131072
#export FI_CXI_DEFAULT_TX_SIZE=32768
export OMP_NUM_THREADS=8
#--hostfile=hostfile_part
source compilesh && mpiexec --cpu-bind=$CPU_BIND -n 8 -ppn 1 ./test_compile
