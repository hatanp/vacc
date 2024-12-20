#include "vacc.h"

#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_tagged.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_cxi_ext.h>

#define DEBUG false
#define EXTRA_DEBUG false
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

    int freeinfo(vacc_fi_info* vacc_fi_info);

    vacc_fi_info_t* init_fi_cxi(int world_size,
                                int rank);//
}