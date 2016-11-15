
#ifdef ENABLE_TASK
  #include "encoder_task.c"
#else
  #include "encoder_orig.c"
#endif