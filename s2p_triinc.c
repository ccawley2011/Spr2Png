#if !defined PIXEL || !defined ROUNDING || !defined SIZE
# error Ouch!
#endif
/* PIXEL: the pixel type */
/* ROUNDING: to round to next word */
/* SIZE: kludge to get round struct word alignment */
/* ALPHA: has a combined alpha channel */
/* MASK: separate alpha channel */

#if !defined ALPHA && SIZE == 4
# define MATCH(x) ((tmp = &(x)), (tmp->r == Trans.p.r && tmp->g == Trans.p.g && tmp->b == Trans.p.b))
#else
# define MATCH(x) ((x) == trans)
#endif

{
#ifdef DEBUG
  static const int print_fn_name_code[] = {
    0xE92D4001, /*	STMFD	r13!,{r0,r14}		*/
    0xE59BE000, /*	LDR	r14,[r11,#0]		*/
    0xE3CEE3FF, /*	BIC	r14,r14,#&FC000003	*/
    0xE53E0004, /* loop	LDR	r0,[r14,#-4]!		*/
    0xE35004FF, /*	CMP	r0,#&FF000000		*/
    0x3AFFFFFC, /*	BCC	loop			*/
    0xE3C004FF, /*	BIC	r0,r0,#&FF000000	*/
    0xE04E0000, /*	SUB	r0,r14,r0		*/
    0xEF000002, /*	SWI	OS_Write0		*/
    0xEF000003, /*	SWI	OS_NewLine		*/
    0xE8FD8001  /*	LDMFD	r13!,{r0,pc}^		*/
  };
  static void (*const print_fn_name)(void) = (void (*)()) print_fn_name_code;
#endif
  long x, y, trimleft;
  PIXEL *ptr, *img = image;
  long rwidth = width + ROUNDING & ~ROUNDING;
#ifndef ALPHA
  char *mptr, *mask = alpha;
  long mwidth = width + 3 & ~3;
# if SIZE == 4
  union { long l; rgb_t p; } Trans;
  PIXEL *tmp;

  Trans.l = trans; /* happens to be in right order */
# endif
#endif

#ifdef DEBUG
  print_fn_name ();
  _swi (OS_File, _INR (0, 2) | _INR (4, 5), 10, "<Wimp$ScrapDir>.Image", 0xFFD, image, ADD (image, height * rwidth));
# ifndef ALPHA
  if (alpha)
    _swi (OS_File, _INR (0, 2) | _INR (4, 5), 10, "<Wimp$ScrapDir>.Mask", 0xFFD, alpha, alpha + height * width);
  printf ("image = %p, mask = %p\n", image, alpha);
# endif
#endif

  if (verbose)
    puts ("Trimming sprite...");

  /* Trim blank rows from bottom */
  y = 0;
  ptr = ADD (img, height * rwidth);
#ifndef ALPHA
  mptr = mask + height * mwidth;
#endif
  do {
    ptr = ADD (ptr, -rwidth);
#ifndef ALPHA
    mptr -= mwidth;
#endif
    x = width;
#ifdef ALPHA
    while (x)
      if (INDEX (ptr, --x).alpha != 0)
	goto do_lower_trim;
#else
    if (alpha) {
      while (x)
	if (mptr[--x])
	  goto do_lower_trim;
    } else {
      while (x)
	if (!MATCH (INDEX (ptr, --x)))
	  goto do_lower_trim;
    }
#endif /* ALPHA */
  } while (++y < height);
  /* we'll only reach this point if the image is completely masked out */
#ifdef DEBUG
  _swi (OS_File, _INR (0, 5), 10, "<Wimp$Scrap>", 0xFFD, 0, img, ADD (img, width * height));
#endif
  return 1;

do_lower_trim:
  if (y)
    height -= y;
  if (verbose > 2)
    printf ("%li row(s) removed from bottom\n", y);

  /* Trim blank rows from top */
  y = 0;
  ptr = img;
#ifndef ALPHA
  mptr = mask;
#endif
  for (;;) {
    x = width;
#ifdef ALPHA
    while (x)
      if (INDEX (ptr, --x).alpha != 0)
	goto do_upper_trim;
#else
    if (alpha) {
      while (x)
	if (mptr[--x])
	  goto do_upper_trim;
    } else {
      while (x)
	if (!MATCH (INDEX (ptr, --x)))
	  goto do_upper_trim;
    }
#endif /* ALPHA */
    ptr = ADD (ptr, rwidth);
#ifndef ALPHA
    mptr += mwidth;
#endif
    y++;
  }

do_upper_trim:
  /* Image shift deferred; just adjust height and start pointer for now */
  height -= y;
  img = ADD (img, y * rwidth);
#ifndef ALPHA
  mask += y * mwidth;
#endif

  if (verbose > 2)
    printf ("%li row(s) removed from top\n", y);

  /* Trim blank columns from left */
  trimleft = width;
  y = height;
  ptr = img;
#ifndef ALPHA
  mptr = mask;
#endif
  do {
    int xx = 0;
#ifdef ALPHA
    while (xx < trimleft && INDEX (ptr, xx).alpha == 0)
      xx++;
#else
    if (alpha)
      while (xx < trimleft && mptr[xx] == 0)
	xx++;
    else
      while (xx < trimleft && MATCH (INDEX (ptr, xx)))
	xx++;
#endif /* ALPHA */
    if (xx < trimleft) {
      trimleft = xx;
      if (!trimleft)
	break;
    }
    ptr = ADD (ptr, rwidth);
#ifndef ALPHA
    mptr += mwidth;
#endif
  } while (--y);
  if (verbose > 2)
    printf ("%li column(s) removed from left\n", trimleft);

  /* Trim blank columns from right */
  x = width;
  y = height;
  ptr = img;
#ifndef ALPHA
  mptr = mask;
#endif
  do {
    long xx = width;
#ifdef ALPHA
    while (--xx >= 0 && INDEX (ptr, xx).alpha == 0)
      ;
#else
    if (alpha)
      while (--xx >= 0 && mptr[xx] == 0)
	;
    else
      while (--xx >= 0 && MATCH (INDEX (ptr, xx)))
	;
#endif /* ALPHA */
    xx = width - 1 - xx;
    if (xx < x) {
      x = xx;
      if (!x)
	break;
    }
    ptr = ADD (ptr, rwidth);
#ifndef ALPHA
    mptr += mwidth;
#endif
  } while (--y);
  if (verbose > 2)
    printf ("%li column(s) removed from right\n", x);

  x += trimleft;
  if (x) {
    long nwidth = (width -= x) + ROUNDING & ~ROUNDING;
    PIXEL *dest = image; /* do the deferred upper trim */
#ifndef ALPHA
    long nmwidth;
    char *mdest;
#endif
    ptr = ADD (img, trimleft);
    for (y = height; y; --y) {
      memmove (dest, ptr, width * SIZE);
      dest = ADD (dest, nwidth);
      ptr = ADD (ptr, rwidth);
    }

#ifndef ALPHA
    if (alpha) {
      nmwidth = width + 3 & ~3;
      mdest = alpha;
      mptr = mask + trimleft;
      for (y = height; y; --y) {
	memmove (mdest, mptr, width);
	mdest += nmwidth;
	mptr += mwidth;
      }
    }
#endif
  } else if (image != img) {
    /* Upper trim but no l/r trim */
    memmove (image, img, height * rwidth * SIZE);
  }

  /* Done. */
  return 0;
}

#undef MATCH
