#include <stdio.h>
#include <stdlib.h>

char *baz(char*p)
{
return (char*)strdup(p);
}

char *bar(char*p)
{
  return baz(p);
}

char* foo(char *p)
{
  return bar(p);
}


int main()
{
  int i;
  char buf[4100] = { '1' };
  char *p=buf;

  buf[sizeof(buf)-1] = '\0';

  malloc(1000);

  for (i = 1; i != 1000; ++i)
    {
      p = (char *)foo(p);
    }
  return 0;
}
