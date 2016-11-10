/* 
 * easy_papi2.c
 */

#include "easy_papi2.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* if EASY_PAPI_ENABLE is false,
   you should be able to compile this
   file without papi and all 
   functions become noop */
#if EASY_PAPI2 || EASY_PAPI
#include <papi.h>
#endif

static epapi_options epapi_options_default_values = {
  0,                        /* events */
  0,          /* sampling clocks (0 clock)*/
  0,                  /* thread_start_hook */
  max_papi_events,        /* max number of events */
  0,                        /* verbosity */
};

static epapi_gdata epapi_global_gdata[1];

/* read environment variable v and parse it as 
   a string */
static int 
getenv_str(const char * v, const char ** y) {
  char * x = getenv(v);
  if (!x) 
    return 0;
  *y = strdup(x);
  return 1;
} 

/* read environment variable v and parse it as 
   an integer */
static int 
getenv_int(const char * v, int * y) {
  char * x = getenv(v);
  if (!x) {
    return 0;
  } else {
    int z = atoi(x);
    *y = z;
    return 1;
  }
} 

static int 
getenv_ull(const char * v, unsigned long long * y) {
  char * x = getenv(v);
  if (!x) {
    return 0;
  } else {
    long long z = atoll(x);
    *y = (unsigned long long)z;
    return 1;
  }
} 

#if 0
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
#endif

static void 
mem_barrier() {
  __sync_synchronize();
}


/* stuff that needs papi.h to compile */

/* show PAPI error message */
static void 
epapi_show_error_(int retval, const char * file, int line)
{
  fprintf(stderr, "error:%s:%d papi %d: %s\n", 
          file, line, retval, 
#if EASY_PAPI2 || EASY_PAPI
          PAPI_strerror(retval)
#else
          "????"
#endif
          );
}

/* parse event name event using PAPI */
static int 
epapi_parse_event_papi(char * event_name, int * event_code, int * papi_retval) {
#if EASY_PAPI2 || EASY_PAPI
  * papi_retval = PAPI_event_name_to_code(event_name, event_code);
  if (* papi_retval == PAPI_OK) {
    return 1;
  } else {
    return 0;
  }
#else
  /* always succeeed */
  (void)event_name; (void)event_code; (void)papi_retval;
  return 1;
#endif
}

/* parse raw event like r1234 (I thought it works,
   but it didn't) */
static int 
epapi_parse_event_raw(const char * event_name, int * event_code) {
  if (strlen(event_name) == 0) return 0;
  else if (event_name[0] != 'r') return 0;
  else {
    char * endptr = 0;
    long ec = strtol(event_name + 1, &endptr, 16);
    if (*endptr == 0) {
      * event_code = (int)ec;
      return 1;
    } else {
      return 0;
    }
  }
}

/* parse event name like 0x1234. it did not work either */
static int 
epapi_parse_event_hex(const char * event_name, int * event_code) {
  if (strlen(event_name) <= 1) return 0;
  else if (event_name[0] != '0' || event_name[1] != 'x') return 0;
  else {
    char * endptr = 0;
    long ec = strtol(event_name + 2, &endptr, 16);
    if (*endptr == 0) {
      * event_code = ec;
      return 1;
    } else {
      return 0;
    }
  }
}

/* parse PAPI event name */
static int 
epapi_parse_event(char * event, int * event_code, int * papi_retval) {
  if (epapi_parse_event_papi(event, event_code, papi_retval)) return 1;
  if (epapi_parse_event_raw(event, event_code)) return 1;
  if (epapi_parse_event_hex(event, event_code)) return 1;
  return 0;
}

/* events : a string like "L1_DCM,L2_DCM,L3_DCM"
   event_codes : an array that can have max_events
   parse events into an array of event codes.
   return the number of events successfully parsed */
static int 
epapi_parse_events(const char * events, 
                   char ** event_names, 
                   int * event_codes, int max_events) {
  int i = 0;
  char event_name[128];
  const char * p = events;

  int k;
  for (k = 0; k < max_events; k++) {
    event_names[k] = NULL;
    event_codes[k] = 0;
  }

  if (p) 
    p = strdup(p);
  /* p is like "PAPI_TOT_INS,PAPI_FP_INS,PAPI_LD_INS.
     split it by ',', convert each of them to event_code,
     and put them into code_names array */
  while (p) {
    const char * fmt = "%s";
    char * q = strchr((char *)p, ',');
    /* n = length of the string before , */
    int n = (q == NULL ? (int)strlen(p) : q - p);
    unsigned int to_write = n + 1;
    if (to_write > sizeof(event_name)) {
      fprintf(stderr, "warning: event name too long %s (ignored)\n", p);
    } else {
      /* first check if PAPI recognizes it */
      int event_code = 0;
      int papi_retval = 0; 
      snprintf(event_name, to_write, fmt, p);
      if (epapi_parse_event(event_name, &event_code, &papi_retval)) {
        if (i < max_events) {
          event_names[i] = strdup(event_name);
          event_codes[i] = event_code;
          //fprintf(stderr, "%s (%x)\n", event_name, event_code);
          i++;
        } else {
          /* but the user specified too many events */
          fprintf(stderr, 
                  "warning: too many events specified%s (ignored)\n", 
                  event_name);
        }
      } else {
        fprintf(stderr, 
                "warning: could not find event %s; "
                "you made typo or forgot to init_runtime? the papi error follows:\n", event_name);
        epapi_show_error_(papi_retval, __FILE__, __LINE__);
      }
    }
    if (q == NULL) break;
    else p = q + 1;
  };
  return i;                        /* event_count */
}

/* create an event set from an array of event_codes,
   set it to thread-local data td. 
   return the number of events successfully put in the set */
static int 
epapi_make_thread_eventset(char ** event_names, 
                           int * event_codes, 
                           int n_events,
                           epapi_tdata * td) {
#if EASY_PAPI2 || EASY_PAPI
  int n_ok = 0;
  int es = PAPI_NULL;
  int retval0 = PAPI_create_eventset(&es);
  int i;
  if (retval0 != PAPI_OK) {
    epapi_show_error_(retval0, __FILE__, __LINE__);
    td->n_events = n_ok;
    td->eventset = es;
    return 0;
  }
  for (i = 0; i < n_events; i++) {
    int retval1 = PAPI_add_event(es, event_codes[i]);
    if (retval1 == PAPI_OK) {
      td->t_event_codes[n_ok++] = event_codes[i];
    } else {
      fprintf(stderr, "couldn't add event %s (code=%x)\n", 
              event_names[i], event_codes[i]);
      epapi_show_error_(retval1, __FILE__, __LINE__);
    }
  }
  td->n_events = n_ok;
  td->eventset = es;
  return n_ok;
#else
  (void)event_names; 
  (void)event_codes;
  (void)n_events;
  (void)td;
  return 0;                        /* no events */
#endif
}

/* show counter values. if valid == 0,
   then the thread has never called epapi_start,
   and thus values should not be shown */
static int
epapi_show_counts(int * event_codes, 
                  long long * values, int n_events) {
#if EASY_PAPI2 || EASY_PAPI
  int i;
  for (i = 0; i < n_events; i++) {
    char name[128];
    int pe = PAPI_event_code_to_name(event_codes[i], name);
    if (pe == PAPI_OK) {
      printf("%s (%x): %lld\n", name, event_codes[i], values[i]);
    } else {
      printf("could not obtain event name for %x: %lld\n", 
             event_codes[i], values[i]);
    }
  }
#else
  (void)event_codes; 
  (void)values;
  (void)n_events;
#endif
  return 1;
}

static int
epapi_add_counts(int * event_codes, 
                 long long * values, int n_events,
                 int * sum_event_codes,
                 long long * sum_values) {
#if EASY_PAPI2 || EASY_PAPI
  int i;
  for (i = 0; i < n_events; i++) {
    char name[128];
    int pe = PAPI_event_code_to_name(event_codes[i], name);
    assert(pe == PAPI_OK);
    sum_event_codes[i] = event_codes[i];
    sum_values[i] += values[i];
  }
#else
  (void)event_codes;
  (void)values;
  (void)n_events;
  (void)sum_event_codes;
  (void)sum_values;
#endif
  return 1;
}

/* add values in td into td_sum */
static int
epapi_add(epapi_gdata * gd, epapi_tdata * td, epapi_tdata * td_sum) {
  if (!gd) 
    gd = epapi_global_gdata;
  epapi_add_counts(td->t_event_codes, td->values, td->n_events,
                   td_sum->t_event_codes, td_sum->values);
  /* maximum of n_events (must be the same) */
  if (td_sum->n_events < td->n_events) {
    td_sum->n_events = td->n_events;
  }
  /* minimum of start_clcok */
  if (td_sum->start_tsc == 0 || td_sum->start_tsc > td->start_tsc) {
    td_sum->start_tsc = td->start_tsc;
  }
  /* sum of all clocks */
  td_sum->clocks += td->clocks;
  /* maximum of all states */
  if (td_sum->epapi_tphase < td->epapi_tphase) {
    td_sum->epapi_tphase = td->epapi_tphase;
  }
  return 1;
}



static int 
wait_until_not_initializing(volatile int * p) {
  int q = *p;
  while (q == epapi_phase_initializing) {
    q = *p;
  } 
  mem_barrier();
  return q;
}

/* ensure *p becomes != epapi_phase_not_initialized
   and only one thread returns with 
   epapi_phase_itializing */
static int 
epapi_onetime_init(volatile int * p) {
  int s = *p;
  if (s == epapi_phase_not_initializing) {
    /* try to be the one who initialize */
    if (__sync_bool_compare_and_swap(p, 
                                     epapi_phase_not_initializing,
                                     epapi_phase_initializing)) {
      /* I am the one who should initialize */
      return epapi_phase_initializing;
    } else {
      /* I am not the one. wait for the initilization to finish */
      return wait_until_not_initializing(p);
    }
  } else if (s == epapi_phase_initializing) {
    return wait_until_not_initializing(p);
  } else {
    return s;
  }
}

static int
epapi_check_globally_initialized(epapi_gdata * gd) {
  if (!gd) 
    gd = epapi_global_gdata;
  return (gd->epapi_gphase >= epapi_phase_initialized);
}

static int
epapi_ensure_globally_initialized(epapi_options * opts, 
                                  epapi_gdata * gd) {
  int s;
  if (!gd) 
    gd = epapi_global_gdata;
  s = epapi_onetime_init(&gd->epapi_gphase);
  if (s == epapi_phase_initialize_failed) {
    return 0;                        /* NG */
  } else if (s >= epapi_phase_initialized) {
    return 1;                        /* OK */
  } else {
    /* set options */
    epapi_options opts_[1];
    assert(s == epapi_phase_initializing);
    /* I am the one who must initialize.
       other threads may be waiting */
    if (!opts) {
      opts = opts_;
      epapi_init_opts(opts);
    }
    gd->opts = *opts;
    
#if EASY_PAPI2 || EASY_PAPI 
    {
      int ver = PAPI_library_init(PAPI_VER_CURRENT);
      if (ver != PAPI_VER_CURRENT) { 
        fprintf(stderr, "error:%s:%d: could not init papi. PAPI_library_init(version=%d.%d.%d.%d) returned version=%d.%d.%d.%d (header version and library version differ; check if you are using papi.h and libpapi.so of the same version)\n",
                __FILE__, __LINE__, 
                PAPI_VERSION_MAJOR(PAPI_VER_CURRENT),
                PAPI_VERSION_MINOR(PAPI_VER_CURRENT),
                PAPI_VERSION_REVISION(PAPI_VER_CURRENT),
                PAPI_VERSION_INCREMENT(PAPI_VER_CURRENT),
                PAPI_VERSION_MAJOR(ver),
                PAPI_VERSION_MINOR(ver),
                PAPI_VERSION_REVISION(ver),
                PAPI_VERSION_INCREMENT(ver));
        gd->epapi_gphase = epapi_phase_initialize_failed;
        mem_barrier();
        return 0;
      }
    }

#if EASY_PAPI_PTHREAD
    if (PAPI_thread_init(pthread_self) != PAPI_OK) {
      fprintf(stderr, "error:%s:%d: could not init papi\n",
              __FILE__, __LINE__);
      gd->epapi_gphase = epapi_phase_initialize_failed;
      mem_barrier();
      return 0;
    }
    {
      int err = pthread_key_create(&gd->epapi_tdata_key, NULL);
      if (err) { 
        perror("pthread_key_create"); 
        gd->epapi_gphase = epapi_phase_initialize_failed;
        mem_barrier();
        return 0;
      }
    }
#endif        /* EASY_PAPI_PTHREAD */
#endif        /* EASY_PAPI2 || EASY_PAPI  */

    {
      /* impose user-specified max on events */
      int max_events = (gd->opts.max_events < max_papi_events ?
                        gd->opts.max_events : max_papi_events);
      gd->n_events = epapi_parse_events(gd->opts.events, 
                                        gd->event_names, 
                                        gd->g_event_codes, 
                                        max_events);
      mem_barrier();
      gd->epapi_gphase = epapi_phase_initialized;
    }
    return 1;
  }
}

/* return the pointer to the calling 
   thread's local data */

static epapi_tdata * 
epapi_thread_local_tdata(epapi_gdata * gd) {
  if (!gd) gd = epapi_global_gdata;
  if (!epapi_check_globally_initialized(gd)) {
    if (gd->epapi_gphase == epapi_phase_initialize_failed) {
      fprintf(stderr, 
              "epapi_thread_local_tdata called after PAPI failed to initialize\n");
    } else {
      fprintf(stderr, 
              "epapi_thread_local_tdata called before epapi_start()\n");
    }
    return 0;
  }
  {
#if EASY_PAPI_PTHREAD
    /* pthread apps */
    epapi_tdata * td;
    td = (epapi_tdata * )pthread_getspecific(gd->epapi_tdata_key);
    if (!td) {
      td = (epapi_tdata *)malloc(sizeof(epapi_tdata));
      pthread_setspecific(gd->epapi_tdata_key, td);
      epapi_init_tdata(td);
    }
#else
    /* non-pthreaded apps */
    static epapi_tdata td_[1];
    static epapi_tdata * td = 0;
    if (!td) {
      td = td_;
      epapi_init_tdata(td);
    }
#endif
    return td;
  }
}

/* insert td into the list of tdata in gd */
static void 
epapi_insert_tdata(epapi_gdata * gd, epapi_tdata * td) {
  epapi_tdata * volatile * head_p = &gd->tdata_list;
  //printf("insert %d\n", myth_get_worker_num());
  while (1) {
    epapi_tdata * head = *head_p;
    td->next = head;
    if (__sync_bool_compare_and_swap(head_p, head, td)) break;
  }
}

/* advance phase by one to indicate we are counting.
   if multiple thread call this at the same time,
   only one will actually increment it, ensuring the
   global state indicates we are counting. this call
   does not have an immediate effects on threads.
   each thread starts counting when it sees epapi_phase
   next time */
static int
epapi_start_globally_counting(epapi_options * opts, 
                              epapi_gdata * gd) {
  if (!gd) 
    gd = epapi_global_gdata;
  if (!epapi_ensure_globally_initialized(opts, gd)) {
    return  0;
  } else {
    volatile int * p = &gd->epapi_gphase;
    int gphase = *p;
    if ((gphase - epapi_phase_counting_0) % 2 == 0) {
      if (gd->opts.verbosity>=1) {
	fprintf(stderr, "epapi_start: already counting (ignored)\n");
      }
      return 1;
    } else {
      if (gd->opts.verbosity>=2) {
	printf("epapi start counting\n");
      }
      /* make sure only one will increment it */
      __sync_bool_compare_and_swap(p, gphase, gphase + 1);
      return 1;
    }
  }
}

/* advance phase by one to indicate we are not counting.
   if multiple thread call this at the same time,
   only one will actually increment it, ensuring the
   global state indicates we are not counting. this call
   does not have an immediate effects on threads.
   each thread stops counting when it sees epapi_phase
   next time */
static int
epapi_stop_globally_counting(epapi_options * opts, 
                             epapi_gdata * gd) {
  if (!gd) 
    gd = epapi_global_gdata;
  if (!epapi_ensure_globally_initialized(opts, gd)) {
    return  0;
  } else {
    volatile int * p = &gd->epapi_gphase;
    int gphase = *p;
    if ((gphase - epapi_phase_counting_0) % 2 == 1) {
      fprintf(stderr, "epapi_stop: already stopped (ignored)\n");
      return 1;
    } else {
      if (gd->opts.verbosity>=2) {
	printf("epapi stop counting\n");
      }
      /* make sure only one will increment it */
      __sync_bool_compare_and_swap(p, gphase, gphase + 1);
      return 1;
    }
  }
}


/* ensure thread-specific initialization has been performed 
   and td properly initialized.
   gd == 0 ==> use the internally-managed global data storage
   td == 0 ==> use the internally-managed thread-local data storage */
static int
epapi_ensure_thread_initialized(epapi_gdata * gd, epapi_tdata * td) {
  if (!gd) 
    gd = epapi_global_gdata;
  if (!td) {
    td = epapi_thread_local_tdata(gd);
    if (!td) 
      return epapi_phase_initialize_failed;
  }
  if (td->epapi_tphase != epapi_phase_not_initializing) {
    return td->epapi_tphase;
  }

  td->sampling_interval = gd->opts.sampling_interval;

#if EASY_PAPI2 || EASY_PAPI && EASY_PAPI_PTHREAD
  {
    int pe = PAPI_register_thread();
    if (pe != PAPI_OK) {
      epapi_show_error_(pe, __FILE__, __LINE__);
      td->epapi_tphase = epapi_phase_initialize_failed;
      return td->epapi_tphase;
    }
  }
#endif
  if (epapi_make_thread_eventset(gd->event_names, 
                                 gd->g_event_codes, 
                                 gd->n_events,
                                 td)) {
    epapi_insert_tdata(gd, td);
    td->epapi_tphase = epapi_phase_initialized;
  } else if (gd->n_events) {
    td->epapi_tphase = epapi_phase_initialize_failed;
  } else {
    td->epapi_tphase = epapi_phase_initialized;
  }
  return td->epapi_tphase;
}

/* ensure that the calling thread's understanding about the
   phase matches the global state. a thread calls this whenever
   epapi_read (and one of its friends) is called, to learn
   that somebody started/stopped counting recently */
static int
epapi_ensure_thread_phase(epapi_gdata * gd, epapi_tdata * td,
                          int gphase, long long * values) {
  if (gphase < epapi_phase_initialized) {
    if (gphase == epapi_phase_initialize_failed) {
      /* somebody attempted to initialize and failed */
      fprintf(stderr, 
              "error: epapi_ensure_thread_phase called after PAPI failed to initialize\n");
      exit(1);
    } else {
      /* a bogus value */
      fprintf(stderr, 
              "error: epapi_ensure_thread_phase called before epapi is not initialized\n");
      exit(1);
    }
    return 0;			/* NG */
  }
  {
    /* ensure the thread has performed thread-specific PAPI initialization */
    int p = epapi_ensure_thread_initialized(gd, td);
    if (p < epapi_phase_initialized) 
      return 0; /* NG */
    /* check if this thread's phase matches global's */
    assert(p <= gphase);
    /* yes, it already matches; nothing to do */
    if (p == gphase) return 1;
    /* this thread is behind global.
       make p either gphase - 2 or gphase - 1 */
    p = gphase - 2 + (gphase - p) % 2;
    assert(p == gphase - 2 || p == gphase - 1);
    td->epapi_tphase = gphase;
    for ( ; p < gphase; p++) {
      /* transition p -> p + 1 */
      if ((p - epapi_phase_counting_0) % 2 == 1) {
	/* phase p is not counting. 
	   change it to p + 1, counting */
        if (gd->opts.thread_start_hook) {
          if (!gd->opts.thread_start_hook(gd, td)) {
            return 0;                        /* NG */
          }
        }
        if (td->n_events) {
          int i;
          for (i = 0; i < td->n_events; i++) {
            values[i] = 0;
          }
#if EASY_PAPI2 || EASY_PAPI
          int pe = PAPI_start(td->eventset);
          if (pe != PAPI_OK) {
            epapi_show_error_(pe, __FILE__, __LINE__);
            return 0;
          }
#endif
        }
        td->next_sampling_clock = 0;
        td->start_tsc = epapi_get_tsc();
      } else {
	/* phase p is counting. 
	   change it to p + 1, not counting */
        td->clocks = epapi_get_tsc() - td->start_tsc;
        if (td->n_events) {
#if EASY_PAPI2 || EASY_PAPI
          int pe = PAPI_stop(td->eventset, values);
          if (pe != PAPI_OK) {
            epapi_show_error_(pe, __FILE__, __LINE__);
            return 0;
          }
#endif
        }
      }
    }
  }
  return 1;
}

/* ------------------------------
   external functions 
   ------------------------------ */

int
epapi_init_opts(epapi_options * opts) {
  /* set real defaults */
  *opts = epapi_options_default_values;
  /* overwrite some by env vars */
  if (getenv_str("EPAPI_EVENTS", &opts->events)
      || getenv_str("EP_EV", &opts->events)) {}
  if (getenv_ull("EPAPI_SAMPLING_INTERVAL", &opts->sampling_interval)
      || getenv_ull("EP_SI", &opts->sampling_interval)) {}
  if (getenv_int("EPAPI_MAX_EVENTS", &opts->max_events)
      || getenv_int("EP_MAX_EV", &opts->max_events)) {}
  if (getenv_int("EPAPI_VERBOSITY", &opts->verbosity)
      || getenv_int("EP_VERBOSITY", &opts->verbosity)
      || getenv_int("EPAPI_VERBOSE", &opts->verbosity)
      || getenv_int("EP_VERBOSE", &opts->verbosity)) {}
  return 1;
}


/* initialize global data gd */
int epapi_init_gdata(epapi_gdata * gd) {
  memset(gd, 0, sizeof(epapi_gdata));
  return 1;
}

/* initialize thread local data td */
int epapi_init_tdata(epapi_tdata * td) {
  memset(td, 0, sizeof(epapi_tdata));
  return 1;
}

/* globally initialize epapi */

int epapi_init_ex(epapi_options * opts, 
                  epapi_gdata * gd) {
  return epapi_ensure_globally_initialized(opts, gd);
}

int epapi_init() {
  return epapi_init_ex(0, 0);
}

static long long
epapi_interpolate(unsigned long long t0, long long v0, unsigned long long t1, long long v1, unsigned long long t) {
  double a = (double)(t - t0) / (double)(t1 - t0);
  double b = (double)(t1 - t) / (double)(t1 - t0);
  return (long long)(a * v1 + b * v0);  
}

/* read counters; assume td is not null */
int
epapi_read_ex(epapi_gdata * gd, epapi_tdata * td, 
              long long * values) {
  if (!gd) 
    gd = epapi_global_gdata;
  if (!td)
    td = epapi_thread_local_tdata(gd);
  if (!values) 
    values = td->values;
  if (!epapi_ensure_thread_phase(gd, td, gd->epapi_gphase, values)) 
    return 0;
  if ((td->epapi_tphase - epapi_phase_counting_0) % 2 == 0) {
    unsigned long long now = epapi_get_tsc() - td->start_tsc;
    td->clocks = now;
    if (td->n_events) {
      if (now < td->next_sampling_clock && td->t0 != 0) {
#if 0
        fprintf(stderr, 
                "skipped now = %llu, next = %llu, interval = %llu\n", 
                now, td->next_sampling_clock, td->sampling_interval);
#endif
        /* interpolate */
        int c;
        /*
        if (td->t0 == 0) {
          td->t0 = td->t1;
          for (c = 0; c < td->n_events; c++) {
            td->values0[c] = td->values1[c];
          }
        }
        */
        for (c = 0; c < td->n_events; c++) {
          values[c] = epapi_interpolate(td->t0, td->values0[c], td->t1, td->values1[c], now);
        }
      } else {
        td->next_sampling_clock = now + td->sampling_interval;
#if EASY_PAPI2 || EASY_PAPI
        int pe = PAPI_read(td->eventset, values);
        if (pe != PAPI_OK) {
          epapi_show_error_(pe, __FILE__, __LINE__);
          return 0;
        }
        /* interpolate */
        td->t0 = td->t1;
        td->t1 = now;
        int c;
        for (c = 0; c < td->n_events; c++) {
          td->values0[c] = td->values1[c];
          td->values1[c] = values[c];
        }
#endif
      }
    }
  }
  return 1;
}

/* read counters; assume td is not null */
int
epapi_read() {
  return epapi_read_ex(0, 0, 0);
}

int
epapi_start_ex(epapi_options * opts, 
               epapi_gdata * gd, epapi_tdata * td) {
  if (!epapi_start_globally_counting(opts, gd))
    return 0;
  return epapi_read_ex(gd, td, 0);
}

int epapi_start() {
  return epapi_start_ex(0, 0, 0);
}

int
epapi_stop_ex(epapi_options * opts, 
              epapi_gdata * gd, epapi_tdata * td) {
  if (!epapi_stop_globally_counting(opts, gd)) 
    return 0;
  return epapi_read_ex(gd, td, 0);
}

int
epapi_stop() {
  return epapi_stop_ex(0, 0, 0);
}

/* print the values of counters to stdout */
int
epapi_show_counters_thread_ex(epapi_gdata * gd, epapi_tdata * td) {
  if (!gd) 
    gd = epapi_global_gdata;
  if (!td) 
    td = epapi_thread_local_tdata(gd);
  if (td->n_events) {
    if ((td->epapi_tphase - epapi_phase_counting_0) / 2
        < (gd->epapi_gphase - epapi_phase_counting_0) / 2) {
      fprintf(stderr, "epapi_show: this thread has never read values in this counting phase\n");
    } else {
      printf("clocks : %lld\n", td->clocks);
      epapi_show_counts(td->t_event_codes, td->values, td->n_events);
    }
  }
  return 1;
}

int
epapi_show_counters_thread() {
  return epapi_show_counters_thread_ex(0, 0);
}

int epapi_show_counters_ex(epapi_gdata * gd) {
  epapi_tdata * td;
  if (!gd) 
    gd = epapi_global_gdata;
  for (td = gd->tdata_list; td; td = td->next) {
    if ((td->epapi_tphase - epapi_phase_counting_0) / 2
        < (gd->epapi_gphase - epapi_phase_counting_0) / 2) {
      continue;
    }
    assert((td->epapi_tphase - epapi_phase_counting_0) / 2
           == (gd->epapi_gphase - epapi_phase_counting_0) / 2);
    epapi_show_counters_thread_ex(gd, td);
  }
  return 1;
}

int epapi_show_counters() {
  return epapi_show_counters_ex(0);
}


/* accumulate all tdata in gd into td_sum */
int
epapi_sum_counters(epapi_gdata * gd, epapi_tdata * td_sum) {
  epapi_tdata * td;
  if (!gd) 
    gd = epapi_global_gdata;
  epapi_init_tdata(td_sum);
  {
    int nc = 0;
    for (td = gd->tdata_list; td; td = td->next, nc++) {
      if ((td->epapi_tphase - epapi_phase_counting_0) / 2
          < (gd->epapi_gphase - epapi_phase_counting_0) / 2) {
        continue;
      }
      assert((td->epapi_tphase - epapi_phase_counting_0) / 2
             == (gd->epapi_gphase - epapi_phase_counting_0) / 2);
      epapi_add(gd, td, td_sum);
    }
    return nc;
  }
}

/* add all tdata in gd and show it */
int
epapi_show_sum_counters_ex(epapi_gdata * gd) {
  epapi_tdata td_sum[1];
  int nc = epapi_sum_counters(gd, td_sum);
  printf("threads : %d\n", nc);
  epapi_show_counters_thread_ex(gd, td_sum);
  return 1;
}

int
epapi_show_sum_counters() {
  return epapi_show_sum_counters_ex(0);
}

int
epapi_get_num_events() {
  return epapi_global_gdata->n_events;
}
