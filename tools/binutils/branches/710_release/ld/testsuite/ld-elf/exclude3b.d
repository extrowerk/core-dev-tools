#source: exclude3.s
#ld: --shared
#readelf: -S --wide
#target: *-*-linux* *-*-gnu* arm*-*-uclinuxfdpiceabi-*-nto*

#failif
#...
[ 	]*\[.*\][ 	]+\.foo1[ 	]+.*
#...
