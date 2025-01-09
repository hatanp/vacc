//TODO
#include "../vacc.h"
#include "../init_cxi.h"

namespace vacc
{
    int ring_allreduce(float *input_buf, float *comm_buf, int elem_count, vacc::vacc_fi_info_t* vacc_fi_info);
}