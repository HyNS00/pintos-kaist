#!/bin/bash
make clean
make
cd build
make tests/threads/priority-sema.result
# make test/
cd ..