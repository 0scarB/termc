#!/bin/sh

set -eu

clang \
    -std=c99 -pedantic \
    -Wall -Werror \
    -O3 \
    termc.c \
    -o termc

