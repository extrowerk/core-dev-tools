VCINSTALLDIR=C:\Program Files\Microsoft Visual Studio .NET 2003
VSINSTALLDIR=${VCINSTALLDIR}\Common7\IDE
MSVCDir=${VCINSTALLDIR}\Vc7
FrameworkSDKDir=${VCINSTALLDIR}\SDK\v1.1

all: make pinfo

make:
	export PATH="${PATH}:/cygdrive/c/Program Files/Microsoft Visual Studio .NET 2003/Vc7/bin:/cygdrive/c/Program Files/Microsoft Visual Studio .NET 2003/Common7/IDE" ; \
	export INCLUDE="${MSVCDir}\ATLMFC\INCLUDE;${MSVCDir}\INCLUDE;${MSVCDir}\PlatformSDK\include\prerelease;${MSVCDir}\PlatformSDK\include;${FrameworkSDKDir}\include;" ; \
	export LIB="${MSVCDir}\ATLMFC\LIB;${MSVCDir}\LIB;${MSVCDir}\PlatformSDK\lib\prerelease;${MSVCDir}\PlatformSDK\lib;${FrameworkSDKDir}\lib" ; \
	export DevEnvDir="${VCINSTALLDIR}\Common7\IDE" ; \
	cd .. ; \
	MAKEFLAGS= nmake /f QMakefile

pinfo:
	echo STATE=Experimental > make.pinfo ; \
	echo INSTALLNAME=make.exe >> make.pinfo ; \
	echo NAME=make.exe >> make.pinfo ; \
	echo INSTALLDIR=usr/bin/ >> make.pinfo ; \
	echo USER=`echo ${USERNAME}` >> make.pinfo ; \
	echo DATE=`date` >> make.pinfo ; \
	echo HOST=`hostname` >> make.pinfo ; \
	echo DESCRIPTION=Dependency/recompilation checking >> make.pinfo ; \
	echo FINFO=0 0 1775 >> make.pinfo

hinstall:
	

clean:
	rm -f make.exe *~ *.obj *.sbr *.pch *.pinfo
