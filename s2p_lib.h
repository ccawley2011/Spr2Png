#ifndef __spr2png_s2p_lib
#define __spr2png_s2p_lib

#include "library.h"
#include "png.h"

extern int png_init;
extern png_structp png_ptr;

/* In c.reduce: */

uint8_t *reduceto8 (uint32_t *image, uint32_t *mask,
  rgb_t **ipalette, int *entries, png_color_16 *bkgd);

uint8_t *reduceto8_alpha(uint32_t *image, uint8_t **mask,
  rgb_t **ipalette, int *entries, png_color_16 *bkgd);

bool reducetogrey (uint32_t *image, uint8_t *mask, bool rgba);

png_color_8 significantbits (uint32_t *image);

/* In c.freqsort: */

void frequency_sort (uint8_t *const image, rgb_t *const palette,
  uint8_t *const alpha, int simplemask, png_color_16 *const bkgd);

/* In c.trim*: */

extern int trim_mask_8 (uint8_t *const image, uint8_t *const alpha, int maskcolour);
extern int trim_mask_8a (grey_t *const image);
extern int trim_mask_24a (rgb_t *const image);
extern int trim_mask_24 (rgb_t *const image, uint8_t *const alpha, uint32_t maskcolour);

#endif
