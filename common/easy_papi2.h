/* 
 * easy_papi2.h
 */
#ifndef __EASY_PAPI_H__
#define __EASY_PAPI_H__
#pragma once

/* easy_papi provides an easy-to-use interface
   to papi library, particularly for multithreaded
   and task parallel apps. 

   (i) single-threaded apps 

   the easiest way to use it for single threaded
   programs is this:

   -------------------------------
   main() {
     epapi_start();
         do computaiton;
     epapi_show_counters();  // at any time
         do computaiton;
     epapi_show_counters();  // at any time
         do computaiton;
   }
   -------------------------------

   or, if you do not want to print counters
   each time you get them, you can get values
   in your program, like this.

   main() {
     long long values[N];
     epapi_start();
         do computaiton;
     epapi_read(0, 0, values); // at any time
         do computaiton;
     epapi_read(0, 0, values); // at any time
         do computaiton;
   }

   where N is the number of counters you want to get.
      
   by default, an environment variable
   EPAPI_EVENTS (or EP_EV) specifies which counters you want to get
   it is a comma-separated list of valid event names,
   which you can obtain by papi_avail or
   papi_native_avail commands. e.g.,

   EPAPI_EVENTS=PAPI_L1_ICM,PAPI_TOT_INS

   (ii) multithreaded programs

   In multithreaded programs, the main thread must
   call epapi_start before any thread is started.
   All other threads should call epapi_read as soon
   as possible before doing computation in order to initiate their own
   counters. Their first invocations of epapi_read() always return counters
   of zero values.

   --------------------------------------
   main() {
     epapi_start();
#pragma omp parallel
     {
       long long values[N];
       epapi_read(0,0,values);
         do computation;
       epapi_read(0,0,values);
         do computation;
       epapi_read(0,0,values);
         do computation;
           ...
     }
   }
   --------------------------------------

   if you know something about raw PAPI
   interface, you may notice that you don't
   need any thread-specific initialization.
   this is automatically handled inside
   epapi_read (or similar counter-reading
   functions), which performs thread-local
   initializations when it is called for the
   first time after epapi_start() is called.
   
   it is particularly convenient for
   dynamically load balanced (task parallel,
   in particular) programs, where you don't
   know which underlying threads a
   computation will run on.

   you just make sure you call epapi_start()
   in one thread and all threads simply start
   calling epapi_read_ex() (or similar 
   functions getting counter values).

 */


/* if EASY_PAPI_PTHREAD is false,
   you should be able to compile this
   file without pthread. it works
   for single-threaded apps only */
#if EASY_PAPI_PTHREAD
#include <pthread.h>
#endif

#if __cplusplus
extern "C" {
#endif
  /* global data */
  typedef struct epapi_gdata epapi_gdata;
  /* thread local data */
  typedef struct epapi_tdata epapi_tdata;

  /* (an arbitrary) statically-imposed limit 
     on the number of PAPI events. I don't
     know how much events PAPI is able to 
     meaningfully support, but it can't be
     too many anyways */
  enum { max_papi_events = 10 };

  /* data structure representing global options,
     such as events you are interested in */
  typedef struct {
    /* comma-separated list of PAPI events. e.g.,
       "PAPI_TOT_INS,PAPI_TOT_CYC,PAPI_L1_DCM" */
    const char * events;
    unsigned long long sampling_interval;
    int (*thread_start_hook)(epapi_gdata * gd, epapi_tdata * td);
    /* impose limits on the number of events
       e.g.,  if it is 2, PAPI_L1_DCM of the above
       will be discarded.
       there is a statically-imposed limit 
       max_papi_events too. */
    int max_events;
    int verbosity;
  } epapi_options;

  /* integers describing phases */
  enum {
    epapi_phase_initialize_failed = -1,
    epapi_phase_not_initializing = 0,
    epapi_phase_initializing = 1,
    epapi_phase_initialized = 2,
    epapi_phase_counting_0 = 3
  };

  struct epapi_gdata {
    epapi_options opts;
    char * event_names[max_papi_events];
    int g_event_codes[max_papi_events];
    int n_events;
    /* global phase:
       epapi_phase_not_initializing (0) 
          : nobody has called epapi_init yet
       epapi_phase_initializing (1) 
          : a thread is initializing epapi. other threads should wait
       epapi_phase_initialized (2) 
          : has been initialized, but counting not started
       epapi_phase_counting_0 (3) : 0th counting started
       4 : 0th counting ended
       5 : 1st counting started
       6 : 1st counting ended
       2n+3 : nth counting started
       2n+4 : nth counting ended
           ...

     this variable monotonically increases whenever
     you start or stop counting.
     */
    int epapi_gphase;
    /* array of thread-local data for all participating threads */
    epapi_tdata * volatile tdata_list;
#if EASY_PAPI_PTHREAD
    pthread_key_t epapi_tdata_key;
#endif
  };

  /* thread local data; the user may supply the storage
     explicitly, or use an internally-managed thread-local 
     storage */
  struct epapi_tdata {
    union {
      struct {
	/* pointer to the next thread */
        epapi_tdata * next;
	/* event codes of your interest */
        int t_event_codes[max_papi_events];
	/* and their counter values */
        long long values[max_papi_events];
	/* start timestamp counter */
        unsigned long long start_tsc;
        unsigned long long clocks;
        unsigned long long sampling_interval;
        unsigned long long next_sampling_clock;
	/* number of events of your interest */
        int n_events;
        int eventset;
	/* the phase of this thread */
        int epapi_tphase;
        unsigned long long t0, t1;
        long long values0[max_papi_events];
        long long values1[max_papi_events];
      };
      char __pad__[64][2];
    };
  };
  
  /* initialize options with defaults; use only when you 
     want to set options programmatically */
  int epapi_init_opts(epapi_options * opts);
  /* initialize global data; use only when you want to 
     access global data directly in your program */
  int epapi_init_gdata(epapi_gdata * gd);
  /* initialize thread local data; use only when you want to 
     access thread-local data directly in your program */
  int epapi_init_tdata(epapi_tdata * td);

  /* ensure epapi has been initialized.
     safe to call multiple times.
     safe to call concurrently.
     you do not have to call it explicitly;
     you can directly call epapi_start, which
     takes care of initialization */
  int epapi_init_ex(epapi_options * opts, epapi_gdata * gd);
  int epapi_init();

  /* ensure we are counting (start counting if it
     is not already). can be called by any number of threads.
     make sure at least one call is made, before any thread 
     reads counters.
     when gd == 0 -> use an internal global variable
     when td == 0 -> alloc and use a thread-specific data.
     when you supply td, you must ensure it is 
     specific to the underlying pthread */
  int epapi_start_ex(epapi_options * opts, 
                     epapi_gdata * gd, epapi_tdata * td);
  int epapi_start();
  
  /* ensure we are not counting, and read the
     counters of the current thread. */
  int epapi_stop_ex(epapi_options * opts, 
                    epapi_gdata * gd, epapi_tdata * td);
  int epapi_stop();

  /* read the counters of the current thread into
     td; it does not change the state of the
     thread; that is, the thread may be counting
     or stopped counting. if epapi_start has been
     called and epapi_stop has not been called
     since you called epapi_start the last time,
     then the thread is counting, and you get
     values since you called epapi_start.
     In multithreaded execution, epapi_start only initializes
     the calling thread, other threads has not been initialized
     yet until their first calls to epapi_read (or epapi_start).
     Their first calls to epapi_read (or epapi_start) will start
     their own counters and always return zero values.
  */
  int epapi_read_ex(epapi_gdata * gd, epapi_tdata * td, 
                    long long * values);
  int epapi_read();
  /* similar to epapi_read, but accumulate the counters */
  int epapi_accum_ex(epapi_gdata * gd, epapi_tdata * td, 
                     long long * values);
  int epapi_accum();

  /* show the values of thread counters */
  int epapi_show_counters_thread_ex(epapi_gdata * gd, epapi_tdata * td);
  int epapi_show_counters_thread();
  /* show the values of counters of all threads */
  int epapi_show_counters_ex(epapi_gdata * gd);
  int epapi_show_counters();
  /* calc sum of counters */
  int epapi_sum_counters(epapi_gdata * gd, epapi_tdata * td_sum);
  /* show the accumulated values of counters of all threads */
  int epapi_show_sum_counters_ex(epapi_gdata * gd);
  int epapi_show_sum_counters();

  int epapi_get_num_events();

  inline unsigned long long 
  epapi_get_tsc() {
    unsigned long long u;
#if defined(__sparc__) && defined(__arch64__)
    asm volatile("rd %%tick, %0" : "=r" (u));
#elif defined(__x86_64__)
    /* no rdtscp on xeon phi */
    asm volatile ("rdtsc; shlq $32,%%rdx; orq %%rdx,%%rax; movq %%rax,%0" 
                  : "=r" (u) : : "%rax", "%rcx", "%rdx");
#elif defined(__i386__)
    asm volatile ("rdtsc" 
                  : "=A" (u) : : "%rax", "%rcx");
#else
#error "define a method to get tsc"
#endif
    return u;
  }
  
#if __cplusplus
};
#endif

#endif
