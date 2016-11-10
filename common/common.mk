# Makefile for common.cc

common_src=${PARSECDIR}/common/common.cc
common_obj?=common.o

$(common_obj) : $(common_src)
	$(CXX) $(CXXFLAGS) -c $(common_src) -o $(common_obj)
