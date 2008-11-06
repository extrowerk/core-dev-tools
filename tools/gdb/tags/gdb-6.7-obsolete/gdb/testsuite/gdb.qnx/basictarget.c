#include <stdio.h>
#include <math.h>

float float_func ()
{
  float returnval = 5;
  return returnval;
}

double calc_dbl_value()
{
  double c;
  double s;
  s = sin(3.1415/4.0);
  c = cos(3.1415/4.0);
  return s / c;
}

double double_func ()
{
  double returnval = calc_dbl_value();
  return returnval;
}

double dummy(double a)
{
  return a + 5;
}

int main () {
  double d = sin(3.14/2.0);
  float_func();
  d = double_func();
  printf("%f\n", d);
  return 0;
}
