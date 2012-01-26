#!/bin/bash

# Script for cross-compiling win32 and darwin on linux host.
# Provided to gdb configure as an argument to --with-python
#
# It needs to respond to --prefix --exec-prefix --includes --libs --cflags --ldflags arguments.
# Gdb configure will pass python-configure.py (path to it) followed by one of
# the above options.

#python_dir=${PWD}/../../python-2.7/win32-x86-o/installprefix
python_dir=${PWD}/../../Python27
python_ver=2.7

#python_root=`echo "${python_dir}/Python${python_ver}" | sed 's,[.],,g'`
#python_root=${python_dir}/usr
python_root=${python_dir}

win32_py_include="-I${python_root}/include -I${python_root}/include/python${python_ver} -DWINVER=0x0500 "
#"-DPy_NO_ENABLE_SHARED"

echo $* >> python.args

if [ $2 == --includes ] ; then
  echo "${win32_py_include}" 
  echo "Answer: ${win32_py_include}" >>python.args
elif [ $2 == --ldflags ] ; then
  echo "-L${python_root}/libs  -lpython27 "
  echo "Answer: -L${python_root}/libs -lpython27 " >>python.args
elif [ $2 == --exec-prefix ] ; then
  echo "${python_root}"
  echo "Answer: ${python_root}" >>python.args
else
  exit 1
fi




