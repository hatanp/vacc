#include "allreduce.h"

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

int vacc::ring_allreduce(int world_size, int rank, vacc::vacc_fi_info_t* vacc_fi_info){
    //std::cout << "starting allreduce: " << world_size << ", " << rank << "\n";
    int err = 0;
    int nic = 0;
    size_t elem_count = 2*512'000'000;
    //size_t elem_count = (16384)*8192;
    //8192*4096/8 is minimum.
    //size_t elem_count = (8192)*4096/8;//4MB to get good link performance
    size_t chunk_count = world_size;
    size_t chunk_elems = elem_count/chunk_count;
    size_t nic_chunk_count = 1;//4 NICs
    size_t pipe_chunk_count = 4;//4 pipeline stages to get "75%" of comms overlaped with reduce sum
    size_t sum_loop_threads = 8;//How many thread to parallelize the reduce sum over
    size_t part_chunk_elems_pipe = chunk_elems/pipe_chunk_count;
    size_t part_chunk_elems_nic = part_chunk_elems_pipe/nic_chunk_count;
    size_t sum_loop_elems = part_chunk_elems_nic/sum_loop_threads;
    float *input_buf = new float[elem_count];
    float *comm_buf = new float[elem_count];

    int ag_start_chunk = rank + 1 < chunk_count ? rank + 1 : rank + 1 - chunk_count;
    int last_ag_recv_chunk = ag_start_chunk + 1 < chunk_count ? ag_start_chunk + 1 : ag_start_chunk + 1 - chunk_count;
    
    unsigned long min_time = std::numeric_limits<unsigned long>::max();

    fi_addr_t ring_prev = rank > 0 ? vacc_fi_info->addr_vect[nic][(rank-1)*NIC_COUNT] : vacc_fi_info->addr_vect[nic][(world_size-1)*NIC_COUNT];
    fi_addr_t ring_next = rank < world_size -1 ? vacc_fi_info->addr_vect[nic][(rank+1)*NIC_COUNT] : vacc_fi_info->addr_vect[nic][0];
    //std::cout << rank << ", ring_prev: " << ring_prev << ",ring_next: " << ring_next << ",nic_count: " << vacc_fi_info->nic_count << "\n";

    for(int l = 0;l<3;l++){
        
        for(int i = 0; i<elem_count; i++){
            input_buf[i] = rank;
            comm_buf[i] = 0;
        }

        MPI_Barrier(MPI_COMM_WORLD); 
        
        unsigned long start = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                        std::chrono::high_resolution_clock::now().time_since_epoch())
                                        .count();

        uint64_t ignore = 0ULL;//65535ULL
        /*Reduce-scatter*/

            
        //Send first ones
        for(int p = 0;p<pipe_chunk_count;p++){
            //Pipeline device to host here
            for(int n = 0;n<nic_chunk_count;n++){
                int c = rank;
                uint64_t send_tag = c+(p<<8)+(n<<16);//tag is the data offset
                //std::cout << "Posting sends with rank: " << rank  << " tag: " << send_tag << " " << " (" <<  c << "," << p << "," << n << ") " << "\n";
                err = fi_tsend(vacc_fi_info->ep[nic], (void*)(input_buf+(c*chunk_elems)+(p*part_chunk_elems_pipe)+(n*part_chunk_elems_nic)), part_chunk_elems_nic*sizeof(float), NULL, ring_next, send_tag, vacc_fi_info->tx_ctx[nic]);
                if (err != FI_SUCCESS) {
                    std::cout << "fi_send 1 TODO error handling " << fi_strerror(err) << "\n";
                    return 1;
                }
            }
        }

        for(int ch = 1;ch<chunk_count;ch++){
            int c = rank-ch  < 0 ? rank-ch+world_size : rank-ch;
            
            //Receives for all parts of the chunk from last member
            for(int p = 0;p<pipe_chunk_count;p++){
                for(int n = 0;n<nic_chunk_count;n++){
                    uint64_t tag = c+(p<<8)+(n<<16);//tag is data offset
                    //std::cout << "Posting recvs with rank: " << rank  << " tag: " << tag << " " << " (" <<  c << "," << p << "," << n << ") " << "\n";
                    //std::cout << "Rank: " << rank  << " tag: " << tag << " offset: " << (c*chunk_elems)+(p*part_chunk_elems_pipe)+(n*part_chunk_elems_nic) << "elems: " << part_chunk_elems_nic << "\n";

                    err = fi_trecv(vacc_fi_info->ep[nic], (void*)(comm_buf+(c*chunk_elems)+(p*part_chunk_elems_pipe)+(n*part_chunk_elems_nic)), part_chunk_elems_nic*sizeof(float), NULL, ring_prev, tag, ignore, vacc_fi_info->rx_ctx[nic]);
                    if (err != FI_SUCCESS) {
                        std::cout << "fi_send 2 TODO error handling " << fi_strerror(err) << "\n";
                        return 1;
                    }
                }
            }

            int completions = 0;
            int fails = 0;
            //Poll for receives
            while (fails < 10000 && completions < pipe_chunk_count*nic_chunk_count){
                struct fi_cq_tagged_entry entry;
                memset(&entry, 0, sizeof(entry));
                entry.op_context = vacc_fi_info->rx_ctx[nic];
                entry.flags = FI_TAGGED;

                struct fi_cq_err_entry cq_err;
                int ret = fi_cq_read(vacc_fi_info->rx_cq[nic], (void*)&entry, 1);
                if (ret > 0) {
                    /*unsigned long now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                            std::chrono::high_resolution_clock::now().time_since_epoch())
                                            .count();*/
                    //uint64_t tag = c+(p<<8)+(n<<16);
                    int c_r = entry.tag & 0xFF;
                    int p_r = (entry.tag & 0xFF00)>>8;
                    int n_r = (entry.tag & 0xFF0000)>>16;
                    //Handle RS recv 
                    {
                        assert(c_r==c);
                        //std::cout << "Rank " << rank << " Got: " << *(comm_buf+(c_r*chunk_elems)+(p_r*part_chunk_elems_pipe)+(n_r*part_chunk_elems_nic)) << " for " << entry.tag << " (" <<  c_r << "," << p_r << "," << n_r << ") "  << " of size " << chunk_count << "," << nic_chunk_count << "," << pipe_chunk_count << " (c,p,n)" << "\n";
                        //last = now;
                        //Probably can be optimized further with vector instructions and such. Good enough for now, will be overlapped with comms anyway..
                        unsigned long t0 = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                        std::chrono::high_resolution_clock::now().time_since_epoch())
                                                        .count();
                        //#pragma omp parallel for schedule(static,4096)
                        //#pragma omp parallel for schedule(static,8)
                        #pragma omp parallel for
                        for(int k=0;k<sum_loop_threads;k++){
                            for(int j=0;j<part_chunk_elems_nic/sum_loop_threads;j++){
                                *(input_buf+(c_r*chunk_elems)+(p_r*part_chunk_elems_pipe)+(n_r*part_chunk_elems_nic)+(k*sum_loop_elems)+j) += *(comm_buf+(c_r*chunk_elems)+(p_r*part_chunk_elems_pipe)+(n_r*part_chunk_elems_nic)+(k*sum_loop_elems)+j);
                            }
                        }
                        //std::this_thread::sleep_for(std::chrono::nanoseconds(1000));
                        unsigned long t1 = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                        std::chrono::high_resolution_clock::now().time_since_epoch())
                                                        .count();
                        //std::cout << (t1-t0)/1000000 << "ms " << t1-t0 << " ns" << "\n";
                        //Send a part of the chunk to the next one, unless we expect to sum to full result. 
                        //std::cout << "rank: " << rank << " ch: " << ch << " c_r: " << c_r << " c: " << c << " last_recv " << last_ag_recv_chunk << " ag_start_chunk: " << ag_start_chunk << " " << (int)(ch < chunk_count-1) << "," <<  (int)(c_r != ag_start_chunk) << "\n";
                        if(c_r != ag_start_chunk){
                            uint64_t send_tag = c_r+(p_r<<8)+(n_r<<16);//tag is the data offset
                            //std::cout << "Posting after sends with rank: " << rank  << " tag: " << send_tag << " " << " (" <<  c_r << "," << p_r << "," << n_r << ") " << "\n";
                            err = fi_tsend(vacc_fi_info->ep[nic], (void*)(input_buf+(c_r*chunk_elems)+(p_r*part_chunk_elems_pipe)+(n_r*part_chunk_elems_nic)), part_chunk_elems_nic*sizeof(float), NULL, ring_next, send_tag, vacc_fi_info->tx_ctx[nic]);
                            if (err != FI_SUCCESS) {
                                std::cout << "fi_send 3 TODO error handling " << fi_strerror(err) << "\n";
                                return 1;
                            } else {
                                //Pipeline host to device
                            }
                        }
                    }

                    completions++;
                } else if (ret == -FI_EAGAIN) {
                    if(EXTRA_DEBUG)
                        std::cout << " recv fi_cq_read FI_EAGAIN " << ret << "\n";
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                    fails += 1;
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
                //std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
            //std::cout << rank << " fails: " << fails << ", completions: " << completions << "\n";
            //return 0;

            

        }

        //MPI_Barrier(MPI_COMM_WORLD);
        //Allgather ring
        
        //Post all receives
        for(int c = 0;c<chunk_count;c++){
            if(c != ag_start_chunk) {
                for(int p = 0;p<pipe_chunk_count;p++){
                    for(int n = 0;n<nic_chunk_count;n++){
                        uint64_t tag = c+(p<<8)+(n<<16);//tag is data offset
                        //std::cout << "Posting recvs with rank: " << rank  << " tag: " << tag << " " << " (" <<  c << "," << p << "," << n << ") " << "\n";
                        //std::cout << "Rank: " << rank  << " tag: " << tag << " offset: " << (c*chunk_elems)+(p*part_chunk_elems_pipe)+(n*part_chunk_elems_nic) << "elems: " << part_chunk_elems_nic << "\n";
                        err = fi_trecv(vacc_fi_info->ep[nic], (void*)(input_buf+(c*chunk_elems)+(p*part_chunk_elems_pipe)+(n*part_chunk_elems_nic)), part_chunk_elems_nic*sizeof(float), NULL, ring_prev, tag, ignore, vacc_fi_info->rx_ctx[nic]);
                        
                        if (err != FI_SUCCESS) {
                            std::cout << "fi_send 4 TODO error handling " << fi_strerror(err) << "\n";
                            return 1;
                        }
                    }
                }
            }
        }
        //Send the starting chunk
        for(int p = 0;p<pipe_chunk_count;p++){
            for(int n = 0;n<nic_chunk_count;n++){
                int c = ag_start_chunk;
                uint64_t send_tag = c+(p<<8)+(n<<16);//tag is the data offset
                //std::cout << "Posting sends with rank: " << rank  << " tag: " << send_tag << " " << " (" <<  c << "," << p << "," << n << ") " << "\n";
                err = fi_tsend(vacc_fi_info->ep[nic], (void*)(input_buf+(ag_start_chunk*chunk_elems)+(p*part_chunk_elems_pipe)+(n*part_chunk_elems_nic)), part_chunk_elems_nic*sizeof(float), NULL, ring_next, send_tag, vacc_fi_info->tx_ctx[nic]);
                if (err != FI_SUCCESS) {
                    std::cout << "fi_send 5 TODO error handling " << fi_strerror(err) << "\n";
                    return 1;
                }
            }
        }

        int completions = 0;
        while(completions < (chunk_count-1)*pipe_chunk_count*nic_chunk_count){
            struct fi_cq_tagged_entry entry;
            memset(&entry, 0, sizeof(entry));
            entry.op_context = vacc_fi_info->rx_ctx[nic];
            entry.flags = FI_TAGGED;

            struct fi_cq_err_entry cq_err;
            int ret = fi_cq_read(vacc_fi_info->rx_cq[nic], (void*)&entry, 1);
            if (ret > 0) {
                //TODO pipeline host to device somewhere here
                int c_r = entry.tag & 0xFF;
                int p_r = (entry.tag & 0xFF00)>>8;
                int n_r = (entry.tag & 0xFF0000)>>16;
                //Send forward in ring as well unless we are last receiver
                if(c_r != last_ag_recv_chunk){
                    uint64_t send_tag = c_r+(p_r<<8)+(n_r<<16);//tag is the data offset
                    //std::cout << "Posting sends with rank: " << rank  << " tag: " << send_tag << " " << " (" <<  c << "," << p << "," << n << ") " << "\n";
                    err = fi_tsend(vacc_fi_info->ep[nic], (void*)(input_buf+(c_r*chunk_elems)+(p_r*part_chunk_elems_pipe)+(n_r*part_chunk_elems_nic)), part_chunk_elems_nic*sizeof(float), NULL, ring_next, send_tag, vacc_fi_info->tx_ctx[nic]);
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

        //MPI_Barrier(MPI_COMM_WORLD);
        
        

            

        unsigned long end = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                        std::chrono::high_resolution_clock::now().time_since_epoch())
                                        .count();
        
        std::cout << (end-start)/1000000 << "ms " << end-start << " ns" << "\n";
        min_time = std::min(end - start, min_time);
        MPI_Barrier(MPI_COMM_WORLD); 
    }
    //std::cout << min_time/1000000 << "ms " << min_time << " ns" << " on rank " << rank << "\n";

    //bits_per_item*elems_per_bucket*num_sends/time
    //num_sends=2*(N-1) in ring, 2 for reduce-scatter and allgather rings, (N-1) sends per ring. 
    std::cout << "\"GB/s\" " << 4 * ((float)elem_count/(float)world_size) * 2*(world_size-1) / (float)min_time << " on rank " << rank << "\n";

    //std::cout << "Rank " << rank << " Got: " << *(input_buf+0) << ',' <<  *(input_buf+1) << "\n";
    int correct = 0;
    int incorrect = 0;
    int correct_result = 0;
    for(int i=0; i<world_size; i++){
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