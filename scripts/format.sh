#!/bin/sh

cd "${MESON_SOURCE_ROOT}"
clang-format -i src/*.c src/*.h
