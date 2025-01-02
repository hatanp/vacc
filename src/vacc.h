#pragma once
#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_tagged.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_cxi_ext.h>



//Message tags for tag matching

namespace vacc
{
	enum vacc_op{
		VACC_OP_RING_AR_RS_1,
		VACC_OP_RING_AR_AG_1
	};
}