Task-Parallel PARSEC
-------------------------------

The original [PARSEC](http://parsec.cs.princeton.edu) (Princeton Application Repository for Shared-Memory Computers) developed by Princeton University is a large benchmark suite containing more than 13 parallel applications of various kinds of emerging workloads. These parallel applications are multithreaded based on one of the three programming models of Pthreads, OpenMP, and Intel TBB. Each of them can currently be parallelized by one, two or all three models; you can know which application is already parallelized by which models by taking a look at the overview table on PARSEC's [Wiki page](http://wiki.cs.princeton.edu/index.php/PARSEC). The three versions of the same application (if exists) reside in the same code base and are switched between each others by the define flags of ```-DENABLE_THREADS``` (Pthreads), ```-DENABLE_OPENMP``` (OpenMP), and ```-DENABLE_TBB``` (TBB).

In this project, we aim to parallelize PARSEC into multiple task programming models which use **task parallelism** (i.e., lightweight user-level threads) instead of the heavy-weight OS threads like Pthreads, or the restrictive parallel for loops like OpenMP's parallel for. We currently support five different task parallel programming systems: [Cilk Plus](https://www.cilkplus.org/), [OpenMP Task](http://www.openmp.org/), [Intel TBB](https://www.threadingbuildingblocks.org/), [MassiveThreads](https://github.com/massivethreads/massivethreads), and [Qthreads](https://github.com/Qthreads/qthreads). By defining a thin generic API layer covering all these five underlying systems, we can simplify our conversion. We just need to write code once using the generic task parallel primitives, then the program can be compiled automatically into all supported underlying systems. The generic layer is called ```tpswitch```, and it is published in our [MassiveThreads](https://github.com/massivethreads/massivethreads) distribution.

Following is a short description about the original PARSEC and then our task-parallel PARSEC (TP-PARSEC), how it is different from the original one, how to actually build and use it.

-------------------------------

# Original PARSEC

1. Download: you can either
 * download [all-in-one package](http://parsec.cs.princeton.edu/download/3.0/parsec-3.0.tar.gz) (2.9 GB) which contains everything
 * or download separately (1) the [core package](http://parsec.cs.princeton.edu/download/3.0/parsec-3.0-core.tar.gz) (112 MB) which excludes input data files for benchmarks, (2) [simulation input package](http://parsec.cs.princeton.edu/download/3.0/parsec-3.0-input-sim.tar.gz) (468 MB), and (3) [native input package](http://parsec.cs.princeton.edu/download/3.0/parsec-3.0-input-native.tar.gz) (2.3 GB). The three separate packages have the same directory structure so you can just simply extract them to the same place and they will fit to each other.

2. Build: everything (build, clean, run of benchmarks) is controlled by a central bash script ```parsecmgmt``` in ```parsec/bin/```. Three main options among others to pass to ```parsecmgmt``` are:
 * ```-a``` (action): to specify what to do, e.g., 'build', 'clean', 'uninstall', 'run'.
 * ```-p``` (package): to specify which benchmark or library package to apply the action to, e.g., 'blackscholes', 'canneal', 'freqmine'.
 * ```-c``` (config): to specify the configuration to build or run the package, e.g., 'gcc', 'gcc-pthreads', 'icc-tbb'.

3. Run: when the action is 'run', you need to (basically) specify two more options: the input to use (```-i```) and the number of threads to run on (```-n```) in order for ```parsecmgmt``` to run the benchmark.

4. Some examples:
  * How to build blackscholes' pthreads version using gcc?
```
parsec/bin $ ./parsecmgmt -a build -p blackscholes -c gcc-pthreads
```
  * How to run freqmine's icc-tbb build on 8 cores with simdev input?
```
parsec/bin $ ./parsecmgmt -a run -p freqmine -c gcc-pthreads -i simdev -n 8
```
  * Or you can build all by: (it takes 30 mins or so, please wait a bit)
```
parsec/bin $ ./parsecmgmt -a build -p all
```

-------------------------------

# Task parallel PARSEC

The repository of our TP-PARSEC (task parallel PARSEC) is on the [internal Gitlab](https://gitlab.eidos.ic.i.u-tokyo.ac.jp/parallel/tp-parsec). Our TP-PARSEC is based on PARSEC 3.0, the latest version. It is equivalent to the PARSEC's core package, which means it does not include simulation inputs and native inputs for the benchmark programs, you need to download them separately via the links above.

## A quick start

1. Clone the repository: ```git clone git@gitlab.eidos.ic.i.u-tokyo.ac.jp:parallel/tp-parsec.git```
2. Download simulation inputs: ```wget http://parsec.cs.princeton.edu/download/3.0/parsec-3.0-input-sim.tar.gz```
3. Extract simulation inputs: ```tar xvzf parsec-3.0-input-sim.tar.gz```
4. Import simulation inputs: ```rsync -a parsec-3.0/* tp-parsec/```
5. (can do this later) Do the same steps 2, 3, 4 for the [native inputs](http://parsec.cs.princeton.edu/download/3.0/parsec-3.0-input-native.tar.gz).
6. Jump into ```tp-parsec``` and initialize/update the three submodules of TP-PARSEC (MassiveThreads, Qthreads, PAPI):
```
tp-parsec $ git submodule update --init pkgs/libs/mth/src
tp-parsec $ git submodule update --init pkgs/libs/qth/src
tp-parsec $ git submodule update --init pkgs/libs/papi/src
tp-parsec $ git submodule update --init toolkit/tp_parsec/mth_tpswitch
```
7. Jump into ```bin```, try building some benchmark and run it:
```
tp-parsec/bin $ ./parsecmgmt2 -a build -p blackscholes -c gcc-task_mth
tp-parsec/bin $ ./parsecmgmt2 -a run -p blackscholes -c gcc-task_mth -i simlarge -n 8
```
8. You're set! Do whatever you want. You can build all benchmarks at once by (which will definitely take time):
```
tp-parsec/bin $ ./parsecmgmt2 -a build -p all -c gcc-task_mth
```

It can be summarized as following.

```
#Install TP-PARSEC
git clone git@gitlab.eidos.ic.i.u-tokyo.ac.jp:parallel/tp-parsec.git
#To install sim input
wget http://parsec.cs.princeton.edu/download/3.0/parsec-3.0-input-sim.tar.gz
tar xvzf parsec-3.0-input-sim.tar.gz
rsync -a --remove-source-files parsec-3.0/* tp-parsec/
rm -r parsec-3.0 parsec-3.0-input-sim.tar.gz
##To install native input
#wget http://parsec.cs.princeton.edu/download/3.0/parsec-3.0-input-native.tar.gz
#tar xvzf parsec-3.0-input-native.tar.gz
#rsync -a --remove-source-files parsec-3.0/* tp-parsec/
#rm -r parsec-3.0 parsec-3.0-input-native.tar.gz
cd tp-parsec
git submodule update -i

#Run something
cd bin
#Run blackscholes with simlarge on 8 cores.
./parsecmgmt2 -a build run -p blackscholes -c gcc-task_mth -i simlarge -n 8
#Specify a library path for icc (if necessary)
LD_LIBRARY_PATH=/opt/intel/lib/intel64 ./parsecmgmt2 -a build run -p blackscholes -c icc-task_mth -i simlarge -n 8
```

## How is it different from the original PARSEC?

### 1. 'parsecmgmt2'
We have implemented an improved version of the central control script **'parsecmgmt2'** (```tp-parsec/bin/parsecmgmt2```) which supports new build configurations and build-config-sourcing mechanisms for task versions together with other improvements, while still maintaining things that parsecmgmt can do.
  * task\_mth: MassiveThreads
  * task\_tbb: Intel TBB
  * task\_qth: Qthreads
  * task\_omp: OpenMP
  * task\_cilkplus: Cilk Plus
  * task\_serial: serial version (disable all generic task primitives)

### 2. MassiveThreads, Qthreads, PAPI added as submodules
MassiveThreads, Qthreads, and PAPI have been added as submodules in the TP-PARSEC git repository. They are located in 'libs' group ('pkgs/libs'). There locations and URLs of linked remote repositories are store in ```tp-parsec/.gitmodules``` by Git.

```
[submodule "mth"]
	path = pkgs/libs/mth/src
	url = https://github.com/massivethreads/massivethreads.git
[submodule "qth"]
	path = pkgs/libs/qth/src
	url = https://github.com/Qthreads/qthreads.git
[submodule "papi"]
	path = pkgs/libs/papi/src
	url = https://bitbucket.org/icl/papi.git
```

When TP-PARSEC is first cloned, these three submodules need to be initialized and pulled down first so that ```parsecmgmt2``` can use them to compile task parallel versions of benchmarks.

```
tp-parsec $ git submodule update --init pkgs/libs/mth/src
tp-parsec $ git submodule update --init pkgs/libs/qth/src
tp-parsec $ git submodule update --init pkgs/libs/papi/src
tp-parsec $ git submodule update --init toolkit/tp_parsec/mth_tpswitch
```

Note that ```git submodule update``` does not pull the latest source code of the submodule from its remote repository, but only the revision (commit) registered in the supermodule TP-PARSEC. If you want to use a submodule latest source code, go straight to its path (e.g., ```pkgs/libs/mth/src``` for MassiveThreads) and run ```git pull``` directly.


### 3. Multiple actions
'parsecmgmt2' supports **multiple actions** specified by the option ```-a```, e.g., ```-a uninstall build``` is legitimate and effective now, the action 'uninstall' will be done first then the action 'build' will be carried on.

### 4. New bldconf(s)
* **New global build configuration files** are added in ```tp-parsec/config/``` in order to provide system-specific compilation flags (CFLAGS, CXXFLAGS) and link options (LDFLAGS, LIBS) for ```parsecmgmt2``` to compile the program into corresponding executables.

 * tp-parsec/config/task.bldconf
 * tp-parsec/config/task\_cilkplus.bldconf
 * tp-parsec/config/task\_mth.bldconf
 * tp-parsec/config/task\_omp.bldconf
 * tp-parsec/config/task\_qth.bldconf
 * tp-parsec/config/task\_serial.bldconf
 * tp-parsec/config/task\_tbb.bldconf

* 'task.bldconf' contains **common options** for task versions, and 'task_mth.bldconf', for example, contains **options specific to** MassiveThreads task version.

* ```parsecmgmt2``` splits the configuration string specified by the '-c' option into sub-strings by hyphens, and sources all '.bldconf' files corresponding to these sub-strings. Especially in case of task versions, an additional ```task.bldconf``` file will be sourced right before the actual ```task_xxx.bldconf``` sub-configuration file is sourced. For examples, for a configuration of ```gcc-task_mth-dr``` ```parsecmgmt2``` will source four **global** configuration files of ```gcc.bldconf```, ```task.bldconf```, ```task_mth.bldconf```, ```dr.bldconf``` **in turn**, then it will continue to source four respective **local** configuration files in the benchmark's ```parsec``` sub-directory. For the original versions rather than 'task' (pthreads, openmp, tbb, serial), ```parsecmgmt2``` maintains the way how ```parsecmgmt``` does which is to source only one single global '.bldconf' file and one single local '.bldconf' file (e.g., ```tp-parsec/config/gcc-pthreads.bldconf``` and ```tp-parsec/pkgs/apps/blackscholes/parsec/gcc-pthreads.bldconf``` for the configuration of ```-c gcc-pthreads```).

 * e.g., in ```gcc.bldconf```: ```platform``` is assigned to ```g``` among other things
 * e.g., in ```icc.bldconf```: ```platform``` is assigned to ```i``` among other things
 * e.g., in ```task.bldconf```: ```version``` is assigned to ```task``` among other things
 * e.g., in ```task_mth.bldconf```: ```task_target``` is assigned to ```mth``` among other things
 * e.g., in ```task_qth.bldconf```: ```task_target``` is assigned to ```qth``` among other things

### 5. DAG Recorder
'parsecmgmt2' also supports **DAG Recorder**. By appending '-dr' to the usual config ('gcc-task\_mth' -> 'gcc-task\_mth-dr'), we can demand 'parsecmgmt2' to compile the corresponding task version together with DAG Recorder (```... -DDAG_RECORDER=2 ... -ldr -lpthread ...```). Compile and link options for DAG Recorder are stored in ```tp-parsec/config/dr.bldconf```.

```
tp-parsec/bin $ cat ../config/dr.bldconf
#!/bin/bash
#
# dr.bldconf - file containing global information necessary to build
#              PARSEC with DAG Recorder
#

# Add options and links for dr (and hooks)
# Only packages of the three benchmarking groups (apps, kernels, netapps) are targeted for task parallelization
if [ "${pkg_group}" == "apps" -o "${pkg_group}" == "kernels" -o "${pkg_group}" == "netapps" ]; then
  global_build_deps="${global_build_deps} hooks mth"
  # for hooks & mth
  cflags="-DENABLE_PARSEC_HOOKS -DDAG_RECORDER=2 -I${PARSECDIR}/pkgs/libs/hooks/inst/${PARSECPLAT}/include -I${PARSECDIR}/pkgs/libs/mth/inst/${PARSECPLAT}/include"
  CFLAGS="${CFLAGS} ${cflags}"
  CXXFLAGS="${CXXFLAGS} ${cflags}"
  LDFLAGS="${LDFLAGS} -L${PARSECDIR}/pkgs/libs/hooks/inst/${PARSECPLAT}/lib -L${PARSECDIR}/pkgs/libs/mth/inst/${PARSECPLAT}/lib -Wl,-R${PARSECDIR}/pkgs/libs/hooks/inst/${PARSECPLAT}/lib -Wl,-R${PARSECDIR}/pkgs/libs/mth/inst/${PARSECPLAT}/lib"
  LIBS="${LIBS} -lhooks -ldr -lpthread"
fi
```

### Summary

* A brief summary of supported build configurations is shown in the table below.

       | Pthreads | OpenMP | TBB | Serial | Task\_mth | Task\_tbb | Task\_qth | Task\_omp | Task\_cilkplus | Task\_serial
------ | -------- | ------ | --- | ------ | --------- | --------- | --------- | --------- | -------------- | ------------
gcc    | mgmt/**mgmt2** | mgmt/**mgmt2** | mgmt/**mgmt2** | mgmt/**mgmt2** | **mgmt2** | **mgmt2** | **mgmt2** | **mgmt2** | **mgmt2** | **mgmt2**
icc    | mgmt/**mgmt2** | mgmt/**mgmt2** | mgmt/**mgmt2** | mgmt/**mgmt2** | **mgmt2** | **mgmt2** | **mgmt2** | **mgmt2** | **mgmt2** | **mgmt2**
gcc-**dr** | n/a        | n/a        | n/a        | n/a        | **mgmt2** | **mgmt2** | **mgmt2** | **mgmt2** | **mgmt2** | **mgmt2** 
icc-**dr** | n/a        | n/a        | n/a        | n/a        | **mgmt2** | **mgmt2** | **mgmt2** | **mgmt2** | **mgmt2** | **mgmt2**

Some examples are:
- How to build MassiveThreads-based task version of streamcluster using gcc?
```
tp-parsec/bin $ ./parsecmgmt2 -a build -p streamcluster -c gcc-task_mth
```
- How to re-build TBB-based task version of canneal using icc?
```
tp-parsec/bin $ ./parsecmgmt2 -a uninstall build -p canneal -c icc-task_tbb
```
- How to build gcc-based Qthreads-based task version of dedup with DAG Recorder?
```
tp-parsec/bin $ ./parsecmgmt2 -a build -p dedup -c icc-task_qth-dr
```
- You can **run** the benchmarks similarly as you do with 'parsecmgmt', just add two more options of input type (e.g., ```-i simlarge```) and number of cores (e.g., ```-n 16```), e.g., run fluidanimate compiled with icc, TBB task, and DAG Recorder on 32 cores and with the native input:
```
tp-parsec/bin $ ./parsecmgmt2 -a run -p fluidanimate -c icc-task_tbb-dr -i native -n 32
```


## How to take part in developing TP-PARSEC?

There are two things to consider when converting an existing application into task parallelism: compilation and source code.

### 1. How to change **compilation**?
* You almost do not need to do anything in the application's Makefile to deal with task versions. All the necessary compile flags and links passed by 'parsecmgmt2' through four variables of ```CFLAGS, CXXFLAGS, LDFLAGS, LIBS``` are already automatically captured by the original Makefile.
  * ```CFLAGS```: compile options for C source files
  * ```CXXFLAGS```: compile options for C++ source files
  * ```LDFLAGS```: library paths to look for linked libraries at compile time ('-L') and runtime ('-Wl,-R')
  * ```LIBS```: libraries to link against with ('-l')

* When you want to pass some additional options in the Makefile, you can branch out the case of ```version=task```. Following is a part of the streamcluster's Makefile which allows the option of using tbbmalloc for task versions. One note is that you actually do not need to append ```-DENABLE_TASK``` into 'CFLAGS' or 'CXXFLAGS' because it has been done automatically by 'parsecmgmt2'.

```
...
ifdef version
  ifeq "$(version)" "pthreads"
    CXXFLAGS :=	$(CXXFLAGS) -DENABLE_THREADS -pthread
    OBJS += parsec_barrier.o
  endif
  ifeq "$(version)" "tbb"
    CXXFLAGS := $(CXXFLAGS) -DTBB_VERSION
    LIBS := $(LIBS) -ltbb
  endif
  ifeq "$(version)" "task"
    CXXFLAGS := $(CXXFLAGS) -DENABLE_TASK
    ifeq ($(USE_TBBMALLOC),1)
      CXXFLAGS := $(CXXFLAGS) -DUSE_TBBMALLOC
      LIBS := $(LIBS) -ltbbmalloc
    endif
  endif
endif
...
```

* The local configuration files ```gcc.bldconf``` and ```icc.bldconf``` sometimes source back to the Pthreads version ```gcc-pthreads.bldconf```. Its purpose is to indicate the preferred *default version* (Pthreads in this case) set by PARSEC creators. But ```gcc-pthreads.bldconf``` usually reassigns the variable ```version``` to ```pthreads``` which erroneously overwrites the assignment ```version=task``` done previously by the global ```task.bldconf``` in case of task configurations (*-task\_*). Hence, in order to avoid errors for task versions, we need to either remove the line ```source ...pthreads.bldconf``` from local ```gcc.bldconf``` or ```icc.bldconf```, or just wrap the line by an if condition of the simple configuration 'gcc' only.

```
if [ "${build}" == "gcc" ]; then
  source ${PARSECDIR}/pkgs/apps/bodytrack/parsec/gcc-pthreads.bldconf
fi
```

#### Tips
* ```version=task``` is sometimes overwritten by local parsec/XXX.bldconf (e.g., apps/XXX/parsec/gcc.bldconf). If so, do as following:

```
#Makefile
ifneq (,$(findstring ENABLE_TASK,$(CXXFLAGS)))
   # for tasks
else
   # for others
endif
```


### 2. How to change **source code**?
* You use ```#ifdef ENABLE_TASK``` pragma to separate your task-parallel code from other versions.
* Remember to include ```tpsiwtch.h``` which translates the generic task parallel primitives into corresponding equivalents of a specific task parallel system.
* Call the function ```tp_init()``` before any invocation to task primitives in order for 'tpswitch' to initialize the corresponding runtime system if necessary.
* Add ```cilk_begin``` and ```cilk_void_return``` (?).

```
#ifdef ENABLE_TASK
#include <tpswitch/tpswitch.h>
#endif

#ifdef ENABLE_THREADS
...
{Pthreads version}
...
#endif

#ifdef ENABLE_OPENMP
...
{original OpenMP version}
...
#endif

#ifdef TBB_VERSION
...
{original TBB version}
...
#endif

#ifdef ENABLE_TASK
...
{task-parallel version}
...
#endif

int main() {
  ...
#ifdef ENABLE_TASK
  tp_init();
#endif
  ...
#ifdef ENABLE_TASK
  create_task();
#endif
  ...
}
```

#### Tips
* You must add OpenMP pragmas if you use ```create_task``` but ```pfor```.
* Please use ```pfor``` with ```PFOR2_EXPERIMENTAL```, which enables an improved ```pfor``` version.

## How to evaluate correctness of code transformation?

There seems no common way to check correctness of the output.
For instance, blackscholes employs a chk_err flag, but bodytrack does nothing.

It is strongly demanded to develop methods to check it.

## Tips

* You can 'clean' (remove objects), 'uninstall' (remove executables), and re-'build' a package by ```./parsecmgmt2 -a clean uninstall build -p {package} ...```. Actually 'clean' is not needed, the 'uninstall' makes the 'build' recompile all object files.
