#ifndef __spr2png_constants_h
#define __spr2png_constants_h

extern const char imagetype[2/*alpha*/][2/*grey*/][6/*bpp*/];

extern const rgb_t rgb2[2], rgb4[4], rgb16[16], rgb256[256];
  // palette information

extern const rgb_t *const palinfo[];
  // palette pointers (index = log2 bpp)

extern const int palsize[];
  // palette sizes

extern const uint8_t modevars[50][3];
  // mode variables (xeig, yeig, lnbpp)

#endif
