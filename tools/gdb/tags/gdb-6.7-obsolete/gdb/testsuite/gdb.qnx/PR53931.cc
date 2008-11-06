#include <iostream>
#include "PR53931.h"
int main() 
{
  Class1<float> c1(15);
  Class2<float> c2(c1);
  std::cout << "value is " << c2.a << std::endl;
  return 0;
}

