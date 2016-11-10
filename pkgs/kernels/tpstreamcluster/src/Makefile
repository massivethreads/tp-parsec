# Makefile for streamcluster

PREFIX=${PARSECDIR}/pkgs/kernels/tpstreamcluster/inst/${PARSECPLAT}

TARGET=streamcluster
OBJS=streamcluster.o common.o

#OPTS += -O0 -g
#OPTS += -pg
OPTS += -DENABLE_TASK

#LINK_OPTS += -pg 
#LINK_OPTS += -ltbb

ifeq ($(USE_TBBMALLOC),1)
	OPTS += -DUSE_TBBMALLOC
	LINK_OPTS += -ltbbmalloc
endif

ifneq ($(version),task)
  $(error This benchmark only supports task versions (e.g., task_mth, task_tbb, task_qth).)
endif

all: $(OBJS)
	$(CXX) $(CXXFLAGS) $(OPTS) $(LDFLAGS) $(OBJS) $(LIBS) $(LINK_OPTS) -o $(TARGET)

include ${PARSECDIR}/common/common.mk

%.o : %.cpp
	$(CXX) $(CXXFLAGS) $(OPTS) -c $<

clean:
	rm -f $(OBJS) $(TARGET)

install:
	mkdir -p $(PREFIX)/bin
	cp -f $(TARGET) $(PREFIX)/bin/$(TARGET)
