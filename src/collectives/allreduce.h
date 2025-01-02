//TODO
#include "../vacc.h"
#include "../init_cxi.h"

namespace vacc
{
    int ring_allreduce(int world_size, int rank, vacc::vacc_fi_info_t* vacc_fi_info);
    int ring_allreduce_na(int world_size, int rank, vacc::vacc_fi_info_na_t* vacc_fi_info);
}