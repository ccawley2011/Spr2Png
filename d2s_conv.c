/* RISC OS Draw file code, taken from the InterGif source
 * Modified to produce properly masked sprites */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <math.h>
#include <ctype.h>
#include "swis.h"
#include "kernel.h"
#include "png.h"
#include "d2s_lib.h"

#ifndef DrawFile_BBox
#define DrawFile_BBox 0x45541
#endif
#ifndef AWRender_DocBounds
#define AWRender_DocBounds 0x46082
#endif
#ifndef AWRender_ClaimVectors
#define AWRender_ClaimVectors 0x46084
#endif

static jmp_buf jump, main_jump;
static int fontmax[6];


static void
cache_fontmax (void)
{
  _swix (Font_ReadFontMax, _OUTR (0, 5),
	 &fontmax[0], &fontmax[1], &fontmax[2], &fontmax[3], &fontmax[4],
	 &fontmax[5]);
}


static void
set_fontmax (int restore)
{
  _swix (Font_SetFontMax, _INR (0, 5),
	 fontmax[0], fontmax[1], restore ? fontmax[2] : 0,
	 restore ? fontmax[3] : 0, restore ? fontmax[4] : 0,
	 restore ? fontmax[5] : 0);
}


static void
sighandler (int signal)
{
  render_shutdown ();
  set_fontmax (1);
  longjmp (jump, signal);
  exit (fail_OS_ERROR);
}


static _kernel_oserror *
rmensure (const char *module, const char *file)
{
  return _swix (OS_Module, _INR (0, 1), 18, module)
       ? _swix (OS_Module, _INR (0, 1), 1, file) : 0;
}


static int
rmversion (const char *module)
{
  const int *mod;
  const char *ver;
  if (_swix (OS_Module, _INR (0, 1) | _OUT (3), 18, module, &mod))
    return 0;
  ver = (char *) mod + mod[5];	/* module help text & version */
  while (*ver && !isdigit (*ver))
    ver++;
  return readfloat (&ver) * 100;
}


typedef struct FONTNAME_T {
  struct FONTNAME_T *next;
  char name[1];			/* placeholder */
} fontname_t;


/* For Draw files */
static int
check_font (const char *font)
{
  static fontname_t *namelist;
  static int warned = 0;
  const fontname_t *ptr;

  debug_printf ("[checking font %s... ", font);
  if (!namelist) {
    static char buffer[256];
    int fn = 1 << 16;
    do {
      if (!_swix (Font_ListFonts, _INR (1, 5) | _OUT (2),
		  buffer, fn, 256, 0, 0,  &fn)) {
	fontname_t *name = spr_malloc (5 + strlen (buffer),
				       "Font name list");
	name->next = namelist;
	namelist = name;
	strcpy (name->name, buffer);
      }
    } while (fn != -1);
  }
  ptr = namelist;
  while (ptr) {
    if (ptr->name[0] == font[0] && !strcmp (ptr->name, font))
    {
      debug_puts ("found]");
      return 0;
    }
    ptr = ptr->next;
  }
  debug_puts ("not found]");
  if (!warned) {
    warned = 1;
    puts ("Warning: the following font(s) are not available:");
  }
  printf (" - %s\n", font);
  return 1;
}


static void
antialias (const rgb_t * srcBits, const png_byte * maskBits,
	   rgb_t * destBits, long background, long w, long h, int nomask)
{
  long x, y, i, j;
  long mw = (4 * w + 31 & ~31) / 8;
  png_byte m[4] = { 0, 0, 0, 0 };
  png_byte t;

  static const char mconv[] =
    { 4, 3, 3, 2, 3, 2, 2, 1, 3, 2, 2, 1, 2, 1, 1, 0 };

  debug_printf ("Antialiasing...\nSrc =%p\nMask=%p\nDest=%p\nw   =%li\nh   =%li\n",
		srcBits, maskBits, destBits, w, h);

  for (y = h; y; --y) {
    for (x = 0; x < w; ++x) {
      const rgb_t *img = srcBits + x * 4;
      const png_byte *mask = maskBits + x / 2;
      if (x & 1) {
	m[0] = mask[0]	 >>4; m[1] = mask[mw]>>4;
	m[2] = mask[2*mw]>>4; m[3] = mask[3*mw]>>4;
      } else {
	m[0] = mask[0]	 &15; m[1] = mask[mw]&15;
	m[2] = mask[2*mw]&15; m[3] = mask[3*mw]&15;
      }
      t = mconv[m[0]]+mconv[m[1]]+mconv[m[2]]+mconv[m[3]];
      if (t && nomask) {
	t = 16;
	m[3] = m[2] = m[1] = m[0] = 0;
      }
      destBits->alpha = t == 16 ? 255 : t << 4;
      if (t) {
	int r = 0, g = 0, b = 0;
	img += 16 * w;
	for (j = 3; j >= 0; --j) {
	  img -= 4 * w;
	  for (i = 3; i >= 0; --i) {
	    if (!(m[j] & 1 << i)) {
	      r += img[i].r;
	      g += img[i].g;
	      b += img[i].b;
	    }
	  }
	}
	switch (t) {		/* for speed */
	case 1:
	  destBits->r = r / 1;
	  destBits->g = g / 1;
	  destBits->b = b / 1;
	  break;
	case 2:
	  destBits->r = r / 2;
	  destBits->g = g / 2;
	  destBits->b = b / 2;
	  break;
	case 4:
	  destBits->r = r / 4;
	  destBits->g = g / 4;
	  destBits->b = b / 4;
	  break;
	case 8:
	  destBits->r = r / 8;
	  destBits->g = g / 8;
	  destBits->b = b / 8;
	  break;
	case 16:
	  destBits->r = r / 16;
	  destBits->g = g / 16;
	  destBits->b = b / 16;
	  break;
	case 10:
	  destBits->r = r / 10;
	  destBits->g = g / 10;
	  destBits->b = b / 10;
	  break;
	default:
	  destBits->r = r / t;
	  destBits->g = g / t;
	  destBits->b = b / t;
	  break;
	}
      } else {
	*(long *) destBits = background;
      }
      destBits++;
    }
    srcBits += w * 4 * 4;	/* four rows */
    if (maskBits)
      maskBits += mw * 4;
  }
}


static void *
do_render (const void *data, size_t nSize, int simplemask, int invert,
	   double scale_x, double scale_y, int type, long background,
	   int trim, void (*std_sighandler) (int), jmp_buf main_j)
{
  long spritex, spritey;
  long areasize;
  spritearea_t *area = 0, *pixels;
  sprite_t *pSprite;
  rgb_t *oSprite;
  long pass, passes, sectiony;
  long basey = tm.y;
  const int mul = (simplemask & 1) ? 1 : 4;
  const char *filetype = type ? "Artworks" : "Draw";
  const char *sprname = type ? "awfile" : "drawfile";

/* simplemask:
 *  0 : antialias + mask
 *  1 :		    mask
 *  2 : antialias
 *  3 : .
 */

  debug_printf ("Image size = %li,%li\nBbox = %li,%li,%li,%li\nClaiming memory for output image...\n", width, height, box.min.x, box.min.y, box.max.x, box.max.y);

  areasize = sizeof (sprite_t) + sizeof (spritearea_t) + height *
	     ((simplemask & 1)
	      ? (width * 4 + (width + 31 & ~31) / 2)
	      : (width * 16 + (width + 31 & ~31) / 2));
  pixels = spr_malloc ((int) areasize, "Output image");
  if (!pixels)
    fail (fail_NO_MEM, "not enough memory to convert %s file", filetype);
  pixels->size = areasize;
  pixels->sprites = 0;
  pixels->first = pixels->free = 16;

  debug_puts ("Creating output sprite...");

  /* OS_SpriteOp create sprite; 24bit, 90dpi */
  _swi (OS_SpriteOp, _INR (0, 6), 256+15, pixels, sprname, 0, width, height, 0x301680B5);

  pSprite = (sprite_t *) ((char *) pixels + 16);
  oSprite = (rgb_t *) ((char *) pSprite + pSprite->image);

  if (verbose > 1)
    printf ("Rendering %s file\n", filetype);

  if (simplemask & 1) {
    area = pixels;
    passes = 1;
    spritex = width;
    spritey = sectiony = height;
    tm.x /= 4;
    basey = tm.y /= 4;
  } else {
    spritex = width * 4;
    spritey = height * 4;
    for (passes = 1; passes < 65; passes *= 2) {
      sectiony = ((height + passes - 1) / passes) * 4;
      areasize = sectiony * (spritex * 4 + (spritex + 31 & ~31) / 8)
		 + sizeof (sprite_t) + sizeof (spritearea_t);
      if (!sectiony)
	break;
      area = heap_malloc ((int) areasize);
      if (area || type /* kludge to get around Artworks problem */)
	break;
    }

    if (!area)
      fail (fail_NO_MEM, "not enough memory to convert %s file", filetype);

    area->size = areasize;
    area->sprites = 0;
    area->first = area->free = 16;

    debug_printf ("Sprite slice: %li*%li, %li bytes\nCreating slice sprite...\n",
		  spritex, sectiony, areasize);

    _swi (OS_SpriteOp, _INR (0, 6), /* OS_SpriteOp create sprite; 24bit, 90dpi */
	  256+15, area, sprname, 0, spritex, sectiony, 0x301680B5);
  }
  _swi (OS_SpriteOp, _INR (0, 2), 256+29, area, sprname); /* OS_SpriteOp add mask */

  pSprite = (sprite_t *) ((char *) area + 16);

  if (type) {			/* Artworks */
    infoblock[0] = 0;
    infoblock[1] = 0;
    infoblock[2] = 0;
    infoblock[3] = 0;
    infoblock[4] = spritex * 256 * 8 / scale_x / mul;
    infoblock[5] = spritey * 256 * 8 / scale_y / mul;
    _swix (Wimp_ReadPalette, 2, vduvars + 3);
  }
  tm.matrix[0] = scale_x * 65536 * mul;
  tm.matrix[3] = scale_y * 65536 * mul;

  debug_printf ("AW info:\n  Origin: %li,%li\n  Bbox: %li,%li - %li,%li\nMatrix: %li,%li; %li,%li; %li,%li\nMask type: %i\n", infoblock[0], infoblock[1], infoblock[2], infoblock[3], infoblock[4], infoblock[5], tm.matrix[0], tm.matrix[1], tm.matrix[2], tm.matrix[3], tm.x, tm.y, simplemask);

  /* The dangerous bit... we need some special signal handlers */

  pass = setjmp (jump);
  if (pass)
    longjmp (main_jump, pass);

  cache_fontmax ();
  setsignal (sighandler);
  if ((simplemask & 1) == 0)
    set_fontmax (0);

  for (pass = 0; pass < passes; pass++) {
    long thisy = sectiony / mul;
    long offset = (pass * sectiony) / mul;
    _kernel_oserror *err = NULL;

    if (verbose > 2)
      printf ("Pass %li of %li\n", pass + 1, passes);

    tm.y = basey - (height - offset - thisy) * 256 * 2 * mul;
    debug_printf ("Y = %li\n", tm.y);
    if (thisy + offset > height)
      thisy = height - offset;
    /* render onto "transparent", find opaque */
    {
      int i = (pSprite->size - pSprite->image) / sizeof (long);
      long *ptr = (long *) ((char *) pSprite + pSprite->image);
      do {
	ptr[--i] = background | 0xFF000000;
      } while (i);
    }
    if (type && spritex > 1024)
    {
      int xoff, i = 0;
      int xl = infoblock[2], xr = infoblock[4];
      char clip[9];
      clip[0] = 24;
      clip[4] = clip[3] = clip[1] = 0;
      clip[5] = 255;
      clip[7] = (char) (spritey * 2 - 1);
      clip[8] = (char) ((spritey * 2 - 1) >> 8);
#define AW_WIDTH_STEP (1024 * 256 * 8 / scale_x / mul)
      for (xoff = 0; xoff < spritex; xoff += 1024)
      {
	infoblock[4] = xl + ++i * AW_WIDTH_STEP - 1;
	if (infoblock[4] > xr)
	  infoblock[4] = xr;
	clip[2] = (char) (xoff >> 7);
	clip[6] = clip[2] + 7;
	if (clip[5] + clip[6] * 256 >= spritex * 2)
	{
	  clip[5] = (char) (spritex * 2 - 1);
	  clip[6] = (char) ((spritex * 2 - 1) >> 8);
	}
	err = render (area, data, nSize, &tm, type, simplemask & 1,
		      clip, sizeof (clip));
	if (err)
	  break;
      }
    }
    else
      err = render (area, data, nSize, &tm, type, simplemask & 1, 0, 0);
    if (err)
      fail (fail_OS_ERROR, err->errmess);
    makemask (pSprite, spritex, sectiony);

#ifdef DEBUG
    if (pass == 0)
      _swi (OS_SpriteOp, _INR (0, 2), 256+12, area, "<Wimp$ScrapDir>.d2s-work");
#endif

    if ((simplemask & 1) == 0)
      antialias ((rgb_t *) ((char *) pSprite + pSprite->image),
		 (png_byte *) pSprite + pSprite->mask,
		 oSprite + offset * width, background, width, thisy,
		 simplemask & 2);
  }

  if (trim)
    switch (simplemask) {
      case 0:
	trim_mask_24 (pixels, (sprite_t*)((char*)pixels + 16), 0);
	if (invert) {
	  int i = width * height;
	  do {
	    oSprite[--i].alpha ^= 255;
	  } while (i);
	} break;
      case 1:
	trim_mask_24 (pixels, (sprite_t*)((char*)pixels + 16), 0);
	{
	  int i = (width + 31 & ~31) / 32 * height;
	  long *mask = (long *) ((char *) pSprite + pSprite->mask);
	  do {
	    mask[--i] ^= -1;
	  } while (i);
	} break;
      case 2:
	trim_mask_24 (pixels, (sprite_t*)((char*)pixels + 16), 0);
	{ /* kill the alpha channel */
	  int i = width * height;
	  do {
	    oSprite[--i].alpha = 0;
	  } while (i);
	}
	_swi (OS_SpriteOp, _INR (0, 2), 256+30, area, sprname); /* OS_SpriteOp remove mask */
	break;
      default: /*3*/
	trim_mask_24 (pixels, (sprite_t*)((char*)pixels + 16), 0);
	_swi (OS_SpriteOp, _INR (0, 2), 256+30, area, sprname); /* OS_SpriteOp remove mask */
    }
  else if (simplemask & 2)
    _swi (OS_SpriteOp, _INR (0, 2), 256+30, area, sprname); /* OS_SpriteOp remove mask */

  if (simplemask & 1)
    debug_printf ("Image is at %p\n", pixels);
  else
    debug_printf ("Slice is at %p, image is at %p\n", area, pixels);

  /* Dangerous bit finished :-) */
  set_fontmax (1);
  setsignal (std_sighandler);

  if ((simplemask & 1) == 0)
    heap_free (area);

  return pixels;
}


void *
convertdraw (const void *data, size_t nSize, int simplemask, int invert,
	     double scale_x, double scale_y, long background, int trim,
	     void (*std_sighandler) (int), jmp_buf main_j)
{ /* returns a sprite area */
  memcpy (main_jump, main_j, sizeof (jmp_buf));

  /* check that it's a Draw file */
  if (*(int *) data != 0x77617244)
    fail (1, "Not a Draw file");
  debug_puts ("RMEnsuring DrawFile...");

  if (rmensure ("DrawFile", "System:Modules.Drawfile")
      && rmensure ("DrawFile", "System:Modules.Toolbox.Drawfile"))
    fail (fail_OS_ERROR, "failed to load the DrawFile module");

  onerr (_swix (DrawFile_BBox, _INR (0, 4),
		0, data, nSize, &tm, &box));

  tm.x = -box.min.x * scale_x;
  tm.y = -box.min.y * scale_y;

  width = ceil (((box.max.x - box.min.x) * scale_x + 2047.0) / 2048.0);
  height = ceil (((box.max.y - box.min.y) * scale_y + 2047.0) / 2048.0);

  box.min.x /= 256;
  box.max.x /= 256;
  box.min.y /= 256;
  box.max.y /= 256;

  if (width <= 0 || height <= 0)
    fail (fail_BAD_IMAGE, "cannot render an empty %s file!", "Draw");

  {
    int end, i = 10;		/* point past header */
    const int *wdata = data;
    const char *cdata;
    while (i < nSize / 4 && wdata[i])
      i += wdata[i + 1] / 4;
    if (i < nSize / 4) {
      debug_printf ("[start of font object = %i]\n", i * 4);
      end = i * 4 + wdata[i + 1];
      if (end > nSize)
	end = nSize;
      i = i * 4 + 8;
      cdata = (const char *) wdata;
      debug_printf ("[end of font object = %i]\n", end);
      while (i < end && cdata[i]) {
	debug_printf ("[file offset = %i]\n", i);
	check_font (cdata + ++i);
	while (cdata[i++]);
      }
    }
  }

  return do_render (data, nSize, simplemask, invert, scale_x, scale_y,
		    0, background, trim, std_sighandler, main_j);
}


void *
convertartworks (const void *data, size_t nSize, int simplemask,
		 int invert, double scale_x, double scale_y,
		 int renderlevel, long background, int trim,
		 void (*std_sighandler) (int), jmp_buf main_j)
{ /* returns a sprite area */
  _kernel_oserror *err;
  const char *awload;

  memcpy (main_jump, main_j, sizeof (jmp_buf));

  if (heap_malloc == malloc)
    fail (fail_OS_ERROR,
	  "sorry, Artworks file rendering requires a dynamic area");

  debug_puts ("RMEnsuring the Artworks modules...");

  awload = getenv ("Alias$LoadArtWorksModules");
  if (!awload || !awload[0]) {
    if (rmensure ("ImageExtend", "<CCShared$Dir>.RMStore.ImageExtnd")
	|| rmensure ("DitherExtend", "<CCShared$Dir>.RMStore.DitherExt")
	|| rmensure ("ArtworksRenderer", "<CCShared$Dir>.RMStore.AWRender")
	|| rmensure ("GDraw", "<CCShared$Dir>.RMStore.GDraw")
	|| rmensure ("FontDraw", "<CCShared$Dir>.RMStore.FontDraw"))
      fail (fail_NO_AWRENDER,
	    "failed to load the Artworks renderer modules");
  } else {
    _kernel_oserror *err = _swix (OS_CLI, _IN (0), "LoadArtWorksModules");
    if (err)
      fail (fail_NO_AWRENDER, err->errmess);
  }
  if (rmversion ("DitherExtend") < 48 || rmversion ("GDraw") < 293
      || rmversion ("ArtworksRenderer") < 121)
    fail (fail_NO_AWRENDER,
	  "sorry, your Artworks renderer modules can't cope with dynamic areas");

  /* check that it's an Artworks file */

  err = aw_fileinit (data, &nSize);
  if (err)
    fail (fail_BAD_IMAGE, err->errmess);
  // "the file version is too old for this ArtworksRenderer module");
  err = _swix (AWRender_DocBounds, _IN (0) | _OUTR (2, 5),
	       data, &box.min.x, &box.min.y, &box.max.x, &box.max.y);
  if (err)
    fail (fail_BAD_IMAGE, err->errmess);

  tm.x = -box.min.x * 4 * scale_x;
  tm.y = -box.min.y * 4 * scale_y;

  width = ceil (((box.max.x - box.min.x) * scale_x + 511.0) / 512.0);
  height = ceil (((box.max.y - box.min.y) * scale_y + 511.0) / 512.0);

  if (width <= 0 || height <= 0)
    fail (fail_BAD_IMAGE, "cannot render an empty %s file!", "Artworks");

  if (!simplemask && renderlevel > 100)
    renderlevel = 100;

  _swix (AWRender_ClaimVectors, 0);
  return do_render (data, nSize, simplemask, invert, scale_x, scale_y,
		    renderlevel + 1, background, trim,
		    std_sighandler, main_j);
}
