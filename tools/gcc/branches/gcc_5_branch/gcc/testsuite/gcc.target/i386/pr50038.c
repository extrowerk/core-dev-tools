/* PR target/50038 */
/* { dg-options "-O2" } */
/* { dg-options "-O2 -march=i686" { target { { i?86-*-* x86_64-*-* } && ia32 } } } */

void
test (int len, unsigned char *in, unsigned char *out)
{
  int i;
  unsigned char xr, xg;
  unsigned char xy=0;
  for (i = 0; i < len; i++)
    {
      xr = *in++;
      xg = *in++;
      xy = (unsigned char) ((19595 * xr + 38470 * xg) >> 16);

      *out++ = xy;
    }
}

/* { dg-final { scan-assembler-times "movzbl" 2 } } */
