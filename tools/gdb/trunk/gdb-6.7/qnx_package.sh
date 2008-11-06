#!/bin/sh
# Script for packaging built executables in format suitable for
# posting on foundry download site.
#
# Names of archives are as follows:
# linux-gdb-6.7-uN.tar.bz2
# nto-gdb-6.7-uN.tar.gz
# win32-gdb-6.7-uN.zip
#
# where N is update number. This must be specified as argument
# of -u option for this script.
#
# Script assumes there is INSTALL_ROOT env. variable set.

UPDATE_N=
README_FILE=

while getopts 'u:r:h' OPT
do
  case "$OPT" in
    h) PRINT_HELP=1 ;;
    u) UPDATE_N=$OPTARG ;;
    r) README_FILE=$OPTARG ;;
    ?) PRINT_HELP=1 ;;
  esac
done

if [ "$UPDATE_N" = "" ]; then
  PRINT_HELP=1
fi

if [ "$README_FILE" = "" ]; then
  PRINT_HELP=1
fi

if [ "$INSTALL_ROOT" = "" ]; then
  PRINT_HELP=1
  echo "INSTALL_ROOT environment variable must be set"
fi

if [ "$PRINT_HELP" ]; then
  echo "Usage: ./qnx_package.sh -uN -rREADMEFILE [-h]"
  echo "    -u - mandatory. Argument is numeric value, update number "
  echo "    -r - mandatory. Argument is README file to be used "
  echo "    -h - print this help message"
  exit 0
fi


cp "$README_FILE" "$INSTALL_ROOT/README-u$UPDATE_N"
echo "Entering $INSTALL_ROOT"
pushd "$INSTALL_ROOT"
echo "Removing extra directories"
rm -rf linux/x86/usr/[!b]*
rm -rf nto/x86/usr/[!b]*
rm -rf win32/x86/usr/[!b]*
echo "archiving..."
tar cjvpf linux-gdb-6.7-u${UPDATE_N}.tar.bz2 linux
tar czvpf nto-gdb-6.7-u${UPDATE_N}.tar.gz nto
zip -r win32-gdb-6.7-u${UPDATE_N}.zip win32
echo "leaving $INSTALL_ROOT"
popd
echo "Done."

