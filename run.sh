#! /bin/sh
# A bit annoying macos matrix of clang vs gcc and debug vs release vs sanitizers.
# This just runs all the combinations.
set -ex
make clean all
make clean all NO_COLOR=1
make clean all DEBUG=0
make clean all SAN= CC=gcc-15 MACOSX_DEPLOYMENT_TARGET=15.0
make clean all DEBUG=0 SAN= CC=gcc-15 MACOSX_DEPLOYMENT_TARGET=15.0
