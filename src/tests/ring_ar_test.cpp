//#include <sycl/sycl.hpp>

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

#define DEBUG false
#define EXTRA_DEBUG false

int main(int argc, char* argv[]) {

    int world_size = 0;
    int rank = 0;
    
    MPI_Init(NULL, NULL);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if(DEBUG){
        std::cout << "world_size: " << world_size << "\n";
        std::cout << "rank: " << rank << "\n";
    } else if(rank==0){
        std::cout << "world_size: " << world_size << "\n";
    }

    struct fi_info *hints, *info;
    int err;
    hints = fi_allocinfo();

    hints->mode = FI_CONTEXT;
    hints->addr_format = FI_ADDR_CXI;
    hints->ep_attr->type = FI_EP_RDM;
    //hints->domain_attr->resource_mgmt = FI_RM_ENABLED;
    hints->domain_attr->resource_mgmt = FI_RM_DISABLED;
    hints->domain_attr->threading = FI_THREAD_SAFE;
    hints->domain_attr->control_progress = FI_PROGRESS_MANUAL;
    hints->domain_attr->data_progress = FI_PROGRESS_MANUAL;
    //hints->tx_attr->size = ;
    hints->caps = FI_MSG | FI_TAGGED | FI_RECV | FI_SEND;

    {
        std::string domain_name = "cxi0";
        //domain_name += std::to_string(rank%8);
        hints->domain_attr->name = strdup(domain_name.c_str());
        //hints->fabric_attr->name = strdup("cxi");
        hints->domain_attr->av_type = FI_AV_TABLE;
    }


    err = fi_getinfo(FI_VERSION(1,20), NULL, NULL, 0ULL, hints, &info);
    if (err != FI_SUCCESS) {
        std::cout << "fi_getinfo TODO error handling lol" << err << fi_strerror(err) << "\n";
    }

    if(DEBUG){
        std::cout << "Fabric name: " << info->fabric_attr->name << "\n";
        std::cout << "Domain name: " << info->domain_attr->name << "\n";
    }
    int info_ind = 1;
    struct fi_info* curr_info = info;
    while(curr_info->next != NULL){
        curr_info = curr_info->next;
        if(DEBUG){
            std::cout << "Domain name on rank " << rank << ", info, " << info_ind  << ": " << curr_info->domain_attr->name << "\n";
        }
        info_ind += 1;
    } 
    fi_freeinfo(hints);
    struct fid_fabric *fabric;
    err = fi_fabric(info->fabric_attr, &fabric, NULL);
    if (err != FI_SUCCESS) {
        std::cout << "fi_fabric TODO error handling lol " << err << fi_strerror(err) << "\n";
    }


    struct fid_domain *domain;
    err = fi_domain(fabric, info, &domain, NULL);
    if (err != FI_SUCCESS) {
        std::cout << "fi_domain TODO error handling lol " << err << fi_strerror(err) << "\n";
    }

    /*
    FI endpoint (EP)
    */
    struct fid_ep *ep;
    err = fi_endpoint(domain, info, &ep, NULL);
    if (err != FI_SUCCESS) {
        std::cout << "fi_endpoint TODO error handling lol " << err << fi_strerror(err) << "\n";
    }


    /*
    FI address vector (AV)
    */
    struct fi_av_attr av_attr;
    struct fid_av *av;
    memset(&av_attr,0,sizeof(av_attr));
    av_attr.type = FI_AV_TABLE;
    //av_attr.flags = FI_SYMMETRIC;
    
    err = fi_av_open(domain, &av_attr, &av, NULL);
    if (err != FI_SUCCESS) {
        std::cout << "fi_av_open TODO error handling lol" << err << fi_strerror(err) << "\n";
    }
    
    err = fi_ep_bind(ep, &(av->fid), 0);
    if (err != FI_SUCCESS) {
        std::cout << "fi_ep_bind av TODO error handling lol" << err << fi_strerror(err) << "\n";
    }

    void* tx_ctx = calloc(1, sizeof(fi_context));
    void* rx_ctx = calloc(1, sizeof(fi_context));
    /*
    FI completion queue (CQ)
    */
    struct fid_cq *tx_cq;
    struct fid_cq *rx_cq;

    struct fi_cq_attr cq_attr_tx;
    memset(&cq_attr_tx,0,sizeof(fi_cq_attr));
    cq_attr_tx.format = FI_CQ_FORMAT_TAGGED;
    cq_attr_tx.size = 16384;

    struct fi_cq_attr cq_attr_rx;
    memset(&cq_attr_rx,0,sizeof(fi_cq_attr));
    cq_attr_rx.format = FI_CQ_FORMAT_TAGGED;
    cq_attr_rx.size = 16384;
    
    err = fi_cq_open(domain, &cq_attr_tx, &tx_cq, NULL);
    if (err != FI_SUCCESS) {
        std::cout << "fi_cq_open1 TODO error handling lol" << err << fi_strerror(err) << "\n";
    }
    err = fi_cq_open(domain, &cq_attr_rx, &rx_cq, NULL);
    if (err != FI_SUCCESS) {
        std::cout << "fi_cq_open2 TODO error handling lol" << err << fi_strerror(err) << "\n";
    }

    // bind TX CQ to EP
    err = fi_ep_bind(ep, &(tx_cq->fid), FI_TRANSMIT);
    if (err != FI_SUCCESS) {
        std::cout << "fi_ep_bind1 TODO error handling lol" << err << fi_strerror(err) << "\n";
    }
    // bind RX CQ to EP
    err = fi_ep_bind(ep, &(rx_cq->fid), FI_RECV);
    if (err != FI_SUCCESS) {
        std::cout << "fi_ep_bind2 TODO error handling lol" << err << fi_strerror(err) << "\n";
    }

    // enable EP
    err = fi_enable(ep);
        if (err != FI_SUCCESS) {
        std::cout << "TODO error handling lol\n";
    }
    
    fi_addr_t *me;
    size_t addrlen = 0;
    fi_getname((fid_t)ep, NULL, &addrlen);
    me = (fi_addr_t *)malloc(addrlen);
    err = fi_getname((fid_t)ep, (void*)me, &addrlen);

    if (DEBUG) {
        std::cout << "addrlen:" << addrlen << " " << sizeof(uint64_t) << "\n";
        std::cout << "addr:" << *me  << "\n";
    }

    assert(addrlen == sizeof(uint64_t));
    void *loaded_addr = calloc(world_size,addrlen);

    MPI_Allgather(me, 1, MPI_UINT64_T, loaded_addr, 1, MPI_UINT64_T, MPI_COMM_WORLD);

    if(DEBUG) {
        std::cout << "loaded_addr: ";
        for(int i=0;i<world_size;i++){ 
            std::cout << *((uint64_t*)loaded_addr+i) << ",";
        }
        std::cout << "\n";
    }
    



    fi_addr_t *addr_vect;
    addr_vect = (fi_addr_t*)calloc(world_size,addrlen);
    int num_success = fi_av_insert(av, loaded_addr, world_size, addr_vect, 0, NULL);
    if(DEBUG)
        std::cout << "num_success fi_av_insert: " << num_success << "\n";


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

    fi_addr_t ring_prev = rank > 0 ? addr_vect[rank-1] : addr_vect[world_size-1];
    fi_addr_t ring_next = rank < world_size -1 ? addr_vect[rank+1] : addr_vect[0];

    for(int l = 0;l<5;l++){
        
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
                err = fi_tsend(ep, (void*)(input_buf+(c*chunk_elems)+(p*part_chunk_elems_pipe)+(n*part_chunk_elems_nic)), part_chunk_elems_nic*sizeof(float), NULL, ring_next, send_tag, tx_ctx);
                if (err != FI_SUCCESS) {
                    std::cout << "fi_send 1 TODO error handling " << fi_strerror(err) << "\n";
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

                    err = fi_trecv(ep, (void*)(comm_buf+(c*chunk_elems)+(p*part_chunk_elems_pipe)+(n*part_chunk_elems_nic)), part_chunk_elems_nic*sizeof(float), NULL, ring_prev, tag, ignore, rx_ctx);
                    if (err != FI_SUCCESS) {
                        std::cout << "fi_send 2 TODO error handling " << fi_strerror(err) << "\n";
                    }
                }
            }

            int completions = 0;

            //Poll for receives
            while (completions < pipe_chunk_count*nic_chunk_count){
                struct fi_cq_tagged_entry entry;
                memset(&entry, 0, sizeof(entry));
                entry.op_context = rx_ctx;
                entry.flags = FI_TAGGED;

                struct fi_cq_err_entry cq_err;
                int ret = fi_cq_read(rx_cq, (void*)&entry, 1);
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
                            err = fi_tsend(ep, (void*)(input_buf+(c_r*chunk_elems)+(p_r*part_chunk_elems_pipe)+(n_r*part_chunk_elems_nic)), part_chunk_elems_nic*sizeof(float), NULL, ring_next, send_tag, tx_ctx);
                            if (err != FI_SUCCESS) {
                                std::cout << "fi_send 3 TODO error handling " << fi_strerror(err) << "\n";
                            } else {
                                //Pipeline host to device
                            }
                        }
                    }

                    completions++;
                } else if (ret == -FI_EAGAIN) {
                    if(EXTRA_DEBUG)
                        std::cout << " recv fi_cq_read FI_EAGAIN " << ret << "\n";
                    //fails += 1;
                }else if (ret == -FI_EAVAIL) {
                    std::cout << rank << " fi_cq_read " << ret << " " << fi_strerror(ret) << "\n";
                    std::cout << "fi_cq_readerr " << " " << fi_cq_readerr(rx_cq, &cq_err, 0) << "\n";
                    std::cout << cq_err.err << " " << fi_strerror(cq_err.err) << "\n";
                    break;
                }else {
                    std::cout << "fi_cq_read unknown return code" << ret << " " << fi_strerror(ret) << "\n";
                    break;
                }
                //std::this_thread::sleep_for(std::chrono::microseconds(100));
            }

            

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
                        err = fi_trecv(ep, (void*)(input_buf+(c*chunk_elems)+(p*part_chunk_elems_pipe)+(n*part_chunk_elems_nic)), part_chunk_elems_nic*sizeof(float), NULL, ring_prev, tag, ignore, rx_ctx);
                        
                        if (err != FI_SUCCESS) {
                            std::cout << "fi_send 4 TODO error handling " << fi_strerror(err) << "\n";
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
                err = fi_tsend(ep, (void*)(input_buf+(ag_start_chunk*chunk_elems)+(p*part_chunk_elems_pipe)+(n*part_chunk_elems_nic)), part_chunk_elems_nic*sizeof(float), NULL, ring_next, send_tag, tx_ctx);
                if (err != FI_SUCCESS) {
                    std::cout << "fi_send 5 TODO error handling " << fi_strerror(err) << "\n";
                }
            }
        }

        int completions = 0;
        while(completions < (chunk_count-1)*pipe_chunk_count*nic_chunk_count){
            struct fi_cq_tagged_entry entry;
            memset(&entry, 0, sizeof(entry));
            entry.op_context = rx_ctx;
            entry.flags = FI_TAGGED;

            struct fi_cq_err_entry cq_err;
            int ret = fi_cq_read(rx_cq, (void*)&entry, 1);
            if (ret > 0) {
                //TODO pipeline host to device somewhere here
                int c_r = entry.tag & 0xFF;
                int p_r = (entry.tag & 0xFF00)>>8;
                int n_r = (entry.tag & 0xFF0000)>>16;
                //Send forward in ring as well unless we are last receiver
                if(c_r != last_ag_recv_chunk){
                    uint64_t send_tag = c_r+(p_r<<8)+(n_r<<16);//tag is the data offset
                    //std::cout << "Posting sends with rank: " << rank  << " tag: " << send_tag << " " << " (" <<  c << "," << p << "," << n << ") " << "\n";
                    err = fi_tsend(ep, (void*)(input_buf+(c_r*chunk_elems)+(p_r*part_chunk_elems_pipe)+(n_r*part_chunk_elems_nic)), part_chunk_elems_nic*sizeof(float), NULL, ring_next, send_tag, tx_ctx);
                    if (err != FI_SUCCESS) {
                        std::cout << "fi_send 6 TODO error handling " << fi_strerror(err) << "\n";
                    }
                }
                completions++;
            } else if (ret == -FI_EAGAIN) {
                if(EXTRA_DEBUG)
                    std::cout << " recv fi_cq_read FI_EAGAIN " << ret << "\n";
                //fails += 1;
            }else if (ret == -FI_EAVAIL) {
                std::cout << rank << " fi_cq_read " << ret << " " << fi_strerror(ret) << "\n";
                std::cout << "fi_cq_readerr " << " " << fi_cq_readerr(rx_cq, &cq_err, 0) << "\n";
                std::cout << cq_err.err << " " << fi_strerror(cq_err.err) << "\n";
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
    std::cout << min_time/1000000 << "ms " << min_time << " ns" << " on rank " << rank << "\n";
    std::cout << "\"GB/s\" " << (float)elem_count*4*(2*((float)(world_size-1))/(float)world_size)/(float)min_time << " on rank " << rank << "\n";

    std::cout << "Rank " << rank << " Got: " << *(input_buf+0) << ',' <<  *(input_buf+1) << "\n";
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
    std::cout << "\n" << "Correct:  " << correct << " Incorrect: " << incorrect << "\n"<< "\n";
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
    std::cout << "Done " << rank << "\n";
    return 0;
}