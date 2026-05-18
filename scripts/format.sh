#!/bin/sh
# This script formats the C source and header files using clang-format.
# 'clang-format' is a required tool and must be available in your PATH.

cd "${MESON_SOURCE_ROOT}"
clang-format -i src/*.c src/*.h
