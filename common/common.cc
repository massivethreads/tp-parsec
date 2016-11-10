/* 
 * common.cc
 */
#if __CILK__
/* we are using cilkc.
   this, along with -x cilk option to cilkc 
   forces it treat this file as a cilk file */
#pragma lang +cilk
#endif

#if __CILK__
/* sched.h requires these two symbols to be defined
   in order to use sched_getaffinity etc. */
#define _GNU_SOURCE
#define __USE_GNU
#include <sched.h>		/* sched_getaffinity etc. */
#undef _GNU_SOURCE
#undef __USE_GNU
#else
#include <sched.h>		/* sched_getaffinity etc. */
#endif

#include <common.h>

void die_(const char * x, 
	  const char * file, int line, const char * fun) {
  fprintf(stderr, "%s:%d: in %s: error: checking '%s' failed\n", 
	  file, line, fun, x);
  exit(1);
}



int g_num_workers;

#if TO_CILK
#define get_init_num_workers() Cilk_active_size
#else
#define get_init_num_workers() get_init_num_workers_no_mit_cilk()

/* get the number of workers of the environment
   at start up. this should be called only once
   in the initialization.
 */
int get_init_num_workers_no_mit_cilk() {
#if TO_SERIAL || TO_MPI || TO_UPC || TO_MTHREAD_DM_STUB
  return 1;
#elif TO_CILKPLUS
  return __cilkrts_get_nworkers();
#elif TO_OMP
  assert(!omp_in_parallel());
  return omp_get_max_threads();
#elif TO_MTHREAD || TO_MTHREAD_NATIVE || TO_MTHREAD_DM
  return myth_get_num_workers();
#elif TO_MADM
  return madm_get_n_procs();
#elif TO_TBB || TO_PTHREAD || TO_QTHREAD || TO_NANOX
  /* in these systems, they are obtained from env var */
#if TO_TBB
  const char * var1 = "TBB_NTHREADS";
  const char * var2 = NULL;
#elif TO_PTHREAD
  const char * var1 = "PTH_NTHREADS";
  const char * var2 = NULL;
#elif TO_QTHREAD
  /* this is wrong! */
  const char * var1 = "QTHREAD_NUM_SHEPHERDS";
  const char * var2 = "QTHREAD_NUM_WORKERS_PER_SHEPHERD";
  return qthread_num_workers();
#elif TO_NANOX
  const char * var1 = "NX_PES";
  const char * var2 = NULL;
#else
#error "get_num_workers"
#endif
  char * v1 = getenv(var1);
  char * v2 = (var2 ? getenv(var2) : NULL);
  int x1, x2;
  if (!v1) {
    fprintf(stderr, "could not get number of workers (set %s)\n", var1);
    exit(1);
  } else {
    x1 = atoi(v1);
    if (x1 <= 0) {
      fprintf(stderr, "could not parse environment variable %s as a number (treated as 1)\n", var1);
      x1 = 1;
    }
  }
  if (!v2) {
    x2 = 1;
  } else {
    x2 = atoi(v2);
    if (x2 <= 0) {
      fprintf(stderr, "could not parse environment variable %s as a number (treated as 1)\n", var1);
      x2 = 1;
    }
  }
  return x1 * x2;
#else
#warning "none of TO_SERIAL/TO_OMP/TO_CILK/TO_TBB/TO_PTHREAD/TO_MTHREAD/TO_MTHREAD_NATIVE/TO_MTHREAD_DM/TO_QTHREAD/TO_NANOS set (get_init_num_workers return 1!)"
  return 1;
#endif
}
#endif

/* 
 * infrastructure for get_worker_num()
 */

#if DAG_RECORDER == 2

#else

#if TO_TBB || TO_PTHREAD || TO_NANOX
thread_id_counter global_thread_id_counter = { 0 };
pthread_key_t thread_id_counter_key;
#endif

#endif

/*
 * terminate runtime
 */

/* what is MGAS_MOCK?? */
#if TO_MTHREAD_DM && defined(TO_MGAS_MOCK) && MGAS_MOCK

#define ALL_WORKER_FACTOR 3

static void accum_perf() {
  volatile int counter=0;
  volatile bool printed=false;
  const int num_workers = get_num_workers();
  do {
    task_group tg;
    for(int i=0; i < ALL_WORKER_FACTOR*num_workers; ++i)
    {
      tg.run([&]{
          if(mgas_reduce_profile())
          {
            __sync_add_and_fetch(&counter, 1);
          }
        });
    }
    tg.wait();
  } while(counter != num_workers);
  do {
    task_group tg;
    for(int i=0; i < ALL_WORKER_FACTOR*num_workers; ++i)
    {
      tg.run([&]{
          if(get_worker_num() == 0 && !printed)
          {
            printed = true;
            mgas_profile_output(stderr);
          }
        });
    }
    tg.wait();
  } while(!printed);
}

template <class T> static void on_all_workers(T func) {
  const int n_workers = get_num_workers();
  volatile int counter = n_workers;
  typedef unsigned long long ull;
  std::vector<ull> flags(n_workers, false);
  task_group tg;
  do {
    for(int i = 0; i < ALL_WORKER_FACTOR*n_workers; ++i)
    {
      tg.run([&](){
          const int worker_id = get_worker_num();
          volatile ull * pflag = &flags[worker_id];
          if(!*pflag) {
            func();
            *pflag = true;
            __sync_sub_and_fetch(&counter, 1);
          }
        });
    }
    tg.wait();
  } while(counter);
}

#endif

void terminate_runtime()
{
#if TO_MTHREAD_DM && MGAS_MOCK
  on_all_workers(mgas_profile_stop);
  accum_perf();
#endif
#if TO_MTHREAD_DM
  mgas_finalize();
#endif
}

/*
 * critical section
 */

lock_t global_lock_for_cs;

/* 
 * init_runtime 
 */

#if TO_MTHREAD_DM
#include <boost/optional.hpp>
#include <boost/lexical_cast.hpp>
template <typename T>
boost::optional<T> fromEnvInt(const char * envname)
{
    const char * str = getenv(envname);
    if(str == nullptr) { return boost::none; }
  try
    { return boost::lexical_cast<T>(str); }
  catch(const boost::bad_lexical_cast & e)
    { return boost::none; }
}
template <typename T>
T fromEnvInt(const char * envname, T initial_value)
{
    boost::optional<T> ans = fromEnvInt<T>(envname);
    return ans ? *ans : initial_value;
  }
#endif

#if TO_MPI
int THREADS;
int MYTHREAD;
#elif TO_UPC
/* UPC already define THREADS and MYTHREAD */

#else
const int THREADS = 1;
const int MYTHREAD = 0;
#endif

cilk void init_runtime_(int *argcp, char ***argvp) {
#if TO_MPI
  MPI_Init(argcp, argvp);
  MPI_Comm_size(MPI_COMM_WORLD, &THREADS);
  MPI_Comm_rank(MPI_COMM_WORLD, &MYTHREAD);
#else
  MY_UNUSED_VAR(argcp);
  MY_UNUSED_VAR(argvp);
#endif

#if TO_QTHREAD
  /* this must come before get_init_num_workers_no_cilk() */
  //qthread_init(num_workers);
  qthread_initialize();
#endif

#if TO_CILK
  g_num_workers = Cilk_active_size;
#else
  g_num_workers = get_init_num_workers_no_mit_cilk();
#endif

#if TO_TBB
  /* it is possible that it is included from C file,
     in which case we do not call it.
     we assume there is still a main C++ file
     and the one defined in C does not get called */
  new tbb::task_scheduler_init(g_num_workers);
#endif
  
#if DAG_RECORDER == 2
#else
#if TO_TBB || TO_PTHREAD || TO_NANOX
  /* see get_worker_num() */
  pthread_key_create(&thread_id_counter_key, NULL);
#endif
#endif

#if TO_MTHREAD_DM
  mgas_initialize_with_threads(argcp, argvp, g_num_workers);
#endif

#if TO_MTHREAD_DM && MGAS_MOCK
  boost::optional<unsigned> latency = fromEnvInt<unsigned>("MYTHDM_LATENCY");
  boost::optional<unsigned> bandwidth = fromEnvInt<unsigned>("MYTHDM_BANDWIDTH");
  unsigned n_proc = fromEnvInt<unsigned>("MYTHDM_PROC", get_num_workers());
  // Environment::set_network_params(latency, bandwidth);
  on_all_workers(mgas_profile_start);
#endif

  // init critical section implementation
  init_critical();
}

/* bind workers to core */

typedef struct available_cores {
  int n;		   /* number of available cores */
  int max_idx;		   /* maximum core index */
  int * cores;		   /* array of available cores (n elements) */
} available_cores;

static available_cores * 
get_available_cores_by_cpulock(int show_progress __attribute__((unused))) {
  char * cpus_locked = getenv("CPUS_LOCKED");
  /* CPUS_LOCKED not set, give up */
  if (cpus_locked == NULL) return NULL;
  else {
    char * p = cpus_locked;
    available_cores * avail
      = (available_cores *)malloc(sizeof(available_cores));
    int n = 0;
    int i;
    /* first count elements separated by commas */
    while (p) {
      n++;
      p = strchr(p, ',');
      if (p) p++;
    }
    /* now we know the number of cores */
    avail->n = n;
    avail->max_idx = -1;
    avail->cores = (int *)malloc(sizeof(int) * n);
    if (avail->cores == NULL) { perror("malloc"); exit(1); }
    p = cpus_locked;
    for (i = 0; i < avail->n; i++) {
      char * q = strchr(p, ',');
      if (i < n - 1) {
	assert(q);
	avail->cores[i] = atoi(strndup(p, q - p));
	p = q + 1;
      } else {
	avail->cores[i] = atoi(strdup(p));
      }
      if (avail->max_idx < avail->cores[i]) {
	avail->max_idx = avail->cores[i];
      }
    }
    return avail;
  }
}

static available_cores * 
get_available_cores_by_getaffinity(int show_progress) {
  cpu_set_t mask[1];
  int sz = sizeof(mask) * 8;
  available_cores * avail
    = (available_cores *)malloc(sizeof(available_cores));
  int UNUSED(rr) = sched_getaffinity(getpid(), sizeof(mask), mask);
  int n = 0;
  int i;
  must(avail);
  must(rr == 0);
  avail->n = CPU_COUNT(mask);
  avail->max_idx = -1;
  if (show_progress) 
    printf("%d avail_cores (from sched_getaffinity)\n", avail->n);

  avail->cores = (int *)malloc(sizeof(int) * avail->n);
  assert(avail->cores);
  for (i = 0; i < sz; i++) {
    if (CPU_ISSET(i, mask)) {
      assert(n < avail->n);
      if (show_progress)
	printf("cores[%d] = %d\n", n, i);
      avail->cores[n] = i;
      avail->max_idx = i;
      n++;
    }
  }
  assert(n == avail->n);
  return avail;
}

static available_cores * 
get_available_cores(int show_progress) {
  available_cores * a;
  a = get_available_cores_by_cpulock(show_progress);
  if (a) return a;
  a = get_available_cores_by_getaffinity(show_progress);
  if (a) return a;
  assert(0);
  return NULL;
}


/* find an available entry in avail_cores and bind to that core.
   avail_cores is an array of integers, of n_avail_cores elements,
   each element of which is an available core index (normally all
   cores but you may have been invoked with taskset -c ...).
 */
#if TO_CILK || TO_CILKPLUS || TO_OMP || TO_NANOX || TO_TBB || TO_QTHREAD
static void bind_to_core(int x) {
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(x, &set);
  if (sched_setaffinity(0, sizeof(set), &set) != 0) {
    fprintf(stderr, 
	    "%s:warning: bind_to_core(%d) failed\n", __FILE__, x);
  }
}

ATTRIBUTE_UNUSED 
cilk_static void move_to_free_core(int x, 
				   int n_workers, available_cores * avail,
				   int show_progress) {
  unsigned short xs[3]; long r;
  int i;
  int c = -1;
  xs[0] = xs[1] = xs[2] = x;
  r = nrand48(xs) % avail->n;
  /* seeking an entry which is not -1 (i.e., is available).
     we should be able to find it in a single round, as long as
     the number of workers <= the number of cores available
     at the beginning */
  for (i = 0; i < avail->n; i++) {
    c = avail->cores[r];
    if (c != -1) {
      /* GCC builtin */
      if (__sync_bool_compare_and_swap(&avail->cores[r], c, -1)) {
	break;
      }
    }
    r = (r + 1) % avail->n;
  }
  if (c != -1) {
    int ok_workers = 0;
    if (show_progress) {
      printf("task %d got core %d\n", x, c);
    }
    bind_to_core(c);
    /* now wait for all workers to get their cores */
    while (ok_workers < n_workers) {
      /* count workers who got a core */
      ok_workers = 0;
      for (i = 0; i < avail->n; i++) {
	if (avail->cores[i] == -1) ok_workers++;
      }
    }
  } else {
    if (show_progress) {
      printf("warning: task %d could not get available core\n", x);
    }
    /* double check if we really ran out the available cores  */
    for (i = 0; i < avail->n; i++) {
      assert(avail->cores[r] == -1);
    }
  }
}
#endif

/* spread workers to available cores.
   make sure only one worker is running on each core 
   and bind workers to them.
   we accomplish this by creating as many 
   'tasks' as the number of physical cores.
 */
cilk_static int 
bind_workers_to_cores_int(int show_progress) {
  cilk_begin;
  available_cores * avail = get_available_cores(show_progress);
  int n_phys_cores = avail->max_idx + 1;
#if TO_MTHREAD || TO_MTHREAD_NATIVE || TO_MTHREAD_DM || TO_SERIAL || TO_MADM

  fprintf(stderr, 
	  "%s:note: do nothing to bind workers to cores\n", 
	  __FILE__);

#elif TO_QTHREAD
  
  fprintf(stderr, 
	  "%s:note: do nothing to bind workers in qthreads\n", 
	  __FILE__);

#elif TO_CILK || TO_CILKPLUS || TO_OMP || TO_NANOX || TO_TBB 
  int n_workers = get_num_workers();
  if (show_progress) {
    printf("binding %d workers to cores\n", n_workers);
  }
  pragma_omp_parallel_single(, {
      int i;
      mk_task_group;
      for (i = 0; i < n_workers; i++) {
	create_task0(spawn move_to_free_core(i, n_workers, avail, show_progress));
      }
      wait_tasks;
    });
#else

  fprintf(stderr, "%s:warning: can't bind workers to cores in this setting\n", __FILE__);
  
#endif

  free(avail->cores);
  free(avail);

  cilk_return(n_phys_cores);
}

cilk void bind_workers_to_cores_void(int show_progress) {
  int n_phys_cores = 0;
  task_group_no_dr;
  call_task_no_dr(n_phys_cores = spawn bind_workers_to_cores_int(show_progress));
  MY_UNUSED_VAR(n_phys_cores);
}

/*  
 *
 */


/* MIT cilk is not able to parse papi.h
   /home/tau/parallel2/sys/include/papi.h:578: syntax error
   /home/tau/parallel2/sys/include/papi.h:578: declaration without a variable

       typedef struct _papi_sprofil {
       void *pr_base;
       unsigned pr_size;
578--> caddr_t pr_off;
       unsigned pr_scale;
       } PAPI_sprofil_t;
 */

#include "easy_papi2.c"

/*
 * backtrace
 */

#if SAMPLE_BACKTRACE
#include "sample_backtrace.c" /* failed to include this file? please make -f common_include.mk install [platform=g/i/..] at parallel2/sys/src/tools/common_include */
#endif

/* (1) call bt_global_init(0) before sampling
   (2) call bt_thread_init() on each thread
 */

/* 
 * dag recorder 
 */

#if !defined(DAG_RECORDER)
#define DAG_RECORDER 0
#endif

#if DAG_RECORDER == 1
#error "DAG_RECORDER == 1 no longer supported. You should give DAG_RECORDER=2 instead"
#endif

ATTRIBUTE_UNUSED cilk_static void use_static_() {
}
