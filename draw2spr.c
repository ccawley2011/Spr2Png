#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include "swis.h"
#include "kernel.h"

/* This code is RISC OS specific. */
/* It makes assumptions about the sizes of various types. */

#include "d2s_lib.h"


static jmp_buf jump;
static _kernel_oserror sigerr;

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
  static bool recur = false;
  if (recur)
    return;
  recur = true;
  render_shutdown ();
  _swi (Hourglass_Off, 0);
}


static void
version (void)
{
  printf ("%s %s " COPYRIGHT " Darren Salt\n"
          "For copyright information, see the spr2png help file.\n",
          program_name, program_version);
  exit (0);
}


static void
help (void)
{
  printf ("Usage: %s [<options>] <src> <dest>\n"
          "Render a Draw or Artworks file as an RGBA sprite suitable for spr2png.\n"
          "Note: cannot render in a taskwindow.\n"
          "\n"
          "  -s, --scale=X[,Y]      scale the image\n"
          "                         formats: plain (1.2), %%age (120%%), ratio (6:5)\n"
          "  -b, --background=BGND  specify a background colour (format = &BBGGRR)\n"
          "  -t, --trim             remove masked-out edge rows and columns\n"
          "  -n, --inverse-alpha    invert the alpha channel\n"
          "  -m, --no-mask          don't generate a mask\n"
          "  -M, --no-blend,        don't create an alpha channel or background blend\n"
          "      --no-alpha\n"
          "  -w  --wide-mask        use wide masks for the alpha channel\n"
          "  -r, --render=LEVEL     Artworks rendering level (default is 11.0 = full)\n"
          "                         (without -m, the image is always anti-aliased)\n"
          "\n"
          "  -v, --verbose          report conversion information\n"
          "      --help             display this help text then exit\n"
          "      --version          display the version number then exit\n",
          program_name);
  exit (0);
}


static const optslist args[] = {
  {1, "help", NO_PARAM},
  {2, "version", NO_PARAM},
  {'M', "no-alpha", NO_PARAM},
  {'M', "no-blend", NO_PARAM},
  {'m', "no-mask", NO_PARAM},
  {'w', "wide-mask", NO_PARAM},
  {'r', "render", REQUIRED_PARAM},
  {'s', "scale", REQUIRED_PARAM},
  {'t', "trim", NO_PARAM},
  {'v', "verbose", REQUIRED_PARAM},
  {3, "cname", REQUIRED_PARAM},
  {4, "csprite", REQUIRED_PARAM},
  {0, 0, NO_PARAM}
};


int
main (int argc, const char *const argv[])
{
  FILE *ifp = 0;
  size_t size;
  long y;
  void *drawfile, *image;
  uint8_t wimpstate, simplemask = 0;
  bool inverse = false, trim = false;
  double scale_x = 1, scale_y = 1;
  int renderlevel = 110;
  long background = 0xFFFFFF;

  const char *from = 0, *to = 0;

  _kernel_last_oserror ();      /* lose any errors caused on startup */

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

  wimpstate = _swi (TaskWindow_TaskInfo, _IN (0) | _RETURN (0), 0);
  if (!wimpstate)               /* not in a taskwindow */
    init_task ("Draw2Spr backend", argv[0]);
  /* no complaint yet - we want to allow --help and --version */

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
              simplemask |= simplemask_NO_BLEND;
              break;
            case 'b':
              p = arg;
              goto get_background;
            case 'm':
              simplemask |= simplemask_NO_MASK;
              break;
            case 'w':
              simplemask |= simplemask_WIDE;
              break;
            case 'n':
              inverse = true;
              break;
            case 'r':
              p = arg;
              goto get_render;
            case 's':
              p = arg;
              goto get_scale;
            case 't':
              trim = true;
              break;
            case 'v':
              verbose++;
              break;
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
                case '#':
                  no_press_space = true;
                  break;
                case 'M':
                  simplemask |= simplemask_NO_BLEND;
                  break;
                case 'b':
                  if (p[1])
                    p++;
                  else if (++y < argc)
                    p = argv[y];
                  else
                    fail (fail_BAD_ARGUMENT, "missing parameter for %s",
                          "-b");
                get_background:
                  errno = 0;
                  background = strtol (p, (char **) &p, 16) & 0xFFFFFF;
                  if (errno || *p--)
                    fail (fail_BAD_ARGUMENT, "bad background colour value");
                  break;
                case 'm':
                  simplemask |= simplemask_NO_MASK;
                  break;
                case 'w':
                  simplemask |= simplemask_WIDE;
                  break;
                case 'n':
                  inverse = true;
                  break;
                case 'r':
                  if (p[1])
                    p++;
                  else if (++y < argc)
                    p = argv[y];
                  else
                    fail (fail_BAD_ARGUMENT, "missing parameter for %s",
                          "-r");
                get_render:renderlevel =
                    readfloat (&p) * 10;
                  if (renderlevel > 110)
                    renderlevel = 110;
                  if (*p--)
                    fail (fail_BAD_ARGUMENT, "bad render type value");
                  break;
                case 's':
                  if (p[1])
                    p++;
                  else if (++y < argc)
                    p = argv[y];
                  else
                    fail (fail_BAD_ARGUMENT, "missing parameter for %s",
                          "-s");
                get_scale:{
                    int get_x;
                    for (get_x = 1; get_x >= 0; --get_x)
                      {
                        double v;
                        scale_y = readfloat (&p);
                        if (scale_y == 0)
                          fail (fail_BAD_ARGUMENT, "bad scale value");
                        switch (*p)
                          {
                          case '%':
                            p++;
                            scale_y /= 100;
                            break;
                          case ':':
                            p++;
                            v = readfloat (&p);
                            if (v)
                              {
                                scale_y /= v;
                                break;
                              }
                            else
                              scale_y = 0;
                          }
                        if (scale_y <= 0 || scale_y > 10)
                          fail (fail_BAD_ARGUMENT, "bad scale value");
                        if (get_x)
                          {
                            scale_x = scale_y;
                            if (*p == ',')
                              p++;      /* specifying a different Y scaling */
                            else
                              break;
                          }
                      }
                    if (*p--)
                      fail (fail_BAD_ARGUMENT, "bad scale value");
                  }
                  break;
                case 't':
                  trim = true;
                  break;
                case 'v':
                  verbose++;
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

  if (wimpstate)
    fail (fail_TASKWINDOW, "cannot render in a taskwindow");

  debug_printf ("Parameters\n"
                "Input     : %s\n"
                "Output    : %s\n"
                "Blended   : %s\n"
                "Masked    : %s\n"
                "Wide      : %s\n"
                "Trim      : %s\n"
                "Inverse   : %s\n"
                "Verbosity : %i\n"
                "Scale     : %g,%g\n"
                "Background: %08lX\n",
                from, to,
                simplemask & simplemask_NO_BLEND ? "no" : "yes",
                simplemask & simplemask_NO_MASK ? "no" : "yes",
                simplemask & simplemask_WIDE ? "yes" : "no",
                trim ? "yes" : "no", inverse ? "yes" : "no",
                verbose, scale_x, scale_y, background);

  y = readtype (from);

  if (y != 0xAFF && y != 0xD94)
    fail (fail_BAD_DATA,
          "'%s' not found, or is not a Draw or Artworks file", from);
  if ((_kernel_osbyte (129, 0, 255) & 0xFF) < 0xA5)
    fail (fail_NO_24BPP, "I can only convert files on a Risc PC");

  heap_init ("Draw2Spr workspace");

  _swi (Hourglass_On, _IN (0), 0);

  ifp = fopen (from, "rb");
  if (ifp == NULL)
    fail (fail_OS_ERROR, 0);
  fseek (ifp, 0, SEEK_END);
  size = (size_t) ftell (ifp);
  fseek (ifp, 0, SEEK_SET);
  drawfile = spr_malloc (size, "input file");
  if (fread (drawfile, size, 1, ifp) != 1)
    fail (fail_OS_ERROR, "couldn't load file");

  switch (y)
    {
    case 0xAFF:         /* Draw file */
      image = convertdraw (drawfile, size, simplemask, inverse,
                           scale_x, scale_y, background, trim,
                           sighandler, jump);
      if (!image)
        fail (fail_NO_MEM, "couldn't convert %s file", "Draw");
      break;
    case 0xD94:         /* Artworks file */
      image = convertartworks (drawfile, size, simplemask, inverse,
                               scale_x, scale_y, renderlevel, background,
                               trim, sighandler, jump);
      if (!image)
        fail (fail_NO_MEM, "couldn't convert %s file", "Artworks");
      break;
    default:                    /* whoops! */
      fail (fail_CONVERT_FAIL, "wibble!");
      return 0; /* fail() doesn't return */
    }
  heap_free (drawfile);

  if (verbose)
    puts ("Writing file...");

  {
    _kernel_oserror *err = _swix (OS_SpriteOp, _INR (0, 2), 256 + 12, image, to);
    if (err)
      fail (fail_OS_ERROR, err->errmess);
  }

  if (verbose)
    puts ("Finished.");

  shutdown ();
  return 0;
}
