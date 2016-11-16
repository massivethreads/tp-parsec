
#ifndef _TASK_LOCK_H_
#define _TASK_LOCK_H_

#include <stdlib.h>

//Very simple spin lock.

typedef struct {
  volatile long lock_value;
} task_lock_t;

int task_lock_init(task_lock_t* lock);
int task_lock_destroy(task_lock_t* lock);
void task_lock_lock(task_lock_t* lock);
void task_lock_unlock(task_lock_t* lock);

#endif // _TASK_LOCK_H_
