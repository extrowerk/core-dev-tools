#include <dlfcn.h>
#include <stdio.h>


int main(int argc, char *argv[])
{
	void *h1 = dlopen(argv[1], 0);
	void *h2 = dlopen(argv[2], 0);
	int (*foo)() = dlsym(h1, "fooimpl");

	if (!foo) foo = dlsym(h2, "fooimpl");

	foo();   // set breakpoint 1 here

	dlclose(h1);
	dlclose(h2);
	return 1;
}
