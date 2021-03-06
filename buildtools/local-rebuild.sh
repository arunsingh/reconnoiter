#!/bin/bash

OS="$1"
if test -z "${OS}" ; then
    echo Usage: local-rebuild.sh '<OS>' '<optional_addl_make_switches>...' 1>&2
    exit 1
fi
shift
source ./buildtools/$OS/env.inc
autoreconf -i
./buildtools/${OS}/configure.sh
./buildtools/build-clean.sh $@
