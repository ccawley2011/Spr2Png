#include <stdio.h>
#include "swis.h"
#include "library.h"
#include "d2s_lib.h"

void trim_mask_24 (spritearea_t *const area, sprite_t *const sprite,
		   int inverse)
{
  int x, y;
  rgb_t *ptr;
  int clear = inverse ? 255 : 0;
  rgb_t *image = (rgb_t*)((char*)sprite + sprite->image);
  char *mask;
  int simple = sprite->image != sprite->mask;
  int mwidth = (width + 31 & ~31) / 8;

  if (verbose)
    puts ("Trimming sprite...");

  /* Trim blank rows from bottom */
  y = 0;
  ptr = image + height * width;
  mask = (char*)sprite + sprite->mask + height * mwidth;
  do {
    ptr -= width;
    mask -= mwidth;
    x = width;
    if (simple) {
      while (x)
	if (--x, (mask[x/8] & 1<<(x & 7)) == 0)
	  goto do_lower_trim;
    } else {
      while (x)
	if (ptr[--x].alpha != clear)
	  goto do_lower_trim;
    }
  } while (++y < height);
  /* we'll only reach this point if the image is completely masked out */
  fail (fail_BAD_IMAGE, "this image is blank!");

do_lower_trim:
  if (y) {
    height -= y;
    _swi (0x2E /*OS_SpriteOp*/, 0x1F,  512+57, area, sprite, 0, -y);
  }
  if (verbose > 2)
    printf ("%i %s(s) removed from %s\n", y, "row", "bottom");

  /* Trim blank rows from top */
  y = 0;
  ptr = image;
  mask = (char*)sprite + sprite->mask;
  for (;;) {
    x = width;
    if (simple) {
      while (x)
	if (--x, (mask[x/8] & 1<<(x & 7)) == 0)
	  goto do_upper_trim;
    } else {
      while (x)
	if (ptr[--x].alpha != clear)
	  goto do_upper_trim;
    }
    ptr += width;
    mask += mwidth;
    y++;
  }

do_upper_trim:
  if (y) {
    height -= y;
    _swi (0x2E, 0x1F,  512+57, area, sprite, height, -y);
  }
  if (verbose > 2)
    printf ("%i row(s) removed from top\n", y);

  /* Trim blank columns from left */
  x = width;
  y = height;
  ptr = image;
  mask = (char*)sprite + sprite->mask;
  do {
    int xx = 0;
    if (simple)
      while (xx < x && (mask[xx/8] & 1<<(xx & 7)))
	xx++;
    else
      while (xx < x && ptr[xx].alpha == clear)
	xx++;
    if (xx < x) {
      x = xx;
      if (!x)
	break;
    }
    ptr += width;
    mask += mwidth;
  } while (--y);

  if (x) {
    width -= x;
    _swi (0x2E, 0x1F,  512+58, area, sprite, 0, -x);
  }
  if (verbose > 2)
    printf ("%i column(s) removed from left\n", x);

  /* Trim blank columns from right */
  x = width - 1;
  y = height;
  ptr = image;
  mask = (char*)sprite + sprite->mask;
  do {
    int xx = width;
    if (simple)
      while (--xx >= 0 && (mask[xx/8] & 1<<(xx & 7)))
	;
    else
      while (--xx >= 0 && ptr[xx].alpha == clear)
	;
    xx = width - xx - 1;
    if (xx < x) {
      x = xx;
      if (!x)
	break;
    }
    ptr += width;
    mask += mwidth;
  } while (--y);

  if (x) {
    width -= x;
    _swi (0x2E, 0x1F,  512+58, area, sprite, width, -x);
  }
  if (verbose > 2)
    printf ("%i column(s) removed from right\n", x);

  /* Done. */
}
