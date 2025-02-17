//#include <sycl/sycl.hpp>

#include "../init_cxi.h"
#include "../collectives/allreduce.h"
#include "../collectives/reduce_scatter.h"
#include "../collectives/allgather.h"

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
#include <fstream>
#include <string>
#include <assert.h>
#include <thread>
#include <vector>

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/uio.h>


int main(int argc, char* argv[]) {

    
    //std::cout << "world_size " << world_size << " rank " << rank << "\n";

    //std::cout << "Fails:  " << fails << "," << fails2 << " ";
    vacc::vacc_fi_info_t* vacc_fi_info = vacc::init_fi_cxi();
    if(vacc_fi_info->status != 0){
        std::cout << "Error " << vacc_fi_info->status << "\n";
        return 1;
        //std::cout << "Done " << vacc_fi_info_t->status << "\n";
        //std::cout << "Done " << vacc::init_fi_cxi(2,2).status << "\n";
    }

    //size_t elem_count = 2*512'000'000;//dividable by 2^16, ~65k
    size_t elem_count = 1'073'741'824;//power of two, 2^28
    int nic_striping = 4;
    size_t nic_elem_count = elem_count/nic_striping;//power of two, 2^28
    float *input_buf = new float[elem_count];
    float *comm_buf = new float[elem_count]();

    unsigned long min_time = std::numeric_limits<unsigned long>::max();

    for(int i = 0; i<3; i++){

        for(int i = 0; i<elem_count; i++){
            input_buf[i] = vacc_fi_info->rank;
            comm_buf[i] = 0.f;
        }

        //MPI_Barrier(MPI_COMM_WORLD); 

        std::vector<std::thread> recv_threads;
        unsigned long start1 = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                        std::chrono::high_resolution_clock::now().time_since_epoch())
                                        .count();
        //Test threading for now
        for(int nic = 0;nic<nic_striping;nic++){
            //std::thread recv_thread(std::thread(vacc::ring_reduce_scatter,input_buf, comm_buf, elem_count, nic, vacc_fi_info);
            recv_threads.push_back(std::thread(vacc::ring_ar_rs,input_buf+nic_elem_count*nic, comm_buf+nic_elem_count*nic, nic_elem_count, nic, vacc_fi_info));
            //recv_thread.join();
        }
        for (auto& thread : recv_threads) {
            thread.join();
        }

        unsigned long end1 = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                        std::chrono::high_resolution_clock::now().time_since_epoch())
                                        .count();
        //clear out tx CQ
        for(int nic = 0;nic<nic_striping;nic++){
            int ret = 1;
            struct fi_cq_tagged_entry tx_entry;
            memset(&tx_entry, 0, sizeof(tx_entry));
            tx_entry.op_context = vacc_fi_info->tx_ctx[nic];
            tx_entry.flags = FI_TAGGED;
            while(ret > 0){
                ret = fi_cq_read(vacc_fi_info->tx_cq[nic], (void*)&tx_entry, 1);
            }
        }
        //MPI_Barrier(MPI_COMM_WORLD);

        //MPI_Barrier(MPI_COMM_WORLD);
        unsigned long start2 = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                        std::chrono::high_resolution_clock::now().time_since_epoch())
                                        .count();
        std::vector<std::thread> ag_threads;
        for(int nic = 0;nic<nic_striping;nic++){
            //std::thread recv_thread(std::thread(vacc::ring_reduce_scatter,input_buf, comm_buf, elem_count, nic, vacc_fi_info);
            ag_threads.push_back(std::thread(vacc::ring_ar_ag,input_buf+nic_elem_count*nic, nic_elem_count, nic, vacc_fi_info));
            //recv_thread.join();
        }
        for (auto& thread : ag_threads) {
            thread.join();
        }

        //vacc::ring_allgather(input_buf, elem_count, nic, vacc_fi_info);
        //vacc::ring_allreduce(input_buf,elem_count,vacc_fi_info);


        unsigned long end = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                        std::chrono::high_resolution_clock::now().time_since_epoch())
                                        .count();
        //Clean tx queue as allgather does not clean them now but reduce-scatter reads it.
        for(int nic = 0;nic<nic_striping;nic++){
            int ret = 1;
            struct fi_cq_tagged_entry tx_entry;
            memset(&tx_entry, 0, sizeof(tx_entry));
            tx_entry.op_context = vacc_fi_info->tx_ctx[nic];
            tx_entry.flags = FI_TAGGED;
            while(ret > 0){
                ret = fi_cq_read(vacc_fi_info->tx_cq[nic], (void*)&tx_entry, 1);
            }

        }
        
        std::cout << (end1-start1)/1000000 << "ms " << (end-start2)/1000000 << "ms " << end-start1 << " ns" << "\n";
        min_time = std::min(end - start1, min_time);
        
        MPI_Barrier(MPI_COMM_WORLD);
    }
    MPI_Barrier(MPI_COMM_WORLD);

    //bits_per_item*elems_per_bucket*num_sends/time
    //num_sends=2*(N-1) in ring, 2 for reduce-scatter and allgather rings, (N-1) sends per ring. 
    std::cout << "\"GB/s\" " << 4 * ((float)elem_count/(float)vacc_fi_info->world_size) * 2*(vacc_fi_info->world_size-1) / (float)min_time << " on rank " << vacc_fi_info->rank << "\n";

    //std::cout << "Rank " << rank << " Got: " << *(input_buf+0) << ',' <<  *(input_buf+1) << "\n";
    int correct = 0;
    int incorrect = 0;
    int correct_result = 0;
    for(int i=0; i<vacc_fi_info->world_size; i++){
        correct_result += i;
    }
    //int correct_result = (int)ring_prev;
    for(int i =0;i<elem_count;i++){
        if(*(input_buf+i)==correct_result){
            correct++;
        }else{
            incorrect++;
        }
    }
    if(incorrect>0){
        std::cout << "Correct:  " << correct << " Incorrect: " << incorrect << "\n";
    }

    std::cout << "Done: " << vacc_fi_info->rank << "\n";
    return 0;
}