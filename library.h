#ifndef __spr2png_library_h
#define __spr2png_library_h

#include "types.h"

#ifdef __riscos
#define COPYRIGHT "\xA9"
#else
#define COPYRIGHT "(c)"
#endif

extern int32_t width, height;

extern spritearea_t *sprites;

typedef union {
  char _msg[2048];
  uint8_t used[256];
  uint32_t unused[65536/32];
  struct { uint8_t u[256]; uint32_t p[256]; } p;
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

void fail (int ret, const char *msg, ...);
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

int argmatch (const optslist *args, const char *arg, const char **param);

#ifdef __riscos
#include "kernel.h"

void setsignal(void (*handler)(int));

extern bool no_press_space;
bool init_task (const char *task, const char *title);

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

#else
#include <stdlib.h>

#define heap_malloc malloc
#define heap_realloc realloc
#define heap_free free

#endif

#ifdef DEBUG
#define debug_puts puts
#define debug_printf printf
#else
#define debug_puts(x)
#define debug_printf(x,...)
#endif

#endif
