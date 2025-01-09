#include "allreduce.h"
#include "allgather.h"
#include "reduce_scatter.h"

#include <mpi.h>

#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_tagged.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_cxi_ext.h>

#include <iostream>
#include <chrono>
#include <thread>
#include <fstream>
#include <string>
#include <assert.h>

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/uio.h>

int vacc::ring_allreduce(float *input_buf, float *comm_buf, int elem_count, vacc::vacc_fi_info_t* vacc_fi_info){
    //std::cout << "starting allreduce: " << world_size << ", " << rank << "\n";


    //MPI_Barrier(MPI_COMM_WORLD);
    //Allgather ring
    

    //MPI_Barrier(MPI_COMM_WORLD);
    
    vacc::ring_reduce_scatter(input_buf, comm_buf, elem_count, vacc_fi_info);
    vacc::ring_allgather(input_buf, elem_count, vacc_fi_info);

    //std::cout << min_time/1000000 << "ms " << min_time << " ns" << " on rank " << rank << "\n";

    /*
    struct fi_cq_tagged_entry entry;
    memset(&entry,0,sizeof(entry));
    entry.op_context = tx_ctx;
    entry.flags = 0;
    entry.len = sizeof(int);
    entry.buf = out_buf;
    entry.data = 0;
    entry.tag = rank == 0 ? 1 : 2;*/

    //std::cout << "Fails:  " << fails << "," << fails2 << " ";
    //std::cout << "Done " << rank << "\n";
    return 0;
}