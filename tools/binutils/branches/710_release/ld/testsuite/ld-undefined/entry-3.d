#name: -shared --entry foo archive
#source: dummy.s
#ld: -shared --entry foo tmpdir/libentry.a
#nm: -n
#target: *-*-linux* *-*-gnu* arm*-*-uclinuxfdpiceabi-*-nto*

#...
[0-9a-f]+ T +foo
#...
