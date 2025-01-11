#PBS -l walltime=00:05:00
#PBS -A Aurora_deployment
#PBS -q lustre_scaling
#PBS -l select=4096
#PBS -l filesystems=flare:home
#PBS -N vacc_ar_test

#export CPU_BIND="verbose,list:0-12:13-25:26-38:39-51:52-64:65-77:78-90:91-103"
export CPU_BIND="verbose,list:0-51:52-103"
export MEM_BIND="list:2:3"
export FI_CXI_RX_MATCH_MODE=hardware
#export FI_CXI_RDZV_THRESHOLD=1000000000000
#export FI_CXI_RDZV_GET_MIN=1000000000000
export FI_CXI_RDZV_THRESHOLD=1
export FI_CXI_RDZV_GET_MIN=1
export FI_CXI_DISABLE_HOST_REGISTER=1
export FI_CXI_OFLOW_BUF_SIZE=1073741824
#export FI_CXI_DEFAULT_CQ_SIZE=131072
#export FI_CXI_DEFAULT_TX_SIZE=32768
export OMP_NUM_THREADS=8
#--hostfile=hostfile_part
#cp $PBS_NODEFILE hostfile
cd /flare/Aurora_deployment/vhat/VACC_testing/vacc
source compile.sh && mpiexec --cpu-bind=$CPU_BIND --mem-bind ${MEM_BIND} -n 8192 -ppn 2 ./test_compile
