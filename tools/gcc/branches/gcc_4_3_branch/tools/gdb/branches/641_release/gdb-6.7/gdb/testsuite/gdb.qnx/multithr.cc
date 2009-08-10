#include <cstdlib>
#include <iostream>
#include <pthread.h>
#include <unistd.h>

using namespace std;

void * start(void * arg) {
	static volatile int delay = 5;
	char buff[] = { 'T', '0' + delay, '\0' };
	pthread_setname_np(pthread_self(), buff);
  //sleep(1);
  cout << "Thread: %d started." << endl;
  if (delay)
  {
	sleep(1);
    pthread_create(0,0,start,0);
    cout << "new thread..." << endl;
    delay--;
    sleep(delay);
  }
  if (gettid() == 2)
  {
	  int *p = 0;
	  *p = 4;
  }
  cout <<"Thread: " << gettid() << " terminated." << endl;
  buff[0] = '?';
  pthread_getname_np (pthread_self(), buff, 3);
  cout << buff << endl;
  return 0;
}

namespace {
	struct Thread
	{
		Thread() 
		{
			pthread_create(0,0,start,0);
			pthread_create(0,0,start,0);
			pthread_create(0,0,start,0);
		}
	};
}

Thread thr;

int main(int argc, char *argv[]) {
	std::cout << "Welcome to the QNX Momentics IDE" << std::endl;
	while (1) { sleep(1); }
	return EXIT_SUCCESS;
}
