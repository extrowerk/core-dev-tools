#source: start.s
#readelf: -d -W
#ld: -shared -z now --enable-new-dtags
#target: *-*-linux* *-*-gnu* *-*-nto*

#failif
#...
 0x[0-9a-f]+ +\(BIND_NOW\) +
#...
