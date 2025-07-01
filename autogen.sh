#!/bin/sh

set -e

aclocal
autoheader
autoconf
automake -a -c
