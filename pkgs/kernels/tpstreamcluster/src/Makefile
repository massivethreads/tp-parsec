# Makefile for streamcluster

PREFIX=${PARSECDIR}/pkgs/kernels/tpstreamcluster/inst/${PARSECPLAT}

TARGET=streamcluster
OBJS=streamcluster.o

OPTS += -DENABLE_TASK -save-temps

ifeq ($(USE_TBBMALLOC),1)
	OPTS += -DUSE_TBBMALLOC
	LINK_OPTS += -ltbbmalloc
endif

ifneq ($(version),task)
  $(error This benchmark only supports task versions (e.g., task_mth, task_tbb, task_qth).)
endif

all: $(OBJS)
	$(CXX) $(CXXFLAGS) $(OPTS) $(LDFLAGS) $(OBJS) $(LIBS) $(LINK_OPTS) -o $(TARGET)

%.o : %.cpp
	$(CXX) $(CXXFLAGS) $(OPTS) -c $<

clean:
	rm -f $(OBJS) $(TARGET)

install:
	mkdir -p $(PREFIX)/bin
	cp -f $(TARGET) $(PREFIX)/bin/$(TARGET)
