#ld: -shared -T dynamic1.ld
#readelf: -l --wide
#target: *-*-linux* *-*-gnu* *-*-nto*

#...
 Section to Segment mapping:
  Segment Sections...
#...
   0[1-9]     .dynamic[ 	]*
#pass
