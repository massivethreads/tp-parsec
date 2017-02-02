//This feature will be introduced in tpswtich.h, in the near future.

#include <tp_parsec.h>

#ifdef PFOR2_REDUCE_EXPERIMENTAL
/*
  tpswitch parallel reduction (pfor_reduce)
  //for C++11
  ReduceTy pfor_reduce<IntTy, StepIntTy, LeafFuncTy>(IntTy FIRST, IntTy LAST, StepIntTy STEP, IntTy GRAINSIZE, LeafFuncTy LEAF(IntTy first, IntTy last), ReduceTy REDUCE_FUNC(ReduceTy a, ReduceTy b))
  REDUCE_FUNC must be associative.
  ReduceTy must have a default constructor and be copyable.

  Example:
  //pfor_reduce version.
  #include <stdio.h>
  #include "tpswitch.h"
  int main() {
    int first = 0;
    int last = 100;
    int step = 1;
    int grainsize = 2;
    int sum = pfor_reduce(first, last, step, grainsize,
      [step] (int innerFirst, int innerLast) {
        int innerSum = 0;
        for (int i = innerFirst; i < innerLast; i += step)
          innerSum += i;
        return innerSum;
      },
      [] (int a, int b) {
        return a + b;
      });
    //sum becomes 0 + 1 + ... + 99
  }

  TODO: OpenMP4.0 supports user-defined reduction. We can use it.
 */

#if __cplusplus >= 201103L
  #define PFOR_REDUCE_IMPL pfor_reduce_bisection
  #if TO_SERIAL
    template<typename IntTy, typename StepIntTy, typename LeafFuncTy, typename ReduceFuncTy>
    static typename std::result_of<LeafFuncTy(IntTy,IntTy)>::type pfor_reduce_bisection(IntTy first, IntTy last, StepIntTy step, IntTy grainsize, LeafFuncTy leaffunc, ReduceFuncTy reducefunc, const char * file, int line) {
      return leaffunc(first, last);
    }
  #elif TO_OMP || TO_TBB || TO_MTHREAD || TO_MTHREAD_NATIVE || TO_QTHREAD || TO_CILKPLUS || TO_NANOX
    template<typename IntTy, typename StepIntTy, typename LeafFuncTy, typename ReduceTy, typename ReduceFuncTy>
    cilk_static void pfor_reduce_bisection_aux(IntTy first, IntTy a, IntTy b, StepIntTy step, IntTy grainsize, LeafFuncTy* leaffunc, ReduceFuncTy* reducefunc, ReduceTy* returnvalue, const char * file, int line) {
      cilk_begin;
      if (b - a <= grainsize) {
        *returnvalue = (*leaffunc)(first + a * step, first + b * step);
      } else {
        const IntTy c = a + (b - a) / 2;
        typedef typename std::result_of<LeafFuncTy(IntTy,IntTy)>::type ResultTy;
        ResultTy value1, value2;
        volatile ResultTy *value1_ptr = &value1, *value2_ptr = &value2;
        mk_task_group;
        create_task0(spawn pfor_reduce_bisection_aux(first, a, c, step, grainsize, leaffunc, reducefunc, value1_ptr, file, line));
        create_task_and_wait(mit_spawn pfor_reduce_bisection_aux(first, c, b, step, grainsize, leaffunc, reducefunc, value2_ptr, file, line));
        *returnvalue = (*reducefunc)(value1, value2);
      }
      cilk_void_return;
    }
    template<typename IntTy, typename StepIntTy, typename LeafFuncTy, typename ReduceFuncTy>
    cilk_static typename std::result_of<LeafFuncTy(IntTy,IntTy)>::type pfor_reduce_bisection(IntTy first, IntTy last, StepIntTy step, IntTy grainsize, LeafFuncTy leaffunc, ReduceFuncTy reducefunc, const char * file, int line) {
      IntTy a = 0;
      IntTy b = (last - first + step - 1) / step;
      typedef typename std::result_of<LeafFuncTy(IntTy,IntTy)>::type ResultTy;
      ResultTy ret;
      volatile ResultTy* ret_ptr=&ret;
      LeafFuncTy*   leaffunc_ptr   = &leaffunc;
      ReduceFuncTy* reducefunc_ptr = &reducefunc;
      call_task(mit_spawn pfor_reduce_bisection_aux(first, a, b, step, grainsize, leaffunc_ptr, reducefunc_ptr, ret_ptr, file, line));
      return ret;
    }
  #endif
  #ifdef PFOR_REDUCE_IMPL
    #include <type_traits>
    //__VA_ARGS__ is to avoid the well-known comma-in-macro problem.
    //std::decay is to get base type (i.e., remove const)
    #define pfor_reduce(FIRST, ...) PFOR_REDUCE_IMPL <std::decay<decltype(FIRST)>::type>(FIRST, __VA_ARGS__, __FILE__, __LINE__)
  #endif
#else
  #error "error: pfor_reduce (parallel reduce) needs C++11; add a flag -std=c++11"
#endif

#endif//PFOR2_REDUCE_EXPERIMENTAL
