#ifndef __spr2png_library_h
#define __spr2png_library_h

#include "C:kernel.h"
#include "types.h"

#define COPYRIGHT "\xA9"

extern long width, height;

extern spritearea_t *sprites; /* == unsigned char */

typedef union {
  char _msg[2048];
  char used[256];
  unsigned long unused[65536/32];
  struct { char u[256]; long p[256]; } p;
} wksp_t;

typedef struct
{
  int id;
  const char *name;
  enum
  { NO_PARAM, REQUIRED_PARAM, OPTIONAL_PARAM }
  param;
}
optslist;


extern wksp_t wksp;

extern int verbose;

extern const char *program_name;
extern const char program_version[];
extern const char *caller_name;
extern const char *caller_sprite;


void setsignal(void (*handler)(int));
void fail (int ret, const char *msg, ...);
void swi (int swi);
const char *argdup (const char *); /*option*/

int readtype (const char *name);
void settype (const char *name, int type);

#ifdef DEBUG
void *spr_malloc (size_t l, const char *const msg);
#else
void *spr_malloc (size_t l);
#define spr_malloc(x,y) (spr_malloc)(x)
#endif

double readfloat (const char **);

extern int no_press_space;
int init_task (const char *task, const char *title);

int argmatch (const optslist *args, const char *arg, const char **param);


/* In s.heap: */

extern int heap_init (const char *);
extern void *(*heap_malloc) (size_t);
extern void *(*heap_realloc) (void *,size_t);
extern void (*heap_free) (const void *const);


#define onerr(swi) \
  { \
    _kernel_oserror *zz = (swi); \
    if (zz) \
      fail (fail_OS_ERROR, zz->errmess); \
  }

#endif

#ifdef DEBUG
#define debug_puts puts
#define debug_printf printf
#else
#define debug_puts(x)
#define debug_printf(x,...)
#endif
