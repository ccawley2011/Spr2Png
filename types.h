#ifndef __spr2png_types_h
#define __spr2png_types_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>

#define fail_BAD_ARGUMENT       1
#define fail_OS_ERROR           2
#define fail_IO_ERROR           3
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
  uint32_t size;
  char name[12];
  int width, height;
  int leftbit, rightbit;
  int image, mask; // offsets
  unsigned int mode;
  // palette (if image!=0x2c)
  // image data
  // mask data (if mask!=image)
} sprite_t;

typedef struct {
  uint32_t size;
  int sprites;
  int first;
  int free;
} spritearea_t;

typedef struct {
  uint8_t r, g, b, alpha;
} rgb_t;

typedef struct {
  uint8_t c, m, y, k;
} cmyk_t;

typedef struct {
  uint8_t grey, alpha;
} grey_t;

typedef struct {
  uint8_t *trans;
  int32_t first, last;
} colour8_t;

typedef enum {
  MASK_SIMPLE,
  MASK_ALPHA,
  MASK_NONE,
  MASK_ALL
} mask_t;

typedef struct {
  int32_t matrix[4];
  int32_t x, y;
} draw_matrix;

typedef struct {
  struct { int32_t x, y; } min;
  struct { int32_t x, y; } max;
} draw_bounds;

#endif
