#source: maxpage1.s
#ld: -z max-page-size=0x200000
#readelf: -l --wide
#target: *-*-linux* *-*-gnu* *-*-nto*

#...
  LOAD+.*0x200000
  LOAD+.*0x200000
#pass
