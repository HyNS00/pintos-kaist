#!/bin/bash
make clean
make
cd build
make tests/threads/alarm-simultaneous.result
# make test/
cd ..