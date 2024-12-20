#include "init_cxi.h"

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

vacc::vacc_fi_info_t* vacc::init_fi_cxi(int world_size, int rank){
    //vacc::vacc_fi_info_t* vacc_fi_info = (vacc::vacc_fi_info_t*)calloc(1,sizeof(vacc::vacc_fi_info_t));
    vacc::vacc_fi_info_t* vacc_fi_info = new vacc::vacc_fi_info_t();
    
    struct fi_info *hints;
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
    //hints->tx_attr->size = 1;
    hints->caps = FI_MSG | FI_TAGGED | FI_RECV | FI_SEND;

    {
        //std::string domain_name = "cxi0";
        //std::string domain_name = "cxi";
        //domain_name += std::to_string(rank%8);
        //hints->domain_attr->name = strdup(domain_name.c_str());
        hints->fabric_attr->name = strdup("cxi");
        hints->domain_attr->av_type = FI_AV_TABLE;
    }


    err = fi_getinfo(FI_VERSION(1,20), NULL, NULL, 0ULL, hints, &vacc_fi_info->info);
    if (err != FI_SUCCESS) {
        std::cout << "fi_getinfo TODO error handling lol" << err << fi_strerror(err) << "\n";
        vacc_fi_info->status = 1;
        return vacc_fi_info;
    }
    fi_freeinfo(hints);

    if(DEBUG){
        std::cout << "Fabric name: " << vacc_fi_info->info->fabric_attr->name << "\n";
        std::cout << "Domain name: " << vacc_fi_info->info->domain_attr->name << "\n";
    }
    int nic_count = 1;
    struct fi_info* curr_info = vacc_fi_info->info;
    while(curr_info->next != NULL){
        curr_info = curr_info->next;
        if(DEBUG){
            std::cout << "Domain name on rank " << rank << ", info, " << nic_count  << ": " << curr_info->domain_attr->name << "\n";
        }
        nic_count += 1;
    }
    //vacc_fi_info->status = 2 + (curr_info->next==NULL);
    //return vacc_fi_info;

    //assert(nic_count==NIC_COUNT);

    vacc_fi_info->nic_count = nic_count;
    
    err = fi_fabric(vacc_fi_info->info->fabric_attr, &vacc_fi_info->fabric, NULL);
    if (err != FI_SUCCESS) {
        std::cout << "fi_fabric TODO error handling lol " << err << fi_strerror(err) << "\n";
        vacc_fi_info->status = 1;
        return vacc_fi_info;
    }
    curr_info = vacc_fi_info->info;

    for(int n = 0; n<nic_count; n++){
        
        err = fi_domain(vacc_fi_info->fabric, curr_info, &vacc_fi_info->domain[n], NULL);
        if (err != FI_SUCCESS) {
            std::cout << "fi_domain TODO error handling lol " << err << fi_strerror(err) << "\n";
            vacc_fi_info->status = 1;
            return vacc_fi_info;
        }
        /*
        FI endpoint (EP)
        */
        err = fi_endpoint(vacc_fi_info->domain[n], curr_info, &vacc_fi_info->ep[n], NULL);
        if (err != FI_SUCCESS) {
            std::cout << "fi_endpoint TODO error handling lol " << err << fi_strerror(err) << "\n";
            vacc_fi_info->status = 1;
            return vacc_fi_info;
        }


        /*
        FI address vector (AV)
        */
        struct fi_av_attr* av_attr = new fi_av_attr();
        av_attr->type = FI_AV_TABLE;
        //av_attr.flags = FI_SYMMETRIC;
        
        err = fi_av_open(vacc_fi_info->domain[n], av_attr, &vacc_fi_info->av[n], NULL);
        if (err != FI_SUCCESS) {
            std::cout << "fi_av_open TODO error handling lol" << err << fi_strerror(err) << "\n";
            vacc_fi_info->status = 1;
            return vacc_fi_info;
        }
        
        err = fi_ep_bind(vacc_fi_info->ep[n], &(vacc_fi_info->av[n]->fid), 0);
        if (err != FI_SUCCESS) {
            std::cout << "fi_ep_bind av TODO error handling lol" << err << fi_strerror(err) << "\n";
            vacc_fi_info->status = 1;
            return vacc_fi_info;
        }

        vacc_fi_info->tx_ctx[n] = calloc(1, sizeof(fi_context));
        vacc_fi_info->rx_ctx[n] = calloc(1, sizeof(fi_context));
        /*
        FI completion queue (CQ)
        */

        struct fi_cq_attr* cq_attr_tx = new fi_cq_attr();
        cq_attr_tx->format = FI_CQ_FORMAT_TAGGED;
        cq_attr_tx->size = 16384;

        struct fi_cq_attr* cq_attr_rx = new fi_cq_attr();
        cq_attr_rx->format = FI_CQ_FORMAT_TAGGED;
        cq_attr_rx->size = 16384;
        
        err = fi_cq_open(vacc_fi_info->domain[n], cq_attr_tx, &vacc_fi_info->tx_cq[n], NULL);
        if (err != FI_SUCCESS) {
            std::cout << "fi_cq_open1 TODO error handling lol" << err << fi_strerror(err) << "\n";
            vacc_fi_info->status = 1;
            return vacc_fi_info;
        }
        err = fi_cq_open(vacc_fi_info->domain[n], cq_attr_rx, &vacc_fi_info->rx_cq[n], NULL);
        if (err != FI_SUCCESS) {
            std::cout << "fi_cq_open2 TODO error handling lol" << err << fi_strerror(err) << "\n";
            vacc_fi_info->status = 1;
            return vacc_fi_info;
        }

        // bind TX CQ to EP
        err = fi_ep_bind(vacc_fi_info->ep[n], &(vacc_fi_info->tx_cq[n]->fid), FI_TRANSMIT);
        if (err != FI_SUCCESS) {
            std::cout << "fi_ep_bind1 TODO error handling lol" << err << fi_strerror(err) << "\n";
            vacc_fi_info->status = 1;
            return vacc_fi_info;
        }
        // bind RX CQ to EP
        err = fi_ep_bind(vacc_fi_info->ep[n], &(vacc_fi_info->rx_cq[n]->fid), FI_RECV);
        if (err != FI_SUCCESS) {
            std::cout << "fi_ep_bind2 TODO error handling lol" << err << fi_strerror(err) << "\n";
            vacc_fi_info->status = 1;
            return vacc_fi_info;
        }

        // enable EP
        err = fi_enable(vacc_fi_info->ep[n]);
            if (err != FI_SUCCESS) {
            std::cout << "TODO error handling lol\n";
        }
        curr_info = curr_info->next;
    }

    fi_addr_t *me[NIC_COUNT];
    fi_addr_t *me_test;
    size_t addrlen = 8;
    for(int i=0; i<NIC_COUNT;i++){

        fi_getname((fid_t)vacc_fi_info->ep[i], NULL, &addrlen);
        assert(addrlen == sizeof(uint64_t));

        me_test = (fi_addr_t *)malloc(addrlen);
        std::cout << "addr:" << *(me_test) << " " << addrlen << "\n";
        err = fi_getname((fid_t)(vacc_fi_info->ep[i]), (void*)me_test, &addrlen);
        if (err != FI_SUCCESS) {
            std::cout << "addr:" << *(me_test)  << "\n";
            std::cout << "fi_getname TODO error handling lol " << err << fi_strerror(err) << "\n";
            vacc_fi_info->status = 1;
            return vacc_fi_info;
        }
    }

    if (true) {
        //std::cout << "addrlen:" << addrlen << " " << sizeof(uint64_t) << "\n";
        std::cout << "addr:" << *(me[0])  << "\n";
    }

    void *loaded_addr = calloc(world_size*NIC_COUNT,addrlen);

    MPI_Allgather(me, NIC_COUNT, MPI_UINT64_T, loaded_addr, NIC_COUNT, MPI_UINT64_T, MPI_COMM_WORLD);

    if(true) {
        std::cout << "loaded_addr: ";
        for(int i=0;i<world_size;i++){ 
            std::cout << *((uint64_t*)loaded_addr+i) << ",";
        }
        std::cout << "\n";
    }


    for(int i=0; i<NIC_COUNT;i++){
        //fi_addr_t *addr_vect;
        vacc_fi_info->addr_vect[i] = (fi_addr_t*)calloc(world_size*NIC_COUNT,addrlen);
        int num_success = fi_av_insert(vacc_fi_info->av[i], loaded_addr, world_size*NIC_COUNT, vacc_fi_info->addr_vect[i], 0ULL, NULL);
        if(num_success != world_size*NIC_COUNT){
            std::cout << "fi_av_insert Not all addr added: " << num_success << "/" << world_size << " " << fi_strerror(num_success) << "\n";
            vacc_fi_info->status = 1;
            return vacc_fi_info;
        }
    }
    
    return vacc_fi_info;
}


int vacc::freeinfo(vacc_fi_info* vacc_fi_info){
    return 0;//TODO
}