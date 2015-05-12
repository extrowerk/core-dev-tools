#!/bin/bash

# Script for cross-compiling win32 and darwin on linux host.
# Provided to gdb configure as an argument to --with-python
#
# It needs to respond to --prefix --exec-prefix --includes --libs --cflags --ldflags arguments.
# Gdb configure will pass python-configure.py (path to it) followed by one of
# the above options.


case ${TARGET_SYSNAME} in
win32*)
	gdb_prereq_dir=${PWD}/../../../gdb-prereq/win32
	python_root=${gdb_prereq_dir}/Python27
	python_ver=2.7
	py_include="-I${python_root}/include -I${python_root}/include/python${python_ver} -DWINVER=0x0500 "
	py_libs="-L${python_root}/libs  -lpython27"
	# py_exec_prefix sets up WITH_PYTHON_PATH in config.h
	# This is needed to automatically setup PYTHONHOME env. var.
	py_exec_prefix="${prefix}/bin"
	;;
darwin*)
	python_ver=2.7
	python_root=/usr/darwin/x86_64-apple-darwin/x86_64-apple-darwin/
	py_include="-I${python_root}/include -I${python_root}/include/python${python_ver}"
	py_libs="-lpython2.7"
	# py_exec_prefix sets up WITH_PYTHON_PATH in config.h
	# This is needed to automatically setup PYTHONHOME env. var.
	py_exec_prefix="${python_root}"
	;;
linux*)
	gdb_prereq_dir=${PWD}/../../../gdb-prereq/linux
	# For testing, it is convenient to be able to run gdb from build directory
	ln -fs ${gdb_prereq_dir}/python27 ..
	python_root=${gdb_prereq_dir}/python27
	python_ver=2.7
	py_include="-I${python_root}/include -I${python_root}/include/python${python_ver}"
	py_libs="-L${python_root}/lib -lpython${python_ver}"
	# py_exec_prefix sets up WITH_PYTHON_PATH in config.h
	# This is needed to automatically setup PYTHONHOME env. var.
	py_exec_prefix="${prefix}/python27"
	;;
*)
	echo "Not configured for python"
	exit 1
	;;
esac


if [ $2 == --includes ] ; then
  echo "${py_include}" 
elif [ $2 == --ldflags ] ; then
  echo "${py_libs}" 
elif [ $2 == --exec-prefix ] ; then
  echo "${py_exec_prefix}"
else
  exit 1
fi

