#!/bin/sh
# svnupdate.sh - script for generating proper (rev. NN) number
# in gdb --version message. 
# This script regenerates file gdb/version.in. The changed file
# should never be committed to svn.
#
#
PRINT_HELP=
REV_NO=
APPEND_DATE=
while getopts 'r:dh' OPT
do
  case $OPT in
    h) PRINT_HELP=1 ;;
    r) # revision number expected
      REV_NO="$OPTARG"
      ;;
    d) APPEND_DATE=1 ;;
    ?) PRINT_HELP=1
    ;;
  esac
done

if [ "$PRINT_HELP" ]; then
  echo "Usage: ./qnx_svnupdate.sh [-r revnumber] [-d] [-h]"
  echo "    -r NN - update to revision NN"
  echo "    -d - append current date and time to the --version message"
  echo "    -h - print this help message"
  exit 0
fi

rm gdb/version.in

if [ "$REV_NO" ]; then
  atrev=`svn update -r${REV_NO}`
else
  atrev=`svn update`
fi

atrev=${atrev##*revision }
atrev=${atrev%\.}

versionin="6.8 qnx-nto (rev. ${atrev})"

if [ "${APPEND_DATE}" ]; then
  now=`date +"%Y%m%d%H%M%S"`
  versionin="${versionin} ${now}"
fi

echo ${versionin}
echo -n ${versionin} > gdb/version.in 

