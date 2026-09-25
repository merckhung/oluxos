#!/bin/bash


source /home/merck/Desktop/Embedded/Toolchains/USE_i686-pc-linux-gnu
PARMS="ARCH=ia32 CROSS_COMPILE=i686-pc-linux-gnu-"


make ${PARMS} clean
make ${PARMS}


