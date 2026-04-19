#!/bin/bash
valgrind --tool=massif --log-file=massif_old.log ./thread_old
valgrind --tool=massif --log-file=massif_new.log ./thread_new