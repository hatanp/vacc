#pragma once
#include "vacc.h"

#define DEBUG false
#define EXTRA_DEBUG false

#define NIC_COUNT 8
#define NIC_PER_CPU 4
namespace vacc
{
    typedef struct vacc_fi_info{
        int status;
        int nic_count;
        struct fi_info* info;
        struct fid_fabric *fabric;
        struct fid_domain *domain[NIC_COUNT];
        struct fid_av *av[NIC_COUNT];
        struct fid_ep *ep[NIC_COUNT];
        void* tx_ctx[NIC_COUNT];
        void* rx_ctx[NIC_COUNT];
        struct fid_cq *tx_cq[NIC_COUNT];
        struct fid_cq *rx_cq[NIC_COUNT];
        fi_addr_t *addr_vect[NIC_COUNT];
    } vacc_fi_info_t;

	//_na for "Non-Array", for single NIC per host debugging
    typedef struct vacc_fi_info_na{
        int status;
        int nic_count;
        struct fi_info* info;
        struct fid_fabric *fabric;
        struct fid_domain *domain[NIC_COUNT];
        struct fid_av *av[NIC_COUNT];
        struct fid_ep *ep[NIC_COUNT];
        void* tx_ctx[NIC_COUNT];
        void* rx_ctx[NIC_COUNT];
        struct fid_cq *tx_cq[NIC_COUNT];
        struct fid_cq *rx_cq[NIC_COUNT];
        fi_addr_t *addr_vect[NIC_COUNT];
    } vacc_fi_info_na_t;
    
    int freeinfo(vacc_fi_info* vacc_fi_info);

    vacc_fi_info_t* init_fi_cxi(int world_size,
                                int rank);//
    vacc_fi_info_na_t* init_fi_cxi_na(int world_size,
                                int rank);//
}