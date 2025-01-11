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

int handle_recv(float* input_buf, float* comm_buf, vacc::vacc_fi_info_t* vacc_fi_info, int c_r, int p_r, int nic, int rs_end_chunk, int ring_next_nic0, size_t chunk_count, size_t chunk_elems, size_t part_chunk_elems_pipe, size_t pipe_chunk_count){
    int err = 0;
    int sum_loop_threads = 8;
    size_t sum_loop_elems = part_chunk_elems_pipe/sum_loop_threads;
    //assert(n_r==nic);
    //std::cout << "Rank " << rank << " Got: " << *(comm_buf+(c_r*chunk_elems)+(p_r*part_chunk_elems_pipe)+(n_r*part_chunk_elems_nic)) << " for " << entry.tag << " (" <<  c_r << "," << p_r << "," << n_r << ") "  << " of size " << chunk_count << "," << nic_chunk_count << "," << pipe_chunk_count << " (c,p,n)" << "\n";
    //last = now;
    /*unsigned long t0 = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    std::chrono::high_resolution_clock::now().time_since_epoch())
                                    .count();*/
    //#pragma omp parallel for schedule(static,4096)
    //#pragma omp parallel for schedule(static,8)
    //Probably can be optimized further with vector instructions and such. Good enough for now, will be overlapped with comms anyway..
    #pragma omp parallel for
    for(int k=0;k<sum_loop_threads;k++){
        for(int j=0;j<part_chunk_elems_pipe/sum_loop_threads;j++){
            *(input_buf
            +(c_r*chunk_elems)
            +(p_r*part_chunk_elems_pipe)
            +(k*sum_loop_elems)+j)
            += 
            *(comm_buf
            +(c_r*chunk_elems)
            +(p_r*part_chunk_elems_pipe)
            +(k*sum_loop_elems)+j);
        }
    }
    //std::this_thread::sleep_for(std::chrono::nanoseconds(1000));
    /*unsigned long t1 = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    std::chrono::high_resolution_clock::now().time_since_epoch())
                                    .count();*/
    //std::cout << (t1-t0)/1000000 << "ms " << t1-t0 << " ns" << "\n";
    //Send a part of the chunk to the next one, unless we expect to sum to full result. 
    //std::cout << "rank: " << rank << " ch: " << ch << " c_r: " << c_r << " c: " << c << " last_recv " << last_ag_recv_chunk << " ag_start_chunk: " << ag_start_chunk << " " << (int)(ch < chunk_count-1) << "," <<  (int)(c_r != ag_start_chunk) << "\n";
    if(c_r != rs_end_chunk){
        uint64_t send_tag = c_r+(p_r<<16);//tag is the data offset
        //std::cout << "Posting after sends with rank: " << rank  << " tag: " << send_tag << " " << " (" <<  c_r << "," << p_r << "," << n_r << ") " << "\n";
        err = fi_tsend(vacc_fi_info->ep[nic], (void*)(input_buf+(c_r*chunk_elems)+(p_r*part_chunk_elems_pipe)), part_chunk_elems_pipe*sizeof(float), NULL, vacc_fi_info->addr_vect[nic][ring_next_nic0+nic], send_tag, vacc_fi_info->tx_ctx[nic]);
        if (err != FI_SUCCESS) {
            std::cout << "fi_send 3 TODO error handling " << fi_strerror(err) << "\n";
            return 1;
        } else {
            //Pipeline host to device
        }
    }
    return 0;
}


int vacc::ring_ar_rs(float *input_buf, float *comm_buf, int elem_count, int nic, vacc::vacc_fi_info_t* vacc_fi_info){

    //comm_buf = new float[elem_count];
    int world_size = vacc_fi_info->world_size;
    int rank = vacc_fi_info->rank;

    int err = 0;
    //size_t elem_count = 2*512'000'000;//dividable by 2^16, ~65k
    //size_t elem_count = 1'073'741'824;//power of two, 2^28
    //size_t elem_count = (16384)*8192;
    //8192*4096/8 is minimum.
    //size_t elem_count = (8192)*4096/8;//4MB to get good link performance
    size_t chunk_count = world_size;
    size_t chunk_elems = elem_count/chunk_count;
    size_t pipe_chunk_count = 4;//4 pipeline stages to get "75%" of comms overlaped with reduce sum
    size_t part_chunk_elems_pipe = chunk_elems/pipe_chunk_count;


    int ag_start_chunk = rank + 1 < chunk_count ? rank + 1 : rank + 1 - chunk_count;
    int rs_end_chunk = ag_start_chunk;
    int last_ag_recv_chunk = ag_start_chunk + 1 < chunk_count ? ag_start_chunk + 1 : ag_start_chunk + 1 - chunk_count;
    
    //fi_addr_t ring_prev = rank > 0 ? vacc_fi_info->addr_vect[nic][(rank-1)*NIC_PER_HOST] : vacc_fi_info->addr_vect[nic][(world_size-1)*NIC_PER_HOST];
    //fi_addr_t ring_next = rank < world_size -1 ? vacc_fi_info->addr_vect[nic][(rank+1)*NIC_PER_HOST] : vacc_fi_info->addr_vect[nic][0];
    //fi_addr_t ring_prev[NIC_PER_HOST];
    //fi_addr_t ring_next[NIC_PER_HOST];
    int ring_prev_nic0 = rank > 0 ? (rank-1)*NIC_PER_HOST : (world_size-1)*NIC_PER_HOST;
    int ring_next_nic0 = rank < world_size -1 ? (rank+1)*NIC_PER_HOST : 0;
    //std::cout << rank << ", ring_prev: " << ring_prev << ",ring_next: " << ring_next << ",nic_count: " << vacc_fi_info->nic_count << "\n";


    uint64_t ignore = 0ULL;//65535ULL
    /*Reduce-scatter*/

    //Post recvs for first chunk
    //Recvs for 5 go 4,3,2,1,0,7,6,5
    {
        int c = rank-1  < 0 ? rank-1+world_size : rank-1;
        for(int p = 0;p<pipe_chunk_count;p++){
            uint64_t tag = c+(p<<16);//tag is data offset
            //std::cout << "Posting recvs with rank: " << rank  << " tag: " << tag << " " << " (" <<  c << "," << p << "," << n << ") " << "\n";
            //std::cout << "Rank: " << rank  << " tag: " << tag << " offset: " << (c*chunk_elems)+(p*part_chunk_elems_pipe)+(n*part_chunk_elems_nic) << "elems: " << part_chunk_elems_nic << "\n";

            err = fi_trecv(vacc_fi_info->ep[nic], (void*)(comm_buf+(c*chunk_elems)+(p*part_chunk_elems_pipe)), part_chunk_elems_pipe*sizeof(float), NULL, vacc_fi_info->addr_vect[nic][ring_prev_nic0+nic], tag, ignore, vacc_fi_info->rx_ctx[nic]);
            if (err != FI_SUCCESS) {
                std::cout << "fi_send 2 TODO error handling " << fi_strerror(err) << "\n";
                return 1;
            }
        }
    }
    //Send first chunk
    for(int p = 0;p<pipe_chunk_count;p++){
        //Pipeline device to host here
        //Pipeline IPC shmem sum here?
        int c = rank;
        uint64_t send_tag = c+(p<<16);//tag is the data offset
        //std::cout << "Posting sends with rank: " << rank  << " tag: " << send_tag << " " << " (" <<  c << "," << p << "," << n << ") " << "\n";
        err = fi_tsend(vacc_fi_info->ep[nic], (void*)(input_buf+(c*chunk_elems)+(p*part_chunk_elems_pipe)), part_chunk_elems_pipe*sizeof(float), NULL, vacc_fi_info->addr_vect[nic][ring_next_nic0+nic], send_tag, vacc_fi_info->tx_ctx[nic]);
        if (err != FI_SUCCESS) {
            std::cout << "fi_send 1 TODO error handling " << fi_strerror(err) << "\n";
            return 1;
        }
    }
    //ch: nth chunk, goes 1,2,3,4,5,6,7 with 8 ranks. 0th was already sent before
    //c: points to the right data, so rank 5 with 8 ranks goes 4,3,2,1,0,7,6 
    for(int ch = 1;ch<chunk_count;ch++){

        int c = rank-ch < 0 ? rank-ch+world_size : rank-ch;
        //Post recv for c+1
        if(ch+1<chunk_count){
            int c_recv = rank-ch-1 < 0 ? rank-ch-1+world_size : rank-ch-1;
            //Post receives for all parts of the chunk from the previous member of the ring
            for(int p = 0;p<pipe_chunk_count;p++){
                uint64_t tag = c_recv+(p<<16);//tag is data offset
                //std::cout << "Posting recvs with rank: " << rank  << " tag: " << tag << " " << " (" <<  c << "," << p << "," << n << ") " << "\n";
                //std::cout << "Rank: " << rank  << " tag: " << tag << " offset: " << (c*chunk_elems)+(p*part_chunk_elems_pipe)+(n*part_chunk_elems_nic) << "elems: " << part_chunk_elems_nic << "\n";

                err = fi_trecv(vacc_fi_info->ep[nic], (void*)(comm_buf+(c_recv*chunk_elems)+(p*part_chunk_elems_pipe)), part_chunk_elems_pipe*sizeof(float), NULL, vacc_fi_info->addr_vect[nic][ring_prev_nic0+nic], tag, ignore, vacc_fi_info->rx_ctx[nic]);
                if (err != FI_SUCCESS) {
                    std::cout << "fi_send 2 TODO error handling " << fi_strerror(err) << "\n";
                    return 1;
                }
            }
        }
        
        //Check that all sends for last chunk went through
        int tx_completions = 0;
        int tx_fails = 0;
        while (tx_completions < pipe_chunk_count){
            struct fi_cq_tagged_entry tx_entry;
            memset(&tx_entry, 0, sizeof(tx_entry));
            tx_entry.op_context = vacc_fi_info->tx_ctx[nic];
            tx_entry.flags = FI_TAGGED;

            struct fi_cq_err_entry cq_err;
            int ret = fi_cq_read(vacc_fi_info->tx_cq[nic], (void*)&tx_entry, 1);
            if (ret > 0) {
                int c_r = tx_entry.tag & 0xFFFF;
                int p_r = (tx_entry.tag & 0xFFFF0000)>>16;
                //std::cout << "delivered " << c_r << ","<< p_r << " on" << vacc_fi_info->rank << "\n";
                tx_completions++;
            } else if (ret == -FI_EAGAIN) {
                if(EXTRA_DEBUG)
                    std::cout << " send fi_cq_read FI_EAGAIN " << ret << "\n";
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                tx_fails += 1;
                //assert(tx_fails<100000);
            }else if (ret == -FI_EAVAIL) {
                std::cout << rank << " fi_cq_read " << ret << " " << fi_strerror(ret) << "\n";
                std::cout << "fi_cq_readerr " << " " << fi_cq_readerr(vacc_fi_info->tx_cq[nic], &cq_err, 0) << "\n";
                std::cout << cq_err.err << " " << fi_strerror(cq_err.err) << "\n";
                return 1;
                break;
            }else {
                std::cout << "fi_cq_read unknown return code" << ret << " " << fi_strerror(ret) << "\n";
                break;
            }
        }
        if(tx_fails == 1000 && tx_completions != pipe_chunk_count){
            std::cout << "Did not process all sends in time " << rank << " did: " << tx_completions << "\n";
        } else if(tx_completions != pipe_chunk_count ){
            std::cout << "Rank " << rank << " did: " << tx_completions << "\n";
        }

        int rx_completions = 0;
        int fails = 0;
        //Poll for receives and sends
        while (rx_completions < pipe_chunk_count){
            //Heap or stack here, no idea.
            //struct fi_cq_tagged_entry* entry = new fi_cq_tagged_entry();
            struct fi_cq_tagged_entry rx_entry;
            memset(&rx_entry, 0, sizeof(rx_entry));
            rx_entry.op_context = vacc_fi_info->rx_ctx[nic];
            rx_entry.flags = FI_TAGGED;

            struct fi_cq_err_entry cq_err;
            int ret = fi_cq_read(vacc_fi_info->rx_cq[nic], (void*)&rx_entry, 1);
            if (ret > 0) {
                /*unsigned long now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                        std::chrono::high_resolution_clock::now().time_since_epoch())
                                        .count();*/
                int c_r = rx_entry.tag & 0xFFFF;
                int p_r = (rx_entry.tag & 0xFFFF0000)>>16;

                if(c_r!=c){
                    std::cout << "ALERT got " << c_r << "," << p_r << " on" << vacc_fi_info->rank << "want" << c << "rank" << rank <<"ch"<<ch<< "\n";
                }
                //assert(c_r==c);
                //int n_r = (entry.tag & 0xFF0000)>>16;

                //Handle RS recv 
                handle_recv(input_buf, comm_buf, vacc_fi_info, c_r, p_r, nic, rs_end_chunk, ring_next_nic0, chunk_count, chunk_elems, part_chunk_elems_pipe, pipe_chunk_count);

                rx_completions++;
            } else if (ret == -FI_EAGAIN) {
                if(EXTRA_DEBUG)
                    std::cout << " recv fi_cq_read FI_EAGAIN " << ret << "\n";
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                fails += 1;
                assert(fails<100000);
            }else if (ret == -FI_EAVAIL) {
                std::cout << rank << " fi_cq_read " << ret << " " << fi_strerror(ret) << "\n";
                std::cout << "fi_cq_readerr " << " " << fi_cq_readerr(vacc_fi_info->rx_cq[nic], &cq_err, 0) << "\n";
                std::cout << cq_err.err << " " << fi_strerror(cq_err.err) << "\n";
                return 1;
                break;
            }else {
                std::cout << "fi_cq_read unknown return code" << ret << " " << fi_strerror(ret) << "\n";
                break;
            }
        }


        //std::cout << "got n recvs" << "\n";
        
        //std::cout << "did n sends" << "\n";
        /*if(c != rs_end_chunk){
            for(int p = 0;p<pipe_chunk_count;p++){
                for(int nic = 0;nic<nic_chunk_count;nic++){
                    uint64_t send_tag = c+(p<<16);//tag is the data offset
                    //std::cout << "Posting sends with rank: " << rank  << " tag: " << send_tag << " " << " (" <<  c << "," << p << "," << n << ") " << "\n";
                    err = fi_tsend(vacc_fi_info->ep[nic], (void*)(input_buf+(c*chunk_elems)+(p*part_chunk_elems_pipe)+(nic*part_chunk_elems_nic)), part_chunk_elems_nic*sizeof(float), NULL, vacc_fi_info->addr_vect[nic][ring_next_nic0+nic], send_tag, vacc_fi_info->tx_ctx[nic]);
                    if (err != FI_SUCCESS) {
                        std::cout << "fi_send 1 TODO error handling " << fi_strerror(err) << "\n";
                        return 1;
                    }
                }
            }
        }*/
        //return 0;
    }
        //std::cout << rank << " fails: " << fails << ", completions: " << completions << "\n";
        //return 0;

        


    //std::cout << "Fails:  " << fails << "," << fails2 << " ";
    //std::cout << "Done " << rank << "\n";
    return 0;
}

int vacc::ring_ar_ag(float *input_buf, int elem_count, int nic, vacc::vacc_fi_info_t* vacc_fi_info){
    int world_size = vacc_fi_info->world_size;
    int rank = vacc_fi_info->rank;

    int err = 0;
    //size_t elem_count = 2*512'000'000;//dividable by 2^16, ~65k
    //size_t elem_count = 1'073'741'824;//power of two, 2^28
    //size_t elem_count = (16384)*8192;
    //8192*4096/8 is minimum.
    //size_t elem_count = (8192)*4096/8;//4MB to get good link performance
    size_t chunk_count = world_size;
    size_t chunk_elems = elem_count/chunk_count;

    //size_t pipe_chunk_count = 4;//4 pipeline stages to get "75%" of comms overlaped with reduce sum
    //size_t part_chunk_elems_pipe = chunk_elems/pipe_chunk_count;

    int ag_start_chunk = rank + 1 < chunk_count ? rank + 1 : rank + 1 - chunk_count;
    int rs_end_chunk = ag_start_chunk;
    int last_ag_recv_chunk = ag_start_chunk + 1 < chunk_count ? ag_start_chunk + 1 : ag_start_chunk + 1 - chunk_count;
    
    //fi_addr_t ring_prev = rank > 0 ? vacc_fi_info->addr_vect[nic][(rank-1)*NIC_PER_HOST] : vacc_fi_info->addr_vect[nic][(world_size-1)*NIC_PER_HOST];
    //fi_addr_t ring_next = rank < world_size -1 ? vacc_fi_info->addr_vect[nic][(rank+1)*NIC_PER_HOST] : vacc_fi_info->addr_vect[nic][0];
    int ring_prev_nic0 = rank > 0 ? (rank-1)*NIC_PER_HOST : (world_size-1)*NIC_PER_HOST;
    int ring_next_nic0 = rank < world_size -1 ? (rank+1)*NIC_PER_HOST : 0;
    //fi_addr_t ring_prev[NIC_PER_HOST];
    //fi_addr_t ring_next[NIC_PER_HOST];
    //std::cout << rank << ", ring_prev: " << ring_prev << ",ring_next: " << ring_next << ",nic_count: " << vacc_fi_info->nic_count << "\n";


    uint64_t ignore = 0ULL;//65535ULL
    /*Reduce-scatter*/


    //Post all receives
    for(int c = 0;c<chunk_count;c++){
        if(c != ag_start_chunk) {
            uint64_t tag = c;//tag is data offset
            //std::cout << "Posting recvs with rank: " << rank  << " tag: " << tag << " " << " (" <<  c << "," << p << "," << n << ") " << "\n";
            //std::cout << "Rank: " << rank  << " tag: " << tag << " offset: " << (c*chunk_elems)+(p*part_chunk_elems_pipe)+(n*part_chunk_elems_nic) << "elems: " << part_chunk_elems_nic << "\n";
            err = fi_trecv(vacc_fi_info->ep[nic], (void*)(input_buf+(c*chunk_elems)), chunk_elems*sizeof(float), NULL, vacc_fi_info->addr_vect[nic][ring_prev_nic0+nic], tag, ignore, vacc_fi_info->rx_ctx[nic]);
            
            if (err != FI_SUCCESS) {
                std::cout << "fi_send 4 TODO error handling " << fi_strerror(err) << "\n";
                return 1;
            }
        }
    }
    //Send the starting chunk
    int c = ag_start_chunk;
    uint64_t send_tag = c;//tag is the data offset
    //std::cout << "Posting sends with rank: " << rank  << " tag: " << send_tag << " " << " (" <<  c << "," << p << "," << n << ") " << "\n";
    err = fi_tsend(vacc_fi_info->ep[nic], (void*)(input_buf+(ag_start_chunk*chunk_elems)), chunk_elems*sizeof(float), NULL, vacc_fi_info->addr_vect[nic][ring_next_nic0+nic], send_tag, vacc_fi_info->tx_ctx[nic]);
    if (err != FI_SUCCESS) {
        std::cout << "fi_send 5 TODO error handling " << fi_strerror(err) << "\n";
        return 1;
    }

    int completions = 0;
    int fails = 0;
    while(completions < (chunk_count-1)){
        struct fi_cq_err_entry cq_err;
        
        struct fi_cq_tagged_entry tx_entry;
        memset(&tx_entry, 0, sizeof(tx_entry));
        tx_entry.op_context = vacc_fi_info->tx_ctx[nic];
        tx_entry.flags = FI_TAGGED;
        int tx_ret = fi_cq_read(vacc_fi_info->tx_cq[nic], (void*)&tx_entry, 1);
        if (tx_ret == -FI_EAGAIN) {
            if(EXTRA_DEBUG)
                std::cout << " recv fi_cq_read FI_EAGAIN " << tx_ret << "\n";
            fails += 1;
            if(fails==10000000){
                std::cout << " timed out at rank " << rank << " at " << completions << " completions out of " << chunk_count << "\n";
                return 1;
            }
        }else if (tx_ret == -FI_EAVAIL) {
            std::cout << rank << " fi_cq_read " << tx_ret << " " << fi_strerror(tx_ret) << "\n";
            std::cout << "fi_cq_readerr " << " " << fi_cq_readerr(vacc_fi_info->tx_cq[nic], &cq_err, 0) << "\n";
            std::cout << cq_err.err << " " << fi_strerror(cq_err.err) << "\n";
            return 1;
            break;
        }else if (tx_ret < 0){
            std::cout << "fi_cq_read unknown return code" << tx_ret << " " << fi_strerror(tx_ret) << "\n";
            break;
        }

        struct fi_cq_tagged_entry rx_entry;
        memset(&rx_entry, 0, sizeof(rx_entry));
        rx_entry.op_context = vacc_fi_info->rx_ctx[nic];
        rx_entry.flags = FI_TAGGED;
        int ret = fi_cq_read(vacc_fi_info->rx_cq[nic], (void*)&rx_entry, 1);
        if (ret > 0) {
            //TODO pipeline host to device somewhere here
            int c_r = rx_entry.tag & 0xFFFF;
            //Send forward in ring as well unless we are last receiver
            if(c_r != last_ag_recv_chunk){
                uint64_t send_tag = c_r;//tag is the data offset
                //std::cout << "Posting sends with rank: " << rank  << " tag: " << send_tag << " " << " (" <<  c << "," << p << "," << n << ") " << "\n";
                err = fi_tsend(vacc_fi_info->ep[nic], (void*)(input_buf+(c_r*chunk_elems)), chunk_elems*sizeof(float), NULL, vacc_fi_info->addr_vect[nic][ring_next_nic0+nic], send_tag, vacc_fi_info->tx_ctx[nic]);
                if (err != FI_SUCCESS) {
                    std::cout << "fi_send 6 TODO error handling " << fi_strerror(err) << "\n";
                    return 1;
                }
            }
            completions++;
        } else if (ret == -FI_EAGAIN) {
            if(EXTRA_DEBUG)
                std::cout << " recv fi_cq_read FI_EAGAIN " << ret << "\n";
            //fails += 1;
        }else if (ret == -FI_EAVAIL) {
            std::cout << rank << " fi_cq_read " << ret << " " << fi_strerror(ret) << "\n";
            std::cout << "fi_cq_readerr " << " " << fi_cq_readerr(vacc_fi_info->rx_cq[nic], &cq_err, 0) << "\n";
            std::cout << cq_err.err << " " << fi_strerror(cq_err.err) << "\n";
            return 1;
            break;
        }else {
            std::cout << "fi_cq_read unknown return code" << ret << " " << fi_strerror(ret) << "\n";
            break;
        }
    }

        


    //std::cout << "Fails:  " << fails << "," << fails2 << " ";
    //std::cout << "Done " << rank << "\n";
    return 0;
}

int vacc::ring_allreduce(float *input_buf, float *comm_buf, int elem_count, int nic, vacc::vacc_fi_info_t* vacc_fi_info){
    //std::cout << "starting allreduce: " << world_size << ", " << rank << "\n";


    //MPI_Barrier(MPI_COMM_WORLD);
    //Allgather ring
    

    //MPI_Barrier(MPI_COMM_WORLD);
    
    vacc::ring_ar_rs(input_buf, comm_buf, elem_count, nic, vacc_fi_info);
    vacc::ring_ar_ag(input_buf, elem_count, nic, vacc_fi_info);

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