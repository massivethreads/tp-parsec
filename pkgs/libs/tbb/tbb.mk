#dir:=tbb42_20140601oss
dir:=tbb2017_20160916oss
tarball:=$(dir)_src.tgz

all: dir

download : $(tarball)

$(tarball):
	rm -f $(tarball)
	wget http://threadingbuildingblocks.org/sites/default/files/software_releases/source/$(tarball)

dir : $(tarball)
	rm -rf src
	tar xf $(tarball)
	mv $(dir) src

distclean:
	rm -rf src $(tarball)

