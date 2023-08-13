#ifndef __spr2png_types_h
#define __spr2png_types_h

#define fail_BAD_ARGUMENT       1
#define fail_OS_ERROR           2
#define fail_LOAD_ERROR         3
#define fail_BAD_DATA           4
#define fail_BAD_IMAGE          5
#define fail_TASKWINDOW         6
#define fail_NO_AWRENDER        7
#define fail_UNSUPPORTED        8
#define fail_LIBPNG_FAIL        128
#define fail_CONVERT_FAIL       128
#define fail_NO_MEM             129
#define fail_NO_24BPP           255

typedef struct { // 32-bit ints required
  long size;
  char name[12];
  int width, height;
  int dummy1, dummy2;
  int image, mask; // offsets
  unsigned int mode;
  // palette (if image!=0x2c)
  // image data
  // mask data (if mask!=image)
} sprite_t;

typedef struct {
  long size;
  int sprites;
  int first;
  int free;
} spritearea_t;

typedef struct {
  char r, g, b, alpha;
} rgb_t;

typedef struct {
  char c, m, y, k;
} cmyk_t;

typedef struct {
  char grey, alpha;
} grey_t;

typedef struct {
  char *trans;
  long first, last;
} colour8_t;

typedef enum {
  MASK_SIMPLE,
  MASK_ALPHA,
  MASK_NONE,
  MASK_ALL
} mask_t;

typedef struct {
  signed long matrix[4];
  signed long x, y;
} draw_matrix;

typedef struct {
  struct { signed long x, y; } min;
  struct { signed long x, y; } max;
} draw_bounds;

#endif
