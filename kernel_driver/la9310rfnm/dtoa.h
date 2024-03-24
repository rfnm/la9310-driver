/* Header for the double to string conversion routines

   Copyright 2006 Frank Heckenbach <f.heckenbach@fh-soft.de>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published
   by the Free Software Foundation, version 2.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING. If not, write to the
   Free Software Foundation, 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#ifndef DTOA_AH
#define DTOA_AH
enum { n_buf_dtoa = 32 };

/* Very simple double to string conversion, as printk doesn't support "%g".
   A buffer must be provided by the called.
   The result is either written to this buffer or given a constant string.
   In any case, the return value points to the result. */
const char *dtoa_r (char s[64], double x);

/* Like dtoa_r(), but using a static set of n_buf_dtoa buffers each.
   The result will be overwritten after this many calls,
   so don't use it more than this number of times per expression!
   Not thread-safe! */
const char *dtoa1 (double x);
const char *dtoa2 (double x);
#endif