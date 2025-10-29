#ifndef __spr2png_d2s_lib_h
#define __spr2png_d2s_lib_h

#include "library.h"
#include <setjmp.h>

/* In c.d2s_conv: */

#define simplemask_NO_BLEND (1 << 0)
#define simplemask_NO_MASK  (1 << 1)
#define simplemask_WIDE     (1 << 2)

extern void *convertdraw (const void *data, size_t nSize, int simplemask,
                          bool invert, double scale_x, double scale_y,
                          long background, bool trim,
                          void (*std_sighandler) (int), jmp_buf main_j);

extern void *convertartworks (const void *data, size_t nSize, int simplemask,
                              bool invert, double scale_x, double scale_y,
                              int renderlevel, long background, bool trim,
                              void (*std_sighandler) (int), jmp_buf main_j);

/* In s.d2s_render: */

/*extern int save_context[4];*/

extern draw_matrix tm;
extern draw_bounds box;
extern long infoblock[];
extern int vduvars[];

extern _kernel_oserror *aw_fileinit (const void *file, size_t * size);

extern _kernel_oserror *render (const void *area, const void *file,
                                size_t size, const draw_matrix * matrix,
                                int type, int thicken, const char *vdu,
                                size_t vdusize);

extern void makemask (sprite_t * sprite, long width, long height);

extern void render_shutdown (void);

/* In c.d2s_trim: */

extern void trim_mask_24 (spritearea_t *, sprite_t *, bool inverse);

#endif
