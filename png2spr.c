#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "swis.h"
#include "kernel.h"
#include "zlib.h"
#include "png.h"
#include "library.h"

#include <assert.h>

/* This code is RISC OS specific. */
/* It makes assumptions about the sizes of various types. */

extern const char program_version[];

static jmp_buf jump;
static _kernel_oserror sigerr;

static png_structp png_ptr;
static png_infop info_ptr;
static int png_init;

static long bgnd = -2;
static double display_gamma = 2.2;
static double image_gamma = 0;
static char pack_mask = 0;
static struct
  {
    char use;
    char reduce;
    char separate;
    char tRNS;
    char inverse;
    char wide;
  } alpha = { 1 };

static struct
  {
    png_bytep list;
    int len;
    int colour;
  } trns;

static char free_dpi = 0;

static long row_width;


#define IS_GREY(x) \
  ((x)== PNG_COLOR_TYPE_GRAY || (x) == PNG_COLOR_TYPE_GRAY_ALPHA)

static void
version (void)
{
  printf ("%s %s " COPYRIGHT " Darren Salt\n"
          "Uses libpng %s and zlib %s.\n"
          "For copyright information, see the png2spr help file.\n",
          program_name, program_version, png_libpng_ver, zlibVersion ());
  exit (0);
}


static void
help (void)
{
  printf ("Usage: %s [<options>] <src> <dest>\n"
"\n"
"  -b, --background[=BGND]    specify a background colour (format = &BBGGRR)\n"
"                               if none, then use image-specified or white\n"
"  -g, --gamma[=GAMMA]        specify a gamma correction value\n"
"      --image-gamma[=GAMMA]    if none, then use image-specified or 1/2.2\n"
"  -d, --display-gamma=GAMMA  specify display gamma correction value\n"
"                               if none, then use 2.2; requires --gamma\n"
"  -M, --no-alpha             ignore any transparency information\n"
"  -p  --pack-mask            pack simple masks to 1bpp (RISC OS 3.6+)\n"
"  -r, --reduce-alpha         reduce the alpha channel to a simple mask\n"
"  -s, --separate-alpha       create a separate alpha channel\n"
"  -n, --inverse-alpha        invert the alpha channel (ie. 0=solid)\n"
"  -w  --wide-mask            use wide masks for the alpha channel\n"
"                             (RISC OS Select, 5.21+)\n"
"  -f, --free-dpi             allow any DPI values (don't clamp to RISC OS)\n"
"\n"
"      --help                 display this help text then exit\n"
"      --version              display the version number then exit\n",
          program_name);
  exit (0);
}


static void
sighandler (int sig)
{
  sigerr = *_kernel_last_oserror ();
  longjmp (jump, sig);
  exit (fail_OS_ERROR);
}


static void
shutdown (void)
{
  static int recur = 0;
  if (recur)
    return;
  debug_puts ("Shutting down...");
  recur = 1;
  switch (png_init)
    {
    case 1: png_destroy_read_struct (&png_ptr, 0, 0); break;
    case 2: png_destroy_read_struct (&png_ptr, &info_ptr, 0); break;
    }
  png_init = 0;
  _swi (Hourglass_Off, 0);
  debug_puts ("Done.");
}


typedef enum
  {
    mask_COMPLEX,
    mask_SIMPLE,
    mask_OPAQUE
  } mask_type;


static mask_type
find_mask_type (png_bytep mask, int indent, int step)
{
  png_uint_32 x, y;
  mask_type all255 = mask_OPAQUE;
  debug_printf ("Checking mask... (%p)\n", mask);
  mask += indent;
  for (y = height; y; --y)
    {
      for (x = width; x; --x)
        {
          int p = *mask; mask += step;
          if (p == 0) all255 = mask_SIMPLE;
          else if (p < 255) return mask_COMPLEX;
        }
      mask = (png_bytep) (((int) mask - indent + 3) & ~3) + indent;
    }
  return all255;
}


static void
add_grey_palette (spritearea_t *const area, sprite_t *const spr)
{
  int y, *pal = (int *) spr + 11;
  onerr (_swix (ColourTrans_WritePalette, _INR (0, 4), area, spr, 0x8000, 0, 1));
  for (y = 255; y >= 0; --y)
    pal[y*2+1] = pal[y*2] = (y * 0x10101) << 8;
}


static void
makemask (png_bytep mask, png_bytep spr, int step, int mask_depth)
{
  long x, y;

  debug_printf ("-> making %dbpp mask", mask_depth);
  spr -= 1;
  for (y = height; y; --y)
    {
      int val = (1 << mask_depth) - 1;
      int b = val, w = 0;
      for (x = width; x; --x)
        {
          if (*(spr += step))
            w |= b;
          if ((b = b << mask_depth) > 255)
            {
              *mask++ = w;
              b = val; w = 0;
            }
        }
      if (b != val)
        *mask++ = w;
      mask = (png_bytep) (((int) mask + 3) & ~3);
      spr = (png_bytep) (((int) spr + 4 - step) & ~3) + step - 1;
    }
}


static void
make_trns (png_bytep mask, png_bytep spr, int bit_depth, int colour_type, int mask_depth)
{
  long x, y;

  if (colour_type == PNG_COLOR_TYPE_RGB ||
      colour_type == PNG_COLOR_TYPE_RGB_ALPHA)
    {
      png_uint_32p spr4 = (png_uint_32p) spr;
      debug_printf ("Simple mask, 24bit (&%06X)\n", trns.colour);
      for (y = height; y; --y)
        {
          int val = (1 << mask_depth) - 1;
          int b = val, w = 0;
          for (x = width; x; --x)
            {
              if (*spr4++ != trns.colour)
                w |= b;
              if ((b = b << mask_depth) > 255)
                {
                  *mask++ = w;
                  b = val; w = 0;
                }
            }
          if (b != val)
            *mask++ = w;
          mask = (png_bytep) (((int) mask + 3) & ~3);
        }
    }
  else
    {
      int pb = (1 << bit_depth) - 1;
      debug_printf
        ("Simple mask, %ibit (&%02X, mask = &%02X, colour type = %i)\n",
         bit_depth, trns.colour, pb, colour_type);
      for (y = height; y; --y)
        {
          int val = (1 << mask_depth) - 1;
          int b = val, w = 0, ps = 0, c = *spr++;
          for (x = width; x; --x)
            {
              int cc = (c >> ps) & pb;
              ps += bit_depth;
              if (ps == 8)
                {
                  ps = 0;
                  c = *spr++;
                }
              switch (trns.len)
                {
                case 0:  if (cc != trns.colour) w |= b; break;
                default: if (cc >= trns.len || trns.list[cc]) w |= b;
                }
              if ((b = b << mask_depth) > 255)
                {
                  *mask++ = w;
                  b = val; w = 0;
                }
            }
          if (b != val)
            *mask++ = w;
          if (!ps)
            spr--;
          mask = (png_bytep) (((int) mask + 3) & ~3);
          spr = (png_bytep) (((int) spr + 3) & ~3);
        }
    }
}


static int
clamp_dpi (int dpi)
{
  return (dpi < 32) ? 22 : (dpi < 64) ? 45 : (dpi < 128) ? 90 : 180;
}


static spritearea_t *
read_png(FILE *fp)
{
  int obit_depth, bit_depth, colour_type, interlace_type;
  int dpix = 90, dpiy = 90;
  long x, y;
  png_color_16 mbgnd;
  const char *pngid = "png";
  const char *maskid = "mask";

  png_ptr = png_create_read_struct (PNG_LIBPNG_VER_STRING, 0, 0, 0);
  if (png_ptr == NULL)
    fail (fail_LIBPNG_FAIL, "libpng init failure");
  png_init = 1;

  /* Allocate/initialize the memory for image information.  REQUIRED. */
  info_ptr = png_create_info_struct (png_ptr);
  if (info_ptr == NULL)
    fail (fail_LIBPNG_FAIL, "libpng init failure");
  png_init = 2;

  if (setjmp (png_jmpbuf(png_ptr)))
    {
      if (fp)
        {
          fclose (fp);
          fp = 0;
        }
      fail (fail_LIBPNG_FAIL, 0);
    }

  debug_puts ("Initialising I/O...");

  png_init_io (png_ptr, fp);
  png_read_info (png_ptr, info_ptr);

  debug_puts ("Initialising image reading...");

  png_get_IHDR (png_ptr, info_ptr, (png_uint_32 *) &width,
                (png_uint_32 *) &height, &obit_depth, &colour_type,
                &interlace_type, NULL, NULL);
  bit_depth = obit_depth;
  if (bit_depth == 16)
    {
      png_set_strip_16(png_ptr); /* strip 16bit -> 8bit */
      bit_depth = 8;
    }

  /* png_set_packing (png_ptr); ** force min. 8bpp */
  png_set_packswap(png_ptr); /* left pixel in low bit(s) */

  if (!alpha.use)
    png_set_strip_alpha (png_ptr); /* strip alpha, no bgnd */

#ifdef DEBUG
  printf ("Source has\n  size = %li x %li\n  depth %i\n  colour type %i\n  interlace type %i\n", width, height, bit_depth, colour_type, interlace_type);
  if (colour_type & PNG_COLOR_MASK_ALPHA)
    puts ("  alpha channel");
  puts ("Initialising image translation...");
#endif

  if (alpha.use && png_get_valid (png_ptr, info_ptr, PNG_INFO_tRNS))
    {
      png_color_16p colour;
      alpha.tRNS = 1;
      png_get_tRNS (png_ptr, info_ptr, &trns.list, &trns.len, &colour);
      if (!trns.list)
        trns.len = 0;
      debug_printf ("tRNS chunk:\n  list has %i entries\n  colour index %i\n    RGB %04X%04X%04X\n    grey %04X\n", trns.len, colour->index, colour->red, colour->green, colour->blue, colour->gray);
      switch (colour_type & ~PNG_COLOR_MASK_ALPHA)
        {
        case PNG_COLOR_TYPE_PALETTE:
          trns.colour = colour->index;
          debug_printf ("  -> colour = &%02X\n", trns.colour);
          break;
        case PNG_COLOR_TYPE_GRAY:
          if (obit_depth == 16)
            trns.colour = colour->gray >> 8;
          else
            trns.colour = colour->gray;
          debug_printf ("  -> grey = &%02X\n", trns.colour);
          break;
        case PNG_COLOR_TYPE_RGB:
          if (obit_depth == 16)
            trns.colour = colour->red >> 8 | (colour->green >> 8) << 8 |
                          (colour->blue >> 8) << 16;
          else
            trns.colour = colour->red | colour->green << 8 |
                          colour->blue << 16;
          debug_printf ("  -> colour = &%06X\n", trns.colour);
        }
      if (colour_type & PNG_COLOR_MASK_ALPHA)
        {
          alpha.tRNS = 0;
          png_set_tRNS_to_alpha (png_ptr);
          debug_puts ("  -> list will be converted\n");
        }
      else if (trns.list)
        {
          int i;
          for (i = trns.len - 1; i >= 0; --i)
            if (trns.list[i] && trns.list[i] < 255)
              {
                alpha.tRNS = 0;
                png_set_tRNS_to_alpha (png_ptr);
                if (colour_type == PNG_COLOR_TYPE_PALETTE)
                  bit_depth = 24;
                colour_type |= PNG_COLOR_MASK_ALPHA;
                break;
              }
          debug_printf ("  -> list will be %s\n",
                        alpha.tRNS ? "used" : "converted");
        }
    }

  if (bgnd != -2)
    {
      png_color_16 *ibgnd;
      debug_puts ("Setting background colour...");
      if (bgnd == -1 && png_get_bKGD (png_ptr, info_ptr, &ibgnd))
        png_set_background (png_ptr, ibgnd, PNG_BACKGROUND_GAMMA_FILE, 1,
                            1.0);
      else
        {
          /* Usefully, if bgnd == -1, this will select white */
          mbgnd.index = 0;
          mbgnd.red   = (png_uint_16) (bgnd & 0xFF);
          mbgnd.green = (png_uint_16) ((bgnd >>  8) & 0xFF);
          mbgnd.blue  = (png_uint_16) ((bgnd >> 16) & 0xFF);
          mbgnd.gray = (png_uint_16)
                       (mbgnd.red * .299 + mbgnd.green * .587 +
                        mbgnd.blue * .114);
          if ((colour_type & ~PNG_COLOR_MASK_ALPHA) == PNG_COLOR_TYPE_GRAY
              && (mbgnd.red != mbgnd.green || mbgnd.red != mbgnd.blue ||
                  mbgnd.green != mbgnd.blue))
            {
              colour_type |= PNG_COLOR_MASK_COLOR;
              bit_depth = 24;
              png_set_gray_to_rgb (png_ptr);
            }
          png_set_background (png_ptr, &mbgnd, PNG_BACKGROUND_GAMMA_SCREEN,
                              0, 1.0);
        }
      pngid = alpha.use ? "png_blend" : "png_unblend";
      alpha.use = 0;
    }
  else if (!alpha.use && (colour_type && PNG_COLOR_MASK_ALPHA))
    pngid = "png_unmask";

  /* If we don't have another value */
  if (image_gamma == 0)
    {
      debug_puts ("Setting sRGB & gamma ...");
      png_set_sRGB (png_ptr, info_ptr, PNG_sRGB_INTENT_ABSOLUTE);
      png_set_gamma (png_ptr, display_gamma, 1/2.2);
    }
  else
    {
      int intent;
      debug_puts ("Setting sRGB or gamma...");
      if (png_get_sRGB (png_ptr, info_ptr, &intent))
        png_set_sRGB (png_ptr, info_ptr, intent);
      else
        {
          if (image_gamma < 0 &&
              !png_get_gAMA(png_ptr, info_ptr, &image_gamma))
            image_gamma = 1/2.2;
          png_set_gamma(png_ptr, display_gamma, image_gamma);
        }
    }
  if ((colour_type & ~PNG_COLOR_MASK_ALPHA) == PNG_COLOR_TYPE_RGB)
    bit_depth = 24;

  if ((colour_type & PNG_COLOR_MASK_ALPHA) == 0 && !alpha.tRNS)
    {
      debug_puts ("(no need for alpha)");
      alpha.use = 0;
    }

  if (alpha.wide && (!alpha.use || alpha.tRNS || alpha.separate))
    {
      debug_puts ("(no need for wide masks)");
      alpha.wide = 0;
    }

  if (colour_type == PNG_COLOR_TYPE_RGB)
    {
      debug_puts ("(adding filler)");
      png_set_filler(png_ptr, 0x00, PNG_FILLER_AFTER); /* RGBA if appropriate */
    }

  png_read_update_info(png_ptr, info_ptr); /* Update info structure etc. */

  {
    spritearea_t *spr_area;
    sprite_t *spr_ptr;
    png_bytep spr_base, *row_ptrs;
    unsigned int mode, palsize = 0;
    row_width = png_get_rowbytes(png_ptr, info_ptr) + 3 & ~3;
    size_t size = 16 + 2 * (44 + 256 * 4 * 2); /* 2 sprites, 2 palettes */

    if (IS_GREY (colour_type))
        debug_puts ("(is grey)");
    else
        debug_puts ("(is rgb)");

    size += row_width * height; /* sprite image */
    if ((colour_type & PNG_COLOR_MASK_ALPHA) && alpha.use)
      {
        debug_puts ("(has alpha)");
        size += ((width + 3) & ~3) * height; /* mask sprite */
      }
    else if (alpha.tRNS)
      {
        debug_puts ("(has simple mask)");
        size += ((width + 31) >> 3 & ~3) * height; /* sprite mask */
      }

    debug_printf ("Creating sprite (area size = %X)...\n", size);
    spr_area = malloc (size);
    row_ptrs = calloc (height, sizeof(char *));
    if (!spr_area || !row_ptrs)
      fail (fail_NO_MEM, "out of memory");
    spr_area->size = size;
    spr_area->sprites = 0;
    spr_area->first = 16;
    spr_area->free = 16;

    {
      png_uint_32 ppix = png_get_x_pixels_per_meter (png_ptr, info_ptr);
      png_uint_32 ppiy = png_get_y_pixels_per_meter (png_ptr, info_ptr);
      debug_printf ("Stored DPM = %i x %i\n", ppix, ppiy);
      if (ppix != 0 && ppiy != 0)
        {
          static const float mpi = 0.0254f;
          ppix = ppix * mpi + .5;
          ppiy = ppiy * mpi + .5;
          dpix = free_dpi ? ppix : clamp_dpi (ppix);
          dpiy = free_dpi ? ppiy : clamp_dpi (ppiy);
          debug_printf ("DPI = %i x %i\n", dpix, dpiy);
        }
    }

    if (bit_depth > 8 || dpix != 90 || dpiy != 90 || pack_mask || alpha.wide)
      {
        switch (bit_depth)
          {
          case  1: mode = 1; palsize =   2; break;
          case  2: mode = 2; palsize =   4; break;
          case  4: mode = 3; palsize =  16; break;
          case  8: mode = 4; palsize = 256; break;
          case 16: mode = 5; break;
          default: mode = 6;
        }
        mode = mode << 27 | 90 << 14 | 90 << 1 | 1;

        if (alpha.wide)
          mode |= 1 << 31;
      }
    else
      {
        switch (bit_depth)
          {
          case  1: mode = 25; palsize =   2; break;
          case  2: mode = 26; palsize =   4; break;
          case  4: mode = 27; palsize =  16; break;
          default: mode = 28; palsize = 256; break;
        }
      }

    onerr (_swix (OS_SpriteOp, _INR (0, 6),
                  256+15, spr_area, pngid, 0, width, height, mode));
    _swix (OS_SpriteOp, _INR (0, 2) | _OUT (2), 256+24, spr_area, pngid, &spr_ptr);
    debug_printf ("Image will have:\n  %i-bit colour (mode %x)\n", bit_depth, mode);

    if (palsize) /* write palette */
      {
        int *const pal = (int *) spr_ptr + 11;
        if (IS_GREY (colour_type))
          {
            /* cheat: force full-size palette */
            onerr (_swix (ColourTrans_WritePalette, _INR (0, 4),
                          spr_area, spr_ptr, 0x8000 /* rubbish */, 0, 1));
            debug_puts ("  a greyscale palette");
            for (y = --palsize; y >= 0; --y)
              pal[y*2+1] = pal[y*2] = ((y * 255 / palsize) * 0x10101) << 8;
          }
        else if (palsize && png_get_valid (png_ptr, info_ptr, PNG_INFO_PLTE))
          {
            int *palptr;
            /* cheat: force full-size palette */
            onerr (_swix (ColourTrans_WritePalette, _INR (0, 4),
                          spr_area, spr_ptr, 0x8000 /* rubbish */, 0, 1));
            memset (pal, 0, palsize*2*4);
            png_get_PLTE (png_ptr, info_ptr, (png_color **) &palptr,
                          (int *) &y);
            debug_printf ("  a colour palette with %li entries\n", y);
#ifdef USING_PNG_MODULE
            for (--y; y >= 0; --y)
              pal[y*2+1] = pal[y*2] = palptr[y];
#else
            for (--y; y >= 0; --y)
              pal[y*2+1] = pal[y*2] = palptr[y] << 8;
#endif
          }
      }

#ifdef DEBUG
    if (alpha.tRNS)
      puts ("  simple transparency (possibly subject to reduction)");
    else if ((colour_type & PNG_COLOR_MASK_ALPHA) && alpha.use)
      puts ("  an alpha channel (possibly subject to reduction)");
#endif

    debug_puts ("Rendering...");
    spr_base = ((png_bytep) spr_ptr) + spr_ptr->image;
    for (y = 0; y < height; ++y)
      row_ptrs[y] = spr_base + y * row_width;
    png_read_image (png_ptr, row_ptrs);
    free (row_ptrs);

    if (alpha.use && alpha.tRNS)
      {
        debug_puts ("Processing simple mask...\n");
        if (alpha.separate)
          {
            sprite_t *mask_ptr;
            onerr (_swix (OS_SpriteOp, _INR (0, 6), 256+15,
                          spr_area, maskid, 0, width, height, 25));
            _swix (OS_SpriteOp, _INR (0, 2) | _OUT (2),
                   256+24, spr_area, maskid,  &mask_ptr);
            make_trns ((png_bytep) mask_ptr + mask_ptr->image,
                       (png_bytep) spr_ptr + spr_ptr->image,
                        bit_depth, colour_type, 1);
            if (alpha.inverse)
              {
                png_bytep p = (png_bytep) mask_ptr + mask_ptr->image;
                y = (int) ((((width + 31) >> 3) & ~3) * height);
                do
                  {
                    y--; p[y] = ~p[y];
                  }
                while (y);
                _swix (OS_SpriteOp, _INR (0, 3), 256+26, spr_area, maskid,
                       "mask_i");
              }
          }
        else
          {
            onerr (_swix (OS_SpriteOp, _INR (0, 2),  256+29, spr_area, pngid));
            if (mode > 255)
                make_trns ((png_bytep) spr_ptr + spr_ptr->mask,
                           (png_bytep) spr_ptr + spr_ptr->image,
                           bit_depth, colour_type, 1);
            else
                make_trns ((png_bytep) spr_ptr + spr_ptr->mask,
                           (png_bytep) spr_ptr + spr_ptr->image,
                           bit_depth, colour_type, bit_depth);
          }
      }
    else if (alpha.use && IS_GREY (colour_type))
      {
        int x;
        png_bytep mask_base, merged = spr_base;
        debug_puts ("Processing grey+alpha...");
        if (alpha.use)
          {
            sprite_t *mask_ptr;
            png_bytep mask;
            mask_type is = find_mask_type (spr_base, 1, 2);
            switch (is)
              {
              case mask_COMPLEX:
              case mask_SIMPLE:
                debug_puts ((is == mask_SIMPLE) ? "-> simple" : "-> complex");
                mask = malloc ((int) (((width + 3) & ~3) * height));
                if (!mask)
                  fail (fail_NO_MEM, "out of memory");
                mask_base = mask;
                memset (mask, 0,
                        (int) ((((width + 31) >> 3) & ~3) * height));
                for (y = (int) height; y; --y)
                  {
                    for (x = (int) width; x; --x)
                      {
                        *spr_base++ = *merged++;
                        *mask_base++ = *merged++;
                      }
                    spr_base  = (png_bytep) (((int) spr_base  + 3) & ~3);
                    mask_base = (png_bytep) (((int) mask_base + 3) & ~3);
                    merged    = (png_bytep) (((int) merged    + 3) & ~3);
                  }
                if (alpha.wide && !alpha.separate)
                  {
                    _swix (OS_SpriteOp, _INR (0, 2), 256+29, spr_area, pngid);
                    memcpy ((png_bytep) spr_ptr + spr_ptr->mask, mask,
                            (int) (((width + 3) & ~3) * height));
                  }
                else if ((is == mask_SIMPLE || alpha.reduce) && !alpha.separate)
                  {
                    _swix (OS_SpriteOp, _INR (0, 2), 256+29, spr_area, pngid);
                    if (mode > 255)
                      {
                        memset ((png_bytep) spr_ptr + spr_ptr->mask, 0,
                                (int) ((((width + 31) >> 3) & ~3) * height));
                        makemask ((png_bytep) spr_ptr + spr_ptr->mask, mask, 1, 1);
                      }
                    else
                      {
                        memset ((png_bytep) spr_ptr + spr_ptr->mask, 0,
                                (int) (row_width * height));
                        makemask ((png_bytep) spr_ptr + spr_ptr->mask, mask, 1, bit_depth);
                      }
                  }
                else
                  {
                    onerr (_swix (OS_SpriteOp, _INR (0, 6), 256+15,
                                  spr_area, maskid, 0, width, height, 28));
                    _swix (OS_SpriteOp, _INR (0, 2) | _OUT (2),
                           256+24, spr_area, maskid,  &mask_ptr);
                    add_grey_palette (spr_area, mask_ptr);
                    if (alpha.inverse)
                      {
                        png_bytep p = (png_bytep) mask_ptr + mask_ptr->image;
                        y = (int) (((width + 3) & ~3) * height);
                        do
                          {
                            y--; p[y] = ~mask[y];
                          }
                        while (y);
                        _swix (OS_SpriteOp, _INR (0, 3), 256+26, spr_area, maskid,
                               "mask_i");
                      }
                    else
                      memcpy ((png_bytep) mask_ptr + mask_ptr->image, mask,
                              (int) (((width + 3) & ~3) * height));
                  }
                free (mask);
                break;
              case mask_OPAQUE:
                debug_puts ("-> unnecessary");
                goto lose_mask_grey;
              }
          }
        else
          {
            /**/lose_mask_grey:
            for (y = (int) height; y; --y)
              {
                for (x = (int) width; x; --x)
                  {
                    *spr_base++ = *merged;
                    merged += 2;
                  }
                spr_base = (png_bytep) (((int) spr_base + 3) & ~3);
                merged   = (png_bytep) (((int) merged   + 3) & ~3);
              }
          }
      }
    else if (alpha.use && (colour_type & PNG_COLOR_MASK_ALPHA))
      {
        sprite_t *mask_ptr;
        png_bytep mask_base;
        debug_puts ("Processing RGBA...");
        switch (find_mask_type (spr_base, 3, 4))
          {
          case mask_COMPLEX:
            debug_puts ("-> complex");
            if (alpha.reduce)
              goto reduce_complex;
            if (alpha.separate)
              {
                /**/separate_simple:
                onerr (_swix (OS_SpriteOp, _INR (0, 6),  256+15,
                              spr_area, maskid, 0, width, height, 28));
                _swix (OS_SpriteOp, _INR (0, 2) | _OUT (2),
                       256+24, spr_area, maskid,  &mask_ptr);
                add_grey_palette (spr_area, mask_ptr);
                mask_base = (png_bytep) mask_ptr + mask_ptr->image;
                spr_base -= 1;
                if (alpha.inverse)
                  for (y = (int) height; y; --y)
                    {
                      for (x = (int) width; x; --x)
                        {
                          *mask_base++ = ~*(spr_base += 4);
                          *spr_base = 0;
                        }
                      mask_base = (png_bytep) (((int) mask_base + 3) & ~3);
                    }
                else
                  for (y = (int) height; y; --y)
                    {
                      for (x = (int) width; x; --x)
                        {
                          *mask_base++ = *(spr_base += 4);
                          *spr_base = 0;
                        }
                      mask_base = (png_bytep) (((int) mask_base + 3) & ~3);
                    }
              }
            else if (alpha.wide)
              {
                /**/wide_simple:
                _swix (OS_SpriteOp, _INR (0, 2), 256+29, spr_area, pngid);
                mask_base = (png_bytep) spr_ptr + spr_ptr->mask;
                spr_base -= 1;
                for (y = (int) height; y; --y)
                  {
                    for (x = (int) width; x; --x)
                      {
                        *mask_base++ = *(spr_base += 4);
                        *spr_base = 0;
                      }
                    mask_base = (png_bytep) (((int) mask_base + 3) & ~3);
                  }
              }
            else
              {
                if (alpha.inverse)
                  {
                    spr_base += 3;
                    y = (int) (((width + 3) & ~3) * height);
                    do
                      {
                        y--; spr_base[y*4] = ~spr_base[y*4];
                      }
                    while (y);
                    _swix (OS_SpriteOp, _INR (0, 3), 256+26, spr_area, pngid,
                           "png_rgba_i");
                  }
                else
                  _swix (OS_SpriteOp, _INR(0, 3), 256+26, spr_area, pngid,
                         "png_rgba");
              }
            break;
          case mask_SIMPLE:
            debug_puts ("-> simple");
            if (alpha.separate)
              goto separate_simple;
            else if (alpha.wide)
              goto wide_simple;
            /**/reduce_complex:
            _swix (OS_SpriteOp, _INR (0, 2),  256+29, spr_area, pngid);
            if (mode > 255)
                makemask ((png_bytep) spr_ptr + spr_ptr->mask, spr_base, 4, 1);
            else
                makemask ((png_bytep) spr_ptr + spr_ptr->mask, spr_base, 4, bit_depth);
            break;
          case mask_OPAQUE:
            debug_puts ("-> unnecessary");
            spr_base -= 1;
            for (y = (int) height; y; --y)
              for (x = (int) width; x; --x)
                *(spr_base += 4) = 0;
            break;
          }
      }
    if (mode > 255)
      spr_ptr->mode = (spr_ptr->mode & 0xF8000001) | dpiy << 14 | dpix << 1;
    return spr_area;
  }
}


static const optslist args[] = {
  {1, "help", NO_PARAM},
  {2, "version", NO_PARAM},
  {'M', "no-alpha", NO_PARAM},
  {'b', "background", OPTIONAL_PARAM},
  {'c', "check-mask", NO_PARAM},
  {'d', "display-gamma", OPTIONAL_PARAM},
  {'f', "free-dpi", NO_PARAM},
  {'g', "gamma", OPTIONAL_PARAM},
  {'g', "image-gamma", OPTIONAL_PARAM},
  {'n', "inverse-alpha", NO_PARAM},
  {'p', "pack-mask", NO_PARAM},
  {'s', "separate-alpha", NO_PARAM},
  {'w', "wide-mask", NO_PARAM},
  {3, "cname", REQUIRED_PARAM},
  {4, "csprite", REQUIRED_PARAM},
  {0, 0, NO_PARAM}
};


int
main (int argc, char *argv[])
{
  FILE *fp;
  spritearea_t *spr;
  int y;
  const char *from = 0, *to = 0;

  {
    char *p = strrchr (argv[0], '.');
    char *q = strrchr (argv[0], ':');
    if (p < q)
      p = q;
    program_name = p ? p + 1 : argv[0];
  }

  atexit (shutdown);

  switch (setjmp (jump))
    {
    case 0:
      break;
    case SIGINT:
      fail (fail_OS_ERROR, "Escape");
    default:
      {
        int *regdump, *os_regdump;
        const char *msg = 0;
        _swix (OS_ChangeEnvironment, _INR (0, 3) | _OUT (3), 7, 0, 0, 0, &regdump);
        _swix (OS_ChangeEnvironment, _INR (0, 3) | _OUT (1), 13, 0, 0, 0, &os_regdump);
        if (regdump && os_regdump)
          memcpy (os_regdump, regdump, 64);
        switch (sigerr.errnum & 0xFFFFFF)
          {
          case 0:
            msg = "Illegal instruction";
            break;
          case 1:
            msg = "Prefetch abort";
            break;
          case 2:
            msg = "Data abort";
            break;
          case 3:
            msg = "Address exception";
            break;
          case 5:
            msg = "Branch through zero";
            break;
          }
        if (msg)
          fail (fail_OS_ERROR, "Internal error: %s at &%08X", msg,
                regdump[15]);
        else
          fail (fail_OS_ERROR, "%s", sigerr.errmess);
      }
    }

  setsignal (sighandler);

//  init_task ("Png2Spr backend", argv[0]);

  y = 0;
  while (++y < argc)
    {
      const char *p = argv[y];

      if (p[0] == '-' && p[1] == '-')
        {
          const char *arg;
          p += 2;
          switch (argmatch (args, p, &arg))
            {
            case 1:
              help ();
            case 2:
              version ();
            case 3:
              caller_name = arg;
              break;
            case 4:
              caller_sprite = arg;
              break;
            case 'M':
              alpha.use = 0;
              break;
            case 'p':
              pack_mask = 1;
              break;
            case 'r':
              alpha.reduce = 1;
              break;
            case 's':
              alpha.separate = 1;
              break;
            case 'b':
              if ((p = arg) != 0)
                goto get_background;
              bgnd = -1;
              break;
            case 'd':
              p = arg;
              goto get_dgamma;
            case 'f':
              free_dpi = 1;
              break;
            case 'g':
              if ((p = arg) != 0)
                goto get_igamma;
              image_gamma = -1;
              break;
            case 'n':
              alpha.inverse = 1;
              break;
            case 'w':
              alpha.wide = 1;
            }
        }
      else if (p[0] == '-')
        {
          if (*++p == '\0')
            fail (fail_BAD_ARGUMENT, "cannot use stdin");
          do
            {
              switch (*p)
                {
                case 'M':
                  alpha.use = 0;
                  break;
                case 'p':
                  pack_mask = 1;
                  break;
                case 'r':
                  alpha.reduce = 1;
                  break;
                case 's':
                  alpha.separate = 1;
                  break;
                case 'b':
                  if (!p[1])
                    {
                      bgnd = -1;
                      break;
                    }
                  p++;
                  /**/get_background:
                  {
                    errno = 0;
                    bgnd = strtol (p, (char **) &p, 16) & 0xFFFFFF;
                    if (errno || *p--)
                      fail (fail_BAD_ARGUMENT, "bad background colour value");
                  }
                  break;
                case 'd':
                  if (!p[1])
                    fail (fail_BAD_ARGUMENT, "missing display gamma value");
                  p++;
                  /**/get_dgamma:
                  display_gamma = readfloat (&p);
                  if (*p--
                      || display_gamma <= 0
                      || display_gamma > 10 /* arbitrary upper limit */ )
                    fail (fail_BAD_ARGUMENT, "bad gamma value %g",
                          display_gamma);
                  break;
                case 'f':
                  free_dpi = 0;
                  break;
                case 'g':
                  if (!p[1])
                    {
                      image_gamma = 1/2.2;
                      break;
                    }
                  p++;
                  /**/get_igamma:
                  image_gamma = readfloat (&p);
                  if (*p--
                      || image_gamma <= 0
                      || image_gamma > 10 /* arbitrary upper limit */ )
                    fail (fail_BAD_ARGUMENT, "bad gamma value %g",
                          image_gamma);
                  break;
                case 'n':
                  alpha.inverse = 1;
                  break;
                case 'w':
                  alpha.wide = 1;
                  break;
                default:
                  fail (fail_BAD_ARGUMENT, "unknown option -%c\n", *p);
                }
            }
          while (*++p);
        }
      else if (from == 0)
        from = argv[y];
      else if (to == 0)
        to = argv[y];
      else
        fail (fail_BAD_ARGUMENT, "too many filenames");
    }
  if (!to)
    fail (fail_BAD_ARGUMENT,
          "too few filenames (need both input and output)");

  if (bgnd != -2)
    alpha.reduce = alpha.separate = 0;

  _swi (Hourglass_On, _IN (0), 0);

  fp = fopen (from, "rb");
  if (!fp)
    fail (fail_LOAD_ERROR, "file '%s' not found or not readable", argv[1]);
  spr = read_png (fp);
  fclose (fp);
  onerr (_swix (OS_SpriteOp, _INR (0, 2), 256+12, spr, to));

  return 0;
}
