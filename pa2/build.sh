#!/bin/bash
export LD_LIBRARY_PATH="/home/dart/stud/mypa/pa2/lib64";
clang -std=c99 -Wall -pedantic *.c -L /home/dart/stud/mypa/pa2/lib64 -lruntime