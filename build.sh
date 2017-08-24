#!/bin/bash
export LD_LIBRARY_PATH="/home/dart/stud/mypa/pa4/lib64";
clang -std=c99 -Wall -pedantic *.c -L /home/dart/stud/mypa/pa5/lib64 -lruntime
LD_PRELOAD=/home/dart/stud/mypa/pa5/lib64/libruntime.so ./a.out -p3 --mutlex