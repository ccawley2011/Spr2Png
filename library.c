#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <math.h>
#ifdef __riscos
#include <signal.h>
#include "swis.h"
#include "kernel.h"
#endif
#include "types.h"
#include "s2p_lib.h"

int32_t width, height;

spritearea_t *sprites;          /* == unsigned char */

const char *program_name;
const char *caller_name;
const char *caller_sprite;

#ifdef __riscos

wksp_t wksp;

int no_press_space;

void
setsignal (void (*handler) (int))
{
  signal (SIGFPE, handler);
  signal (SIGILL, handler);
  signal (SIGINT, handler);
  signal (SIGSEGV, handler);
  signal (SIGSTAK, handler);
  signal (SIGOSERROR, handler);
}


static void
err_report (void)
{
  wksp.unused[0] = 0; /* set error number */
  _swix (Wimp_ReportError, _INR (0,5),
         wksp._msg, caller_sprite ? 0x109 : 1, caller_name,
         caller_sprite, 1, 0);
}


#ifdef __GNUC__
static _kernel_oserror *
commandwindow (const char *title)
{
  return _swix (Wimp_CommandWindow, _IN (0), title);
}
#else
__swi (XOS_Bit | Wimp_CommandWindow)
     _kernel_oserror *commandwindow (const char *title);
#endif


static void
kill_commandwindow (void)
{
  commandwindow ((const char *) (no_press_space ? -1 : 0));
}


int
init_task (const char *task, const char *title)
{
  int istask = !_swix (Wimp_Initialise, _INR (0, 3), 310, 0x4B534154, task, 0);
  if (istask && !commandwindow (title))
    atexit (kill_commandwindow);
  return istask;
}

#endif

void
fail (int ret, const char *msg, ...)
{
  va_list args;

  fflush (stdout);
/*
  if (caller_name)
    {
      int task;
      if (_swix (Wimp_ReadSysInfo, _IN (0) | _OUT (0), 5, &task))
        task = 0;
      if (!task)
        caller_name = 0;
    }
 */
  if (msg)
    {
#ifdef __riscos
      if (caller_name)
        {
          va_start (args, msg);
          vsprintf (wksp._msg + 4, msg, args);
          va_end (args);
          if (islower (msg[0]))
            wksp._msg[4] = toupper (msg[0]);
          err_report ();
        }
      else
#endif
        {
          fprintf (stderr, "%s: ", program_name);
          va_start (args, msg);
          vfprintf (stderr, msg, args);
          va_end (args);
          fprintf (stderr, "\n");
        }
    }
  else
    {
#ifdef __riscos
      _kernel_oserror *x = _kernel_last_oserror ();
      if (x)
        {
          strcpy (wksp._msg, x->errmess);
          if (caller_name)
            err_report ();
          else
            fprintf (stderr, "%s: %s\n", program_name, wksp._msg);
        }
      else if (errno == 0)
        abort ();
      else if (caller_name)
        {
          strcpy (wksp._msg + 4, strerror (errno));
          err_report ();
        }
      else
#endif
        perror (0);
    }
  if (ret == fail_BAD_ARGUMENT && !caller_name)
    fprintf (stderr, "Try '%s --help' for more information.\n", program_name);
  exit (ret);
}


int
readtype (const char *file)     /* -2 if not found, -1 if untyped */
{
#ifdef __riscos
  int found, type;
  onerr (_swix (OS_File, _INR (0, 1) | _OUT (0) | _OUT (6), 23, file, &found, &type));
  return found ? type : -2;
#else
  (void)file;
  return -1;
#endif
}


void
settype (const char *file, int type)
{
#ifdef __riscos
  onerr (_swix (OS_File, _INR(0, 2), 18, file, type));
#else
  (void)file;
  (void)type;
#endif
}


#ifdef DEBUG
void *(spr_malloc) (size_t l, const char *const msg)
#else
void *(spr_malloc) (size_t l)
#endif
{
  void *v = heap_malloc (l);
  debug_printf ("Claimed %li bytes for %s\n", l, msg);
  if (v == 0)
    fail (fail_NO_MEM, "Out of memory (%s)", "sprite");
  return v;
}


const char *
argdup (const char *arg)
{
  char *q = strchr (arg, '=');
  int l = q ? q - arg : strlen (arg);
  q = malloc (l + 1);
  memcpy (q, arg, l);
  q[l] = 0;
  return q;
}


double
readfloat (const char **p)
{                               /* returns 0 on error */
  const char *pp = *p;
  int value = 0;
  int dot = -1;
  for (;;)
    {
      char c = *pp++;
      if (isdigit (c))
        {
          value = value * 10 + c - '0';
          if (dot != -1)
            dot++;
        }
      else if (c == '.')
        {
          if (dot != -1)
            return 0;
          dot = 0;
        }
      else
        break;
    }
  *p = pp - 1;
  return (dot < 0) ? value : (value / pow (10, dot));
}


/* a minimal getopt_long_only... */
int
argmatch (const optslist *args, const char *arg, const char **param)
{
  const optslist *opt = args - 1;
  const optslist *best = 0;
  int bestlen = 0;
  const char *a;

  while (opt++, opt->name)
    {
      int i;
      const char *c = opt->name;
      a = arg;
      while (*a == *c && *c)
        {
          a++;
          c++;
        }
      if (*a && *a != '=')
        continue;
      i = a - arg;
      if (i && i == bestlen)
        fail (fail_BAD_ARGUMENT, "ambiguous option --%s", argdup (arg));
      if (i > bestlen)
        {
          best = opt;
          bestlen = i;
        }
    }
  if (!best)
    fail (fail_BAD_ARGUMENT, "unknown option --%s", argdup (arg));
  if (param)
    *param = 0;
  a = arg + bestlen;
  switch (best->param)
    {
    case REQUIRED_PARAM:
      if (*a++ != '=')
        fail (fail_BAD_ARGUMENT, "missing parameter for --%s", arg);
      if (param)
        *param = a;
      break;
    case OPTIONAL_PARAM:
      if (param)
        *param = (*a == '=') ? (a + 1) : 0;
    }
  return best->id;
}
