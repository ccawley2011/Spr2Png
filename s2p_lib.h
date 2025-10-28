#ifndef __spr2png_s2p_lib
#define __spr2png_s2p_lib

#include "library.h"
#include "png.h"

extern int png_init;
extern png_structp png_ptr;

/* In c.reduce: */

char *reduceto8 (uint32_t *image, uint32_t *mask,
  rgb_t **ipalette, int *entries, png_color_16 *bkgd);

char *reduceto8_alpha(uint32_t *image, char **mask,
  rgb_t **ipalette, int *entries, png_color_16 *bkgd);

int reducetogrey (uint32_t *image, char *mask, int rgba);

png_color_8 significantbits (uint32_t *image);

/* In c.freqsort: */

void frequency_sort (char *const image, rgb_t *const palette,
  char *const alpha, int simplemask, png_color_16 *const bkgd);

/* In c.trim*: */

extern int trim_mask_8 (char *const image, char *const alpha, int maskcolour);
extern int trim_mask_8a (grey_t *const image);
extern int trim_mask_24a (rgb_t *const image);
extern int trim_mask_24 (rgb_t *const image, char *const alpha, uint32_t maskcolour);

#endif
