# Because TBB's top-level Makefile does not have "make install",
# include this file at the end of ./src/Makefile so that PARSEC's parsecmgmt
# can know how to install this TBB package into appropriate directories,
# e.g., "include ../makeinstall.mk".
# This action should be taken every time a new TBB tarball is downloaded
# and extracted into ./src by tbb.mk.

inst_dir=${PARSECDIR}/pkgs/libs/tbb/inst/${PARSECPLAT}
build_dir=${PARSECDIR}/pkgs/libs/tbb/obj/${PARSECPLAT}
install:
	mkdir -p $(inst_dir)/lib
	cp -f $(work_dir)_release/libtbb*.so* $(inst_dir)/lib
	mkdir -p $(inst_dir)/include
	cp -rf $(build_dir)/include/* $(inst_dir)/include/
