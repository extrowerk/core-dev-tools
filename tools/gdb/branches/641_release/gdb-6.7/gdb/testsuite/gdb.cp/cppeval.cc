#include <cstdlib>
#include <iostream>
#include <string.h>


/* NOTE: We intentionally don't make the classes virtual.  */
class Name
{
public:
  Name(void) { myName = 0; } 
  ~Name(void) { delete myName; }
  char* myName;
};

int main(int argc, char *argv[]) 
{
  Name n;
  return EXIT_SUCCESS;
}

