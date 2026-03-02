#!/bin/bash
valgrind --tool=massif --log-file=massif.log ./main
