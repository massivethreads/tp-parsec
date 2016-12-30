#include "tpswitch/tpswitch.h"

#ifndef _TP_PARSEC_HEADER_
#define _TP_PARSEC_HEADER_

// alias for cilk_XXX
#define task_begin           cilk_begin
#define task_return          cilk_return
#define task_return_t        cilk_return_t
#define task_spawn           cilk_spawn
#define task_void_return     cilk_void_return

// alias for omp_XXX
#ifndef pragma_omp_parallel_single
  #define pragma_omp_parallel_single(clause, S) do { S } while(0)
#endif//pragma_omp_parallel_single

#define task_parallel_region pragma_omp_parallel_single

#endif//_TP_PARSEC_HEADER_
