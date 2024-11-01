#!/bin/bash

export AR="/usr/bin/ar"
export ARFLAGS="rcs"  # Remove the `D` flag here
export CC="/usr/bin/clang"
export CFLAGS="-g0"
export LD="/usr/bin/clang"
export LDFLAGS="-Wl,-S -lstdc++ -undefined error"

# Run the configure and make commands as rules_foreign_cc would
./configure --without-guile --with-guile=no --disable-dependency-tracking --prefix=$PWD/install
make -j"$(nproc)"
make install
