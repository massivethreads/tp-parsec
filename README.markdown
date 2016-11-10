Task-parallel version of PARSEC
-------------------------------

# Original PARSEC

## How to build?
* 1. Download all files.
* 2. ```$ cd parsec/bin/```
* 3. ```parsec/bin$ ./parsecmgmt -a build -p all```
  * It takes 30 mins or so. Please wait a bit.

## How to run?
* 1. At first, download native input from the official page, or here: http://parsec.cs.princeton.edu/download/3.0/parsec-3.0-input-native.tar.gz
* 2. Uncompress it and overwrite all files.
* 3. ```$ cd parsec/bin/```
* 4. ```parsec/bin$ ./parsecmgmt -a run -p [all|blackscholes|bodytrack...] -n $THREAD_NUM -i [test|simdev|...|native]```

-------------------------------

# Task parallel PARSEC

I completed implementation of all task versions of ```blackscholes```.

I've checked the correctness of its transformation.

## Readme
* 1. It may (fully?) support ```parsecmgmt``` without any modification for now.
* 2. I checked Massivethreads (icc/gcc), Intel TBB (icc/gcc), OpenMP (icc/gcc), QThreads (icc/gcc), and CilkPlus (icc only) version.
* 3. We use ```common.h```, ```tpswitch.h```, ```compile.mk```, and ```urun``` mechanism.
* 4. It currenctly needs some efforts to support icc.
* 5. A little tricky.
* 6. If you know better solution. please tell us.

## How to build and run?
* 1. Clone repository: ```git clone git@gitlab.eidos.ic.i.u-tokyo.ac.jp:parallel/tp-parsec.git```
* 2. Download a parallel2 repository in tp-parsec/toolkit/parallel2: ```svn checkout svn+ssh://vega/repos/parallel2```
* 3. Make & Make install

```
tp-parsec$ cd toolkit/parallel2/sys/src/
src$ make
src$ make install
```

* 4. (Optional) If you want to create icc version, type following commands.

```
# icc is located in /opt/intel/ for some environments (e.g. magellan).
tp-parsec$ cd toolkit/parallel2/sys/src/
src$ PATH=$PATH:/opt/intel/bin/ make platform=i
src$ PATH=$PATH:/opt/intel/bin/ make platform=i install
```

* 5. Build & Run

```
tp-parsec$ cd bin
bin$ parallel2_dir={tp-parsec/toolkit/parallel2} ./parsecmgmt -a build -p blackscholes -c gcc-task_mth
bin$ parallel2_dir={tp-parsec/toolkit/parallel2} ./parsecmgmt -a run -p blackscholes -c gcc-task_mth -n 4
# if icc is available.
bin$ parallel2_dir={tp-parsec/toolkit/parallel2} CC_HOME={/opt/intel} ./parsecmgmt -a build -p blackscholes -c icc-task_cilkplus
bin$ parallel2_dir={tp-parsec/toolkit/parallel2} CC_HOME={/opt/intel} ./parsecmgmt -a run -p blackscholes -c icc-task_cilkplus -n 4

# Concrete examples:
bin$ parallel2_dir=~/tp-parsec/toolkit/parallel2 ./parsecmgmt -a build -p blackscholes -c gcc-task_mth
bin$ parallel2_dir=~/tp-parsec/toolkit/parallel2 ./parsecmgmt -a run -p blackscholes -c gcc-task_mth -n 4
bin$ parallel2_dir=~/tp-parsec/toolkit/parallel2 CC_HOME=/opt/intel ./parsecmgmt -a build -p blackscholes -c icc-task_cilkplus
bin$ parallel2_dir=~/tp-parsec/toolkit/parallel2 CC_HOME=/opt/intel ./parsecmgmt -a run -p blackscholes -c icc-task_cilkplus -n 4
```

* 6. (Optional) add larger inputs

```
# download native-size input.
tp-parsec$ wget http://parsec.cs.princeton.edu/download/3.0/parsec-3.0-input-native.tar.gz
tp-parsec$ tar xvzf parsec-3.0-input-native.tar.gz
tp-parsec$ rsync -a parsec-3.0/* .
tp-parsec$ rm -r parsec-3.0
# for evaluation
# bin$ parallel2_dir={tp-parsec/toolkit/parallel2} CC_HOME={/opt/intel} ./parsecmgmt -a run -p blackscholes -c icc-task_cilkplus -n 4 -i native
```

## Temporary Conventions
* Use ```tpswitch/tpswitch.h```.
* ```ENABLE_TASK``` is defined in the code for task version. Use ```#ifdef```.
* For makefile, task version is inserted into ```${target_task}``` (e.g., mth, omp, tbb, qth, cilkplus are supported by compile.mk)
 * Now everything except simple Cilk works well (icc/gcc-mth/omp/tbb/qth & icc-cilkplus)
* parallel2's root directory is assigned into ```${parallel2_dir}```
* A name convention of config files is ```{compiler}-task_{target_task}``` (e.g., ```gcc-task_mth```)
* ```g``` is assigned into ```${platform}``` for gcc compilation, while ```i``` is for icc in Makefile.
* ```${CC_HOME}``` is an environment variable storing a path to icc. PARSEC seems to use this convention.

## How to evaluate correctness of code transformation?

There seems no common way to check correctness of the output.
For instance, blackscholes employs a chk_err flag, but bodytrack does nothing.

It is strongly demanded to develop methods to check it.

## How did you add new task parallel system?

* Add ```{application}/config/gcc-task_{target_task}.bldconf```
* Also add ```tp-parsec/config/gcc-task_{target_task}.bldconf``` unless it exists.

## How did you write Makefile?
* 1. Copy original ```Makefile``` to ```Makefile.orig```
* 2. Rewrite ```Makefile``` as follows:

```
ifeq "$(version)" "task"
  include Makefile.task
else
  include Makefile.orig
endif
```
* 3. Write ```Makefile.task``` by modifying ```Makefile``` as follows:

Key points are:
 - Set default_all, exe_prefix etc... for ```compile.mk```
 - Write ```clean``` and ```install```
 - Declare ```ENABLE_TASK```
 - Create wrapper for ```urun``` (echo *** in ```install```)

```
PREFIX=${PARSECDIR}/pkgs/apps/blackscholes/inst/${PARSECPLAT}

CSRC    = $(SRC)
TARGET  = blackscholes
M4_BASE = .
MACROS  = c.m4.pthreads

ifdef source
        ifeq "$(source)" "simd"
                SRC = blackscholes.simd.c
                CXXFLAGS += -msse3
        endif
else
        SRC = blackscholes.c
endif

# Default build single precision version
NCO     = -DNCO=4 -Dfptype=float

ifdef chk_err
ERR     = -DERR_CHK
endif

ifdef single
NCO = -DNCO=4 -Dfptype=float
endif

ifdef size
SZ = -DN=$(size)
else
SZ = -DN=960
endif

ifdef double
NCO = -DNCO=2 -Dfptype=double
endif

app_root       = ${PARSECDIR}/pkgs/apps/blackscholes
icc_dir       ?= ${CC_HOME}
icc           ?= ${icc_dir}/compiler/bin/icc
default_all    : ${task_target}_exes
src_dir       ?= $(app_root)/src
exe_prefix    ?= $(app_root)/inst/${PARSECPLAT}/${TARGET}
obj_dir       ?= $(app_root)/obj/${PARSECPLAT}

c_srcs   :=
cxx_srcs := blackscholes.cxx
cxx_opts := $(CXXFLAGS) $(MT) $(NCO) $(FUNC) $(ERR) -DENABLE_TASK
targets  := blackscholes

include $(parallel2_dir)/sys/src/tools/makefiles/compile.mk


clean:
        rm -f -r $(app_root)/inst/${PARSECPLAT}/*

install:
        mkdir -p $(app_root)/inst/${PARSECPLAT}/bin
        mv -f $(exe_prefix)* $(app_root)/inst/${PARSECPLAT}/bin/$(TARGET)_${task_target}
        echo "\$${parallel2_dir}/sys/inst/${platform}/bin/urun -t ${task_target} -p \$$1 -f ${platform} -- \$$(dirname \$$0)/$(TARGET)_${task_target} \$$1 \$$2 \$$3" > $(app_root)/inst/${PARSECPLAT}/bin/$(TARGET)
        chmod 775 $(app_root)/inst/${PARSECPLAT}/bin/$(TARGET)
```

## How did you rewrite source codes?

Please look at ```blackscholes.c``` for example, especially around ```ENABLE_TASK```.

Don't forget to ...
* add ```cilk_begin``` and ```cilk_void_return```.
* include  ```#include <tpswitch/tpswitch.h>``` (```#include <common.h>``` is no more needed)
* write ```init_task_runtime();``` at the beginning of ```main()```, surrounded by ```#ifdef ENABLE_TASK```

## How to live together with CMake and compile.mk?

At present, I have no good idea. An example of ```raytrace``` shows how I struggled to forcedly combine them.

## Tips

* You can clean the binaries by ```-a uninstall```
