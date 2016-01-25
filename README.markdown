Task-parallel version of PARSEC
-------------------------------

# Original PARSEC

## How to build?
1. Download all files.
2. ```$ cd parsec/bin/```
3. ```parsec/bin$ ./parsecmgmt -a build -p all```
  * It takes 30 mins or so. Please wait a bit.

## How to run?
1. At first, download native input from the official page, or here: http://parsec.cs.princeton.edu/download/3.0/parsec-3.0-input-native.tar.gz
2. Uncompress it and overwrite all files.
3. ```$ cd parsec/bin/```
4. ```parsec/bin$ ./parsecmgmt -a run -p [all|blackscholes|bodytrack...] -n $THREAD_NUM```

-------------------------------

# Task parallel PARSEC

## Readme
1. It may (fully?) support ```parsecmgmt``` without any modification for now.
2. I only checked a Massivethreads version, but easily extend.
3. We use ```tp_switch.h```, ```compile.mk```, and ```urun``` mechanism.
4. It doesn't work for Cilk, because I didn't write cilk_return_void cilk_static, etc
5. It currenctly need some efforts to support icc.
6. A little tricky.
7. You must know the better solution. Please teach me...
 
## How to build and run?
1. Clone repository: ```git clone -b trial_blackscholes git@gitlab.eidos.ic.i.u-tokyo.ac.jp:parallel/tp-parsec.git```
2. Download a parallel2 repository in tp-parsec/toolkit/parallel2 ```checkout svn+ssh://vega/repos/parallel2```
3. Make & Make install
```
cd tp-parsec/toolkit/parallel2/sys/src/
make
make install
```

4. Build & Run
```
cd tp-parsec/bin
parallel2_dir={tp-parsec/toolkit/parallel2} ./parsecmgmt -a build -p blackscholes -c gcc-task_mth
parallel2_dir={tp-parsec/toolkit/parallel2} ./parsecmgmt -a run -p blackscholes -c gcc-task_mth -n 4
# for example
# parallel2_dir=~/tp-parsec/toolkit/parallel2 ./parsecmgmt -a build -p blackscholes -c gcc-task_mth -n 4
```

Looks very easy.

## Temporary rule I used
* Use ```tpswitch/tpswitch.h```.
* ```ENABLE_TASK``` is defined in the code.
* For makefile, task version is inserted into ```${target_task}``` (e.g., mth, omp, tbb ..., supported by compile.mk)
* parallel2's root directory is assigned into ```${parallel2_dir}```
* config convention is ```gcc-task_{target_task}``` (e.g., ```gcc-task_mth```)

## How did you add new task parallel system?
Currently, only supports Massivethreads version.
* Add BOTH ```tp-parsec/config/gcc-task_{target_task}.bldconf``` and ```{application}/config/gcc-task_{target_task}.bldconf```
Please check the Massivethreads version.

## How did you write Makefile?
* Copy original ```Makefile``` to ```Makefile.orig```
* Rewrite ```Makefile``` as follows: 

```
ifeq "$(version)" "task"
  include Makefile.task
else
  include Makefile.orig
endif
```
*  Write ```Makefile.task``` by modifying ```Makefile``` as follows:

Key points are:
 - Set default_all, exe_prefix etc... for ```compile.mk```
 - Write ```clean``` and ```install```
 - Declare ENABLE_TASK
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
icc_dir       ?= ${ICC_DIR}
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
        echo "\$${parallel2_dir}/sys/inst/g/bin/urun -t ${task_target} -p \$$1 -f g \$$(dirname \$$0)/$(TARGET)_${task_target} \$$1 \$$2 \$$3" > $(app_root)/inst/${PARSECPLAT}/bin/$(TARGET)
        chmod 775 $(app_root)/inst/${PARSECPLAT}/bin/$(TARGET)
```

## How did you rewrite source codes?

Please see ```blackscholes.c``` for example, especially around ```ENABLE_TASK```.
Just ``` #include <tpswitch/tpswitch.h> ``` works well.

## Tips

* You can clean the binaries by ```-a uninstall```


