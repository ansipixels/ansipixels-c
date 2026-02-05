#! /bin/sh
make clean all
make clean all SAN= CC=gcc-15 MACOSX_DEPLOYMENT_TARGET=15.0
