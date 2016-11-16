
#include "task_lock.h"

//Very simple spin lock.

int task_lock_init(task_lock_t* lock){
  lock->lock_value = 0;
  return 0;
}
int task_lock_destroy(task_lock_t* lock){
  return 0;
}
void task_lock_lock(task_lock_t* lock) {
  while(!__sync_bool_compare_and_swap(&lock->lock_value, 0, 1))
    __sync_synchronize();
}
void task_lock_unlock(task_lock_t* lock) {
  lock->lock_value = 0;
  __sync_synchronize();
}
