/* Paletted image: usage frequency sorting */

#include <stdio.h>
#include "png.h"
#include "s2p_lib.h"

#define SWAP(x,y,t) { t __swap=x; x=y; y=__swap; }

void frequency_sort(uint8_t *const image, rgb_t *const palette,
  uint8_t *const alpha, int simplemask, png_color_16 *const bkgd)
{
  int32_t x, y;
  int count[256] = {0};
  uint8_t sort[256];
  uint8_t map[256];
  uint8_t *im;

  if (simplemask) simplemask=1;

  if (verbose > 1)
    puts("Sorting palette by usage");

  debug_puts("Frequency count");
  /* Frequency count */
  im = image;
  y = height;
  do {
    x = width;
    do {
      x--;
      count[im[x]]++;
    } while (x);
    im += width+3 & -4;
  } while (--y);

  debug_puts("Sorting...");
  /* Reverse sort (nice and simple) */
  /* First, we need to fill in the reverse mapping table */
  for (x = 255; x >= 0; --x)
    sort[x] = (uint8_t) x;
  for (x = simplemask; x < 255; x++)
    if (alpha) {
      for (y = x + 1; y < 256; y++)
        if (alpha[y] < alpha[x]
            || (alpha[y] == alpha[x] && count[y]>count[x])) {
          SWAP (palette[y] ,palette[x], rgb_t);
          SWAP (count[y] ,count[x], int);
          SWAP (sort[y] ,sort[x], uint8_t);
          SWAP (alpha[y] ,alpha[x], uint8_t);
        }
    } else {
      for (y = x + 1; y < 256; y++)
        if (count[y] > count[x]) {
          SWAP(palette[y], palette[x], rgb_t);
          SWAP(count[y], count[x], int);
          SWAP(sort[y], sort[x], uint8_t);
      }
    }
  /* Now we build the forward mapping table */
  for (x=255; x>=0; --x) map[sort[x]]=(uint8_t)x;

  debug_puts("Modifying the image");
  /* Modify the image */
  im = image;
  y = height;
  do {
    x = width;
    do {
      x--;
      im[x]=map[im[x]];
    } while (x);
    im += width+3 & -4;
  } while (--y);

  bkgd->index = map[bkgd->index];
}
