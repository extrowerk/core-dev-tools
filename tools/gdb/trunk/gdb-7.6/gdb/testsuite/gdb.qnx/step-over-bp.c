#include <stdlib.h>
#include <stdio.h>
int main(int argc, char *argv[])
{
  short a = 5; 
  short b;
  short c;
  short d;

  b = 42;	  // set breakpoint 1 here
  b = a * a + 10; // set breakpoint 2 here
  c = 50;	  // set breakpoint 3 here 
  d = (b < c)?(c):(b);	// set breakpoint 4 here
  a = c - b;		// final step here
  b += a;
  c = (d < b)?(b):(d);	// set breakpoint 5 here
  return c;
}
