#include <stdio.h>
#include <iostream>
#include <fstream>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <sys/resource.h>
#include <limits.h>
#include <sys/time.h>

#ifndef USE_TBBMALLOC
#define USE_TBBMALLOC 1
#endif

#ifndef GRAIN_SIZE
#define GRAIN_SIZE 50
#endif

#include <tbb/tbb.h>

#define USE_PFOR 1

#if USE_PFOR
  //use pfor
  #define PFOR_TO_ORIGINAL 1
  #define PFOR2_EXPERIMENTAL 1
  #include <tpswitch/tpswitch.h>
  #define PFOR2_REDUCE_EXPERIMENTAL 1
  #include "pfor_reduce.h"
#else
  //use mtbb::parallel_for and parallel_reduce, which don't support OpenMP and Cilkplus.
  #include <tpswitch/tpswitch.h>
  //#include <tbb/parallel_for.h>
  //#include <tbb/parallel_reduce.h>
  //#include "tbb/blocked_range.h"
  //#include "tbb/task_scheduler_init.h"
  #include <mtbb/parallel_for.h>
  #include <mtbb/parallel_reduce.h>
#endif

#define TBB_STEALER (tbb::task_scheduler_init::occ_stealer)
#define NUM_DIVISIONS (nproc)

#if USE_TBBMALLOC
#include <tbb/cache_aligned_allocator.h>
#endif

#ifdef ENABLE_PARSEC_HOOKS
#include <hooks.h>
#endif

using namespace std;

#define MAXNAMESIZE 1024 // max filename length
#define SEED 1
/* increase this to reduce probability of random error */
/* increasing it also ups running time of "speedy" part of the code */
/* SP = 1 seems to be fine */
#define SP 1 // number of repetitions of speedy must be >=1

/* higher ITER --> more likely to get correct # of centers */
/* higher ITER also scales the running time almost linearly */
#define ITER 3 // iterate ITER* k log k times; ITER >= 1

#define CACHE_LINE 32 // cache line in byte

/* this structure represents a point */
/* these will be passed around to avoid copying coordinates */
typedef struct {
  float weight;
  float * coord;
  long assign;  /* number of point where this one is assigned */
  float cost;  /* cost of that assignment, weight*distance */
} Point;

/* this is the array of points */
typedef struct {
  long num; /* number of points; may not be N if this is a sample */
  int dim;  /* dimensionality */
  Point * p; /* the array itself */
} Points;

static bool * switch_membership; //whether to switch membership in pgain
static bool * is_center; //whether a point is a center
static int * center_table; //index table of centers

static int nproc; //# of threads

static int env_grain_size;

#if USE_TBBMALLOC
tbb::cache_aligned_allocator<float> memoryFloat;
tbb::cache_aligned_allocator<Point> memoryPoint;
tbb::cache_aligned_allocator<long> memoryLong;
tbb::cache_aligned_allocator<int> memoryInt;
tbb::cache_aligned_allocator<bool> memoryBool;
tbb::cache_aligned_allocator<double> memoryDouble;
#endif


float dist(Point p1, Point p2, int dim);


/*** for parallel_for and parallel_reduce ***/

struct HizReduction {
private:
  double hiz;
public:
  Points * points;
  HizReduction(Points * points_) : hiz(0), points(points_) {}
  HizReduction(HizReduction & d, tbb::split) {hiz = 0; points = d.points;}

  void operator()(const tbb::blocked_range<int> & range) {
    double myhiz = 0;
    long ptDimension = points->dim;
    int begin = range.begin();
    int end = range.end();
    
    for (int kk = begin; kk != end; kk++) {
      myhiz += dist(points->p[kk], points->p[0], ptDimension) * points->p[kk].weight;
    }
    hiz += myhiz;
  }

  void join(HizReduction & d) {hiz += d.getHiz(); /*fprintf(stderr,"reducing: %lf\n",hiz);*/}
  double getHiz() { return hiz; }
};

struct CenterCreate {
  Points * points;
  CenterCreate (Points * p): points(p) {}
  
  void operator()(const tbb::blocked_range<int> & range) const {
    int begin = range.begin();
    int end = range.end();
    
    for(int k = begin; k != end; k++) {
      float distance = dist(points->p[k], points->p[0], points->dim);
      points->p[k].cost = distance * points->p[k].weight;
      points->p[k].assign = 0;
    }
  }

};

struct CenterOpen {
private:
  double total_cost;
public:
  Points * points;
  int i;
  int type; /*type=0: compute. type=1: reduction */
  CenterOpen(Points * p) : points(p), total_cost(0), type(0) {}
  CenterOpen(CenterOpen & rhs, tbb::split) {
    total_cost = 0;
    points = rhs.points;
    i = rhs.i;
    type = rhs.type;
  }

  void operator()(const tbb::blocked_range<int> & range) {
    int begin = range.begin();
    int end = range.end();

    if (type) {
      double local_total = 0.0;
      for(int k = begin; k != end; k++)
        local_total += points->p[k].cost;
      total_cost += local_total;
    } else {
      for(int k = begin; k != end; k++)  {
        float distance = dist(points->p[i], points->p[k], points->dim);
        if (i && distance * points->p[k].weight < points->p[k].cost)  {
          points->p[k].cost = distance * points->p[k].weight;
          points->p[k].assign = i;
        }
      }
    }
  }

  void join(CenterOpen & lhs) { total_cost += lhs.getTotalCost(); }
  double getTotalCost() { return total_cost; }
};


/*** for tbb::task ***/

void
center_table_count(int pid, int stride, Points * points, double * work_mem) {
  long bsize = points->num / ((NUM_DIVISIONS));
  long k1 = bsize * pid;
  long k2 = k1 + bsize;
  if (pid == (NUM_DIVISIONS) - 1)
    k2 = points->num;

  int count = 0;
  for (int i = k1; i < k2; i++) {
    if (is_center[i]) {
      center_table[i] = count++;
    }
  }
  work_mem[pid * stride] = count;
}

void
fix_center(int pid, int stride, Points * points, double * work_mem) {
  long bsize = points->num / ((NUM_DIVISIONS));
  long k1 = bsize * pid;
  long k2 = k1 + bsize;
  if (pid == (NUM_DIVISIONS) - 1)
    k2 = points->num;
    
  for (int i = k1; i < k2; i++) {
    if(is_center[i]) {
      center_table[i] += (int) work_mem[pid * stride];
    }
  }
}

void
lower_cost(int pid, int stride, Points * points, long x, double * work_mem, int K) {
  long bsize = points->num / ((NUM_DIVISIONS));
  long k1 = bsize * pid;
  long k2 = k1 + bsize;
  if (pid == (NUM_DIVISIONS) - 1)
    k2 = points->num;
  
  //my *lower* fields
  double * lower = &work_mem[pid * stride];
  double local_cost_of_opening_x = 0;
  double * cost_of_opening_x = &work_mem[pid * stride + K + 1];

  int i;
  for (i = k1; i < k2; i++) {
    float x_cost = dist(points->p[i], points->p[x], points->dim) * points->p[i].weight;
    float current_cost = points->p[i].cost;

    if ( x_cost < current_cost ) {

      // point i would save cost just by switching to x
      // (note that i cannot be a median,
      // or else dist(p[i], p[x]) would be 0)
        
      switch_membership[i] = 1;
      local_cost_of_opening_x += x_cost - current_cost;
        
    } else {
        
      // cost of assigning i to x is at least current assignment cost of i
        
      // consider the savings that i's **current** median would realize
      // if we reassigned that median and all its members to x;
      // note we've already accounted for the fact that the median
      // would save z by closing; now we have to subtract from the savings
      // the extra cost of reassigning that median and its members
      int assign = points->p[i].assign;
      lower[center_table[assign]] += current_cost - x_cost;
      //fprintf(stderr,"Lower[%d]=%lf\n",center_table[assign], lower[center_table[assign]]);
    }
  }
    
  *cost_of_opening_x = local_cost_of_opening_x;
}

void
center_close(int pid, int stride, Points * points, double * work_mem, int K, double z) {
  long bsize = points->num / ((NUM_DIVISIONS));
  long k1 = bsize * pid;
  long k2 = k1 + bsize;
  if (pid == (NUM_DIVISIONS) - 1)
    k2 = points->num;

  double * gl_lower = &work_mem[(NUM_DIVISIONS) * stride];
  double * cost_of_opening_x;
  int local_number_of_centers_to_close = 0;

  double * number_of_centers_to_close = &work_mem[pid * stride + K];
  cost_of_opening_x = &work_mem[pid * stride + K + 1];
    
  for (int i = k1; i < k2; i++) {
    if (is_center[i]) {
      double low = z;
      //aggregate from all threads
      for (int p = 0; p < (NUM_DIVISIONS); p++) {
        low += work_mem[center_table[i] + p * stride];
      }
      gl_lower[center_table[i]] = low;
      if (low > 0) {
        // i is a median, and
        // if we were to open x (which we still may not) we'd close i

        // note, we'll ignore the following quantity unless we do open x
        ++local_number_of_centers_to_close;
        *cost_of_opening_x -= low;
      }
    }
  }
  *number_of_centers_to_close = (double) local_number_of_centers_to_close;
}

void
save_money(int pid, int stride, Points * points, long x, double * work_mem) {
  long bsize = points->num / ((NUM_DIVISIONS));
  long k1 = bsize * pid;
  long k2 = k1 + bsize;
  if (pid == (NUM_DIVISIONS) - 1)
    k2 = points->num;

  double * gl_lower = &work_mem[(NUM_DIVISIONS) * stride];

  //  we'd save money by opening x; we'll do it
  int i;
  for (int i = k1; i < k2; i++) {
    bool close_center = gl_lower[center_table[points->p[i].assign]] > 0 ;
    if (switch_membership[i] || close_center) {
      // Either i's median (which may be i itself) is closing,
      // or i is closer to x than to its current median
      points->p[i].cost = points->p[i].weight * dist(points->p[i], points->p[x], points->dim);
      points->p[i].assign = x;
    }
  }
  for (int i = k1; i < k2; i++) {
    if (is_center[i] && gl_lower[center_table[i]] > 0) {
      is_center[i] = false;
    }
  }
  if (x >= k1 && x < k2) {
    is_center[x] = true;
  }
}

/********************************************/


/* shuffle points into random order */
void
shuffle(Points * points) {
  long i, j;
  Point temp;
  for (i = 0; i < points->num - 1; i++) {
    j = (lrand48() % (points->num - i)) + i;
    temp = points->p[i];
    points->p[i] = points->p[j];
    points->p[j] = temp;
  }
}

/* shuffle an array of integers */
void
intshuffle(int * intarray, int length) {
  long i, j;
  int temp;
  for (i = 0; i < length; i++) {
    j = (lrand48() % (length - i)) + i;
    temp = intarray[i];
    intarray[i] = intarray[j];
    intarray[j] = temp;
  }
}

/* compute Euclidean distance squared between two points */

//float dist(Point p1, Point p2, int dim) __attribute__ ((noinline));

float
dist(Point p1, Point p2, int dim) {
  int i;
  float result = 0.0;
  for (i = 0; i < dim; i++)
    result += (p1.coord[i] - p2.coord[i]) * (p1.coord[i] - p2.coord[i]);
  return result;
}

/* run speedy on the points, return total cost of solution */
float
pspeedy(Points * points, float z, long * kcenter) {
  static double totalcost;
  static bool open = false;
  static double * costs; //cost for each thread.
  static int i;


  /* create center at first point, send it to itself */
  {
    CenterCreate c(points);
    //int grain_size = points->num / ((NUM_DIVISIONS));
    //tbb::parallel_for(tbb::blocked_range<int>(0, points->num, env_grain_size), c);
    #if USE_PFOR
      pfor(0, points->num, 1, env_grain_size, [&c] (int f, int t) { c(tbb::blocked_range<int>(f, t, env_grain_size)); });
    #else
      mtbb::parallel_for<tbb::blocked_range<int>, CenterCreate>(tbb::blocked_range<int>(0, points->num, env_grain_size), c);
    #endif
  }
  *kcenter = 1;


  {
    int grain_size = points->num / ((NUM_DIVISIONS));
    double acc_cost = 0.0;
    CenterOpen c(points);
    for(i = 1; i < points->num; i++ )  {
      bool to_open = ((float) lrand48() / (float) INT_MAX) < (points->p[i].cost / z);
      if (to_open) {
        (*kcenter)++;
        c.i = i;
        //fprintf(stderr,"** New center for i=%d\n",i);
        //tbb::parallel_reduce(tbb::blocked_range<int>(0, points->num, env_grain_size), c);
        #if USE_PFOR
          pfor(0, points->num, 1, env_grain_size, [&c] (int f, int t) { c(tbb::blocked_range<int>(f, t, env_grain_size)); });
        #else
          mtbb::parallel_for<tbb::blocked_range<int>, CenterOpen>(tbb::blocked_range<int>(0, points->num, env_grain_size), c);
        #endif
      }
    }

    c.type = 1; /* Once last time for actual reduction */
    //tbb::parallel_reduce(tbb::blocked_range<int>(0, points->num, env_grain_size), c);
    #if USE_PFOR
      double c_total_cost = pfor_reduce(0, points->num, 1, env_grain_size,
                                          [&points] (int f, int t) -> double {
                                          int begin = f;
                                          int end = t;
                                          double local_total_cost = 0.0;
                                          for(int k = begin; k != end; k++)
                                            local_total_cost += points->p[k].cost;
                                          return local_total_cost;
                                        },
                                        [] (double a, double b) -> double {
                                          return a + b;
                                        });
      totalcost = z * (*kcenter);
      totalcost += c_total_cost;
    #else
      mtbb::parallel_reduce<tbb::blocked_range<int>, CenterOpen>(tbb::blocked_range<int>(0, points->num, env_grain_size), c);
      totalcost = z * (*kcenter);
      totalcost += c.getTotalCost();
    #endif
  }
  return totalcost;
}


/* For a given point x, find the cost of the following operation:
 * -- open a facility at x if there isn't already one there,
 * -- for points y such that the assignment distance of y exceeds dist(y, x),
 *    make y a member of x,
 * -- for facilities y such that reassigning y and all its members to x 
 *    would save cost, realize this closing and reassignment.
 * 
 * If the cost of this operation is negative (i.e., if this entire operation
 * saves cost), perform this operation and return the amount of cost saved;
 * otherwise, do nothing.
 */

/* numcenters will be updated to reflect the new number of centers */
/* z is the facility cost, x is the number of this point in the array 
   points */

//double
void
pgain(long x, Points * points, double z, long int * numcenters, double * result) {
  cilk_begin;
  int i;
  int number_of_centers_to_close = 0;

  static double * work_mem;
  static double gl_cost_of_opening_x;
  static int gl_number_of_centers_to_close;

  //each thread takes a block of working_mem.
  int stride = *numcenters + 2;

  //make stride a multiple of CACHE_LINE
  int cl = CACHE_LINE / sizeof(double);
  if (stride % cl != 0) { 
    stride = cl * (stride / cl + 1);
  }
  int K = stride - 2 ; // K==*numcenters
  
  //my own cost of opening x
  double cost_of_opening_x = 0;

  work_mem = (double *) calloc(stride * ((NUM_DIVISIONS) + 1), sizeof(double));
  
  gl_cost_of_opening_x = 0;
  gl_number_of_centers_to_close = 0;


  /*For each center, we have a *lower* field that indicates 
    how much we will save by closing the center. 
    Each thread has its own copy of the *lower* fields as an array.
    We first build a table to index the positions of the *lower* fields. 
  */

  int p;
  
  /*****  loopA() *****/
  {
    mk_task_group;
    for (p = 0; p < (NUM_DIVISIONS) - 1; p++)
      create_task0(spawn center_table_count(p, stride, points, work_mem));
    call_task(spawn center_table_count(p, stride, points, work_mem));
    wait_tasks;
    
    int accum = 0;
    for (p = 0; p < (NUM_DIVISIONS); p++) {
      int tmp = (int) work_mem[p * stride];
      work_mem[p * stride] = accum;
      accum += tmp;
    }
  }

  
  {
    mk_task_group;
    for (p = 0; p < (NUM_DIVISIONS) - 1; p++)
      create_task0(spawn fix_center(p, stride, points, work_mem));
    call_task(spawn fix_center(p, stride, points, work_mem));
    wait_tasks;
  }    

  /***************/

  //now we finish building the table. clear the working memory.
  memset(switch_membership, 0, points->num * sizeof(bool));
  memset(work_mem, 0, (NUM_DIVISIONS + 1) * stride * sizeof(double));

  /* loopB */
  {
    mk_task_group;
    for (p = 0; p < (NUM_DIVISIONS) - 1; p++)
      create_task0(spawn lower_cost(p, stride, points, x, work_mem, K));
    call_task(spawn lower_cost(p, stride, points, x, work_mem, K));
    wait_tasks;
  }    

  /* LoopC */
  {
    mk_task_group;
    for (p = 0; p < (NUM_DIVISIONS) - 1; p++)
      create_task0(spawn center_close(p, stride, points, work_mem, K, z));
    call_task(spawn center_close(p, stride, points, work_mem, K, z));
    wait_tasks;
  }    


  gl_cost_of_opening_x = z;
  //aggregate
  for (int p = 0; p < (NUM_DIVISIONS); p++) {
    gl_number_of_centers_to_close += (int) work_mem[p * stride + K];
    gl_cost_of_opening_x += work_mem[p * stride + K + 1];
  }

  // Now, check whether opening x would save cost; if so, do it, and
  // otherwise do nothing

  if (gl_cost_of_opening_x < 0) {

    /* loopD */
    //SaveMoneyTask &t = *new ( tbb::task::allocate_root() )  SaveMoneyTask(stride, points, x, work_mem);
    //tbb::task::spawn_root_and_wait(t);
    mk_task_group;
    for (p = 0; p < (NUM_DIVISIONS) - 1; p++)
      create_task0(spawn save_money(p, stride, points, x, work_mem));
    call_task(spawn save_money(p, stride, points, x, work_mem));
    wait_tasks;

    *numcenters = *numcenters + 1 - gl_number_of_centers_to_close;
    
  } else {
    
    gl_cost_of_opening_x = 0;  // the value we'll return
    
  }

  free(work_mem);

  //return -gl_cost_of_opening_x;
  *result = -gl_cost_of_opening_x;
  cilk_void_return;
}


/* facility location on the points using local search */
/* z is the facility cost, returns the total cost and # of centers */
/* assumes we are seeded with a reasonable solution */
/* cost should represent this solution's cost */
/* halt if there is < e improvement after iter calls to gain */
/* feasible is an array of numfeasible points which may be centers */

float
pFL(Points * points, int * feasible, int numfeasible, double z, long * k, double cost, long iter, double e) {
  long i;
  long x;
  double change;
  long numberOfPoints;

#if USE_TBBMALLOC
  double * changes = (double *) memoryDouble.allocate(iter * sizeof(double));
#else  
  double * changes = (double *) malloc(iter * sizeof(double));
#endif  
  
  change = cost;
  /* continue until we run iter iterations without improvement */
  /* stop instead if improvement is less than e */
  while (change / cost > 1.0 * e) {
    change = 0.0;
    numberOfPoints = points->num;
    /* randomize order in which centers are considered */    
    intshuffle(feasible, numfeasible);

    //mk_task_group;
    for (i = 0; i < iter; i++) {
      x = i % numfeasible;
      //fprintf(stderr,"Iteration %d z=%lf, change=%lf\n",i,z,change);
      //change += pgain(feasible[x], points, z, k);
      pgain(feasible[x], points, z, k, changes + i);
      //create_task0(spawn pgain(feasible[x], points, z, k, changes + i));
      //fprintf(stderr,"*** change: %lf, z=%lf\n",change,z);
    }
    //wait_tasks;
    for (i = 0; i < iter; i++)
      change += changes[i];
    cost -= change;
  }
#if USE_TBBMALLOC
  memoryDouble.deallocate(changes, sizeof(double));
#else
  free(changes);
#endif  

  return cost;
}


int
selectfeasible_fast(Points * points, int ** feasible, int kmin) {
  int numfeasible = points->num;
  if (numfeasible > (ITER * kmin * log((double) kmin)))
    numfeasible = (int) (ITER * kmin * log((double) kmin));
  *feasible = (int *) malloc(numfeasible * sizeof(int));
  
  float * accumweight;
  float totalweight;

  /* 
     Calcuate my block. 
     For now this routine does not seem to be the bottleneck, so it is not parallelized. 
     When necessary, this can be parallelized by setting k1 and k2 to 
     proper values and calling this routine from all threads ( it is called only
     by thread 0 for now ). 
     Note that when parallelized, the randomization might not be the same and it might
     not be difficult to measure the parallel speed-up for the whole program. 
   */
  //  long bsize = numfeasible;
  long k1 = 0;
  long k2 = numfeasible;

  float w;
  int l,r,k;

  /* not many points, all will be feasible */
  if (numfeasible == points->num) {
    for (int i = k1; i < k2; i++)
      (*feasible)[i] = i;
    return numfeasible;
  }
#if USE_TBBMALLOC
  accumweight = (float *) memoryFloat.allocate(sizeof(float) * points->num);
#else
  accumweight = (float *) malloc(sizeof(float) * points->num);
#endif

  accumweight[0] = points->p[0].weight;
  totalweight = 0;
  for (int i = 1; i < points->num; i++) {
    accumweight[i] = accumweight[i-1] + points->p[i].weight;
  }
  totalweight = accumweight[points->num - 1];

  for (int i = k1; i < k2; i++ ) {
    w = (lrand48() / (float) INT_MAX) * totalweight;
    //binary search
    l = 0;
    r = points->num - 1;
    if (accumweight[0] > w) {
      (*feasible)[i] = 0; 
      continue;
    }
    while (l + 1 < r ) {
      k = (l + r) / 2;
      if ( accumweight[k] > w )
        r = k;
      else
        l = k;
    }
    (*feasible)[i]=r;
  }

#if USE_TBBMALLOC
  memoryFloat.deallocate(accumweight, sizeof(float));
#else
  free(accumweight); 
#endif

  return numfeasible;
}

/* compute approximate kmedian on the points */
float
pkmedian(Points * points, long kmin, long kmax, long * kfinal, int pid, pthread_barrier_t * barrier) {
  cilk_begin;
  int i;
  double cost;
  double lastcost;
  double hiz, loz, z;

  static long k;
  static int * feasible;
  static int numfeasible;
  static double * hizs;


  //  hizs = (double*)calloc(nproc,sizeof(double));
  hiz = loz = 0.0;
  long numberOfPoints = points->num;
  long ptDimension = points->dim;

  //my block
  long bsize = points->num / nproc;
  long k1 = bsize * pid;
  long k2 = k1 + bsize;
  if (pid == nproc - 1) k2 = points->num;


  //fprintf(stderr,"Starting Kmedian procedure\n");
  //fprintf(stderr,"%i points in %i dimensions\n", numberOfPoints, ptDimension);

  //tbb::parallel_reduce(tbb::blocked_range<int>(0, points->num, env_grain_size), h);
  #if USE_PFOR
    double h_hiz = pfor_reduce(0, points->num, 1, env_grain_size,
                               [&points] (int f, int t) -> double {
	                         long ptDimension = points->dim;
                                 int begin = f;
                                 int end = t;
                                 double local_hiz = 0.0;
                                 for (int kk = begin; kk != end; kk++)
                                   local_hiz += dist(points->p[kk], points->p[0], ptDimension) * points->p[kk].weight;
                                 return local_hiz;
                               },
                               [] (double a, double b) -> double {
                                 return a + b;
                               });
    hiz = h_hiz;
  #else
    HizReduction h(points);
    mtbb::parallel_reduce<tbb::blocked_range<int>,HizReduction>(tbb::blocked_range<int>(0, points->num, env_grain_size), h);
    hiz = h.getHiz();
  #endif

  loz = 0.0;
  z = (hiz + loz) / 2.0;

  /* NEW: Check whether more centers than points! */
  if (points->num <= kmax) {
    /* just return all points as facilities */
    for (long kk = 0; kk < points->num; kk++) {
      points->p[kk].assign = kk;
      points->p[kk].cost = 0;
    }
    
    cost = 0;
    *kfinal = k;

    cilk_return(cost);
  }

  mk_task_group;

  shuffle(points);
  create_task1( cost, spawn [&](){ cost = pspeedy(points, z, &k); }() );
  wait_tasks;

  i=0;

  /* give speedy SP chances to get at least kmin/2 facilities */
  while ((k < kmin) && (i < SP)) {
	create_task1( cost, spawn [&](){ cost = pspeedy(points, z, &k); }() );
    wait_tasks;
    i++;
  }

  /* if still not enough facilities, assume z is too high */
  while (k < kmin) {
    if (i >= SP) {
      hiz = z;
      z = (hiz + loz) / 2.0;
      i=0;
    }
    
    shuffle(points);
    create_task1( cost, spawn [&](){ cost =  pspeedy(points, z, &k); }() );
    wait_tasks;
    i++;
  }

  /* now we begin the binary search for real */
  /* must designate some points as feasible centers */
  /* this creates more consistancy between FL runs */
  /* helps to guarantee correct # of centers at the end */

  numfeasible = selectfeasible_fast(points, &feasible, kmin);
  for (int i = 0; i < points->num; i++) {
    //fprintf(stderr,"\t-->is_center[%d]=true!\n",points->p[i].assign);
    is_center[points->p[i].assign] = true;
  }


  while (1) {
    mk_task_group;
    
    /* first get a rough estimate on the FL solution */
    lastcost = cost;
    create_task1( cost, spawn [&](){ cost = pFL(points, feasible, numfeasible, z, &k, cost, (long) (ITER * kmax * log((double) kmax)), 0.1); }() );
    wait_tasks;

    /* if number of centers seems good, try a more accurate FL */
    if (((k <= (1.1) * kmax) && (k >= (0.9) * kmin)) ||
        ((k <= kmax + 2) && (k >= kmin - 2))) {
      
      /* may need to run a little longer here before halting without
         improvement */
      create_task1( cost, spawn [&](){ cost = pFL(points, feasible, numfeasible, z, &k, cost, (long) (ITER * kmax * log((double) kmax)), 0.001); }() );
      wait_tasks;
    }

    if (k > kmax) {
      /* facilities too cheap */
      /* increase facility cost and up the cost accordingly */
      loz = z;
      z = (hiz + loz) / 2.0;
      cost += (z - loz) * k;
    }
    if (k < kmin) {
      /* facilities too expensive */
      /* decrease facility cost and reduce the cost accordingly */
      hiz = z;
      z = (hiz + loz) / 2.0;
      cost += (z - hiz) * k;
    }

    /* if k is good, return the result */
    /* if we're stuck, just give up and return what we have */
    if (((k <= kmax) && (k >= kmin)) || ((loz >= (0.999) * hiz))) {
      break;
    }

  }

  //  fprintf(stderr,"Cleaning up...\n");
  //clean up...
  free(feasible); 
  *kfinal = k;

  cilk_return(cost);
}

/* compute the means for the k clusters */
int
contcenters(Points * points) {
  long i, ii;
  float relweight;

  for (i = 0; i < points->num; i++) {
    /* compute relative weight of this point to the cluster */
    if (points->p[i].assign != i) {
      relweight = points->p[points->p[i].assign].weight + points->p[i].weight;
      relweight = points->p[i].weight / relweight;
      for (ii = 0; ii < points->dim; ii++) {
        points->p[points->p[i].assign].coord[ii] *= 1.0 - relweight;
        points->p[points->p[i].assign].coord[ii] += points->p[i].coord[ii] * relweight;
      }
      points->p[points->p[i].assign].weight += points->p[i].weight;
    }
  }
  
  return 0;
}

/* copy centers from points to centers */
void
copycenters(Points * points, Points * centers, long * centerIDs, long offset) {
  long i;
  long k;

  bool * is_a_median = (bool *) calloc(points->num, sizeof(bool));

  /* mark the centers */
  for (i = 0; i < points->num; i++) {
    is_a_median[points->p[i].assign] = 1;
  }

  k = centers->num;

  /* count how many  */
  for (i = 0; i < points->num; i++) {
    if (is_a_median[i]) {
      memcpy(centers->p[k].coord, points->p[i].coord, points->dim * sizeof(float));
      centers->p[k].weight = points->p[i].weight;
      centerIDs[k] = i + offset;
      k++;
    }
  }

  centers->num = k;

  free(is_a_median);
}

struct pkmedian_arg_t {
  Points * points;
  long kmin;
  long kmax;
  long * kfinal;
  int pid;
  pthread_barrier_t * barrier;
};

void *
localSearchSub(void * arg_) {
  pkmedian_arg_t * arg= (pkmedian_arg_t *) arg_;
  pkmedian(arg->points, arg->kmin, arg->kmax, arg->kfinal, arg->pid, arg->barrier);
  return NULL;
}

void
localSearch(Points * points, long kmin, long kmax, long * kfinal) {
  pkmedian_arg_t arg;
  arg.points = points;
  arg.kmin = kmin;
  arg.kmax = kmax;
  arg.pid = 0;
  arg.kfinal = kfinal;
  localSearchSub(&arg);
}


class PStream {
public:
  virtual size_t read( float * dest, int dim, int num ) = 0;
  virtual int ferror() = 0;
  virtual int feof() = 0;
  virtual ~PStream() {}
};

//synthetic stream
class SimStream : public PStream {
public:
  SimStream(long n_ ) {
    n = n_;
  }
  size_t read( float * dest, int dim, int num ) {
    size_t count = 0;
    for ( int i = 0; i < num && n > 0; i++ ) {
      for ( int k = 0; k < dim; k++ ) {
        dest[i * dim + k] = lrand48() / (float) INT_MAX;
      }
      n--;
      count++;
    }
    return count;
  }
  int ferror() {
    return 0;
  }
  int feof() {
    return n <= 0;
  }
  ~SimStream() {}
private:
  long n;
};

class FileStream : public PStream {
public:
  FileStream(char* filename) {
    fp = fopen( filename, "rb");
    if( fp == NULL ) {
      fprintf(stderr,"error opening file %s\n.",filename);
      exit(1);
    }
  }
  size_t read( float* dest, int dim, int num ) {
    return std::fread(dest, sizeof(float)*dim, num, fp); 
  }
  int ferror() {
    return std::ferror(fp);
  }
  int feof() {
    return std::feof(fp);
  }
  ~FileStream() {
    fprintf(stderr,"closing file stream\n");
    fclose(fp);
  }
private:
  FILE* fp;
};

void outcenterIDs( Points* centers, long* centerIDs, char* outfile ) {
  FILE* fp = fopen(outfile, "w");
  if( fp==NULL ) {
    fprintf(stderr, "error opening %s\n",outfile);
    exit(1);
  }
  int* is_a_median = (int*)calloc( sizeof(int), centers->num );
  for( int i =0 ; i< centers->num; i++ ) {
    is_a_median[centers->p[i].assign] = 1;
  }

  for( int i = 0; i < centers->num; i++ ) {
    if( is_a_median[i] ) {
      fprintf(fp, "%ld\n", centerIDs[i]);
      fprintf(fp, "%lf\n", centers->p[i].weight);
      for( int k = 0; k < centers->dim; k++ ) {
        fprintf(fp, "%lf ", centers->p[i].coord[k]);
      }
      fprintf(fp,"\n\n");
    }
  }
  fclose(fp);
}

void
streamCluster(PStream * stream, long kmin, long kmax, int dim, long chunksize, long centersize, char * outfile) {
  cilk_begin;
#if USE_TBBMALLOC
  float * block = (float *) memoryFloat.allocate(chunksize * dim * sizeof(float));
  float * centerBlock = (float *) memoryFloat.allocate(centersize * dim * sizeof(float));
  long * centerIDs = (long *) memoryLong.allocate(centersize * dim * sizeof(long));
#else
  float * block = (float *) malloc(chunksize * dim * sizeof(float));
  float * centerBlock = (float *) malloc(centersize * dim * sizeof(float));
  long * centerIDs = (long *) malloc(centersize * dim * sizeof(long));
#endif

  if (block == NULL) { 
    fprintf(stderr, "not enough memory for a chunk!\n");
    exit(1);
  }

  Points points;
  points.dim = dim;
  points.num = chunksize;
  points.p = 
#if USE_TBBMALLOC
    (Point *)memoryPoint.allocate(chunksize*sizeof(Point), NULL);
#else
    (Point *)malloc(chunksize*sizeof(Point));
#endif

  for( int i = 0; i < chunksize; i++ ) {
    points.p[i].coord = &block[i*dim];
  }

  Points centers;
  centers.dim = dim;
  centers.p = 
#if USE_TBBMALLOC
    (Point *)memoryPoint.allocate(centersize*sizeof(Point), NULL);
#else
    (Point *)malloc(centersize*sizeof(Point));
#endif
  centers.num = 0;

  for( int i = 0; i < centersize; i++ ) {
    centers.p[i].coord = &centerBlock[i*dim];
    centers.p[i].weight = 1.0;
  }

  long IDoffset = 0;
  long kfinal;
  while (1) {

    size_t numRead  = stream->read(block, dim, chunksize); 
    fprintf(stderr,"read %lu points\n",numRead);

    if( stream->ferror() || numRead < (unsigned int) chunksize && !stream->feof() ) {
      fprintf(stderr, "error reading data!\n");
      exit(1);
    }

    points.num = numRead;
    for ( int i = 0; i < points.num; i++ ) {
      points.p[i].weight = 1.0;
    }

#if USE_TBBMALLOC
    switch_membership = (bool*)memoryBool.allocate(points.num*sizeof(bool), NULL);
    is_center = (bool*)calloc(points.num,sizeof(bool));
    center_table = (int*)memoryInt.allocate(points.num*sizeof(int));
#else
    switch_membership = (bool*)malloc(points.num*sizeof(bool));
    is_center = (bool*)calloc(points.num,sizeof(bool));
    center_table = (int*)malloc(points.num*sizeof(int));
#endif

    //fprintf(stderr,"center_table = 0x%08x\n",(int)center_table);
    //fprintf(stderr,"is_center = 0x%08x\n",(int)is_center);

    mk_task_group;
    create_task2( points, kfinal, spawn localSearch( &points, kmin, kmax, &kfinal ) );
    wait_tasks;

    //fprintf(stderr,"finish local search\n");
    contcenters( &points ); /* sequential */
    if( kfinal + centers.num > centersize ) {
      //here we don't handle the situation where # of centers gets too large. 
      fprintf(stderr,"oops! no more space for centers\n");
      exit(1);
    }

    copycenters( &points, &centers, centerIDs, IDoffset ); /* sequential */
    IDoffset += numRead;

#if USE_TBBMALLOC
    memoryBool.deallocate(switch_membership, sizeof(bool));
    free(is_center);
    memoryInt.deallocate(center_table, sizeof(int));
#else
    free(is_center);
    free(switch_membership);
    free(center_table);
#endif

    if( stream->feof() ) {
      break;
    }
  }

  //finally cluster all temp centers
#if USE_TBBMALLOC
  switch_membership = (bool*)memoryBool.allocate(centers.num*sizeof(bool));
  is_center = (bool*)calloc(centers.num,sizeof(bool));
  center_table = (int*)memoryInt.allocate(centers.num*sizeof(int));
#else
  switch_membership = (bool*)malloc(centers.num*sizeof(bool));
  is_center = (bool*)calloc(centers.num,sizeof(bool));
  center_table = (int*)malloc(centers.num*sizeof(int));
#endif

  mk_task_group;
  create_task2( centers, kfinal, spawn localSearch( &centers, kmin, kmax, &kfinal ) ); // parallel
  wait_tasks;
  contcenters( &centers );
  outcenterIDs( &centers, centerIDs, outfile );
  cilk_void_return;
}

long parsec_usecs() {
   struct timeval t;
   gettimeofday(&t,NULL);
   return t.tv_sec * 1000000 + t.tv_usec;
}

int
main(int argc, char ** argv) {
  
  char * outfilename = new char[MAXNAMESIZE];
  char * infilename = new char[MAXNAMESIZE];
  long kmin, kmax, n, chunksize, clustersize;
  int dim;

#ifdef PARSEC_VERSION
#define __PARSEC_STRING(x) #x
#define __PARSEC_XSTRING(x) __PARSEC_STRING(x)
  //fprintf(stderr, "PARSEC Benchmark Suite Version "__PARSEC_XSTRING(PARSEC_VERSION)"\n");
  fprintf(stderr, "PARSEC Benchmark Suite Version %s \n", __PARSEC_XSTRING(PARSEC_VERSION));
  fflush(NULL);
#else
  fprintf(stderr, "PARSEC Benchmark Suite\n");
  fflush(NULL);
#endif //PARSEC_VERSION
#ifdef ENABLE_PARSEC_HOOKS
  __parsec_bench_begin(__parsec_streamcluster);
#endif

  if (argc < 10) {
    fprintf(stderr,"usage: %s k1 k2 d n chunksize clustersize infile outfile nproc\n", argv[0]);
    fprintf(stderr,"  k1:          Min. number of centers allowed\n");
    fprintf(stderr,"  k2:          Max. number of centers allowed\n");
    fprintf(stderr,"  d:           Dimension of each data point\n");
    fprintf(stderr,"  n:           Number of data points\n");
    fprintf(stderr,"  chunksize:   Number of data points to handle per step\n");
    fprintf(stderr,"  clustersize: Maximum number of intermediate centers\n");
    fprintf(stderr,"  infile:      Input file (if n<=0)\n");
    fprintf(stderr,"  outfile:     Output file\n");
    fprintf(stderr,"  nproc:       Number of threads to use\n");
    fprintf(stderr,"\n");
    fprintf(stderr, "if n > 0, points will be randomly generated instead of reading from infile.\n");
    exit(1);
  }

  kmin = atoi(argv[1]);
  kmax = atoi(argv[2]);
  dim = atoi(argv[3]);
  n = atoi(argv[4]);
  chunksize = atoi(argv[5]);
  clustersize = atoi(argv[6]);
  strcpy(infilename, argv[7]);
  strcpy(outfilename, argv[8]);
  nproc = atoi(argv[9]);

  env_grain_size = GRAIN_SIZE;
  char * str_grain_size = getenv("GRAIN_SIZE");
  if (str_grain_size) {
    env_grain_size = atol(str_grain_size);
  }
  printf("env_grain_size = %d\n", env_grain_size);
  
  srand48(SEED);
  PStream * stream;
  if( n > 0 ) {
    stream = new SimStream(n);
  }
  else {
    stream = new FileStream(infilename);
  }

#ifdef ENABLE_PARSEC_HOOKS
  __parsec_roi_begin();
#endif

  tp_init();
  
  double time = (double) parsec_usecs();
  
  streamCluster(stream, kmin, kmax, dim, chunksize, clustersize, outfilename);
  
  time = (((double) parsec_usecs()) - time) / 1000000.0;
  printf("kernel_execution_time = %lf seconds\n", time);

#ifdef ENABLE_PARSEC_HOOKS
  __parsec_roi_end();
#endif

  delete stream;

#ifdef ENABLE_PARSEC_HOOKS
  __parsec_bench_end();
#endif
  
  return 0;
}
