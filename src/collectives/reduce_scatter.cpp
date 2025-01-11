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
#include <fstream>
#include <string>
#include <assert.h>
#include <thread>
#include <vector>

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/uio.h>



int vacc::ring_reduce_scatter(float *input_buf, float *comm_buf, int elem_count, int nic, vacc::vacc_fi_info_t* vacc_fi_info){
    return 0;
}