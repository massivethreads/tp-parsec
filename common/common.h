/* 
 * common.h
 */

/* 
   goals: it is possible to write:

   (1) "all C" programs
   (2) "mixed C++ and C" programs
   (3) "mixed Cilk and C" programs

   (1) may be compiled into 
   serial, OpenMP
   MassiveThreads/Qthreads/Nanox (with no tasks defined)
   Intel Cilk

   (2) may be compiled into 
   serial, OpenMP, TBB
   MassiveThreads/Qthreads/Nanox (tasks defined)
   Intel Cilk

   (3) may be compiled into
   serial, OpenMP, TBB
   MassiveThreads/Qthreads/Nanox (tasks defined)
   Intel Cilk, MIT Cilk

   in all cases, it must be possible that all files
   include common.cilkh without any problem.
   but some C++-dependent functionalities may not
   be available to C programs.

   to this end, the following rules are followed.

   all symbols must be either:
   (a) defined as C symbols (extern "C"), or
   (b) defined only in C++ and not in C

   use (a) for all symbols that do not depend on
   C++ functions.

 */

/* 
   conditional compilation issues.
   there are two ways you want to condition compilation.
   (1) condition on which compiler is compiling the program
   (2) condition on to which platform you are compiling the 
   program for.

   for example, you might want to have a program that combines
   a .cilk and a .c file, compile the .cilk file by cilkc
   and compile the .c file by gcc.  when compiling the latter 
   with gcc you still want to compile pieces FOR cilk.

   for (1), use whatever symbols defined by the compiler.
   for example GCC defined __GNUC__, Intel C compiler defines
   __ICC, cilkc compiler defined __CILK__, etc.

   for (2), we use a convention that when you are compiling a source
   for platform XXXX, we define a symbol TO_XXXX.  for example, when
   you compile into OpenMP, you have TO_OMP defined (one of TO_OMP,
   TO_TBB, TO_CILK, TO_MTHREAD, TO_QTHREAD, TO_NANOX is defined).

 */

#pragma once

#ifndef __COMMON_H__
#define __COMMON_H__

/* we are using cilkc; better to always treat it as cilk file */
#if __CILK__
#pragma lang +Cilk
#endif

/* 
 * include necessary inlude files
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

/*
 * compiler hack toolchain
 */

#if __cplusplus
extern "C" {
#endif
void die_(const char * err_msg, 
	  const char * file, int line, const char * fun);
#if __cplusplus
}
#endif

#define must(exp) if (!(exp)) die_(#exp, __FILE__, __LINE__, __func__)


#define MY_UNUSED_VAR(x) (void)(x)

#if defined(__GNUC__) && !__CILK__

#define ATTRIBUTE_UNUSED __attribute__((unused))
#define UNUSED(x) x __attribute__((unused))

/* 
 variables are sometime used only in assert( ... );
 such variables become unused and get compiler warnings
 when NDEBUG is defined
 */

#if defined(NDEBUG)
#define UNUSED_IF_NDEBUG(x) x __attribute__((unused))
#else
#define UNUSED_IF_NDEBUG(x) x
#endif

#elif __CILK__

/* I don't know how to define a macro to prevent cilkc
   from warning unused variable.  with gcc,
   int x __attribute__ ((unused)) 
   is it, so the macro below.  with cilkc, if we define 
   UNUSED macro, it warns by saying it's already defined
   at include/cilk/cilk-sysdep.h:67;
   if we remove the macro, it still complains x to be
   unused; for now, we don't define and ignore the warning
 */
// #define UNUSED(x)

#define ATTRIBUTE_UNUSED 
#define UNUSED_IF_NDEBUG(x) x

#else

#define UNUSED(x) x
#define UNUSED_IF_NDEBUG(x) x

#endif

/* 
 * make sure nullptr is defined 
 */

#ifdef __cplusplus

#if defined(__GXX_EXPERIMENTAL_CXX0X__) || __cplusplus > 201103L
/* C++11. nullptr defined by the sytem */
#else
// C++03 or older
#define nullptr NULL
#endif

#endif


/* 
 * system-specific include files
 */

/* 
 * OpenMP
 */
#if TO_OMP
#include <omp.h>
#endif

/* 
 * TBB
 */
#if TO_TBB
#include <pthread.h>
#if __cplusplus
#include <tbb/tbb.h>
#include <tbb/task_scheduler_init.h>
#include <tbb/spin_mutex.h>
#include <tbb/scalable_allocator.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_reduce.h>
#include <tbb/blocked_range.h>
#include <tbb/blocked_range2d.h>
#include <tbb/blocked_range3d.h>
//using namespace tbb;
#else

#warning "you are compiling a C file to TBB. TBB functions not available in this file"

#endif	/* __cplusplus */
#endif	/* TBB */

/* 
 * Cilk
 */
/* we are compiling into Cilk, by icc */
#if TO_CILKPLUS
#include <cilk/cilk.h>
#include <cilk/cilk_api.h>
#include <pthread.h>		/* for mutex */
#endif

/* we are compiling into Cilk, by cilkc  */
#if __CILK__
/* not TO_CILK.  when we compile C files into a Cilk executable,
   it is compiled by gcc, and we don't want to include them */
#include <cilk.h>
#include <cilk-lib.cilkh>

/* with cilkc, some functions do not get defined by
   including corresponding fucntions, apparently 
   because they need certain macros to be defined
   (e.g., #define _BSD_SOURCE, _GNU_SOURCE .
   if we permanently define them, it generates 
   a suite of synax errors in many header files
   (perhaps because they use GNU specific extensions
   when _GNU_SOURCE is defined?)
   we could temporarily define them and undefine
   them after #include, but we don't know what
   are their side effects. play safe for now.
   when you see some implicit definition warnings,
   add their signatures here. */

char *strdup(const char *s);
char *strndup(const char *s, size_t n);
long int nrand48(unsigned short S[3]);
int getloadavg(double loadavg[], int nelem);

#endif	/* TO_CILK */

/* 
 * Pthread or MassiveThreads
 */
#if TO_PTHREAD || TO_MTHREAD
#include <pthread.h>
#endif

/* 
 * MassiveThreads
 */
#if TO_MTHREAD || TO_MTHREAD_NATIVE || TO_MTHREAD_DM

/* pthread.h still necessary for mutex */
#include <pthread.h>

/* myth.h is a C header file.
   when including from C++, 
   mark all symbols as extern "C" */
#ifdef __cplusplus
extern "C" {
#endif
#include <myth/myth.h>
#ifdef __cplusplus
}
#endif
#endif

/* 
 * MassiveThreads/DM
 */
#if TO_MTHREAD_DM
/* I don't know MTHREAD_DM functions are inherently C++ dependent.
   but I assume so.  just like TBB, we include it only when the
   outer program is C++. */
#if __cplusplus
#include <mgaspp/mgaspp.hpp>
#else
#warning "you are compiling a C file into MTHREAD_DM. DM functions not available in this file"
#endif
#endif

#if TO_MADM
#include <madm.h>
#endif


/* 
 * Qthread
 */
#if TO_QTHREAD
#include <qthread.h>
#include <pthread.h>
#endif

/* 
 * Nanos++
 */
#if TO_NANOX
#include <nanos.h>
#include <pthread.h>
#endif

/* 
 * MPI
 */
#if TO_MPI
#include <mpi.h>
#endif

/* 
 * UPC
 */
#if TO_UPC
#include <upc_relaxed.h>
#include <upc_collective.h>
#endif

/* -------------------------------------
 define cilk and cilk_static
 keyword so that we can portably declare
 a function to be a Cilk procedure
 when compiled by cilkc
 ------------------------------------ */

#if __CILK__
#define cilk_static cilk
#else
#define cilk
#define cilk_static static
#endif

/* -------------------------------------
   define '__shared__' parameter.
   it means shared in UPC and nothing in other systems 
   ------------------------------------- */

#if TO_UPC
#define __shared__ shared
#else
#define __shared__
#endif


#if HPCTOOLKIT
#include <hpctoolkit.h>
#endif

/* -------------------------------------
 * define get_num_workers
 * -------------------------------------
 * 
 * this should return the number of workers used in the
 * entire program.  complications:
 * 
 * (1) Cilk gives Cilk_active_size for it, but it can
 * only be used within a Cilk procedure.  we cannot use
 * it inside a regular function.
 * (2) OpenMP gives two things close to but not exactly
 * what we need.
 * omp_get_num_threads() returns the number of threads 
 * within a parallel region
 * omp_get_max_threads() returns the number of "spare" 
 * threads not currently being used + 1. we call omp_get_max_threads()
 * in initialization and remembers that value
 * (3) in many platforms, we obtain it from an environment
 * variable, but querying it every time is expensive.
 * (4) to sum up, in all platforms except for Cilk, we query
 * it in the initialization and set it to g_num_workers.
 * in Cilk, we don't do anything in initialization and 
 * get_num_workers() is a macro Cilk_active_size
 */

#ifdef __cplusplus
extern "C" {
#endif
  static inline int get_num_workers();
  /* the global variable to remember the number of workers */
  extern int g_num_workers;
#ifdef __cplusplus
}
#endif


static inline int get_num_workers() {
#if TO_SERIAL || TO_MPI
  return 1;
#elif TO_UPC
  return THREADS;
#elif TO_CILK || TO_CILKPLUS || TO_OMP || TO_MTHREAD || TO_MTHREAD_NATIVE || TO_MTHREAD_DM || TO_TBB || TO_PTHREAD || TO_QTHREAD || TO_NANOX
  return g_num_workers;
#elif TO_MADM
  return madm_get_n_procs();
#else
#warning "none of TO_SERIAL/TO_OMP/TO_CILK/TO_CILKPLUS/TO_TBB/TO_PTHREAD/TO_MTHREAD/TO_MTHREAD_NATIVE/TO_MTHREAD_DM/TO_QTHREAD/TO_NANOS set (get_init_num_workers return 1!)"
  return 1;
#endif
}

/* --------------------------
 * define get_worker_num
 * --------------------------
 * this should return the id of the current worker.
 * the number must be in [0,NW) where NW is the number
 * of workers.
 *
 * complications:
 * (1) Cilk gives Self for it, but it can
 * only be used within a Cilk procedure.  we cannot use
 * it inside a regular function.
 * (2) many systems do not provide such a thing. on such
 * systems we implement it using thread local storage
 */

#if DAG_RECORDER == 2

#define get_worker_num() dr_get_worker()

#elif TO_CILK

#define get_worker_num() Self

#else

#define get_worker_num() get_worker_num_no_mit_cilk()

#if TO_TBB || TO_PTHREAD || TO_NANOX
typedef struct {
  long c;
} thread_id_counter;

#ifdef __cplusplus
extern "C" {
#endif
  extern thread_id_counter global_thread_id_counter;
  extern pthread_key_t thread_id_counter_key;
  static inline long thread_id_counter_get_next();
#ifdef __cplusplus
}
#endif

static inline long thread_id_counter_get_next() {
  return __sync_fetch_and_add(&global_thread_id_counter.c, 1);
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
  static inline int get_worker_num_no_mit_cilk();
#ifdef __cplusplus
}
#endif

static inline int get_worker_num_no_mit_cilk() {
/* what is MTHREAD_DM_STUB??? */
#if TO_SERIAL || TO_MPI || TO_MTHREAD_DM_STUB
  return 0;
#elif TO_OMP
  /* not quite right. only true inside a parallel region */
  return omp_get_thread_num();
#elif TO_CILKPLUS
  return __cilkrts_get_worker_number();
#elif TO_UPC
  return MYTHREAD;
#elif TO_MTHREAD || TO_MTHREAD_NATIVE || TO_MTHREAD_DM
  return myth_get_worker_num();
#elif TO_MADM
  return madm_get_pid();
#elif TO_QTHREAD
  int w = qthread_worker(NULL);
  assert(w < g_num_workers);
  return w;
#elif TO_TBB || TO_PTHREAD || TO_NANOX
  void * x = pthread_getspecific(thread_id_counter_key);
  if (x) {
    int w = (long)x - 1;
    assert(w < g_num_workers);
    return w;
  } else {
    long c = thread_id_counter_get_next();
    pthread_setspecific(thread_id_counter_key, (void *)(c + 1));
    int w = c;
    assert(w < g_num_workers);
    return w;
  }
#else  /* FIXME: */
#warning "FIXME: get_worker_num() always return 0 in this platform"
  return 0;
#endif
}
#endif


/* -------------------------------------
 * MassiveThreads DM mock
 * -------------------------------------
 */

#ifdef __cplusplus
extern "C" {
#endif
  void terminate_runtime();
#ifdef __cplusplus
}
#endif

#if 0 && !defined(MGAS_H)
/* global pointer */
typedef void * mgasptr_t;

/* global pointer vector */
typedef struct mgas_vector {
  mgasptr_t mp;
  size_t size;
} mgas_vector_t;
#endif

#if TO_MTHREAD_DM
/* localize needs C++ */
#ifndef __cplusplus
#error "MTHREAD DM needs C++"
#endif /* __cplusplus */

template <class T>
struct mgas_type_traits
{
  typedef T value_type;
  typedef mgaspp::mgas_ptr<T> pointer_type;
  typedef mgaspp::mgas_ptr<const T> const_pointer_type;
};

#define GAS_PTR(type) mgas_type_traits<type>::pointer_type
#define GAS_ALLOC(type, size, page) mgaspp::alloc<type>(size)
#define GAS_CALLOC(type, size, page) ({                         \
      mgas_ptr<type> X_ = GAS_ALLOC(type, size, page);          \
      mgas_vector_t v_[] = {{0, size}};                         \
      auto lX_ = LOCALIZE(X, size, MGAS_VEC_NULL, true);        \
      memset(0, lX_, size);                                     \
      COMMIT(X_, v_, true);                                     \
      UNLOCALIZE(X_);                                           \
      X_;                                                       \
    })
#define GAS_FREE(gptr) mgaspp::free(gptr)
#define LOCALIZE_V(type, gptr, vec, own) (      \
    mgaspp::localize<type>(                     \
      (vec),                                    \
      sizeof(vec)/sizeof((vec)[0]),             \
      (own)))
#define COMMIT_V(gptr, vec, own)                \
  (mgaspp::commit                               \
   (vec, sizeof(vec)/sizeof(vec[0]), own))

#define LOCALIZE_S(gptr, size, stride, count, own)      \
  (mgaspp::localize((gptr), (size), (stride), count, (own)))
#define COMMIT_S(gptr, size, stride, count, own)    \
  (mgaspp::commit((gptr), (size), (stride), count, (own)))

#if 0
the following line gets:
...common.cilkh:342:1: warning: multi-line comment [-Wcomment]
#define LOCALIZE_C(gptr, cache_size, load_size, own)      \
   (LOCALIZE_S((gptr), (cache_size), (cache_size), ({1, load_size}), (own)))
#endif


#define LOCALIZE_C(gptr, cache_size, load_size, own)                \
  ({ const size_t count[] = {1, (cache_size)};                      \
    LOCALIZE_S((gptr), (cache_size), (cache_size), count, (own));})

#if 0
 #define COMMIT_C(gptr, cache_size, load_size, own)    \
   (COMMIT_S((gptr), (cache_size), (cache_size), ({1, load_size}), (own)))
#endif

#define COMMIT_C(gptr, cache_size, load_size, own)              \
  ({ const size_t count[] = {1, (load_size)};                   \
    COMMIT_S((gptr), (load_size), (load_size), count, (own));})

#define UNLOCALIZE(gptr) (mgaspp::unlocalize(gptr))

#define GAS_PTR_TRAITS mgas_type_traits

#else /* MTHREAD_DM */

#ifndef __cplusplus

#define GAS_PTR(type) type *

#else  /* __cplusplus */

template <class T>
struct default_type_traits
{
  typedef T value_type;
  typedef T* pointer_type;
  typedef const T* const_pointer_type;
};

#define GAS_PTR(type) default_type_traits<type>::pointer_type

#define GAS_PTR_TRAITS default_type_traits

#endif /* __cplusplus */

#define GAS_ALLOC(type, size, page) ((type *)malloc(size))
#define GAS_CALLOC(type, size, page) ((type *)calloc((size), 1))
#define GAS_PROC(x)
#define GAS_FREE(gptr) free(gptr)

#define LOCALIZE_V(type, gptr, vec, own) (gptr)
#define COMMIT_V(gptr, vec, own)

#define LOCALIZE_S(gptr, size, stride, count, own) (gptr)
#define COMMIT_S(gptr, size, stride, count, own)

#define LOCALIZE_C(gptr, cache_size, load_size, own) (gptr)
#define COMMIT_C(gptr, cache_size, load_size, own)

#define UNLOCALIZE(gptr)

#define GAS_PTR_ADD(gptr, offset) ((gptr) + (offset))

#endif	/* MTHREAD_DM */

#ifndef ASSERT_MESSAGE
#define ASSERT_MESSAGE(expr, ...)               \
  { bool ret = expr;                            \
    if(!ret) {                                  \
      fprintf(stderr, "ASSERT: " __VA_ARGS__);  \
    }                                           \
  }
#endif /* else MTHREAD_DM */

/* -------------------------------------
 * define init_runtime
 * -------------------------------------
 */

#if TO_MPI
extern int THREADS;
extern int MYTHREAD;
#elif TO_UPC
/* UPC already define THREADS and MYTHREAD */

#else
extern const int THREADS;
extern const int MYTHREAD;
#endif

/* -------------------------------------
 * define init_runtime
 * -------------------------------------
 */

#ifdef __cplusplus
extern "C" {
#endif
  cilk void init_runtime_(int *argcp, char ***argvp);
#ifdef __cplusplus
}
#endif

#define init_runtime(argcp, argvp) call_task(spawn init_runtime_(argcp, argvp))

/* 
 * define rdtsc function
 */

#ifdef __cplusplus
extern "C" {
#endif
  static inline unsigned long long rdtsc();
#ifdef __cplusplus
}
#endif

#ifdef __x86_64__

#if !TO_UPC
static inline unsigned long long rdtsc() {
  unsigned long long u;
  asm volatile ("rdtsc;shlq $32,%%rdx;orq %%rdx,%%rax":"=a"(u)::"%rdx");
  return u;
}
#endif

#elif defined(__sparc__) && defined(__arch64__)

#if !TO_UPC
static inline unsigned long long rdtsc(void)
{
  unsigned long long u;
  asm volatile("rd %%tick, %0" : "=r" (u));
  return u;
}
#endif

#else

#if !TO_UPC
static inline unsigned long long rdtsc() {
  unsigned long long u;
  asm volatile ("rdtsc" : "=A" (u));
  return u;
}
#endif

#endif

/* rdtsc or a similar function */
static inline unsigned long long get_tsc() {
  return rdtsc();
}

/* 
 * define cur_time (seconds in double)
 */

#ifdef __cplusplus
extern "C" {
#endif
  static inline double cur_time();
#ifdef __cplusplus
}
#endif

static inline double cur_time() {
#if 0 && defined(CLOCK_REALTIME)
  struct timespec tp[1];
  clock_gettime(CLOCK_REALTIME, tp);
  return tp->tv_sec + tp->tv_nsec * 1.0e-9;
#else
  struct timeval tp[1];
  gettimeofday(tp, NULL);
  return tp->tv_sec + tp->tv_usec * 1.0e-6;
#endif
}

#ifdef __cplusplus
extern "C" {
#endif
  static inline unsigned long long cur_time_ull();
#ifdef __cplusplus
}
#endif

static inline unsigned long long cur_time_ull() {
#if defined(CLOCK_REALTIME)
  struct timespec tp[1];
  clock_gettime(CLOCK_REALTIME, tp);
  return tp->tv_sec * 1000000000UL + tp->tv_nsec;
#else
  struct timeval tp[1];
  gettimeofday(tp, NULL);
  return tp->tv_sec * 1000000000UL + tp->tv_usec * 1000;
#endif
}



/* 
 * profiling:
 *  if you want to use PAPI, add 
   parameters:=papi
   papi:=1
 to your Makefile
 */

/* Cilk cannot handle papi.h */
#if EASY_PAPI2 && !TO_CILK
#undef EASY_PAPI2
#endif

#if TO_SERIAL
#define EASY_PAPI_PTHREAD 0
#else
#define EASY_PAPI_PTHREAD 1
#endif
/* 
   if your compilation fails with 
   easy_papi.h no such file or directory,
   you should install common_include again.

   there must be a symlink from
      parallel2/sys/inst/{g,i,im}/include/easy_papi.{h,c}
   -> parallel2/sys/src/tools/easy_papi/easy_papi.{h,c}

   these links are installed when you run:
      make -f common_include.mk install
   in common_include directory.
   if nothing happens, delete install_done
   and run it again
 */

#include "easy_papi2.h" /* you should install common_include again */



/* 
 * define TBB's task_group-like class for appropriate platforms
 *   (Pthreads, MassiveThreads, Qthread, Nano++)
 * primary interface:
 *
 *  task_group tg;
 *  tg.run(closure);
 *  tg.wait()
 */

#if TO_TBB
#if __cplusplus
/* now you always include mtbb/task_group and the life is simpler */
#include <mtbb/task_group.h>
#else
#warning "you are compiling a C file to TBB. task_group function not available in this file"
#endif

#elif TO_MTHREAD || TO_MTHREAD_NATIVE || TO_PTHREAD || TO_QTHREAD || TO_NANOX
#if __cplusplus
#include <mtbb/task_group.h>
#else
#warning "you are compiling a C file to MTHREAD/PTHREAD/QTHREAD/NANOX. task_group function not available in this file"
#endif

#endif

/* 
 * common task parallel macros:

 they provide macros similar to Cilk spawn/sync, 
 OpenMP task/taskwait, and TBB task_gropu::run/wait
 in a common syntax.

 * the following is the primary interface
   - create_task's (there are several versions)
     - create_task0(            spawn E)
     - create_task1(v0,         spawn E)
     - create_task2(v0,v1,      spawn E)
     - create_taskA(spawn E)
   - wait_tasks
   - create_task_and_wait(spawn E)
   - call_task(spawn E)
   - task_group

 to absorb subtle difference between Cilk and other
 models, 

 * we also define two
   MIT-cilk-only keywords 'cilk' and 'spawn' into empty
   string. we also define special keyword cilk_static,
   which gets expanded into 'cilk' in MIT-cilk and
   static in others.

            | MIT-cilk         | Intel Cilk | others
  spawn     | cilk (undefined) | cilk_spawn |
   cilk     | cilk (undefined) |            |
cilk_static | cilk             | static     | static

* cilk_return(expression)
* cilk_return_void
   
------------------------

  A simple quicksort-like example
  cilk void qs(A, p, q) {
   if (q - p < 10) insertion_sort(A, p, q);
   else {
     task_group;
     c = partition(A, p, q);
     create_task0(spawn qs(A, p, c-1));
     create_task_and_wait(spawn qs(A, c, q));
   }
 }

 the numbers after create_task indicates the number
 of variables you want to share between parent and the child.
 create_task1(var, E) creates a task doing E, but sharing
 var.  create_taskA(E) create a task sharing all variables.

 the following indidates how to use them.
 
  cilk void foo() {
     int a[3];
     int b[1000];
     task_group;
     create_task0(spawn f(x));
     create_task1(a, a[0] = spawn f(x));
     create_task2(a, b, a[1] = spawn g(x, b));
     create_taskA(a[2] = spawn g(x, b));
     wait_tasks;
  }

 In OpenMP, the above gets expanded to:

  cilk void foo() {
     int a[3];
     int b[1000];
#pragma omp task
     f(x);
#pragma omp task shared(a)
     a[0] = spawn f(x);
#pragma omp task shared(a, b)
     a[1] = spawn g(x, b);
#pragma omp task default(shared)
     a[2] = spawn g(x, b);
#pragma omp taskwait
  }

 In MIT Cilk, 

  cilk void foo() {
     int a[3];
     int b[1000];

     spawn f(x);
     a[0] = spawn f(x);
     a[1] = spawn g(x, b);
     a[2] = spawn g(x, b);
     sync;
  }

 In Intel Cilk:

  void foo() {
     int a[3];
     int b[1000];

     cilk_spawn f(x);
     a[0] = cilk_spawn f(x);
     a[1] = cilk_spawn g(x, b);
     a[2] = cilk_spawn g(x, b);
     cilk_sync;
  }

 In TBB:

  void foo() {
    int a[3];
    int b[1000];
    tbb::task_group tg;
    tg.run([=] { f(x) });
    tg.run([=,&a] { a[0] = f(x); });
    tg.run([=,&a,&b] { a[1] = spawn g(x, b); });
    tg.run([&] { a[2] = spawn g(x, b); });
    tg.wait();
  }

  and in other platforms supporting task_group:

  void foo() {
    int a[3];
    int b[1000];
    mtbb::task_group tg;
    tg.run([=] { f(x) });
    tg.run([=,&a] { a[0] = f(x); });
    tg.run([=,&a,&b] { a[1] = spawn g(x, b); });
    tg.run([&] { a[1] = spawn g(x, b); });
    tg.wait();
  }

  the only difference between the last and tbb is the namespace
  task_group class is in (tbb::task_group vs. mtbb::task_group).
  mtbb::task_group is defined in mtbb/task_group.h in 
  MassiveThreads distribution.

  Note that, in OpenMP and Cilk, task_group macro is noop.


 * call_task_and_wait(spawn E) 

   as its name suggests, it behaves like:

     call_taskA(spawn E); wait_tasks;

   but in TBB-like models (TBB, MTHREAD, QTHREAD, NANOX),
   the task creation is omitted. so it is expanded into:

     E; wait_tasks;

   note that:
     - wait_tasks waits for ALL outstanding tasks,
       not just the task just created by the immediately
       preceeding call_taskA(spawn E)
     - the task is created with all variables shared.
       it is safe as the parent immediately waits for
       the task just created.
     

 * call_task(spawn E)

   this has a behavior that may surprise you on Cilk.
   in OpenMP and TBB-like systems (TBB, MTHREAD, QTHREAD, NANOX), it is
   exactly expanded into E.  it is equivalent to a serial call.
    
   in Cilk, it is equivalent to call_task_and_wait(spawn E);
   that is,

     call_taskA(spawn E); wait_tasks;

    why? well, MIT-Cilk does not provide a means to 
    call a cilk procedure sequentially.  all you can do is
    to spawn it and immediately sync with it, which is exactly
    what call_task(spawn E) does.  note however that this is
    not equivalent to calling E serially, as sync always waits
    for the completion of ALL outstanding children. it is
    equivalent to calling E serially when you don't have any
    outstanding children.

 */

/* the C99 trick to make a macro that is expanded into #pragma 
   http://gcc.gnu.org/onlinedocs/cpp/Pragmas.html
*/

#define do_pragma(x)                  _Pragma( #x )

/* 
   pragma_omp(X) 
   --> #pragma omp X in OpenMP
   --> noop in anything but OpenMP
 */

#if TO_OMP
#define pragma_omp(x)              do_pragma(omp x)
#else
#define pragma_omp(x)
#define pragma_omp_parallel_single(clause, S) do { S } while(0)
#endif


/* 
   now we are going to define task parallel macros.

   for each of the macros (create_taskX, wait_tasks, and task_group),
   we first define a vanilla version without DAG recorder, with
   suffix '_no_dr'. for example, create_task0_no_dr, wait_tasks_no_dr,
   etc.  we then define a version with DAG recorder, with
   suffix '_dr'.

   the real version is finally defined as either of xxxx_no_dr or xxxx_dr,
   depending on whether DAG_RECORDER is defined.

 */

#if TO_SERIAL || TO_MPI

/* in serial env, they are all NOOPs */

#define spawn 
#define task_group_no_dr
#define wait_tasks_no_dr

#define create_task0_no_dr(E)           E
#define create_task1_no_dr(s0, E)       E
#define create_task2_no_dr(s0, s1, E)   E
#define create_taskA_no_dr(E)           E
#define create_task_and_wait_no_dr(E)   E
#define call_task_no_dr(E)              E

#elif TO_OMP

/* OpenMP: 
   create_task0(spawn E) -> 
#pragma omp task  
   E

   create_task1(v0, spawn E) -> 
#pragma omp task shared(v0) 
   E

   wait_tasks
#pragma omp taskwait

   task_group -> noop
   
 */

#define spawn 
#define task_group_no_dr
#define wait_tasks_no_dr              pragma_omp(taskwait)

#define untied_ untied

#define create_task0_no_dr(E)         pragma_omp(task untied_)               E
#define create_task1_no_dr(s0, E)     pragma_omp(task untied_ shared(s0))    E
#define create_task2_no_dr(s0, s1, E) pragma_omp(task untied_ shared(s0,s1)) E
#define create_taskA_no_dr(E)         pragma_omp(task untied_ default(shared)) E
#define create_taskc_no_dr(E)         pragma_omp(task untied_))              E()

#define create_task_and_wait_no_dr(E)					\
  do { create_taskA_no_dr(E); wait_tasks_no_dr; } while(0)
#define create_taskc_and_wait_no_dr(E)					\
  do { create_taskc_no_dr(E); wait_tasks_no_dr; } while(0)

#define call_task_no_dr(E) E
#define call_taskc_no_dr(E) E()

#elif TO_CILK || TO_CILKPLUS

/* either Intel Cilk or MIT-Cilk.

   create_task0(spawn E) --> spawn E
   create_task1(x, x = spawn E) --> x = spawn E
   call_task(spawn E) --> spawn E; sync;
   wait_tasks --> sync
   task_group is noop.
 */

#if TO_CILKPLUS
#define spawn            cilk_spawn
#define wait_tasks_no_dr cilk_sync
#elif TO_CILK
#define wait_tasks_no_dr sync
#else
#warning "you are compiling a Cilk program with a compiler not cilkc or icc. spawn/sync are IGNORED"
#define spawn
#define wait_tasks_no_dr
#endif

#define task_group_no_dr

#define create_task0_no_dr(E)                E
#define create_task1_no_dr(s0, E)            E
#define create_task2_no_dr(s0, s1, E)        E
#define create_taskA_no_dr(E)                E
#define create_task_and_wait_no_dr(E)    do { E;   wait_tasks_no_dr; } while(0)
#define create_taskc_and_wait_no_dr(E)   do { E(); wait_tasks_no_dr; } while(0)
#define call_task_no_dr(E)               do { E;   wait_tasks_no_dr; } while(0)
#define call_taskc_no_dr(E)              do { E(); wait_tasks_no_dr; } while(0)

#elif TO_MTHREAD || TO_MTHREAD_NATIVE || TO_MTHREAD_DM || TO_PTHREAD || TO_QTHREAD || TO_NANOX || TO_TBB

/* A TBB-like family

   create_task0(E)       -> tg.run([=]         { E; })
   create_task1(v0,E)    -> tg.run([=,&v0]     { E; })
   create_task2(v0,v1,E) -> tg.run([=,&v0,&v1] { E; })
   wait_tasks            -> tg.wait()
   task_group            -> tbb::task_group tg; (TBB)
                            mtbb::task_group tg; (others)

*/

#if __cplusplus

#define spawn 
#if TO_TBB
typedef tbb::task_group task_group_t;
#else
typedef mtbb::task_group task_group_t;
#endif
#define task_group_no_dr               task_group_t __tg__
#define wait_tasks_no_dr               __tg__.wait()

#define create_task0_no_dr(E)          __tg__.run([=]         { E; })
#define create_task1_no_dr(s0, E)      __tg__.run([=,&s0]     { E; })
#define create_task2_no_dr(s0, s1, E)  __tg__.run([=,&s0,&s1] { E; })
#define create_taskA_no_dr(E)          __tg__.run([&]         { E; })
#define create_taskc_no_dr(E)          __tg__.run(E)

//#define create_task_and_wait_no_dr(E) do { E; wait_tasks_no_dr; } while(0)
#define create_task_and_wait_no_dr(E)  do { create_taskA_no_dr(E); wait_tasks_no_dr; } while(0)
#define create_taskc_and_wait_no_dr(E) do { create_taskc_no_dr(E); wait_tasks_no_dr; } while(0)

#define call_task_no_dr(E)             E
#define call_taskc_no_dr(E)            E()

#else  /* not C++ */

#warning "you are compiling a C program into MTHREAD/MTHREAD_DM/PTHREAD/QTHREAD/NANOX/TBB; task_group/create_task{0,1,2}/wait_tasks etc. are IGNORED in this file"

#define spawn 
#define task_group_no_dr
#define wait_tasks_no_dr

#define create_task0_no_dr(E)          E
#define create_task1_no_dr(s0, E)      E
#define create_task2_no_dr(s0, s1, E)  E
#define create_taskA_no_dr(E)          E
#define create_taskc_no_dr(E)          E()

#define create_task_and_wait_no_dr(E)  E
#define create_taskc_and_wait_no_dr(E) E()
#define call_task_no_dr(E)             E
#define call_taskc_no_dr(E)            E()

#endif	/* __cplusplus */

#elif TO_MADM

#define spawn 
#define task_group_no_dr
#define wait_tasks_no_dr

#define create_task0_no_dr(E)          E
#define create_task1_no_dr(s0, E)      E
#define create_task2_no_dr(s0, s1, E)  E
#define create_taskA_no_dr(E)          E
#define create_taskc_no_dr(E)          E()

#define create_task_and_wait_no_dr(E)  E
#define create_taskc_and_wait_no_dr(E) E()
#define call_task_no_dr(E)             E
#define call_taskc_no_dr(E)            E()
    
#else

#error "neither TO_SERIAL/TO_OMP/TO_TBB/TO_CILK/TO_PTHREAD/TO_MTHREAD/TO_MTHREAD_NATIVE/TO_MTHREAD_DM/TO_QTHREAD/TO_NANOX defined"

#endif





/*
 * lock
 *  not sure they are flexible enough. probably we need
 *  an interface with which you can pass pointers to locks 
 *  around.  the current interface (e.g., lock_set(x) -> omp_set_lock(&x))
 *  assume locks are always referenced by their original variable name.
 *  go for it for now but anticipate some changes in future.
 */

#if TO_SERIAL

typedef int lock_t;
#define lock_init(x)
#define lock_set(x)
#define lock_unset(x)
#define lock_destroy(x)

#elif TO_OMP

typedef omp_lock_t lock_t;
#define lock_init(x)    omp_init_lock(&x)
#define lock_set(x)     omp_set_lock(&x)
#define lock_unset(x)   omp_unset_lock(&x)
#define lock_destroy(x)

#elif 0 
/* 
 TO_TBB

 I used to use these (tbb::mutex) for TBB.
 but it makes it C++-dependent. 
 it is troublesome as lock is used inside
 a part of data strcture of dag recorder.
 I am allocating that data structure with
 malloc, not new, and as such, constructure
 of tbb::mutex won't be called. this is
 clearly wrong. if I turned the malloc into
 new, then C++ programs are fine, but it
 at least needs an ifdef to make it work
 with Cilk (and other C sources). 

 I don't know what it the ultimately right
 solution, but for now I simply avoid making
 it C++-dependent. 

 TBB's mutex simply resorts to pthread mutex,
 at least on Unix.
*/

#if __cplusplus

#include <tbb/mutex.h>
typedef tbb::mutex lock_t;
#define lock_init(x) 
#define lock_set(x)         x.lock()
#define lock_unset(x)       x.unlock()
#define lock_destroy(x)

#else

#warning "you are compiling a C file to TBB. lock implementation not available in this file"

/* TODO: fix this.
   this results in very confusing result when TBB app 
   has a C source file in it. 
   The C source will define lock_t differently from
   C++ sources.
   Perhaps we should not define them at all, but in that
   case, DAG_RECORDER does not work, as it uses lock_t internally.
   
   I am doing this right now to avoid compilation errors 
   when using C source in TBB apps (e.g., mm).
   we need a decent solution on this.
 */

typedef int lock_t;
#define lock_init(x)
#define lock_set(x)
#define lock_unset(x)
#define lock_destroy(x)

#endif

#elif TO_CILKPLUS

typedef pthread_mutex_t lock_t;
#define lock_init(x)    pthread_mutex_init(&x, NULL)
#define lock_set(x)     pthread_mutex_lock(&x)
#define lock_unset(x)   pthread_mutex_unlock(&x)
#define lock_destroy(x) pthread_mutex_destroy(&x)

#elif TO_CILK

#if __CILK__
typedef Cilk_lockvar lock_t;
#define lock_init(x)    Cilk_lock_init(x)
#define lock_set(x)     Cilk_lock(x)
#define lock_unset(x)   Cilk_unlock(x)
#define lock_destroy(x)

#else
/* here, we are compiling C source by gcc
   to an object file that will eventually
   linked into Cilk executable */

#include <pthread.h>
typedef pthread_mutex_t lock_t;
#define lock_init(x)    pthread_mutex_init(&x, NULL)
#define lock_set(x)     pthread_mutex_lock(&x)
#define lock_unset(x)   pthread_mutex_unlock(&x)
#define lock_destroy(x) pthread_mutex_destroy(&x)

#endif

#elif TO_MTHREAD

/* TODO: fix it 
   currently, it is used inside dag recorder when
   dumping (worker-local) log into (shared) file.
   see dr_interval_records_dump_
   and dr_interval_record_dump_.
   it looks like:

   irs = acquire worker-local log;
   lock(x);
   dump irs into file;
   unlock(x);

   this assumes worker identifier does
   not change across lock/unlock, which is not
   the case in MassiveThreads's Pthread-compatbile API's
   pthread_mutex_lock.

   possible solutions are:
    1. resort to spin lock
    2. use native mthreads API
    3. get rid of the notion of "worker-local" log

    my ideal solution is 3.  2. will be useful in
    its own right. 
    for now, the quickest and dirtiest solution...
*/

typedef pthread_spinlock_t lock_t;
#define lock_init(x)    pthread_spin_init(&x, PTHREAD_PROCESS_PRIVATE)
#define lock_set(x)     pthread_spin_lock(&x)
#define lock_unset(x)   pthread_spin_unlock(&x)
#define lock_destroy(x) pthread_spin_destroy(&x)

#elif TO_TBB || TO_PTHREAD || TO_MTHREAD_NATIVE || TO_MTHREAD_DM

/* see above (search backwards TO_TBB) for why
   I am using pthread mutex here for TBB. */

typedef pthread_mutex_t lock_t;
#define lock_init(x)    pthread_mutex_init(&x, NULL)
#define lock_set(x)     pthread_mutex_lock(&x)
#define lock_unset(x)   pthread_mutex_unlock(&x)
#define lock_destroy(x) pthread_mutex_destroy(&x)

#elif TO_QTHREAD

typedef aligned_t lock_t;
#define lock_init(x)    x = 1
#define lock_set(x)     qthread_lock(&x)
#define lock_unset(x)   qthread_unlock(&x)
#define lock_destroy(x)

#elif TO_NANOX || TO_MPI || TO_UPC_ || TO_MADM
/* well we need to do something, but for now... */

#warning "NANOX/MPI/UPC/MADM lock implementation are currently no-ops"
typedef int lock_t;
#define lock_init(x)
#define lock_set(x)
#define lock_unset(x)
#define lock_destroy(x)

#else

#error "neither TO_SERIAL/TO_OMP/TO_TBB/TO_CILK/TO_PTHREAD/TO_MTHREAD/TO_MTHREAD_NATIVE/TO_MTHREAD_DM/TO_QTHREAD/TO_NANOX defined"

#endif

/* 
 * critical section
 */

#if 0
/* this didn't work; it seems that there is a strange interaction
   among pragma_omp(...)'s
*/
#if TO_OMP
static void init_critical() { }
#define critical_section(S) pragma_omp(critical) do { S; } while(0)
#endif
#endif

extern lock_t global_lock_for_cs;
static void init_critical() { lock_init(global_lock_for_cs); }
static void enter_critical() { lock_set(global_lock_for_cs); }
static void leave_critical() { lock_unset(global_lock_for_cs); }
#define critical_section(S) do { enter_critical(); do { S; } while (0); leave_critical(); } while (0)

/*
 * atomic update primitives
 */
#if 0
#if TO_OMP
#define atomic_add(p, x)  pragma_omp(atomic) *p += (x)
#define atomic_sub(p, x)  pragma_omp(atomic) *p -= (x)
#define atomic_xor(p, x)  pragma_omp(atomic) *p ^= (x)
#endif
#endif

#define atomic_add(p, x)  __sync_fetch_and_add(p, x)
#define atomic_sub(p, x)  __sync_fetch_and_sub(p, x)
#define atomic_xor(p, x)  __sync_fetch_and_xor(p, x)



/* 
 * -------------------------------------------
 * DAG recorder
 * -------------------------------------------
 */
#if 1
/* always include them; we like to allow a program
   to use dr_start(0) etc. even if DAG_RECORDER=0
 */
#include <dag_recorder.h>
#include <tpswitch/tpswitch.h>

#else

#if DAG_RECORDER == 2
/* new dag recorder */

#include <dag_recorder.h>
#include <tpswitch/tpswitch.h>

#elif DAG_RECORDER == 1

#error "DAG_RECORDER == 1 no longer supported. You should give DAG_RECORDER=2 instead"

#elif DAG_RECORDER == 0

/* no-op */
#define dr_start(x) do {} while(0)
#define dr_stop()   do {} while(0)
#define dr_dump()   do {} while(0)

#else

#error "set DAG_RECORDER to 0 or 2"

#endif	

#endif

/* this function is defined only to supress warnings
   by C compiler, saying those static functions are 
   defined but not used */

ATTRIBUTE_UNUSED static void dr_use_static_functions() {
  init_critical();
  enter_critical();
  leave_critical();
}


/* 
 * now we define real interfaces, either to
 *  xxxx_dr or xxxx_no_dr, depending on whther 
 *  DAG_RECORDER is defined
 */


#if DAG_RECORDER == 0
#include <tpswitch/tpswitch.h>
#endif  /* DAG_RECORDER == 0,1 */

#define create_task0_if(X,E) \
do { if (X) { create_task0(E); } else { call_task(E); } } while(0)
#define create_task1_if(X,s0,E) \
do { if (X) { create_task1(s0,E); } else { call_task(E); } } while(0)
#define create_task2_if(X,s0,s1,E) \
do { if (X) { create_task2(s0,s1,E); } else { call_task(E); } } while(0)
#define create_taskA_if(X,E) \
do { if (X) { create_taskA(E); } else { call_task(E); } } while(0)
#define create_taskc_if(X,E) \
do { if (X) { create_taskc(E); } else { call_taskc(E); } } while(0)
#define create_task_and_wait_if(X,E) \
do { if (X) { create_task_and_wait(E); } else { call_task(E); } } while(0)
#define create_taskc_and_wait_if(X,E) \
do { if (X) { create_taskc_and_wait(E); } else { call_taskc(E); } } while(0)

/* 
 * barrier
 */

#if TO_OMP
#define spmd_barrier() pragma_omp(barrier)
#elif TO_UPC
#define spmd_barrier() upc_barrier
#elif TO_MPI
#define spmd_barrier() MPI_Barrier
#elif TO_TBB || TO_CILK || TO_PTHREAD || TO_MTHREAD || TO_MTHREAD_NATIVE || TO_MTHREAD_DM || TO_QTHREAD || TO_NANOX || TO_SERIAL
#define spmd_barrier()
#endif

/* shared all alloc */

#if TO_UPC
#define shared_all_alloc(nblocks, block_sz) upc_all_alloc(nblocks, block_sz)
#else
#define shared_all_alloc(nblocks, block_sz) malloc((nblocks) * (block_sz))
#endif

/* --------------------------------------------
  bind workers to cores
-------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif
cilk void bind_workers_to_cores_void(int show_progress);
#ifdef __cplusplus
}
#endif

#define bind_workers_to_cores(show_progress) \
  call_task(spawn bind_workers_to_cores_void(show_progress))

#if 0

/* 
 * profiling:
 *  if you want to use PAPI, add 
   parameters:=papi
   papi:=1
 to your Makefile
 */

/* Cilk cannot handle papi.h */
#if EASY_PAPI2 && !TO_CILK
#undef EASY_PAPI2
#endif

#if TO_SERIAL
#define EASY_PAPI_PTHREAD 0
#else
#define EASY_PAPI_PTHREAD 1
#endif
/* 
   if your compilation fails with 
   easy_papi.h no such file or directory,
   you should install common_include again.

   there must be a symlink from
      parallel2/sys/inst/{g,i,im}/include/easy_papi.{h,c}
   -> parallel2/sys/src/tools/easy_papi/easy_papi.{h,c}

   these links are installed when you run:
      make -f common_include.mk install
   in common_include directory.
   if nothing happens, delete install_done
   and run it again
 */

#include "easy_papi2.h" /* you should install common_include again */

#endif

/* backtrace */

/* sample_backtrace.h is in parallel2/sys/src/tools/sample_backtrace/ .
   it should be symlinked from 
   parallel2/sys/inst/{g,i,..}/include/ .
   running "make -f common_include.mk install" at common_include
   directory should install this symlink.
*/

#if SAMPLE_BACKTRACE
#include "sample_backtrace.h" /* failed to include this file? please make -f common_include.mk install [platform=g/i/..] at parallel2/sys/src/tools/common_include */
#endif

#endif /* __COMMON_H__ */
