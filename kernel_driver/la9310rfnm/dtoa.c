#include "dtoa.h"

const char *dtoa_r (char s[64], double x)
{
  char *se = s + 64, *p = s, *pe = se, *pd = (char *) 0;
  int n, e = 0;
  if (x == 0)
    return "0";
  if (!(x < 0) && !(x > 0))
    return "NaN";
  if (x / 2 == x)
    return x < 0 ? "-Inf" : "Inf";
  if (x < 0)
    *p++ = '-', x = -x;
  if (x >= 1)
    while (x >= 10)
      x /= 10, e++;
  else if (x > 0 && x < 1e-4)
    while (x < 1)
      x *= 10, e--;
  for (n = 0; n <= 15; n++)
    {
      int i = (int) x;
      if (i > 9)
        i = 9;
      x = (x - i) * 10;
      *p++ = '0' + i;
      if (!pd)
        {
          if (e > 0 && e < 6)
            e--;
          else
            *p++ = '.', pd = p;
        }
    }
  while (p > pd && p[-1] == '0')
    p--;
  if (p == pd)
    p--;
  if (e)
    {
      *p++ = 'e';
      if (e >= 0)
        *p++ = '+';
      else
        *p++ = '-', e = -e;
      do
        *--pe = '0' + (e % 10), e /= 10;
      while (e);
      while (pe < se)
        *p++ = *pe++;
    }
  *p = 0;
  return s;
}

const char *dtoa1 (double x)
{
//  return "R";
  static int c_buf = 0;
  static char buf[n_buf_dtoa][64];
  return dtoa_r (buf[c_buf++ % n_buf_dtoa], x);
}

const char *dtoa2 (double x)
{
  static int c_buf = 0;
  static char buf[n_buf_dtoa][64];
  return dtoa_r (buf[c_buf++ % n_buf_dtoa], x);
}