#source: start.s
#ld: -shared -z global
#readelf: -d
#target: *-*-linux* *-*-gnu* arm*-*-uclinuxfdpiceabi-*-nto*

#...
 0x0*6ffffffb \(FLAGS_1\) *Flags: GLOBAL
#pass
