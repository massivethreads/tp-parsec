
#ifndef _TASK_LOCK_H_
#define _TASK_LOCK_H_

#include <stdlib.h>

//Very simple spin lock.

typedef long task_lock_t;

static void task_lock_init(task_lock_t* lock){
  lock = (task_lock_t)malloc(sizeof(task_lock_t));
}
static void task_lock_destroy(task_lock_t* lock){
  free(lock);
}
static void task_lock_lock(task_lock_t* lock) {
  while(!__sync_bool_compare_and_swap(lock, 0, 1))
    __sync_synchronize();
}
static void task_lock_unlock(task_lock_t* lock) {
  *lock = 0;
  __sync_synchronize();
}


#endif // _TASK_LOCK_H_
