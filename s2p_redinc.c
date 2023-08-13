/* Conversion to 8bpp
 * WITH_ALPHA = include alpha channel information
 * WITH_BGND  = accept background colour information
 */

#ifdef WITH_ALPHA
# define COLOURMASK 0xFFFFFFFF
#else
# define COLOURMASK 0xFFFFFF
#endif


#ifdef WITH_ALPHA
static int getcolours_alpha(
# ifndef WITH_BGND
  const void *const image, rgb_t_int *const palette,
  char *const mask)
# else /* WITH_BGND */
  const void *const image, int *const colourp,
  rgb_t_int *const palette, char *const mask, const long bgnd)
# endif /* WITH_BGND */
#else
static int getcolours(
# ifndef WITH_BGND
  const void *const image, rgb_t_int *const palette)
# else /* WITH_BGND */
  const void *const image, int *const colourp,
  rgb_t_int *const palette, const long bgnd)
# endif /* WITH_BGND */
#endif
{
  long y;
  const unsigned long *im;
  int colour=0;
#ifdef WITH_BGND
  int bgndindex = /*(bgnd == -1) ? 0 :*/ -1;
#endif /* WITH_BGND */

  debug_puts("finding colours...");
  y=height;
  im=image;
  do { /* find ranges for allocation */
    long i=width;
    do {
      long j = im[--i] & COLOURMASK;
#ifdef WITH_ALPHA
      /* throw the pixel away if it's completely masked out */
      if ((j & 0xFF000000) == 0) j = 0;
#endif
      if (colour) {
        /* binary search */
        int p=colour, m=0, b=0;
        while (p) {
          m = b + p/2;
          if (j == palette[m].i)
            goto result;
          if (j > palette[m].i) {
            b = m + 1;
            p -= (p>>1) + 1;
          } else {
            p >>= 1;
          }
        }
        /* make sure we're pointing at a larger value */
        if (j > palette[m].i)
          m++;
        if (colour == 256)
          goto failed;
        /* shuffle the 'higher' colour values up */
        p = colour;
        while (p > m) {
          palette[p].i = palette[p-1].i;
          p--;
        }
        palette[m].i = j;
      } else {
        palette[0].i = j;
      }
      colour++;
result:;
    } while (i);
    im += width;
  } while (--y);

#ifdef DEBUG
  _swi (OS_File, _INR (0, 5),10,"<Wimp$ScrapDir>.palette",0xFFD,0,palette,palette+256);
  printf ("number of palette entries: %i\n",colour);
#endif

#ifdef WITH_BGND
  for (y = 0; y < colour; ++y)
# ifdef WITH_ALPHA
    if (bgndindex == -1
        && ((palette[y].i & 0xFFFFFF) == bgnd || palette[y].i == 0)) {
      bgndindex = y; break;
    }
# else
    if (bgndindex == -1 && palette[y].i == bgnd) {
      bgndindex = y; break;
    }
# endif
#endif /* WITH_BGND */

#ifdef WITH_ALPHA
  debug_puts("filling in mask data...");
  for (y=0; y<colour; ++y)
    mask[y] = (char)(palette[y].i >> 24);
#endif
#ifndef WITH_BGND
  return colour;
#else /* WITH_BGND */
  *colourp=colour;
  return bgndindex;
#endif /* WITH_BGND */

failed:
#ifndef WITH_BGND
  return 260; /* out of range */
#else /* WITH_BGND */
  *colourp = 260; /* out of range */
  return -1;
#endif /* WITH_BGND */
}


#ifdef WITH_ALPHA
#ifndef WITH_BGND
char *reduceto8_alpha(long *image, char **imask, long **ipalette,
  int *entries)
#else /* WITH_BGND */
char *reduceto8_alpha(long *image, char **imask,
  rgb_t **ipalette, int *entries, png_color_16 *bkgd)
#endif /* WITH_BGND */
#else
#ifndef WITH_BGND
char *reduceto8(long *image, long **ipalette, int *entries)
#else /* WITH_BGND */
char *reduceto8(long *image, long *mask,
  rgb_t **ipalette, int *entries, png_color_16 *bkgd)
#endif /* WITH_BGND */
#endif
{
  int colour=0, LEDs=0;
  long x, y;
  long *im;
#ifdef WITH_BGND
  long bgnd=bkgd
    ? (long)bkgd->red | (long)bkgd->green<<8 | (long)bkgd->blue<<16
    : -1;
  int bgndindex;
#endif /* WITH_BGND */
  char *im2;
#ifndef WITH_BGND
  long *palette = heap_malloc(256*sizeof(long));
#else /* WITH_BGND */
  rgb_t_int *palette = heap_malloc(256*sizeof(rgb_t_int));
#endif /* WITH_BGND */
#ifdef WITH_ALPHA
  char *mask = heap_malloc(256);
  if (!palette || !mask)
    fail(fail_NO_MEM,"out of memory (reduction to 8bpp) (%s)",
      !palette ? "palette" : "mask");
#else
# ifdef WITH_BGND
  long maskcolour = *mask;
# endif /* WITH_BGND */
  if (!palette)
    fail(fail_NO_MEM,"out of memory (reduction to 8bpp) (%s)", "palette");
#endif

  if (verbose > 1)
    puts("Attempting to losslessly reduce the image to 8bit");

  debug_puts("Finding which colours are used...");
  LEDs = _swi (Hourglass_LEDs, _INR (0, 1),  1, 0); /* hourglass LED 1 on */
  memset(palette,0,256*sizeof(rgb_t_int));
#ifdef WITH_ALPHA
  memset(mask,255,256);
# ifndef WITH_BGND
  colour = getcolours_alpha(image, palette, mask);
# else /* WITH_BGND */
  bgndindex = getcolours_alpha(image, &colour, palette, mask, bgnd);
# endif /* WITH_BGND */
#else
# ifndef WITH_BGND
  colour = getcolours(image, palette);
# else /* WITH_BGND */
  bgndindex = getcolours(image, &colour, palette, bgnd);
# endif /* WITH_BGND */
#endif
#ifndef WITH_BGND
  if (colour > 256)
#else /* WITH_BGND */
  if (colour > (bgnd == -1 ? 255 : 256))
#endif /* WITH_BGND */
    goto failed;
#if defined WITH_BGND && !defined WITH_ALPHA
  debug_printf("maskcolour = %06lX\n",maskcolour);
  if (maskcolour != -1) {
    y=0;
    while (y<colour && maskcolour!=palette[y].i) ++y;
    *mask = maskcolour = (int)y;
    debug_printf("Mask colour is now %li\n", maskcolour);
  }
#endif /* WITH_BGND && !WITH_ALPHA */
  /* convert the image to 8bpp paletted */
  im=(long*)image;
  im2=(char*)image;
  for (y=height; y; --y) {
    x=0;
    do {
      /* bsearch: &(MM)BBGGRR -> index */
      int p=colour, m=0, b=0;
      long j = im[x] & COLOURMASK;
#ifdef WITH_ALPHA
      /* throw the pixel away if it's completely masked out */
      if ((j & 0xFF000000) == 0) j = 0;
#endif
      while (p) {
        m = b + p/2;
        if (j == palette[m].i)
          goto result;
        if (j > palette[m].i) {
          b = m + 1;
          p -= (p>>1) + 1;
        } else {
          p >>= 1;
        }
      }
      fail(fail_BAD_DATA,"Erk! (%08lX)", j); /* This Never Happens */
result:
      /* store the index */
      im2[x] = m;
    } while (++x<width);
    im+=width;
    im2+=width+3 & ~3;
#ifdef WITH_BGND
  }
  /* if we have a background colour, ensure that it's in the palette */
  if (bkgd) {
    if (bgndindex != -1) {
      debug_printf("Background is at palette entry %i\n", bgndindex);
      palette[bgndindex].i = bgnd;
      bkgd->index = bgndindex;
    } else {
      debug_puts("Background occupies an extra palette entry");
      palette[colour].i = bgnd;
      bkgd->index = colour++;
    }
#endif /* WITH_BGND */
  }
  _swi (Hourglass_LEDs, _INR (0, 1),  LEDs, 0); /* hourglass LED 1 off */
#ifdef WITH_ALPHA
  *imask = mask;
#endif
  *ipalette = (rgb_t*)palette;
  *entries = colour;
  if (verbose)
    puts("Image successfully reduced to 8bit");
  return (char *)image;

failed:
  if (palette) heap_free (palette);
  if (verbose)
    puts("Image cannot be reduced to 8bit");
  return 0;
}


#undef COLOURMASK
