#include <stdio.h>
#include <string.h>
#include "swis.h"
#include "png.h"
#include "s2p_lib.h"

typedef union
{
  long i;
  rgb_t p;
}
rgb_t_int;

#define WITH_BGND
#include "s2p_redinc.c"
#define WITH_ALPHA
#include "s2p_redinc.c"

int
reducetogrey (long *image, char *mask, int rgba)
{
  long x, y;
  rgb_t *im = (rgb_t *) image;
  char *imret;

  if (verbose > 1)
    puts ("Attempting to losslessly reduce the image to greyscale");

  if (rgba)
  {
    debug_printf ("(rgba; image at %p)\n", im);
    for (y = height * width - 1; y >= 0; --y)
      if (!(im[y].alpha && im[y].r == im[y].g && im[y].r == im[y].b))
      {
	if (verbose)
	  puts ("Image cannot be reduced to greyscale");
	return 0;
      }
  }
  else if (mask)
  {
    const rgb_t *i = im;
    const char *m = mask;
    debug_printf ("(alpha; image at %p, mask at %p)\n", im, mask);
    for (y = height; y; --y)
    {
      for (x = width; x; --x)
      {
	if (*m++ && !(i->r == i->g && i->r == i->b))
	{
	  if (verbose)
	    puts ("Image cannot be reduced to greyscale");
	  return 0;
	}
	i++;
      }
      m = (char *) (((int) m + 3) & ~3);
    }
  }
  else
  {
    debug_printf ("(plain; image at %p)\n", im);
    for (y = height * width - 1; y >= 0; --y)
      if (!(im[y].r == im[y].g && im[y].r == im[y].b))
      {
	if (verbose)
	  puts ("Image cannot be reduced to greyscale");
	return 0;
      }
  }

  imret = (char *) im;
  if (rgba)
    for (y = height; y; --y)
    {
      for (x = width; x; --x)
      {
	imret[1] = im->alpha;
	imret[0] = im->r;
	imret += 2;
	im += 1;
      }
      if ((int) imret & 2)
        imret += 2;
    }
  else if (mask)
    for (y = height; y; --y)
    {
      for (x = width; x; --x)
      {
	imret[1] = *mask++;
	imret[0] = im->r;
	imret += 2;
	im += 1;
      }
      if ((int) imret & 2)
	imret += 2;
      mask = (char *) (((int) mask + 3) & ~3);
    }
  else
    for (y = height; y; --y)
    {
      for (x = width; x; --x)
      {
	*imret++ = im->r;
	im += 1;
      }
      if ((int) imret & 3)
	imret = (char *)((int) imret + 3 & ~3);
    }
  if (verbose)
    puts ("Image successfully reduced to greyscale");
  return 1;
}


png_color_8 significantbits (long *image)
{
  long y;
  rgb_t *im = (rgb_t *) image;
  png_color_8 ret = { 8, 8, 8, 8, 8 };
  rgb_t test = { 0x7F, 0x7F, 0x7F };

  if (verbose > 1)
    puts ("Significant bits tests");

#define BIT(C) \
  if (test.C & 128>>x) { \
    int bits = im->C & mask; \
    bits |= bits >> (8-x); \
    bits |= bits >> (16-2*x); \
    bits |= bits >> (32-4*x); \
    if (im->C != bits) test.C &= ~(128>>x); \
  }

  for (y = height * width - 1; y >= 0; --y)
  {
    int x;
    for (x = 7; x; --x)
    {
      int mask = 0xFF << x;
      BIT (r);
      BIT (g);
      BIT (b);
    }
    if (test.r == 0 && test.g == 0 & test.b == 0)
      break;
    im++;
  }

  for (y = 7; y; --y)
  {
    if (test.r & (128 >> y))
      ret.red = 8 - y;
    if (test.g & (128 >> y))
      ret.green = 8 - y;
    if (test.b & (128 >> y))
      ret.blue = 8 - y;
  }

  debug_printf ("Bit tests: r %02X, g %02X, b %02X\n", test.r, test.g, test.b);

  if (verbose)
    printf ("Significant bits: r %i, g %i, b %i\n", ret.red, ret.green,
	    ret.blue);
  return ret;
}
