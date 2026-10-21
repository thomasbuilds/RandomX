#!/bin/sh
# Build agent X's analysis harness against the lead's optimised x86-64 library build (read-only).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
RX_ROOT=${RX_ROOT:-$HERE/../../..}
LIB=${RX_BUILD:-$RX_ROOT/build}/librandomx.a
mkdir -p $HERE/bin
g++ -std=c++17 -O2 -maes -mssse3 -Wall -Wno-unused-function -I$RX_ROOT/src \
    $HERE/src/xsim.cpp $LIB -lpthread -o $HERE/bin/${OUT:-xsim}
echo built $HERE/bin/xsim
