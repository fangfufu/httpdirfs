#!/bin/sh

cd "${MESON_SOURCE_ROOT}"
astyle --style=kr --align-pointer=name --max-code-length=80 src/*.c src/*.h
