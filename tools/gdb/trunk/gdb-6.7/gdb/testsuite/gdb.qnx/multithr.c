#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

void * start(void * arg) {
	static volatile int delay = 5;
	
  //sleep(1);
  printf("Thread: %d started.\n", gettid());
  if (delay)
  {
	sleep(1);
    pthread_create(0,0,start,0);
    printf("new thread...\n");
    delay--;
    sleep(delay);
  }
  if (gettid() == 2)
  {
//	  int *p = 0;
//	  *p = 4;
  }
  printf("Thread: %d terminated.\n", gettid());
  return 0;
}

int main(int argc, char *argv[]) {
	int i = 20;
	printf("Welcome to the Momentics IDE\n");
	pthread_create(0,0,start,0);
	while (i--) { sleep(1); }
	return EXIT_SUCCESS;
}
