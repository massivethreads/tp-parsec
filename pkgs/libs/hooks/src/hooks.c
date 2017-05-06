/* Copyright (c) 2006-2008 by Princeton University
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Princeton University nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY PRINCETON UNIVERSITY ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL PRINCETON UNIVERSITY BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** \file hooks.c
 * \brief An implementation of the PARSEC Hooks Instrumentation API.
 *
 * In this file the hooks library functions are implemented. The hooks API
 * is defined in header file hooks.h.
 *
 * The default functionality can be enabled and disabled by defining the
 * corresponding macros in file config.h. A detailed description of all
 * features is available there.
 */

/* NOTE: A detailed description of each hook function is available in file
 *       hooks.h.
 */

#include "include/hooks.h"
#include "config.h"

#include <stdio.h>
#include <assert.h>

/* Time measurement can be enabled in file config.h. */
#if ENABLE_TIMING
#include <sys/time.h>
/* Elapsed time of the whole benchmark run */
static double bench_time;              /* unit: second */
static unsigned long long bench_clock; /* unit: clock cycle */
/* Elapsed time of the Region-of-Interest (ROI) */
static double roi_time;                /* unit: second */
static unsigned long long roi_clock;   /* unit: clock cycle */
#endif /* ENABLE_TIMING */

#if ENABLE_SETAFFINITY
#include <sched.h>
#include <stdlib.h>
#include <stdbool.h>
#endif //ENABLE_SETAFFINITY

#if DAG_RECORDER == 2
//#include <dag_recorder.h>
//#include <tpswitch/tpswitch.h>
#include <tp_parsec.h>
#endif

/** Enable debugging code */
#define DEBUG 0

#if DEBUG
/* Counters to keep track of number of invocations of hooks */
static int num_bench_begins = 0;
static int num_roi_begins = 0;
static int num_roi_ends = 0;
static int num_bench_ends = 0;
#endif

/* get current time by syscall */
double hooks_cur_time() {
  struct timeval tp[1];
  gettimeofday(tp, 0);
  return tp->tv_sec + 1.0e-6 * tp->tv_usec;
}

/* get current clock (time stamp counter) */
#if defined(__x86_64__)

  static unsigned long long hooks_rdtsc() {
    unsigned long long u;
    asm volatile ("rdtsc;shlq $32,%%rdx;orq %%rdx,%%rax":"=a"(u)::"%rdx");
    return u;
  }
  
#elif defined(__sparc__) && defined(__arch64__)
  
  static unsigned long long hooks_rdtsc(void) {
    unsigned long long u;
    asm volatile("rd %%tick, %0" : "=r" (u));
    return u;
  }

#else
  
  static unsigned long long hooks_rdtsc() {
    unsigned long long u;
    asm volatile ("rdtsc" : "=A" (u));
    return u;
  }
  
#endif


/** \brief Variable for unique identifier of workload.
 *
 * This variable stores the unique identifier of the current benchmark program.
 * It is set in function __parsec_bench_begin().
 */
static enum __parsec_benchmark bench;

/* NOTE: Please look at hooks.h to see how these functions are used */

void __parsec_bench_begin(enum __parsec_benchmark __bench) {
  #if DEBUG
  num_bench_begins++;
  assert(num_bench_begins==1);
  assert(num_roi_begins==0);
  assert(num_roi_ends==0);
  assert(num_bench_ends==0);
  #endif //DEBUG

  printf(HOOKS_PREFIX" PARSEC Hooks (Version "HOOKS_VERSION") entering bench\n");
  fflush(NULL);

  //Store global benchmark ID for other hook functions
  bench = __bench;

  #if ENABLE_SETAFFINITY
  //default values
  int cpu_num= CPU_SETSIZE;
  int cpu_base = 0;

  //check environment for desired affinity
  bool set_range = false;
  char *str_num = getenv(__PARSEC_CPU_NUM);
  char *str_base = getenv(__PARSEC_CPU_BASE);
  if(str_num != NULL) {
    cpu_num = atoi(str_num);
    set_range = true;
    if(str_base != NULL) {
      cpu_base = atoi(str_base);
      set_range = true;
    }
  }

  //check for legal values
  if(cpu_num < 1) {
    fprintf(stderr, HOOKS_PREFIX" Error: Too few CPUs selected.\n");
    exit(1);
  }
  if(cpu_base < 0) {
    fprintf(stderr, HOOKS_PREFIX" Error: CPU range base too small.\n");
    exit(1);
  }
  if(cpu_base + cpu_num > CPU_SETSIZE) {
    fprintf(stderr, HOOKS_PREFIX" Error: CPU range exceeds maximum value (%i).\n", CPU_SETSIZE-1);
    exit(1);
  }

  //set affinity
  if(set_range) {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    int i;
    for(i = cpu_base; i < cpu_base + cpu_num; i++) {
      CPU_SET(i, &mask);
    }
    printf(HOOKS_PREFIX" Using %i CPUs (%i-%i)\n", cpu_num, cpu_base, cpu_base+cpu_num-1);
    sched_setaffinity(0, sizeof(mask), &mask);
  }
  #endif //ENABLE_SETAFFINITY

  /* Get bench start time */
#if ENABLE_TIMING
  bench_time = hooks_cur_time();
  bench_clock = hooks_rdtsc();
#endif /* ENABLE_TIMING */
}

void __parsec_bench_end() {
  /* Get bench time */
#if ENABLE_TIMING
  bench_time = hooks_cur_time() - bench_time;
  bench_clock = hooks_rdtsc() - bench_clock;
#endif /* ENABLE_TIMING */
    
  #if DEBUG
  num_bench_ends++;
  assert(num_bench_begins==1);
  assert(num_roi_begins==1);
  assert(num_roi_ends==1);
  assert(num_bench_ends==1);
  #endif //DEBUG

  fflush(NULL);

  #if DAG_RECORDER == 2
  dr_dump();
  #endif

  printf(HOOKS_PREFIX" PARSEC Hooks terminating\n");
#if ENABLE_TIMING
  printf(HOOKS_PREFIX" Bench time: %lf sec, %llu clocks.\n", bench_time, bench_clock);
#endif /* ENABLE_TIMING */
}

void __parsec_roi_begin() {
  #if DEBUG
  num_roi_begins++;
  assert(num_bench_begins==1);
  assert(num_roi_begins==1);
  assert(num_roi_ends==0);
  assert(num_bench_ends==0);
  #endif //DEBUG

  printf(HOOKS_PREFIX" Entering ROI\n");
  fflush(NULL);

  #if ENABLE_SIMICS_MAGIC
  MAGIC_BREAKPOINT;
  #endif //ENABLE_SIMICS_MAGIC

  #if ENABLE_PTLSIM_TRIGGER
  ptlcall_switch_to_sim();
  #endif //ENABLE_PTLSIM_TRIGGER

  #if DAG_RECORDER == 2
  dr_start(NULL);
  #endif

  /* Get roi start time */
#if ENABLE_TIMING
  roi_time = hooks_cur_time();
  roi_clock = hooks_rdtsc();
#endif /* ENABLE_TIMING */
}


void __parsec_roi_end() {
  /* Get roi time */
#if ENABLE_TIMING
  roi_time = hooks_cur_time() - roi_time;
  roi_clock = hooks_rdtsc() - roi_clock;
#endif /* ENABLE_TIMING */

  #if DAG_RECORDER == 2
  dr_stop();
  #endif

  #if DEBUG
  num_roi_ends++;
  assert(num_bench_begins==1);
  assert(num_roi_begins==1);
  assert(num_roi_ends==1);
  assert(num_bench_ends==0);
  #endif //DEBUG

  #if ENABLE_SIMICS_MAGIC
  MAGIC_BREAKPOINT;
  #endif //ENABLE_SIMICS_MAGIC

  #if ENABLE_PTLSIM_TRIGGER
  ptlcall_switch_to_native();
  #endif //ENABLE_PTLSIM_TRIGGER

  printf(HOOKS_PREFIX" Leaving ROI\n");
#if ENABLE_TIMING
  printf(HOOKS_PREFIX" ROI time: %lf sec, %llu clocks.\n", roi_time, roi_clock);
#endif /* ENABLE_TIMING */
  fflush(NULL);
}

