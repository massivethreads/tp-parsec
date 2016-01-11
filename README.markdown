Task-parallel version of PERSEC
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


