#ld: -shared -z relro
#readelf: -l --wide
#target: *-*-linux-gnu *-*-gnu* *-*-nacl* *-*-nto*

#...
  GNU_RELRO .*
#pass
