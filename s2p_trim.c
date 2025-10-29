#include <stdio.h>
#include <string.h>
#include "swis.h"
#include "s2p_lib.h"

#undef ALPHA

#define ADD(x,y) ((PIXEL*)((uint8_t*)(x) + (y) * SIZE))
#define INDEX(x,y) (*(PIXEL*)((uint8_t*)(x) + (y) * SIZE))

int trim_mask_8 (uint8_t *const image, uint8_t *const alpha, int trans)
#define PIXEL uint8_t
#define ROUNDING 3
#define SIZE 1
#include "c.s2p_triinc"

#undef PIXEL
#undef ROUNDING
#undef SIZE

#define ALPHA

int trim_mask_8a (grey_t *const image)
#define PIXEL grey_t
#define ROUNDING 1
#define SIZE 2
#include "c.s2p_triinc"

#undef PIXEL
#undef ROUNDING
#undef SIZE

int trim_mask_24a (rgb_t *const image)
#define PIXEL rgb_t
#define ROUNDING 0
#define SIZE 4
#include "c.s2p_triinc"

#undef ALPHA

int trim_mask_24 (rgb_t *const image, uint8_t *const alpha, uint32_t trans)
#include "c.s2p_triinc"
