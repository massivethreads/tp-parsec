
#ifeq "$(version)" "task"
#  include Makefile.task
#else
#  include Makefile.orig  
#endif

ifneq (,$(findstring ENABLE_TASK,$(CXXFLAGS)))
  include Makefile.task
else
  include Makefile.orig  
endif
