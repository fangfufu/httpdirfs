#!/bin/sh

cd "${MESON_SOURCE_ROOT}"
mkdir -p docs/html
doxygen Doxyfile
