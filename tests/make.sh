#!/bin/sh

gcc -I../src -g -O0 -o test ../src/lru_cache.c test.c
