# Naming convention:
#
# ${host_os}-${host_cpu}-${host_cpu_variant}-${target_os_cpu_variant}[-nopython]
#
# For nto-arm* we need to specify endian and variant. In that case naming
# is:
#
# ${host_os}-${host_cpu}-${host_cpu_endianes}-${host_cpu_variant}-${target_os_cpu_variant}[-nopython]
#
#
#
# Setup host_os host_cpu target_os and target_cpu
build_dir=$(basename ${PWD})
SIFS=${IFS}
IFS=-
set -- ${build_dir}
IFS=${SIFS}
host_os=$1
host_cpu=$2
host_cpu_variant=$3
target_os_cpu_variant=$4
case ${build_dir} in
  nto-arm*)
	SIFS=${IFS}
	IFS=-
	set -- ${build_dir}
	IFS=${SIFS}
	host_os=$1
	host_cpu=$2
	host_cpu_endian=$3
	host_cpu_variant=$4
	target_os_cpu_variant=$5
	;;
  *)
    ;;
esac
#
# Get sdp version
# Assume ntox86-gcc is always available
#
qnx_sdp_version=$(ntox86-gcc -dumpmachine)
qnx_sdp_version=${qnx_sdp_version##*-}
SIFS=${IFS}
IFS=.
set -- ${qnx_sdp_version}
IFS=${SIFS}
qnx_install_version=$1$2$3
echo "qnx_install_version=${qnx_install_version}"


# Setup host_cpu and platform
case ${TARGET_SYSNAME} in
  nto)
    qnx_abiext_host=""
    host_platform="unknown"
    case ${host_cpu}${host_cpu_endian}${host_cpu_variant} in
      aarch64*)
	    host_cpu_canonical=aarch64
	    ;;
      arm*v7*)
	    qnx_abiext_host=eabi
	    host_cpu_canonical=arm
	    ;;
      arm*)
	    host_cpu_canonical=arm
	    ;;
      x86_64*)
	    host_cpu_canonical=x86_64
	    host_platform="pc"
	    ;;
      x86*)
	    case ${qnx_sdp_version} in
	      qnx6.[234].*)
		    host_cpu_canonical=i386
		    ;;
	      qnx6.[56].*)
		    host_cpu_canonical=i486
		    ;;
	      qnx*)
		    host_cpu_canonical=i586
		    ;;
	      *) echo "Uknown sdp version: ${qnx_sdp_version}"
		    ;;
	    esac
	    host_platform="pc"
	    ;;
      ppc*)
	    host_cpu_canonical=powerpc
	    ;;
    esac
    ;;
  linux)
    host=linux
    ;;
  darwin)
    host=darwin
    ;;
  win32)
    host=win32
    ;;
  win64)
    host=win64
    ;;
  *)
    echo "Could not recognize host: ${host_os}"
    Error 1
    ;;
esac

# setup target
qnx_abiext_target=""
target_platform=""
target_cpu=""
target_os="nto"
configure_opts=""
case ${target_os_cpu_variant} in
  ntoaarch64*)
    target_cpu="aarch64"
    target_platform="unknown"
    ;;
  ntoarm*v7*)
    target_cpu="arm"
    qnx_abiext_target="eabi"
    target_platform="unknown"
    CFLAGS=-D__QNXTARGET_ARM__
    CXXFLAGS=-D__QNXTARGET_ARM__
    ;;
  ntoarm*)
    target_cpu="arm"
    target_platform="unknown"
    CFLAGS=-D__QNXTARGET_ARM__
    CXXFLAGS=-D__QNXTARGET_ARM__
    ;;
  ntox86_64*)
    target_cpu=x86_64
    target_platform="pc"
    ;;
  ntox86*)
    case ${qnx_sdp_version} in
      qnx6.[234].*) target_cpu=i386 ;;
      qnx6.[56].*) target_cpu=i486 ;;
      *) target_cpu=i586
      configure_opts="--enable-64-bit-bfd=yes"
      ;;
    esac
    target_platform="pc"
    ;;
  ntoppc*)
    target_cpu="powerpc"
    target_platform="unknown"
    ;;
  ntosh*)
    target_cpu="sh"
    target_platform="unknown"
    ;;
  ntomips*)
    target_cpu="mips"
    target_platform="unknown"
    ;;
  *)
    echo "Don't have special settings for target_os_cpu_variant=${target_os_cpu_variant}"
    target_os=""
    target_cpu=""
    ;;
esac

case ${TARGET_SYSNAME} in
  nto)
    host=${host_cpu_canonical}-${host_platform}-${target_os}-${qnx_sdp_version}${qnx_abiext_host}
    configure_host="--host=${host}"
    ;;
  linux)
    case ${qnx_sdp_version} in
      qnx6.6*)
	PYTHON_CPU=${host_cpu}
	# PYTHON_CPU is used in python-config-cross.sh
	export PYTHON_CPU
      ;;
      qnx7*)
        PYTHON_CPU=${host_cpu}
	# PYTHON_CPU is used in python-config-cross.sh
	export PYTHON_CPU
      ;;
      *)
      ;;
    esac
    ;;
  *)
    host=""
    configure_host=""
    ;;
esac
target=${target_cpu}-${target_platform}-${target_os}-${qnx_sdp_version}${qnx_abiext_target}
configure_target="--target=${target}"

# setup srcdir
srcdir=${PWD}/..

# export target as our build-cfg proper does not understand
# host/target concept.

# Print important info:
echo "host=${host}"
echo "target=${target}"
echo "SYSNAME=${SYSNAME}"
echo "TARGET_SYSNAME=${TARGET_SYSNAME}"


echo "host_os=${host_os}"
echo "host_cpu=${host_cpu}"
echo "qnx_abiext_host=${qnx_abiext_host}"
echo "host=${host}"
echo "target_os=${target_os}"
echo "target_cpu=${target_cpu}"
echo "target=${target}"
echo "qnx_abiext_target=${qnx_abiext_target}"

# No nl_langinfo in libc++ yet so disable langinfo even though 
# langinfo.h is installed
if [ x${host_os} == xnto ]; then
  export am_cv_langinfo_codeset=no
fi


# export TARGET_SYSNAME - needed for python-config-cross.sh
export TARGET_SYSNAME

function hook_preconfigure {
  # determine if no python support is desired
  case ${TARGET_SYSNAME} in
	win64*)
	  with_python_opt="--with-python=${srcdir}/python-config-cross.sh"
	  ;;
	win32*)
	  with_python_opt="--with-python=${srcdir}/python-config-cross.sh"
	  ;;
	nto*)
	# For now, do not build python support.
	  with_python_opt="--with-python=no"
	  ;;
	darwin*)
	  with_python_opt="--with-python=${srcdir}/python-config-cross.sh"
	  ;;
	linux*)
	  # rpath must be reserved so it can be changed with chrpath
	  if [ "${OFFICIAL_BUILD}" != "" ]; then
		# For official builds, we provide python install for building
		with_python_opt="--with-python=${srcdir}/python-config-cross.sh"
		LDFLAGS="${LDFLAGS} -Wl,-rpath=_ORIGIN/../python27/lib:_ORIGIN/../lib"
	  else
		# Let configure decide; if there is python available, then
		# it will use it.
		with_python_opt=
	  fi
	  ;;
	*)
#all others, rely on distro's python
	  with_python_opt="--with-python=yes"
	  ;;
  esac


  nopython_program_suffix=""

  # nopython in directory name says "no python support desired"
  case ${PWD} in
    *nopython*)
      with_python_opt="--with-python=no"
      nopython_program_suffix="-nopython"
      ;;
  esac

  configure_opts="${configure_opts} ${configure_host} ${configure_target}"
#  configure_opts="${configure_opts} --without-expat"
  configure_opts="${configure_opts} --with-bugurl=no"

  install_sysname=${TARGET_SYSNAME}
  # Setup install dirs.
  case ${TARGET_SYSNAME} in
    nto*)
      root_dir="/usr"
      case ${qnx_sdp_version} in
	qnx7*)
	      install_sysname="qnx7"
	      ;;
	qnx6*)
	      install_sysname="qnx6"
	      ;;
	*) echo "Uknown sdp version: ${qnx_sdp_version}"
	      ;;
      esac

      # Our makefiles hard code 6.5.0 CC
      unset CC
      ;;
    win*)
      root_dir="/c"
      ;;
    darwin*)
      root_dir="/Developer/SDKs/"
      ;;
    *)
      root_dir="/opt"
      ;;
  esac

  configure_opts="${configure_opts} ${with_python_opt} TARGET_SYSNAME=${TARGET_SYSNAME}"

  basedir=${root_dir}/${qnx_install_version}/host/${install_sysname}/${host_cpu}/usr

  SKIP_PROJECTS="binutils gas gold gprof ld"
  for p in $SKIP_PROJECTS ; do
    configure_opts="${configure_opts} --disable-$p"
  done

  configure_opts="${configure_opts} --prefix=${basedir}"
  configure_opts="${configure_opts} --exec-prefix=${basedir}"
  configure_opts="${configure_opts} --with-local-prefix=${basedir}"
  configure_opts="${configure_opts} --program-prefix=${target_os_cpu_variant}-"
  # This setting of --includedir is so that the readline compiles don't
  # get things from the wrong place - we'd prefer not to have to say
  # anything at all and let the qcc/gcc get the system headers on their
  # own, but the only thing we can do is point it at a safe place.
  #configure_opts="${configure_opts} --includedir=."
  configure_opts="${configure_opts} --enable-gdbmi"
#	configure_opts="${configure_opts} --enable-build-warnings=-Wall"
  configure_opts="${configure_opts} --disable-nls"
  configure_opts="${configure_opts} --disable-tui"
  configure_opts="${configure_opts} --disable-sim --without-sim"
#configure_opts="${configure_opts} --with-expat=no"
  configure_opts="${configure_opts} --disable-werror"
  if [ "${nopython_program_suffix}" != "" ]; then
    configure_opts="${configure_opts} --program-suffix=${nopython_program_suffix}"
  fi
  configure_opts="${configure_opts} --verbose"
  #
  # CFLAGS for particular target OS
  case ${target_os} in 
    nto)
      CFLAGS="${CFLAGS} -D__QNXTARGET__ -g0 -O -D_LARGEFILE64_SOURCE ${CCOPTS}"
      export CFLAGS
      CXXFLAGS="${CXXFLAGS} -D__QNXTARGET__ -g0 -O -D_LARGEFILE64_SOURCE ${CCOPTS}"
      export CXXFLAGS
    ;;
  esac
}

function hook_premake {
  export TERMCAP="-lncurses"
  strip_r_switch
}

function hook_postconfigure {
  if test ${target} = ntomips -a ${SYSNAME} = nto; then
    for i in Makefile sim/Makefile sim/igen/Makefile; do
      cp $i $i.bak
      sed -e '/^CC_FOR_BUILD =/s/= .*$/= unset QNX_TARGET; \/usr\/bin\/cc/' <$i.bak >$i
      rm $i.bak
    done
  fi
}

function hook_postmake {
  # Modify stack limit for selfhosted gdb
  case ${TARGET_SYSNAME} in
    nto*)
      echo "Setting stack limit to 4M..."
      elfnote -L -S 4M gdb/gdb
      ;;
    linux*)
      if [ "${OFFICIAL_BUILD}" != "" ]; then
	# Build machine must have chrpath and make it
	# available in $PATH
	chrpath -r \$ORIGIN/../python27/lib:\$ORIGIN/../lib gdb/gdb
      fi
      ;;
  esac
}

function hook_pinfo {
  cd gdb
  gen_pinfo -e -ngdb ${target}-gdb usr/bin USE="%1>%C --help" LICE=GPL DESCRIPTION="GNU Debugger 7.12"
}
