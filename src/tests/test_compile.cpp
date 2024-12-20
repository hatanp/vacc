//#include <sycl/sycl.hpp>

#include "../init_cxi.h"

#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_tagged.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_cxi_ext.h>

#include <mpi.h>
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


int main(int argc, char* argv[]) {

    int world_size = 0;
    int rank = 0;

    MPI_Init(NULL, NULL);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    

    //std::cout << "Fails:  " << fails << "," << fails2 << " ";
    vacc::vacc_fi_info_t* vacc_fi_info_t = vacc::init_fi_cxi(world_size,rank);
    if(vacc_fi_info_t->status == 0){
        std::cout << "Done " << vacc_fi_info_t->status << "\n";
        //std::cout << "Done " << vacc::init_fi_cxi(2,2).status << "\n";
        return 0;
    } else {
        return 1;
    }
}