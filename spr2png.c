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

/* This code is RISC OS specific. */
/* It makes assumptions about the sizes of various types. */

#include "s2p_lib.h"
#include "s2p_const.h"


static jmp_buf jump;
static _kernel_oserror sigerr;

int png_init = 0;
png_structp png_ptr;

int verbose;


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
  if (png_init)
    png_destroy_write_struct (&png_ptr, (png_infopp) NULL);
  png_init = 0;
  _swi (Hourglass_Off, 0);
  debug_puts ("Done.");
}


#ifdef USE_PNG_MODULE
static char *
modver (const char *mod)
{
  char *hlp, *module;
  int i = 0;
  if (_swix (OS_Module, _INR (0, 1) | _OUT (3), 18, mod, &module))
    return "not loaded";
  module += ((int *) module)[5];
  do
  {
    char c = *module++;
    if (c == 9)
      i = (i + 8) & ~7;
    else
      ++i;
  }
  while (i < 16);
  i = 0;
  for (;;)
  {
    char c = *module++;
    ++i;
    if (c == 0 || c == 9 || c == 32)
      break;
  }
  hlp = malloc (i);
  if (!hlp)
    return "n/a";
  memcpy (hlp, module - i, i);
  hlp[i - 1] = 0;
  return hlp;
}


static void
version (void)
{
  char *pv = modver ("PNG"), *zv = modver ("ZLib");
  printf ("%s %s " COPYRIGHT " Darren Salt\n"
          "Uses libpng %s (module %s) and zlib %s (module %s).\n"
          "For copyright information, see the spr2png help file.\n",
          program_name, program_version,
          pv[0] == 'n' ? "x.x.x" : png_libpng_ver, pv,
          zv[0] == 'n' ? "x.x.x" : zlibVersion (), zv);
  exit (0);
}

#else

static void
version (void)
{
  printf ("%s %s " COPYRIGHT " Darren Salt\n"
          "Uses libpng %s and zlib %s.\n"
          "For copyright information, see the spr2png help file.\n",
          program_name, program_version, png_libpng_ver, zlibVersion ());
  exit (0);
}
#endif


static void
help (void)
{
  printf ("Usage: %s [<options>] [--] <src> <dest>\n"
          "\n"
          "  -i, --interlace         create an interlaced PNG\n"
          "  -b, --background=BGND   specify a background colour (format = &BBGGRR)\n"
          "  -x, --dpi               store scale information as pixels per metre\n"
          "  -X, --pixel-size        ditto, but as physical pixel size\n"
          "  -d, --set-dpi=X[,Y]     set dots-per-inch value(s), overriding the image\n"
          "\n"
          "Sprite options:\n"
          "  -a, --alpha             treat 32bpp image as RGBA instead of RGBX\n"
          "  -n, --inverse-alpha     alpha channel is inverse (ie. 0=solid)\n"
          "\n"
          "Draw and Artworks options:\n"
          "  -S, --scale=X[,Y]       set image scale (%%age, ratio, or value)\n"
          "  -m, --no-mask           don't generate a mask (but still allow trimming)\n"
          "  -M, --no-blend,         don't create an alpha channel or background blend\n"
          "      --no-alpha\n"
          "  -R, --render=LEVEL      Artworks rendering level (default is 11.0)\n"
          "                          (see 'draw2spr --help' for more information)\n"
          "\n"
          "Packing options:\n"
          "  -c, --check-mask        lose mask if unused, or image if fully masked out\n"
          "  -t, --trim              remove masked-out edge rows and columns\n"
          "  -r, --reduce            try to convert (back) to paletted RGB/RGBA\n"
          "  -G, --check-grey        try to convert to greyscale\n"
          "  -p, --pack-pixels       if the image can be made 1, 2 or 4bpp, do so\n"
          "  -s, --frequency-sort    sort palette, putting most-used colours first\n"
          "  -B, --significant-bits  create sBIT chunk (recommended for 16bpp sprites)\n"
          "\n"
          "Compression options:\n"
          "  -f, --filter=FLTR       filter type(s): None, Sub, Up, Avg, Paeth; all\n"
          "  -w, --window-bits=NUM   compression window size (9 to 15)\n"
          "  -z, --strategy=TYPE     compression strategy, by name or number:\n"
          "                          0=default, 1=filtered, 2=huffman, 3=rle\n"
          "  -1, --fast              compress faster\n"
          "  -9, --best              compress better (default)\n"
          "\n"
          "Settings for image renderers:\n"
          "  -g, --gamma=GAMMA       specify a gamma correction value\n"
          "  -I, --intent=INTENT     rendering intent, by number (-1...3) or name:\n"
          "                          none, perceptual, relative, saturation, absolute.\n"
          "                          prefix with '+' to set gamma and chromaticity\n"
          "  -C, --chromaticity=LIST set the chromaticity value pairs for white, red,\n"
          "                          green and blue. Values must be separated by commas;\n"
          "                          use 'w', 'r', 'g', 'b' for default value pairs.\n"
          "                          Value range is 0 to 1.\n"
          "\n"
          "  -v, --verbose           report conversion information\n"
          "      --help              display this help text then exit\n"
          "      --version           display the version number then exit\n",
          program_name);
  exit (0);
}


static char *
local_strdup (const char *s)
{
  char *d = spr_malloc (strlen (s) + 1, "strdup");
  strcpy (d, s);
  return d;
}


static void
checkspr (const sprite_t * spr)
{
  if (spr->size < 48 ||
      spr->image < 44 || spr->image > spr->size - 4 ||
      spr->mask < 44 || spr->mask > spr->size - 4 || spr->mask < spr->image)
    fail (fail_BAD_DATA, "Unrecognised sprite data");
}


static int
modevar (unsigned int mode, int var)
{
  return _swi (OS_ReadModeVariable, _INR (0, 1) | _RETURN (2), mode, var);
}


static void
getsprinfo (const spritearea_t * sprites, sprite_t * spr,
            long *w, long *h, long *xres, long *yres, unsigned int *m,
            unsigned int *flags)
            /* writes to all regardless */
{
  const unsigned int mode = *m = spr->mode;
  spr->mode &= ~(1 << 31);
  _swi (OS_SpriteOp, _INR (0, 2) | _OUTR (3, 4), 0x228, sprites, spr, w, h);
  spr->mode = mode;
  if (*m < 256) {
    *xres = 180 >> modevar (*m, 4);
    *yres = 180 >> modevar (*m, 5);
    *flags = modevar(*m, 0);
  } else if ((*m & 0x780F000F) == 0x78000001) {
    *xres = 180 >> ((*m >> 4) & 3);
    *yres = 180 >> ((*m >> 6) & 3);
    *flags = *m & 0xFF00;
  } else {
    *xres = (*m >> 1) & 8191;
    *yres = (*m >> 14) & 8191;
    *flags = 0;
  }
}


static void
remove_wastage (const spritearea_t * sprites, const sprite_t * spr)
{
  if (~spr->mode & 1 << 31)
    _swi (OS_SpriteOp, _INR (0, 2), 0x236, sprites, spr);
}


static int
bpp (unsigned int mode)
{
  if ((mode & 0x780F000F) == 0x78000001)
    return ((mode >> 20) - 1) & 127;
  else if (mode > 255)
    return ((mode >> 27) - 1) & 15;
  switch (modevar (mode, 3))
  {
  case 1:
    return 0;
  case 3:
    return 1;
  case 15:
    return 2;
  default:
    return 3;
  }
}


static const char *
list_used (const char *im, const char *imask, int lnbpp)
{
  char *used = spr_malloc (256, "used palette entries");
  int bpp = 1 << lnbpp;
  int mask = (1 << bpp) - 1;
  int y = height;
  memset (used, 0, 256);
  do
  {
    int x = -width;
    int b;
    do
    {
      char pixels = *im++;
      b = 0;
      if (imask)
        do
        {
          if (*imask++)
            used[pixels >> b & mask] = 1;
          b += bpp;
        } while (++x < 0 && b < 8);
      else
        do
        {
          used[pixels >> b & mask] = 1;
          b += bpp;
        } while (++x < 0 && b < 8);
    } while (x < 0);
    im = (const char *) (3L + (long) im & -4L);
    imask = (const char *) (3L + (long) imask & -4L);
  } while (--y);
#ifdef DEBUG
  _swi (OS_File, _INR (0, 5), 10, "<Wimp$ScrapDir>.used", 0xFFD, 0, used, used + 256);
#endif
  return used;
}


static int
checkgrey (const sprite_t * spr, const char *used)
{
  unsigned long x, *p;
  const unsigned int m = bpp (spr->mode);
  static const unsigned long count[] = { 2, 4, 16, 256 };
  static const unsigned long mul[] = { 0xFFFFFF, 0x555555, 0x111111, 0x10101 };
  debug_puts ("Checking for greyscale palette...");
  if (m > 3 || spr->image != 44 + count[m] * 8)
    return 0;
  p = (unsigned long *) (spr + 1);
  if (used)
    for (x = 0; x < count[m]; x++)
    {
      if (used[x] && *p >> 8 != (x * mul[m]))
        return 0;
      p += 2;
    }
  else
    for (x = 0; x < count[m]; x++)
    {
      if (*p >> 8 != (x * mul[m]))
      {
        return 0;
      }
      p += 2;
    }
  debug_puts ("Palette is standard grey");
  return 1;
}


static void
readpalette (const spritearea_t * area, sprite_t * spr, rgb_t * palette)
{
  unsigned int *p, q;
  unsigned long mode = spr->mode;
  spr->mode &= ~(1 << 31);
  _swi (ColourTrans_ReadPalette, _INR (0, 4), area, &spr->name, palette, 1024, 0);
  spr->mode = mode;
  p = (unsigned int *) palette;
  for (q = 256; q; --q)
    *p++ >>= 8;
}


static void
cmyk_to_rgb (const void *image)
{
  /* Convert CMYK to RGB */
  union {
    rgb_t r;
    cmyk_t c;
  } *i = (void *) image;
  int y;

  debug_puts ("Converting CMYK to RGB...");
  for (y = width * height; y; --y)
  {
    i->r.r = (i->c.c + i->c.k) < 255 ? 255 - (i->c.c + i->c.k) : 0;
    i->r.g = (i->c.m + i->c.k) < 255 ? 255 - (i->c.m + i->c.k) : 0;
    i->r.b = (i->c.y + i->c.k) < 255 ? 255 - (i->c.y + i->c.k) : 0;
    i->r.alpha = 0xFF;
    i++;
  }
}


static rgb_t *
expand12to24 (const void *image)
{
  long y = height;
  const short *im12 = image;
  long *im24;
  rgb_t *imret;

  if (verbose > 1)
    printf ("Expanding image from %ibit to %ibit\n", 12, 24);
  imret = spr_malloc (width * height * sizeof (rgb_t), "24bpp image");
  im24 = (long *) imret;        /* sizeof(long)==sizeof(rgb_t) */
  do
  {
    long x = width;
    do
    {
      *im24++ = ((*im12 & 15L) * 17L) |
                (((long) (*im12 >> 4) & 15L) * 17L) << 8 |
                (((long) (*im12 >> 8) & 15L) * 17L) << 16 |
                (((long) (*im12 >> 12) & 15L) * 17L) << 24;
      im12++;
    } while (--x);
    if (width & 1)
      im12++;
  } while (--y);
  return imret;
}


static rgb_t *
expand15to24 (const void *image)
{
  long y = height;
  const short *im15 = image;
  long *im24;
  rgb_t *imret;

  if (verbose > 1)
    printf ("Expanding image from %ibit to %ibit\n", 15, 24);
  imret = spr_malloc (width * height * sizeof (rgb_t), "24bpp image");
  im24 = (long *) imret;        /* sizeof(long)==sizeof(rgb_t) */
  do
  {
    long x = width;
    do
    {
      *im24++ = ((*im15 & 31L) * 33L) >> 2 |
                ((((long) (*im15 >> 5) & 31L) * 33L) >> 2) << 8 |
                ((((long) (*im15 >> 10) & 31L) * 33L) >> 2) << 16 |
                ((*im15 & (1 << 15)) ? 0xFF000000 : 0);
      im15++;
    } while (--x);
    if (width & 1)
      im15++;
  } while (--y);
  return imret;
}


static rgb_t *
expand16to24 (const void *image)
{
  long y = height;
  const short *im16 = image;
  long *im24;
  rgb_t *imret;

  if (verbose > 1)
    printf ("Expanding image from %ibit to %ibit\n", 16, 24);
  imret = spr_malloc (width * height * sizeof (rgb_t), "24bpp image");
  im24 = (long *) imret;        /* sizeof(long)==sizeof(rgb_t) */
  do
  {
    long x = width;
    do
    {
      *im24++ = ((*im16 & 31L) * 33L) >> 2 |
                ((((long) (*im16 >> 5) & 63L) * 16L) >> 2) << 8 |
                ((((long) (*im16 >> 11) & 31L) * 33L) >> 2) << 16;
      im16++;
    } while (--x);
    if (width & 1)
      im16++;
  } while (--y);
  return imret;
}


static grey_t *
expand8to8alpha (const char *image, const char *mask)   /* grey */
{
  long y = height;
  const char *im8 = image;
  const char *ma8 = mask;
  long *im16;                   /* read 4, write 2+2 pixels */
  grey_t *imret;

  if (verbose > 1)
    puts ("Expanding image from 8bit to 8bit plus alpha");
  imret = spr_malloc (height * (width + 3 & ~3) * 2L, "8bpp+alpha image");
  im16 = (long *) imret;
  if (ma8)
    do
    {
      long x = width + 1 & -2;
      do
      {
        long w = *im8++ | *ma8++ << 8;
        *im16++ = w | (long) (*im8++) << 16 | (long) (*ma8++) << 24;
      } while (x -= 2);
      if ((long) im8 & 2)
      {
        im8 += 2;
        ma8 += 2;
      }
    } while (--y);
  else
    do
    {
      long x = width + 1 & -2;
      do
      {
        long w = *im8++;
        *im16++ = w | (long) (*im8++) << 16;
      } while (x -= 2);
      if ((long) im8 & 2)
        im8 += 2;
    } while (--y);
  return imret;
}


static char *
expandto8 (const void *image, int lnbpp)
{
  long y = height;
  int bpp = 1 << lnbpp;
  int mask = (1 << bpp) - 1;
  const char *imx = image;
  char *im8, *imret;

  if (verbose > 1)
    printf ("Expanding image from %ibit to %ibit\n", 1 << lnbpp, 8);
  im8 = imret = spr_malloc (height * (width + 3 & -4), "8bpp image");
  do
  {
    long x = -width;
    int b;
    do
    {
      char pixels = *imx++;
      b = 0;
      do
      {
        *im8++ = pixels >> b & mask;
        b += bpp;
      } while (++x < 0 && b < 8);
    } while (x < 0);
    im8 = (char *) (3L + (long) im8 & -4L);
    imx = (char *) (3L + (long) imx & -4L);
  } while (--y);
  return imret;
}


static char *
expandtogrey (char *image, const char *used, int lnbpp, const rgb_t * pal)
{
  int i;
  long y;
  const int mask = (1 << (1 << lnbpp)) - 1;

  debug_puts ("Checking the palette for other than standard 8bpp grey");
  for (i = mask; i >= 0; --i)
  {
    if (used[i] &&
        (pal[i].r != pal[i].g || pal[i].r != pal[i].b ||
         pal[i].g != pal[i].b))
      return 0;
  }
  if (verbose)
    puts (lnbpp < 3 ? "Palette is <256 greyscale"
          : "Palette is unsorted greyscale");
  if (lnbpp < 3)
    image = expandto8 (image, lnbpp);
  for (y = (width + 3 & ~3) * height - 1; y >= 0; --y)
    image[y] = pal[image[y]].r;
  return image;
}


static char *
expandmaskto8 (const void *image, int lnbpp)
{
  long y = height;
  int bpp = 1 << lnbpp;
  int mask = (1 << bpp) - 1;
  const char *imx = image;
  char *im8, *imret;

  if (verbose > 1)
    printf ("Expanding mask from %ibit to 8bit\n", 1 << lnbpp);
  im8 = imret = spr_malloc (height * (width + 3 & -4), "8bpp mask");
  do
  {
    long x = -width;
    int b;
    do
    {
      char pixels = *imx++;
      b = 0;
      do
      {
        *im8++ = pixels >> b & mask ? 255 : 0;
        b += bpp;
      } while (++x < 0 && b < 8);
    } while (x < 0);
    im8 = (char *) (3L + (long) im8 & -4L);
    imx = (char *) (3L + (long) imx & -4L);
  } while (--y);
  return imret;
}


static char *
expandalphato8 (const void *image, int lnbpp)
{
  long y = height;
  int bpp = 1 << lnbpp;
  int mask = (1 << bpp) - 1;
  const char *imx = image;
  char *im8, *imret;
  const char mul = 255 / mask;

  if (verbose > 1)
    printf ("Expanding alpha from %ibit to 8bit\n", 1 << lnbpp);
  im8 = imret = spr_malloc (height * (width + 3 & -4), "8bpp alpha");
  do
  {
    long x = -width;
    int b;
    do
    {
      char pixels = *imx++;
      b = 0;
      do
      {
        *im8++ = (pixels >> b & mask) * mul;
        b += bpp;
      } while (++x < 0 && b < 8);
    } while (x < 0);
    im8 = (char *) (3L + (long) im8 & -4L);
    imx = (char *) (3L + (long) imx & -4L);
  } while (--y);
  return imret;
}


static void *
expandto24 (const void *image, int lnbpp, const rgb_t * palette)
{
  long y = height;
  int bpp = 1 << lnbpp;
  int mask = (1 << bpp) - 1;
  const char *imx = image;
  rgb_t *im24, *imret;
  /* due to alignment, this maps nicely onto png_color
   * (unless using ROLtd's PNG module) */

  if (verbose > 1)
    printf ("Expanding image from %ibit to %ibit\n", 1 << lnbpp, 24);
  if (palette == 0)
    palette = palinfo[lnbpp];
  im24 = imret = spr_malloc (height * width * 4, "24bpp image");
  do
  {
    int b = 0;
    long x = width;
    do
    {
      im24->r = palette[*imx >> b & mask].r;
      im24->g = palette[*imx >> b & mask].g;
      im24->b = palette[*imx >> b & mask].b;
      im24++;
      b += bpp;
      if (b > 7)
      {
        b = 0;
        imx++;
      }
    } while (--x);
    imx = (char *) (3L + (b > 0) + (long) imx & -4L);
  } while (--y);
  return imret;
}


static char *
reduce8alphato8 (char *image, char *image8, char **mask)
{
  int y, m;
  char *im, *imsrc, *msk;
  if (image8)
  {
    debug_puts ("8bpp version exists - using it");
    heap_free (image);
    return image8;
  }
  debug_puts ("8bpp version doesn't exist - creating");
  im = imsrc = image;
  y = height;
  msk = *mask;
  m = (msk == image) ? (width + 3 & ~3) * height : 0;
  if (m && (msk = spr_malloc (m, "grey mask")) == 0)
    fail (fail_NO_MEM, "out of memory (%s)", "grey mask");
  do
  {
    int x = width;
    do
    {
      *im++ = *imsrc++;
      *msk++ = *imsrc++;
    } while (--x);
    im = (char *) ((uintptr_t) im + 3 & ~3);
    imsrc = (char *) ((uintptr_t) imsrc + 3 & ~3);
    msk = (char *) ((uintptr_t) msk + 3 & ~3);
  } while (--y);
  if (m)
  {
    *mask = image + m;
    memcpy (*mask, msk - m, m);
    heap_free (msk);
  }
  return image;
}


static mask_t
rgba_stdmask (const rgb_t * image)
{
  long y = height;
  const rgb_t *i = image;
  int all0 = 1, all255 = 1;

  do
  {
    long x = width;
    do
    {
      char c = i[--x].alpha;
      if (c != 0 && c != 255)
        return MASK_ALPHA;      /* needs alpha channel */
      if (c)
        all0 = 0;
      if (c != 255)
        all255 = 0;
    } while (x);
    i += width;
  } while (--y);
  if (all0)
    return MASK_ALL;
  if (all255)
    return MASK_NONE;
  return MASK_SIMPLE;           /* OK to use simple transparency */
}


static mask_t
stdmask (const char *image, int greya)
{
  long y = height;
  int step = greya ? 2 : 1;
  const char *i = image;
  int all0 = 1, all255 = 1;

  do
  {
    long x = width * step - 1;
    do
    {
      char c = i[x];
      if (c != 0 && c != 255)
        return MASK_ALPHA;      /* needs alpha channel */
      if (c)
        all0 = 0;
      if (c != 255)
        all255 = 0;
    } while ((x -= step) >= 0);
    i += (width * step) + 3 & -4;
  } while (--y);
  if (all0)
    return MASK_ALL;
  if (all255)
    return MASK_NONE;
  return MASK_SIMPLE;           /* OK to use simple transparency */
}


static void
find_used (const char *image, const char *mask)
{
  long y = height;
  const char *i = image, *m = mask;

  memset (wksp.used, 0, sizeof (wksp.used));
  do
  {                             /* which logical colours? */
    long x = width;
    do
    {
      if (m[--x])
        wksp.used[i[x]] = 1;
    } while (x);
    i += width + 3 & -4;
    m += width + 3 & -4;
  }
  while (--y != 0L);
}


static int
apply_mask (char *image, const char *mask)
{
  char *i = image;
  const char *m = mask;
  int colour = 0, applied = 0;
  long y = height;

  if (verbose > 1)
    puts ("Applying mask");
  find_used (image, mask);
  while (colour < 256 && wksp.used[colour])
    colour++;                   /* first unused */
  if (colour == 256)
    return 260;                 /* >256 is sufficient */
  do
  {
    long x = width;
    do
    {
      if (m[--x] == 0)
      {
        i[x] = colour;
        applied = 1;
      }
    } while (x);
    i += width + 3 & ~3;
    m += width + 3 & ~3;
  } while (--y);
  return applied ? colour : 256;        /* return mask colour */
}


static long
find_used_24 (const long *image, const char *mask, long b)
{
  long y = height;
  const long *i = image;
  const char *m = mask;
  unsigned long j;

  memset (wksp.unused, 0, sizeof (wksp.unused));

  do
  {                             /* which logical colours? */
    long x = width;
    do
    {
      if (m[--x] && (i[x] & 0xFF0000) == b << 16)
      {
        j = (long) i[x] & 0xFFFF;
        wksp.unused[j >> 5] |= 1L << (j & 31);
      }
    } while (x);
    i += width;
    m += width + 3 & -4;
  } while (--y);

  for (j = 0; j < 65536 / 32; ++j)
  {
    long i = ~wksp.unused[j];
    long k;
    for (k = 0; k < 32; ++k)
      if (i & 1L << k)
        return b << 16 | j << 5 | k;
  }
  return -1;
}


static long
apply_mask_24 (long *image, const char *mask,
               const png_color_16 * bkgd, int lnbpp)
{
  long *i;                      /* rgb_t */
  const char *m;
  int applied = 0, b;
  long y;
  long colour = -1;
  long bgnd = bkgd
    ? (long) bkgd->red | (long) bkgd->green << 8 | (long) bkgd->blue << 16
    : -1;

  if (verbose > 1)
    puts ("Applying mask");

  if (bkgd)
  {
    y = height;
    i = image;
    m = mask;
    do
    {
      long x = width;
      do
      {
        if (m[--x] && ((i[x] ^ bgnd) & 0xFFFFFF) == 0)
          goto bgnd_used;
      } while (x);
      i += width;
      m += width + 3 & -4;
    } while (--y);
    colour = bgnd;
    bgnd_used:/**/;
  }

  if (colour == -1)
  {
    if (bkgd && lnbpp == 4)
    {
      colour = bgnd & 0xF8F8F8;
      if ((colour & 0xFF) == 0)
        colour++;
    }
    else
    {
      for (b = 0; b < 256 && colour == -1; ++b)
        colour = find_used_24 (image, mask, b);
      if (colour == -1)
        return 1 << 25;
    }
  }

  i = image;
  m = mask;
  y = height;
  do
  {
    long x = width;
    do
    {
      if (m[--x] == 0)
      {
        i[x] = colour;
        applied = 1;
      }
    } while (x);
    i += width;
    m += width + 3 & -4;
  } while (--y);
  return applied ? colour : 1 << 24;    /* return mask colour */
}


static void
find_used_p (const char *image)
{
  long y = height;
  const char *i = image;

  memset (wksp.used, 0, sizeof (wksp.used));
  do
  {                             /* which logical colours? */
    long x = width;
    do
    {
      wksp.used[i[--x]] = 1;
    } while (x);
    i += width + 3 & -4;
  } while (--y != 0L);
}


static int
pack_palette (char *image, rgb_t * palette, long mask, char *masked_p)
{                               /* palette has 256 entries; returns number used */
  char *i;
  long y = height;
  int colours, masked = mask < 256;

  if (verbose > 1)
    puts ("Packing palette");
  find_used_p (image);
  if (masked)
  {
    if (wksp.used[mask] == 0)
      masked = 0;
    else
      wksp.used[mask] = 0;
  }
  y = 0;
  colours = masked;             /* mask must be colour 0 */
  do
  {                             /* pack the palette, assign new logical colours */
    if (wksp.used[y])
    {
      if (colours > masked)
      {
        int x = masked;
        long py = ((long *) (palette))[y];
        while (x < colours && wksp.p.p[x] != py)
          x++;
        if (x < colours)
        {
          wksp.used[y] = x;
          continue;
        }
      }
      wksp.used[y] = colours;
      wksp.p.p[colours] = ((long *) (palette))[y];
      colours++;
    }
  } while (++y < 256);
  y = height;
  i = image;
  do
  {                             /* modify image to reflect above changes */
    long x = width;
    do
    {
      --x;
      i[x] = wksp.used[i[x]];
    } while (x);
    i += width + 3 & -4;
  } while (--y);
  if (!masked)
    *masked_p = 0;
  memcpy (palette, wksp.p.p, sizeof (wksp.p.p));
  return colours;
}


static void
make_rgba (void *image, char *mask, int lnbpp)
{
  long y = height;
  char *m = mask;
  if (lnbpp == 3)
  {
    char *i = image;
    do
    {
      long x = width;
      do
      {
        --x;
        i[x * 2 + 1] = m[x];
      } while (x);
      i += 2 * width + 3 & -4;
      m += width + 3 & -4;
    } while (--y);
  }
  else
  {
    rgb_t *i = image;
    do
    {
      long x = width;
      do
      {
        --x;
        i[x].alpha = m[x];
      } while (x);
      i += width;
      m += width + 3 & -4;
    } while (--y);
  }
}


static int
packgrey (const char *image)
{
  int bits = 7, x, y;
  const char *im = image;

  debug_puts ("Checking for reduced greyscale...");

  for (y = height; y; --y)
  {
    for (x = width; x; --x)
    {
      char b = *im++;
      if ((b ^ (b >> 4)) & 15)
        return 8;
      if (bits & 2)
        if (b && b != 0x55 && b != 0xAA && b != 0xFF)
          bits &= ~3;
      if (bits & 1)
        if (b && b != 0xFF)
          bits &= ~1;
      if (bits == 0)
        return 8;
    }
    im = (const char *) ((uintptr_t) im + 3 & ~3);
  }
  return (bits & 1) ? 1 : (bits & 2) ? 2 : 4;
}


static int
get_named_arg (const char *p, const char *const f[], const char *msg)
{
  unsigned int m;
  char *q;
  size_t s = 0;
  while (f[s][0])
    ++s;

  errno = 0;
  m = strtol (p, &q, 10);
  if (errno || *q || m >= s)
  {
    int x = strlen (p);
    if (!x)
      fail (fail_BAD_ARGUMENT, "null %s", msg);
    m = 0;
    while (f[m][0] && strncmp (p, f[m] + 1, x > f[m][0] ? x : f[m][0]))
      ++m;
    if (!f[m][0])
      fail (fail_BAD_ARGUMENT, "unknown %s", msg);
  }
  return m;
}


static const optslist args[] = {
  {1, "help", NO_PARAM},
  {2, "version", NO_PARAM},
  {'1', "fast", NO_PARAM},
  {'6', "normal", NO_PARAM},
  {'9', "best", NO_PARAM},
  {'B', "significant-bits", NO_PARAM},
  {'C', "chromaticity", REQUIRED_PARAM},
  {'G', "check-grey", NO_PARAM},
  {'I', "intent", REQUIRED_PARAM},
  {'M', "no-alpha", NO_PARAM},
  {'M', "no-blend", NO_PARAM},
  {'R', "render", REQUIRED_PARAM},
  {'S', "scale", NO_PARAM},
  {'X', "pixel-size", NO_PARAM},
  {'a', "alpha", NO_PARAM},
  {'b', "background", REQUIRED_PARAM},
  {'c', "check-mask", NO_PARAM},
  {'d', "set-dpi", REQUIRED_PARAM},
  {'f', "filter", REQUIRED_PARAM},
  {'g', "gamma", OPTIONAL_PARAM},
  {'i', "interlace", NO_PARAM},
  {'m', "no-mask", NO_PARAM},
  {'n', "inverse-alpha", NO_PARAM},
  {'p', "pack-pixels", NO_PARAM},
  {'r', "reduce", NO_PARAM},
  {'s', "frequency-sort", NO_PARAM},
  {'t', "trim", NO_PARAM},
  {'v', "verbose", REQUIRED_PARAM},
  {'w', "window-bits", REQUIRED_PARAM},
  {'x', "dpi", OPTIONAL_PARAM},
  {'z', "strategy", REQUIRED_PARAM},
  {3, "cname", REQUIRED_PARAM},
  {4, "csprite", REQUIRED_PARAM},
  {0, 0, NO_PARAM}
};

int
main (int argc, const char *const argv[])
{
  FILE *fp = 0, *ifp = 0;
  png_infop info_ptr;
  size_t size;
  long x, y, xres, yres;
  int lnbpp, masklnbpp;
  long maskcolour;              /* png_color & rgb_t fit in a word */
  unsigned int m, modeflags;
  sprite_t *imagespr, *maskspr;
  void *image, *image8 = 0;
  char *mask = 0, *palmask = 0;
  rgb_t *palette = 0;
  png_color_16 bkgd = { 0, 0, 0, 0, 0 };
  long bgnd = 0;
  int pal_entries, mask_entries = 0;
  int num_passes;
  int dpix = 0, dpiy = 0;
  char
    grey = 0,
    alpha = 0,
    masked = 0,
    checkmask = 0,
    simple_mask = 0,
    rgba = 0,
    background = 0,
    inverse = 0,
    interlace = 0,
    reducegrey = 0,
    reduce = 0,
    trim = 0,
    pixel_size = 0,
    freqsort = 0,
    packbits = 0,
    testsigbits = 0,
    have_chroma = 0;
  struct { char filter, strategy, bits, level; } compress = { 0 };
  struct { char intent, force; } srgb = { 0 };
  double chroma[4][2], gamma = 0;
  png_color_8 sigbit = { 8, 8, 8, 8, 8 };

  char *scale = 0;
  char *renderlevel = 0;

  const char *from = 0, *fromtemp = 0, *to = 0, *used = 0;

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

  init_task ("Spr2Png backend", argv[0]);

#define CHECKARG(opt) \
  if (p[1]) p++; \
  else if (++y < argc) p = argv[y]; \
  else fail (fail_BAD_ARGUMENT, "missing parameter for --%s", opt);

  y = 0;
  while (++y < argc)
  {
    const char *p = argv[y];
    if (p[0] == '-' && p[1] == '-')
    {
      const char *arg;
      if (!p[2])
        break;
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
      case '0':
        compress.level = 0;
        break;
      case '1':
        compress.level = 1;
        break;
      case '6':
        compress.level = 6;
        break;
      case '9':
        compress.level = 9;
        break;
      case 'B':
        testsigbits = 1;
        break;
      case 'C':
        p = arg;
        goto get_chroma;
      case 'G':
        reducegrey = 1;
        break;
      case 'I':
        p = arg;
        goto get_intent;
      case 'M':
        simple_mask |= 1;
        break;
      case 'R':
        p = arg;
        goto get_render;
      case 'S':
        p = arg;
        goto get_scale;
      case 'X':
        pixel_size |= 2;
        break;
      case 'a':
        rgba = 1;
        break;
      case 'b':
        p = arg;
        goto get_background;
      case 'c':
        checkmask = 1;
        break;
      case 'd':
        if (!pixel_size)
          pixel_size = 1;
        p = arg;
        goto get_dpi;
      case 'f':
        p = arg;
        goto get_filters;
      case 'g':
        if ((p = arg) != 0)
          goto get_gamma;
        gamma = 1 / 2.2;
        break;
      case 'i':
        interlace = 1;
        break;
      case 'm':
        simple_mask |= 2;
        break;
      case 'n':
        inverse = 1;
        break;
      case 'p':
        packbits = 1;
        /*break;*/
      case 'r':
        reduce = 1;
        break;
      case 's':
        freqsort = 1;
        break;
      case 't':
        trim = 1;
        break;
      case 'v':
        verbose++;
        break;
      case 'w':
        p = arg;
        goto get_window_bits;
      case 'x':
        pixel_size |= 1;
        break;
      case 'z':
        p = arg;
        goto get_strategy;
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
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
          compress.level = *p - '0';
          break;
        case 'B':
          testsigbits = 1;
          break;
        case 'C':
          CHECKARG ("chromaticity");
          /**/ get_chroma:
          have_chroma = 1;
          {
            int l;
            for (l = 0; l < 4; ++l)
            {
              switch (*p)
              {
              case 'w': case 'W':
                chroma[l][0] = 0.3127; chroma[l][1] = 0.329; break;
              case 'r': case 'R':
                chroma[l][0] = 0.64;   chroma[l][1] = 0.33;  break;
              case 'g': case 'G':
                chroma[l][0] = 0.3;    chroma[l][1] = 0.6;   break;
              case 'b': case 'B':
                chroma[l][0] = 0.15;   chroma[l][1] = 0.06;  break;
              default:
                chroma[l][0] = readfloat (&p);
                if (*p++ != ',' || chroma[l][0] < 0 || chroma[l][0] > 1)
                  fail (fail_BAD_ARGUMENT, "bad chromaticity value");
                chroma[l][1] = readfloat (&p);
                if (*p++ != (l == 3 ? 0 : ',')
                    || chroma[l][1] < 0 || chroma[l][1] > 1)
                  fail (fail_BAD_ARGUMENT, "bad chromaticity value");
              }
            }
            p = "x";
          }
          break;
        case 'G':
          reducegrey = 1;
          break;
        case 'I':
          CHECKARG ("intent");
          /**/ get_intent:
          {
            static const char *const f[] = {
              "\004none", "\012perceptual", "\010relative",
              "\012saturation", "\010absolute", ""
            };
            if ((srgb.force = (*p == '+')) != 0)
              ++p;
            srgb.intent = get_named_arg (p, f, "rendering intent");
            p = strchr (p, 0) - 1;
          }
          break;
        case 'M':
          simple_mask |= 1;
          break;
        case 'R':
          CHECKARG ("render");
          /**/ get_render:
          m = strlen (p);
          free (renderlevel);
          renderlevel = local_strdup (p);
          p = "x";
          break;
        case 'S':
          CHECKARG ("scale")
          /**/ get_scale:
          m = strlen (p);
          free (scale);
          scale = local_strdup (p);
          p = "x";
          break;
        case 'X':
          pixel_size |= 2;
          break;
        case 'a':
          rgba = 1;
          break;
        case 'b':
          CHECKARG ("background");
          /**/ get_background:
          errno = 0;
          background = 1;
          bgnd = strtol (p, (char **) &p, 16) & 0xFFFFFF;
          if (errno || *p--)
            fail (fail_BAD_ARGUMENT, "bad background colour value");
          bkgd.red = (png_byte) (bgnd & 0xFF);
          bkgd.green = (png_byte) (bgnd >> 8 & 0xFF);
          bkgd.blue = (png_byte) (bgnd >> 16);
          bkgd.gray = (png_byte)
                      (bkgd.red * .299 + bkgd.green * .587 +
                       bkgd.blue * .114);
          bkgd.index = 0;
          break;
        case 'c':
          checkmask = 1;
          break;
        case 'd':
          CHECKARG ("set-dpi");
          /**/ get_dpi:
          dpiy = dpix = strtol (p, (char **) &p, 10);
          if (errno || (*p && *p != ',') || dpix < 1 || dpix > 8191)
            fail (fail_BAD_ARGUMENT, "bad DPI value");
          if (*p)
          {
            dpiy = strtol (++p, (char **) &p, 10);
            if (errno || *p-- || dpiy < 1 || dpiy > 8191)
              fail (fail_BAD_ARGUMENT, "bad DPI value");
          }
          p = "x";
          break;
        case 'f':
          CHECKARG ("filter") /**/ get_filters:
          {
            size_t l;
            --p;
            if (!strcmp (p + 1, "all"))
            {
              compress.filter = PNG_ALL_FILTERS;
              p = "x";
            }
            do
            {
              static const char *const f[] = {
                "\4none", "\3sub", "\2up", "\3avg", "\5paeth", ""
              };
              l = strcspn (++p, ",");
              if (!l)
                fail (fail_BAD_ARGUMENT, "null filter type");
              x = 0;
              while (f[x][0] && strncmp (p, f[x] + 1,
                                         l > f[x][0] ? l : f[x][0]))
                x++;
              if (f[x][0])
                compress.filter |= 8 << x;
              else if (!compress.filter)
                goto short_filters;
              else
                fail (fail_BAD_ARGUMENT, "unknown filter type");
            } while (*(p += l));
            --p;
            break;
            /**/ short_filters:
            do
            {
              static const char f[] = "nsuap";
              x = 0;
              while (f[x] && *p != f[x])
                x++;
              if (f[x])
                compress.filter |= 8 << x;
              else
                fail (fail_BAD_ARGUMENT, "unknown filter type");
            } while (*++p);
            --p;
          }
          break;
        case 'g':
          if (!p[1])
          {
            gamma = 1 / 2.2;
            break;
          }
          ++p;
          /**/ get_gamma:
          gamma = readfloat (&p);
          if (*p-- || gamma <= 0
                   || gamma > 10 /* arbitrary upper limit */ )
            fail (fail_BAD_ARGUMENT, "bad gamma value");
          break;
        case 'i':
          interlace = 1;
          break;
        case 'm':
          simple_mask |= 2;
          break;
        case 'n':
          inverse = 1;
          break;
        case 'p':
          packbits = 1;
          /*break;*/
        case 'r':
          reduce = 1;
          break;
        case 's':
          freqsort = 1;
          break;
        case 't':
          trim = 1;
          break;
        case 'v':
          verbose++;
          break;
        case 'w':
          CHECKARG ("window-bits");
          /**/ get_window_bits:
          errno = 0;
          m = strtol (p, (char **) &p, 10);
          if (errno || *p-- || m < 9 || m > 15)
            fail (fail_BAD_ARGUMENT, "bad window value");
          compress.bits = (char) m;
          break;
        case 'x':
          pixel_size |= 1;
          break;
        case 'z':
          CHECKARG ("strategy");
          /**/ get_strategy:
          {
            static const char *const f[] = {
              "\7default", "\010filtered", "\7huffman", "\3rle", ""
            };
            compress.strategy = get_named_arg (p, f, "compression strategy");
            p = strchr (p, 0) - 1;
          }
          break;
        default:
          fail (fail_BAD_ARGUMENT, "unknown option -%c\n", *p);
        }
      } while (*++p);
    }
    else if (from == 0)
      from = argv[y];
    else if (to == 0)
      to = argv[y];
    else
      fail (fail_BAD_ARGUMENT, "too many filenames");
  }
  while (++y < argc)
  {
    if (from == 0)
      from = argv[y];
    else if (to == 0)
      to = argv[y];
  }
  if (!to)
    fail (fail_BAD_ARGUMENT,
          "too few filenames (need both input and output)");

  m = readtype (from);
  fromtemp = from;

  switch (m)
  {
  case 0xFF9:           /* sprite */
    break;
  case 0xAFF:           /* Draw */
  case 0xD94:           /* Artworks */
    {
      char *cmd = spr_malloc (1024, "draw2spr command"), *args;
      char *out = local_strdup (tmpnam (0));    /* output */
      fromtemp = local_strdup (tmpnam (0));     /* image */
      sprintf (cmd, "<Spr2Png$Dir>.draw2spr >%s 2>&1 %s %s -#",
               out, from, fromtemp);
      args = cmd + strlen (cmd);
      if (background)
        sprintf (args, " -b%06lX", bgnd);
      if (trim)
        strcat (args, " -t");
      switch (simple_mask)
      {
      case 1:
        strcat (args, " -M");
        break;
      case 2:
        strcat (args, " -m");
        break;
      case 3:
        strcat (args, " -mM");
      }
      if (scale)
        sprintf (args + strlen (args), " \"-s%s\"", scale);
      if (renderlevel)
        sprintf (args + strlen (args), " \"-r%s\"", renderlevel);

      switch (verbose)
      {
      case 1:
        strcat (cmd, " -v");
        break;
      case 2:
        strcat (cmd, " -vv");
        break;
      case 3:
        strcat (cmd, " -vvv");
      }
      if (verbose > 2)
        printf ("calling draw2spr: %s\n",
                *args ? args + 1 : "(no extra arguments)");
      else if (verbose == 2)
        puts ("calling draw2spr");
      debug_printf ("Temp sprite = %s\nTemp text   = %s\nCommand line = %s\n",
                    fromtemp, out, cmd);
//      m = !_swix (Wimp_Initialise, _INR (0, 3),
//                310, 0x4B534154, "spr2png", 0);
      onerr (_swix (Wimp_StartTask, _IN (0), cmd));
//      if (m)
//      _swix (Wimp_CloseDown, _INR (0, 1), 0, 0);
      free (cmd);
      ifp = fopen (out, "r");
      if (ifp)
      {
        /* do magic to cause deletion of outf on close */
        ifp->__flag |= 0xAD800001;
        sscanf (out + strlen (out) - 8, "%X", &ifp->__signature);
        do
        {
          int l = fread (wksp._msg, 1, sizeof (wksp._msg), ifp);
          if (l)
            fwrite (wksp._msg, 1, l, stdout);
        } while (!feof (ifp) && !ferror (ifp));
        fclose (ifp);
        ifp = 0;
      }
      from = "(scrap file)";
      rgba = checkmask = 1 && (simple_mask & 2) == 0;
      /*trim = */ inverse = 0;
      if (readtype (fromtemp) == 0xFF9)
        break;
    }                           /* fall through */
  case -2:
    fail (fail_BAD_DATA, "%s: not found", from);
  default:
    fail (fail_BAD_DATA, "%s: not a sprite, Draw or Artworks file", from);
  }

  heap_init ("Spr2Png workspace");

  png_ptr = png_create_write_struct (PNG_LIBPNG_VER_STRING, 0, 0, 0);
  if (png_ptr == NULL)
    fail (fail_LIBPNG_FAIL, "libpng init failure");

  png_init = 1;

  info_ptr = png_create_info_struct (png_ptr);
  if (info_ptr == NULL)
    fail (fail_LIBPNG_FAIL, "libpng init failure");

  if (setjmp (png_jmpbuf(png_ptr)))
  {
    if (fp)
      fclose (fp);
    if (ifp)
      fclose (ifp);
    png_destroy_write_struct (&png_ptr, (png_infopp) NULL);
    fail (fail_LIBPNG_FAIL, 0);
  }

  _swi (Hourglass_On, _IN (0), 0);

  ifp = fopen (fromtemp, "rb");
  if (ifp == NULL)
    fail (fail_OS_ERROR, 0);
  if (fromtemp != from)
  {
    /* We have a temp file; do magic to cause its deletion on close */
    ifp->__flag |= 0xAD800001;
    sscanf (fromtemp + strlen (fromtemp) - 8, "%X", &ifp->__signature);
  }
  fseek (ifp, 0, SEEK_END);
  size = (size_t) ftell (ifp);
  fseek (ifp, 0, SEEK_SET);
  sprites = spr_malloc (size + 4, "input file");
  if (fread ((char *) (sprites) + 4, size, 1, ifp) != 1)
    fail (fail_OS_ERROR, "couldn't load file");
  if (fclose (ifp))
    fail (fail_OS_ERROR, 0);
  sprites->size = size + 4;

  if (sprites->first > sprites->free)
    fail (fail_BAD_DATA, "invalid sprite file");
  if (sprites->sprites < 1)
    fail (fail_BAD_DATA, "there must be at least one sprite");
  imagespr = (sprite_t *) (sprites->first + (char *) sprites);
  checkspr (imagespr);
  remove_wastage (sprites, imagespr);
  getsprinfo (sprites, imagespr, &width, &height, &xres, &yres, &m, &modeflags);
  /* override the sprite's settings */
  if (dpix)
  {
    xres = dpix;
    yres = dpiy;
  }
  if (!pixel_size && (xres < 1 || xres != yres))
    fail (fail_BAD_IMAGE, "the image sprite must have square pixels");
  lnbpp = bpp (m);

  if (m & 1 << 31)
  {
    /* Alpha-masked sprite (RO Select) */
    rgba = 0;
    alpha = 1;
  }

  if (modeflags & (1 << 15))
  {
    /* RGBA sprite (RO 5) */
    rgba = 1;
  }

  if (lnbpp == 5 && rgba)
  {
    alpha = 1;
    masklnbpp = 3;
    maskspr = imagespr;
  }
  else
  {
    if (lnbpp > 5)
      switch (lnbpp)
      {
      case 6:
      case 9:
        alpha = 0;
        break;
      case 7:
        fail (fail_UNSUPPORTED, "%s format is not supported",
              "24bpp packed");
        break;
      case 8:
        fail (fail_UNSUPPORTED, "%s format is not supported", "JPEG");
        break;
      case 15:
        break;
      case 16:
      case 17:
        fail (fail_UNSUPPORTED, "%s format is not supported", "YCbCr");
        break;
      default:
        fail (fail_UNSUPPORTED, "sprite format %i is not recognised",
              lnbpp + 1);
      }

    rgba = 0;

    /* In case we're using the first sprite's own mask */
    if (m & 1 << 31)
      masklnbpp = 3;            /* 8-bit alpha (Select) */
    else
      masklnbpp = m > 255 ? 0 : lnbpp;  /* 1-bit mask if new fmt */

    /* Check for a separate mask sprite */
    maskspr = (sprite_t *) (imagespr->size + (uintptr_t) imagespr);
    if (maskspr >= (sprite_t *) (sprites->free + (char *) sprites))
      maskspr = 0;

    if (maskspr)
    {
      long xr, yr;
      unsigned int mf;
      getsprinfo (sprites, maskspr, &x, &y, &xr, &yr, &m, &mf);
      checkspr (maskspr);
      masklnbpp = bpp (m);
      if (masklnbpp > 3 || !checkgrey (maskspr, 0))
        fail (fail_BAD_IMAGE, "mask sprite must have a greyscale palette");
      if (x != width || y != height)
        fail (fail_BAD_IMAGE, "sprite sizes do not match");
      if (xr != xres || yr != yres)
        fail (fail_BAD_IMAGE, "sprite resolutions do not match");
      remove_wastage (sprites, imagespr);
      alpha = 1;
      m &= ~(1 << 31);  /* clear the Select alpha bit */
    }

    if (lnbpp < 4)
    {
      palette = heap_malloc (256 * sizeof (rgb_t));
      if (!palette)
        fail (fail_NO_MEM, "out of memory (%s)", "palette");
    }
    if (!maskspr && imagespr->mask > imagespr->image)
      maskspr = (sprite_t *) (sprites->first + (char *) sprites);
  }

  if (palette)
  {
    debug_puts ("Palette initialisation...");
    if (imagespr->image > 44)
      readpalette (sprites, imagespr, palette);
    else
    {
      memset (palette, 0, 256 * sizeof (rgb_t));
      memcpy (palette, palinfo[lnbpp], palsize[lnbpp]);
    }
  }

  image = (void *) (imagespr->image + (char *) imagespr);
  if (maskspr)
  {
    debug_puts ("Mask initialisation and expansion...");
    mask = ((alpha && !(m & 1 << 31)) ? maskspr->image : maskspr->mask) +
           (char *) maskspr;
    if (masklnbpp < 3)
    {
      mask = alpha ? expandalphato8 (mask, masklnbpp)
                   : expandmaskto8 (mask, masklnbpp);
      masklnbpp = 3;
    }
  }

  switch (lnbpp)
  {
  case 4:
    image = expand15to24 (image);
    lnbpp = 5;
    break;
  case 6:
    cmyk_to_rgb (image);
    lnbpp = 5;
    break;
  case 9:
    image = expand16to24 (image);
    lnbpp = 5;
    break;           
  case 15:
    image = expand12to24 (image);
    lnbpp = 5;
    break;
  }

  if (alpha && !(m & 1 << 31) && inverse)
  {
    debug_puts ("Inverting the mask...");
    if (rgba)
    {
      rgb_t *i = image;
      for (y = width * height; y; i[--y].alpha ^= 255)
        ;
    }
    else
    {
      long *i = (long *) mask;
      for (y = (width + 3 & -4) * height / 4; y; i[--y] ^= -1)
        ;
    }
  }

  if (mask && (imagespr != maskspr) /*||  mask != (char *) maskspr + maskspr->mask) */ )
  {
    debug_printf ("Mask type checking... (imagespr=%p, maskspr=%p)\n",
                  imagespr, maskspr);
    switch (stdmask (mask, 0))
    {
    case MASK_ALL:              /* nothing but mask */
      debug_puts ("-- blank");
      if (checkmask)
      {
        memset (image, 0,
                (size_t) (lnbpp < 4 ? height * (width + 3 & -4)
                                    : height * width * 4));
        lnbpp = 0;
        alpha = 0;      /* image wiped; no point in keeping it */
      }
      break;
    case MASK_NONE:     /* has a mask, but it's unused */
      debug_puts ("-- unused");
      if (checkmask)
      {
        if (masklnbpp != 3 && !rgba)
          heap_free (mask);
        mask = 0;
        alpha = 0;
        rgba = 0;               /* ... so lose it */
      }
      break;
    case MASK_SIMPLE:
      debug_puts ("-- simple");
      alpha = 0;
      break;            /* no point in using alpha */
    case MASK_ALPHA:
      debug_puts ("-- alpha");
      break;            /* if this returned, alpha==1 anyway */
    }
  }

  if (lnbpp < 4)
    used = list_used (image, mask, lnbpp);

  if (lnbpp == 3 && checkgrey (imagespr, used))
  {
    grey = 1;
    palette = 0;
  }

  if (grey && alpha)
    image = expand8to8alpha (image8 = image, mask);
  else if (lnbpp <= 3)
  {
    if (alpha)
    {
      image = expandto24 (image, lnbpp, palette);
      heap_free (palette);
      palette = 0;
      lnbpp = 5;
    }
    else if (lnbpp < 3)
    {
      if (!reducegrey)
        image = expandto8 (image, lnbpp);
      else
      {
        void *im = expandtogrey (image, used, lnbpp, palette);
        if (im)
        {
          heap_free (palette);
          palette = 0;
          grey = 1;
          image = im;
        }
        else
          image = expandto8 (image, lnbpp);
      }
      lnbpp = 3;
    }
    else
    {
      void *im = expandtogrey (image, used, lnbpp, palette);
      if (im)
      {
        heap_free (palette);
        palette = 0;
        grey = 1;
        image = im;
      }
    }
  }
  else if (reducegrey && reducetogrey (image, mask, rgba))
  {
    alpha = rgba || mask != 0;
    rgba = 0;
    grey = 1;
    lnbpp = 3;
  }

  if (grey && alpha)
  {
    debug_puts ("Mask type checking... (grey + alpha)");
    switch (stdmask (image, 1))
    {
    case MASK_ALL:              /* nothing but mask */
      debug_puts ("-- blank");
      if (checkmask)
      {
        memset (image, 0, ((width * 2 + 2) & ~3) * height);
        lnbpp = alpha = 0;      /* image wiped; no point in keeping it */
      }
      break;
    case MASK_NONE:     /* has a mask, but it's unused */
      debug_puts ("-- unused");
      if (checkmask)
      {
        image = reduce8alphato8 (image, image8, &mask);
        alpha = masked = 0;     /* ... so lose it */
      }
      break;
    case MASK_SIMPLE:
      debug_puts ("-- simple");
      if (checkmask)
      {
        image = reduce8alphato8 (image, image8, &mask);
        alpha = 0;
      }
      break;            /* no point in using alpha */
    case MASK_ALPHA:
      debug_puts ("-- alpha");
      break;            /* if this returned, alpha==1 anyway */
    }
  }

  /* trim image if requested */
  if (trim)
  {
    m = 0;
    switch (imagetype[alpha][grey][lnbpp])
    {
    case 0:             /* 8bpp grey, no alpha */
      if (background)
        m = trim_mask_8 (image, 0, bkgd.index);
      break;
    case 2:             /* 24bpp, no alpha */
      if (mask)
        m = trim_mask_24 (image, mask, maskcolour);
      else if (background)
        m = trim_mask_24 (image, 0, bgnd);
      break;
    case 3:             /* 8bpp, simple mask or alpha */
      if (mask)
        m = trim_mask_8 (image, mask, maskcolour);
      else if (background)
        m = trim_mask_8 (image, 0, bkgd.index);
      break;
    case 4:             /* 8bpp grey, alpha */
      m = trim_mask_8a (image) * 2;
      break;
    case 6:             /* 24bpp, alpha */
      if (!rgba)
        m = trim_mask_24 (image, mask, maskcolour);
      else
        m = trim_mask_24a (image) * 2;
    }
    switch (m)
    {
    case 1:
      fail (fail_BAD_IMAGE, "this image is fully transparent");
    case 2:
      fail (fail_BAD_IMAGE, "%s - does it have a separate alpha channel?",
            "this image is fully transparent");
    }
  }

  if (!alpha)
  {
    debug_puts ("Mask application...");
    masked = mask != 0;
    if (lnbpp < 4)
    {
      maskcolour = masked ? apply_mask (image, mask) : 256;
      debug_printf ("Initial mask colour = %*lX\n", lnbpp < 4 ? 2 : 6, maskcolour);
      if (maskcolour > 256)
      {
        if (grey)
          image = expand8to8alpha (image, mask);
        else
        {
          void *x = expandto24 (image, 3, palette);
          if (lnbpp < 3)
            heap_free (image);
          heap_free (palette);
          palette = 0;
          lnbpp = 5;
          image = x;
        }
        masked = 0;
        alpha = 1;
      }
      else
      {
        if (maskcolour == 256)
          masked = 0;
        if (!grey)
        {
          pal_entries = pack_palette (image, palette, maskcolour, &masked);
          maskcolour = 0;
          if (masked && background)
          {
            palette[maskcolour].r = (png_byte) bkgd.red;
            palette[maskcolour].g = (png_byte) bkgd.green;
            palette[maskcolour].b = (png_byte) bkgd.blue;
          }
        }
      }
    }
    else
    {
      maskcolour = masked
                   ? apply_mask_24 (image, mask, background ? &bkgd : 0, lnbpp)
                   : 1 << 24;
      if (maskcolour >= 1 << 24)
      {
        masked = 0;
        if (maskcolour > 1 << 24)
          alpha = 1;
      }
    }
    if (masked)
      debug_printf ("Using mask colour %*lX\n", lnbpp < 4 ? 2 : 6, maskcolour);
  }

  debug_printf ("reduce = %i, lnbpp = %i, alpha = %i%s, masked = %i\n"
                "imagespr = %p, image = %p; maskspr = %p, mask = %p\n",
                reduce, lnbpp, alpha, (m & 1 << 31) ? " (Select)" : "", masked,
                imagespr, image, maskspr, mask);

  if (reduce && lnbpp > 3)
  {
    char *im;
    mask_t masktype = checkmask ? MASK_SIMPLE : MASK_ALPHA;
    debug_puts ("Palette reduction tests...");
    if (alpha && !rgba)
    {
      debug_puts ("- applying alpha channel -> RGBA");
      make_rgba (image, mask, lnbpp);
      rgba = 1;
    }
    else if (masked)
    {
      long *im = image;
      char *m = mask;
      long w;
      debug_puts ("- applying mask colour -> RGBA");
      y = height;
      do
      {
        x = width;
        do
        {
          w = im[--x] & 0xFFFFFF;
          im[x] = w | (w == maskcolour ? 0 : 0xFF000000);
        } while (x);
        im += width;
        m += width + 3 & -4;
      } while (--y);
      /*maskcolour = 1 << 24; */
      rgba = 1;
    }
    if (rgba && checkmask)
      switch (masktype = rgba_stdmask (image))
      {
      case MASK_ALL:    /* nothing but mask */
        debug_puts ("RGBA: image is blank");
        memset (image, 0, height * width * 4);
        break;
        case MASK_NONE: /* has a mask, but it's unused */
        debug_puts ("RGBA: mask is unused");
        alpha = rgba = 0;
        break;
      case MASK_SIMPLE:
        debug_puts ("RGBA: mask is simple");
        alpha = 0;
        masked = 1;
        break;
      default:
        /* Otherwise it's full alpha, or simple; treat as full alpha */
        debug_puts ("RGBA: mask is alpha");
      }
    im = rgba ? reduceto8_alpha (image, &palmask, &palette, &pal_entries,
                                 (background && (masked || alpha)) ? &bkgd : 0)
              : reduceto8 (image, &maskcolour, &palette, &pal_entries,
                           (background && (masked || alpha)) ? &bkgd : 0);
    if (masked && !rgba)
      debug_printf ("Confirming mask colour %*lX\n", lnbpp < 4 ? 2 : 6, maskcolour);
    if (im)
    {
      if (lnbpp == 4)
        heap_free (image);
      image = im;
      lnbpp = 3;
      if (masktype == MASK_SIMPLE && palmask)
      {
        /* find a suitable transparent colour */
        for (x = 0; x < pal_entries; ++x)
          if (!palmask[x])
            break;
        maskcolour = (x < pal_entries) ? x : 0;
        heap_free (palmask);
        palmask = 0;
      }
      if (palmask)
        masked = 1;
      else if (masked && maskcolour)
      {
        debug_puts ("Shifting mask to colour 0...");
        for (x = maskcolour; x; --x)
          palette[x] = palette[x - 1];
        palette[0].r = (png_byte) bkgd.red;
        palette[0].g = (png_byte) bkgd.green;
        palette[0].b = (png_byte) bkgd.blue;
        y = height;
        do
        {               /* modify image to reflect above changes */
          x = width;
          do
          {
            --x;
            if (im[x] <= maskcolour)
              im[x] = (im[x] == maskcolour) ? 0 : im[x] + 1;
          } while (x);
          im += width + 3 & -4;
        } while (--y);
        maskcolour = 0;
      }
    }
  }
  else if (rgba && checkmask)
  {
    debug_puts ("Alpha channel checking...");
    switch (rgba_stdmask (image))
    {
    case MASK_ALL:              /* nothing but mask */
      memset (image, 0, height * width * 4);
      break;
    case MASK_NONE:             /* has a mask, but it's unused */
      alpha = rgba = 0;
      break;
      /* Otherwise it's full alpha, or simple; treat as full alpha */
    }
  }
  if (testsigbits && lnbpp > 3)
    sigbit = significantbits (image);

  if (freqsort && lnbpp <= 3 && palette && !grey)
  {
    frequency_sort (image, palette, palmask, masked && !palmask, &bkgd);
    maskcolour = 0;
  }

  /* paletted RGBA: remove trailing 'fully opaque' mask entries */
  if (palmask)
  {
    debug_puts ("Trimming trailing opaque entries...");
    mask_entries = pal_entries;
    while (mask_entries && palmask[mask_entries - 1] == 255)
      mask_entries--;
    if (!mask_entries)
    {
      /* no mask entries left? no mask :-) */
      debug_puts ("All mask entries trimmed!");
      heap_free (palmask);
      palmask = 0;
      masked = alpha = 0;
    }
  }

  if (verbose)
  {
    int o = 0;
    puts ("The image:");
    if (grey)
    {
      puts (" - is greyscale");
      o = 1;
    }
    else if (palette)
    {
      if (verbose > 1)
        printf (" - has a%s with %i entries\n", " palette", pal_entries);
      else
        printf (" - has a%s\n", " palette");
      o = 1;
    }
    if (palmask || alpha)
    {
      if (palmask && verbose > 1)
        printf (" - has a%s with %i entries\n", "n alpha channel",
                mask_entries);
      else
        printf (" - has a%s\n", "n alpha channel");
      o = 1;
    }
    else if (masked)
    {
      puts (" - has a simple mask");
      o = 1;
    }
    if (!o)
      puts (" - is 24bit");
    puts ("Compressing...");
  }

  debug_puts ("Opening output file...");
  fp = fopen (to, "wb");
  if (!fp)
    fail (fail_OS_ERROR, 0);
  _kernel_last_oserror ();      /* discard */

  debug_puts ("Setting load/exec...");
  onerr (_swix (OS_File, _INR (0, 2), 2, to, 0xDEADDEAD));
  onerr (_swix (OS_File, _INR (0, 1) | _IN (3), 3, to, 0xDEADDEAD));
  debug_puts ("Initialising PNG I/O...");
  png_init_io (png_ptr, fp);

  if (alpha && !rgba && !grey)
    make_rgba (image, mask, lnbpp);

  /* set pixel packing */
  x = (packbits && grey && !alpha) ? packgrey (image) :
      (lnbpp > 3 || !packbits) ? 8 :
      (pal_entries <= 2) ? 1 :
      (pal_entries <= 4) ? 2 :
      (pal_entries <= 16) ? 4 : 8;
  debug_printf ("Using %i bits per channel\n", (int) x);

  png_set_IHDR (png_ptr, info_ptr, width, height, (int) x,
                imagetype[alpha][grey][lnbpp],
                interlace ? PNG_INTERLACE_ADAM7 : PNG_INTERLACE_NONE,
                PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
  png_set_compression_level (png_ptr, compress.level);
  if (compress.strategy)
    png_set_compression_strategy (png_ptr, compress.strategy - 1);
  if (compress.bits)
    png_set_compression_window_bits (png_ptr, compress.bits);
  if (lnbpp == 5 &&
      (sigbit.red != 8 || sigbit.green != 8 || sigbit.blue != 8))
    png_set_sBIT (png_ptr, info_ptr, (png_color_8p) & sigbit);

  //m = 1024 / (int) width;
  //if (lnbpp > 3)
  //  m /= lnbpp - 2;
  //png_set_flush (png_ptr, m < 16 ? 16 : m);   /* hopefully reduce memory rq... */

  if (compress.filter)          /* set allowed filter types */
    png_set_filter (png_ptr, PNG_FILTER_TYPE_BASE, compress.filter);

  if (palette && !grey)         /* declare palette if needed */
  {
#ifdef USING_PNG_MODULE
    int i = pal_entries;
    int *pal = (int *) palette;
    do
    {
      pal[--i] <<= 8;
    } while (i);
#endif
    png_set_PLTE (png_ptr, info_ptr, (png_color *) palette, pal_entries);
  }

  if (background)
  {                             /* declare the background colour if specified */
    if (masked || alpha)        /* (but not if no mask or alpha channel) */
      png_set_bKGD (png_ptr, info_ptr, &bkgd);
  }

  debug_printf ("Image type = %i\n", imagetype[alpha][grey][lnbpp]);

  if (masked)
  {
    switch (imagetype[alpha][grey][lnbpp])
    {
    case 3:
      if (palmask)
      {
        debug_printf ("Paletted RGBA, %i entries\n", mask_entries);
        png_set_tRNS (png_ptr, info_ptr, (png_bytep) palmask, mask_entries, 0);
      }
      else
      {
        /* maskcolour *should* be 0, for paletted images */
        png_byte *mask = spr_malloc (maskcolour + 1L, "8bpp mask");
        memset (mask, (int) maskcolour + 1, (size_t) maskcolour);
        mask[maskcolour] = 0;
        debug_printf ("Mask colour number = %li\n", maskcolour);
        png_set_tRNS (png_ptr, info_ptr, mask, (int) maskcolour + 1, 0);
      }
    case 4:
    case 6:
      break;
    default:
      {
        png_color_16 maskrgb;
        if (lnbpp < 4)
          maskrgb.gray = maskrgb.index = (int) maskcolour;
        else
        {
          maskrgb.red = (png_uint_16) (maskcolour & 0xFF);
          maskrgb.green = (png_uint_16) (maskcolour >> 8 & 0xFF);
          maskrgb.blue = (png_uint_16) (maskcolour >> 16 & 0xFF);
        }
        debug_printf ("Mask colour number = &%6lX\n", maskrgb);
        png_set_tRNS (png_ptr, info_ptr, 0, 0, &maskrgb);
      }
    }
  }

  if (modeflags & (1 << 14))
  {
    png_set_bgr(png_ptr);
  }

  if (lnbpp > 3 && !alpha)
  {
    /*png_set_filler(png_ptr,0,PNG_FILLER_AFTER); */
    long y = height;
    void *i = image;
    do
    {                   /* libpng handles RGBX, but more generically... */
      char *p = i, *q = i;
      long j;
      for (j = width - 1; j; j--)
      {
        (p += 3)[0] = (q += 4)[0];
        p[1] = q[1];
        p[2] = q[2];
      }
      i = (void *) ((long *) i + width);
    } while (--y);
  }

  if (srgb.intent)
  {
/* Weird. This works, but the following commented-out code doesn't.
 * (Norcroft RISC OS cc 5.54)
 */
    void (*func) (png_const_structp, png_infop, int) =
      srgb.force ? png_set_sRGB_gAMA_and_cHRM : png_set_sRGB;
    func (png_ptr, info_ptr, srgb.intent - 1);
/*
    if (srgb.force)
      png_set_sRGB_gAMA_and_cHRM (png_ptr, info_ptr, srgb.intent - 1);
    else
      png_set_sRGB (png_ptr, info_ptr, srgb.intent - 1);
*/
  }
  else
  {
    if (gamma)
      png_set_gAMA (png_ptr, info_ptr, gamma);
    if (have_chroma)
      png_set_cHRM (png_ptr, info_ptr, chroma[0][0], chroma[0][1],
                    chroma[1][0], chroma[1][1], chroma[2][0], chroma[2][1],
                    chroma[3][0], chroma[3][1]);
  }

  if (pixel_size & 1)
  {
    static const double inm = 100 / 2.54;       /* inches per metre */
    png_set_pHYs (png_ptr, info_ptr, inm * xres, inm * yres,
                  PNG_RESOLUTION_METER);
  }
  if (pixel_size & 2)
  {
    static const double inm = 100 / 2.54;       /* inches per metre */
    png_set_sCAL (png_ptr, info_ptr, PNG_RESOLUTION_METER,
                  1 / inm / xres, 1 / inm / yres);
  }

  png_write_info (png_ptr, info_ptr);

  if (lnbpp < 4 && packbits)
    png_set_packing (png_ptr);

  _swi (Hourglass_LEDs, _INR (0, 1), 2, -1);    /* LED 2 on */
  num_passes = interlace ? png_set_interlace_handling (png_ptr) : 1;
  if (palmask)
    alpha = 0;
  x = num_passes * height;
  m = 0;                        /* hourglass */
  do
  {
    long y = height;
    void *i = image;
    do
    {
      _swi (Hourglass_Percentage, _IN (0), m++ * 100 / (int) x);
      png_write_row (png_ptr, i);
      if (lnbpp == 3 && alpha)
        i = (void *) ((char *) i + 2 * (width + 1 & ~1));       /* avoid grey_t alignment */
      else if (lnbpp < 4)
        i = (void *) ((char *) i + (width + 3 & ~3));
      else
        i = (void *) ((long *) i + width);
    } while (--y);
  } while (--num_passes);
  _swi (Hourglass_LEDs, _INR (0, 1), 2, -1);    /* LED 2 off */

  png_write_end (png_ptr, info_ptr);
  if (fclose (fp))
    fail (fail_OS_ERROR, 0);
  shutdown ();

  onerr (_swix (OS_File, _INR(0, 2), 18, to, 0xB60));

  if (verbose)
    puts ("Finished.");
  exit (0);
}
