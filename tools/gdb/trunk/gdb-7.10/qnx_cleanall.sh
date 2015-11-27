#!/bin/bash
# Cleanup all build directories (but leave GNUmakefile files).
#
#

PRINT_HELP=
YES_CLEAN=
OSTOCLEAN=

while getopts 'yo:h' OPT
do
  case "$OPT" in
    h) PRINT_HELP=1 ;;
    y) YES_CLEAN=1 ;;
    o) OSTOCLEAN=$OPTARG ;;
    ?) PRINT_HELP=1 ;;
  esac
done

if [ "${YES_CLEAN}" != "1" ]; then
  PRINT_HELP=1
fi

RMARG="linux-x86_64-*/[!G]* nto-*-*/[!G]* win32-x86-*/[!G]* win64-x86_64-*/[!G]* darwin-x86_64-*/[!G]*"

if [ "$OSTOCLEAN" ]; then
  case $OSTOCLEAN in
    linux) RMARG="linux-x86*/[!G]*"
      ;;
    nto) RMARG="nto-*-*/[!G]*"
      ;;
    win32) RMARG="win32-x86-*/[!G]*"
      ;;
    win64) RMARG="win64-x86_64-*/[!G]*"
      ;;
    darwin) RMARG="darwin-x86_64-*/[!G]*"
      ;;
    *)
      PRINT_HELP=1 ;;
  esac
fi

if [ "$PRINT_HELP" ]; then
  echo "Usage: ./qnx_cleanall.sh -y [-h] [-o HOSTOS]"
  echo "    -y - mandatory, here only to avoid accidental execution "
  echo "    -o - HOSTOS string. One of: linux,win32,darwin or nto "
  echo "    -h - print this help message"
  return 1
else
  echo "Executing: rm -rf ${RMARG}"
  rm -rf ${RMARG}
fi

