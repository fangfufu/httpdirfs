#!/bin/sh

cd "${MESON_SOURCE_ROOT}"
mkdir -p doc/man
help2man --name "mount HTTP directory as a virtual filesystem" \
	--no-discard-stderr ./httpdirfs > doc/man/httpdirfs.1
