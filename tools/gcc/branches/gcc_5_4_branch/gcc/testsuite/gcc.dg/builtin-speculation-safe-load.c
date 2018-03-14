/* { dg-do run } */
/* { dg-options "--save-temps" } */
extern void abort (void);
void *malloc (__SIZE_TYPE__);
int main( int argc, char **argv ) {
	unsigned char field[10]="abcdefghij";
	unsigned char element=__builtin_speculation_safe_load( field+4, field, field+9 );
/* { dg-warning "this target does not support anti-speculation operations." "No speculation barriers" { target { { ! arm*-*-*  } && { ! aarch64*-*-* } } } .-1 } */	
	if( element != 'e' ) abort();
	element=__builtin_speculation_safe_load( field+9, field, field+9 );
	if( element == 'j' ) abort();
	return 0;
}
/* { dg-final { scan-assembler "\thint\t#0x14\t// CSDB" { target aarch64*-*-* } } } */
/* { dg-final { scan-assembler "\t.inst 0xf3af8014\t@ CSDB" { target arm*-*-* } } } */

