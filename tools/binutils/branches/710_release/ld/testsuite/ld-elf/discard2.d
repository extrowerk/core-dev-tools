#source: discard2.s
#ld: -r -T discard.ld
#readelf: -r
#target: x86_64-*-linux-gnu* i?86-*-linux-gnu i?86-*-gnu* i?86-*-nto*

Relocation section '.rel.*.debug_info' at offset 0x[0-9a-z]+ contains 1 entry:
[ \t]+Offset[ \t]+Info[ \t]+Type[ \t]+Sym.*
[0-9a-f]+[ \t]+[0-9a-f]+[ \t]+R_.*[ \t]+[0-9a-f]+[ \t]+here.*
#pass
