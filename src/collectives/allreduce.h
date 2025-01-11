//TODO
#include "../vacc.h"
#include "../init_cxi.h"

namespace vacc
{
    int ring_ar_rs(float *input_buf, float *comm_buf, int elem_count, int nic, vacc::vacc_fi_info_t* vacc_fi_info);
    int ring_ar_ag(float *input_buf, int elem_count, int nic, vacc::vacc_fi_info_t* vacc_fi_info);
    int ring_allreduce(float *input_buf, float *comm_buf, int elem_count, int nic, vacc::vacc_fi_info_t* vacc_fi_info);
}