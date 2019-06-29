/* Copyright (C) 2001-2017 Peter Selinger.
   This file is part of Potrace. It is free software and it is covered
   by the GNU General Public License. See the file COPYING for details. */


/* The PDF backend of Potrace. Stream compression is optionally
	supplied via the functions in flate.c. */

#include "backend_pdf.h"

typedef int color_t;

size_t dummy_xship(std::vector<unsigned char> &fout, int filter, char *s, size_t len) {
    
    std::string bytes(s, len);
    
    std::copy(bytes.begin(), bytes.end(), std::back_inserter(fout));
    
    return len;
}

#define TRY(x) if (x) goto try_error

/* ---------------------------------------------------------------------- */
/* auxiliary: growing arrays */

struct intarray_s {
  int size;
  int *data;
};
typedef struct intarray_s intarray_t;

static inline void intarray_init(intarray_t *ar) {
  ar->size = 0;
  ar->data = NULL;
}

static inline void intarray_term(intarray_t *ar) {
  free(ar->data);
  ar->size = 0;
  ar->data = NULL;
}

/* Set ar[n]=val. Expects n>=0. Grows array if necessary. Return 0 on
   success and -1 on error with errno set. */
static inline size_t intarray_set(intarray_t *ar, int n, int val) {
  int *p;
  int s;

  if (n >= ar->size) {
    s = n+1024;
    p = (int *)realloc(ar->data, s * sizeof(int));
    if (!p) {
      return -1;
    }
    ar->data = p;
    ar->size = s;
  }
  ar->data[n] = val;
  return 0;
}

/* ---------------------------------------------------------------------- */
/* some global variables */

static intarray_t xref;
static int nxref = 0;
static intarray_t pages;
static int npages;
static size_t streamofs;
static size_t outcount;  /* output file position */

/* ---------------------------------------------------------------------- */
/* functions for interfacing with compression backend */

/* xship: callback function that must be initialized before calling
   any other functions of the "ship" family. xship_file must be
   initialized too. */

/* print the token to f, but filtered through a compression
   filter in case filter!=0 */
static size_t (*xship)(std::vector<unsigned char> &fout, int filter, char *s, size_t len);
//static FILE *xship_file;

/* ship PDF code, filtered */
static int ship(std::vector<unsigned char> &fout, const char *fmt, ...) {
    
    va_list args;
    static char buf[4096]; /* static string limit is okay here because
                            we only use constant format strings - for
                            the same reason, it is okay to use
                            vsprintf instead of vsnprintf below. */
    va_start(args, fmt);
    vsprintf(buf, fmt, args);
    buf[4095] = 0;
    va_end(args);
    
    outcount += xship(fout, 1, buf, strlen(buf));
    return 0;
}

/* ship PDF code, unfiltered */
static int shipclear(std::vector<unsigned char> &fout, const char *fmt, ...) {
    
  static char buf[4096];
  va_list args;

  va_start(args, fmt);
  vsprintf(buf, fmt, args);
  buf[4095] = 0;
  va_end(args);

  outcount += xship(fout, 0, buf, strlen(buf));
  return 0;
}

/* set all callback functions */
static void pdf_callbacks(info_t *info) {

//  if (info.compress) {
//    xship = pdf_xship;
//  } else {
    xship = dummy_xship;
//  }
//  xship_file = fout;
}

/* ---------------------------------------------------------------------- */
/* PDF path-drawing auxiliary functions */

/* coordinate quantization */
static inline point_t unit(dpoint_t p, info_t *info) {
    
  point_t q;

  q.x = (long)(floor(p.x*info->unit+.5));
  q.y = (long)(floor(p.y*info->unit+.5));
  return q;
}

static void pdf_coords(std::vector<unsigned char> &fout, dpoint_t p, info_t *info) {
    
  point_t cur = unit(p, info);
  ship(fout, "%ld %ld ", cur.x, cur.y);
}

static void pdf_moveto(std::vector<unsigned char> &fout, dpoint_t p, info_t *info) {
    
  pdf_coords(fout, p, info);
  ship(fout, "m\n");
}

static void pdf_lineto(std::vector<unsigned char> &fout, dpoint_t p, info_t *info) {
    
  pdf_coords(fout, p, info);
  ship(fout, "l\n");
}

static void pdf_curveto(std::vector<unsigned char> &fout, dpoint_t p1, dpoint_t p2, dpoint_t p3, info_t *info) {
    
  point_t q1, q2, q3;

  q1 = unit(p1, info);
  q2 = unit(p2, info);
  q3 = unit(p3, info);

  ship(fout, "%ld %ld %ld %ld %ld %ld c\n", q1.x, q1.y, q2.x, q2.y, q3.x, q3.y);
}

/* this procedure returns a statically allocated string */
static const char *pdf_colorstring(const color_t col) {
    
  double r, g, b;
  static char buf[100];

  r = (col & 0xff0000) >> 16;
  g = (col & 0x00ff00) >> 8;
  b = (col & 0x0000ff) >> 0;

  if (r==0 && g==0 && b==0) {
    return "0 g";
  } else if (r==255 && g==255 && b==255) {
    return "1 g";
  } else if (r == g && g == b) {
    sprintf(buf, "%.3f g", r/255.0);
    return buf;
  } else {
    sprintf(buf, "%.3f %.3f %.3f rg", r/255.0, g/255.0, b/255.0);
    return buf;
  }
}

//static color_t pdf_color = -1;

static void pdf_setcolor(std::vector<unsigned char> &fout, const color_t col, color_t *pdf_color) {
    
  if (col == *pdf_color) {
    return;
  }
  *pdf_color = col;

  ship(fout, "%s\n", pdf_colorstring(col));
}

/* explicit encoding, does not use special macros */
static int pdf_path(std::vector<unsigned char> &fout, potrace_curve_t *curve, info_t *info) {
    
  int i;
  dpoint_t *c;
  int m = curve->n;

  c = curve->c[m-1];
  pdf_moveto(fout, c[2], info);

  for (i=0; i<m; i++) {
    c = curve->c[i];
    switch (curve->tag[i]) {
    case POTRACE_CORNER:
      pdf_lineto(fout, c[1], info);
      pdf_lineto(fout, c[2], info);
      break;
    case POTRACE_CURVETO:
      pdf_curveto(fout, c[0], c[1], c[2], info);
      break;
    }
  }
  return 0;
}

/* ---------------------------------------------------------------------- */
/* Backends for various types of output. */

/* Normal output: black on transparent */
static int render0(std::vector<unsigned char> &fout, potrace_path_t *plist, info_t *info, color_t *pdf_color) {
    
  potrace_path_t *p;

  pdf_setcolor(fout, info->color, pdf_color);
  list_forall (p, plist) {
    pdf_path(fout, &p->curve, info);
    ship(fout, "h\n");
    if (p->next == NULL || p->next->sign == '+') {
      ship(fout, "f\n");
    }
  }
  return 0;
}

/* Opaque output: alternating black and white */
static int render0_opaque(std::vector<unsigned char> &fout, potrace_path_t *plist, info_t *info, color_t *pdf_color) {
    
  potrace_path_t *p;
  
  list_forall (p, plist) {
    pdf_path(fout, &p->curve, info);
    ship(fout, "h\n");
    pdf_setcolor(fout, p->sign=='+' ? info->color : info->fillcolor, pdf_color);
    ship(fout, "f\n");
  }
  return 0;
}

/* select the appropriate rendering function from above */
static int pdf_render(std::vector<unsigned char> &fout, potrace_path_t *plist, info_t *info, color_t *pdf_color) {
    
  if (info->opaque) {
    return render0_opaque(fout, plist, info, pdf_color);
  }
  return render0(fout, plist, info, pdf_color);
}  

/* ---------------------------------------------------------------------- */
/* PDF header and footer */

int init_pdf(std::vector<unsigned char> &fout, info_t *info) {
    
    intarray_init(&xref);
	intarray_init(&pages);
	nxref = 0;
	npages = 0;

	/* set callback functions for shipping routines */
	pdf_callbacks(info);
	outcount = 0;

	shipclear(fout, "%%PDF-1.3\n");

	TRY(intarray_set(&xref, nxref++, (int)outcount));
	shipclear(fout, "1 0 obj\n<</Type/Catalog/Pages 3 0 R>>\nendobj\n");

	TRY(intarray_set(&xref, nxref++, (int)outcount));
    shipclear(fout, (char *)"2 0 obj\n"
              "<</Creator"
              "(potrace 1.15, written by Peter Selinger 2001-2017)>>\n"
              "endobj\n");

	/* delay obj #3 (pages) until end */
	nxref++;

//    fflush(fout);
	return 0;

 try_error:
        return 1;
}

int term_pdf(std::vector<unsigned char> &fout, info_t *info) {
    
	size_t startxref;
	int i;

	pdf_callbacks(info);

	TRY(intarray_set(&xref, 2, (int)outcount));
	shipclear(fout, "3 0 obj\n"
		"<</Type/Pages/Count %d/Kids[\n", npages);
	for (i = 0; i < npages; i++)
		shipclear(fout, "%d 0 R\n", pages.data[i]);
	shipclear(fout, "]>>\nendobj\n");

	startxref = outcount;

	shipclear(fout, "xref\n0 %d\n", nxref + 1);
	shipclear(fout, "0000000000 65535 f \n");
	for (i = 0; i < nxref; i++)
		shipclear(fout, "%0.10d 00000 n \n", xref.data[i]);

	shipclear(fout, "trailer\n<</Size %d/Root 1 0 R/Info 2 0 R>>\n", nxref + 1);
	shipclear(fout, "startxref\n%d\n%%%%EOF\n", startxref);

//    fflush(fout);
    
	intarray_term(&xref);
	intarray_term(&pages);
	return 0;

 try_error:
        return 1;
}

/* if largebbox is set, set bounding box to pagesize. Return 0 on
   success or 1 on error with errno set. */
static int pdf_pageinit(std::vector<unsigned char> &fout, imginfo_t *imginfo, int largebbox, info_t *info, color_t *pdf_color) {
    
	double origx = imginfo->trans.orig[0] + imginfo->lmar;
	double origy = imginfo->trans.orig[1] + imginfo->bmar;
	double dxx = imginfo->trans.x[0] / info->unit;
	double dxy = imginfo->trans.x[1] / info->unit;
	double dyx = imginfo->trans.y[0] / info->unit;
	double dyy = imginfo->trans.y[1] / info->unit;

	double pagew = imginfo->trans.bb[0]+imginfo->lmar+imginfo->rmar;
	double pageh = imginfo->trans.bb[1]+imginfo->tmar+imginfo->bmar;

	*pdf_color = -1;

	TRY(intarray_set(&xref, nxref++, (int)outcount));
	shipclear(fout, "%d 0 obj\n", nxref);
	shipclear(fout, "<</Type/Page/Parent 3 0 R/Resources<</ProcSet[/PDF]>>");
	if (largebbox) {
	  shipclear(fout, "/MediaBox[0 0 %d %d]", info->paperwidth, info->paperheight);
	} else {
	  shipclear(fout, "/MediaBox[0 0 %f %f]", pagew, pageh);
	}
	shipclear(fout, "/Contents %d 0 R>>\n", nxref + 1);
	shipclear(fout, "endobj\n");

	TRY(intarray_set(&pages, npages++, nxref));

	TRY(intarray_set(&xref, nxref++, (int)outcount));
	shipclear(fout, "%d 0 obj\n", nxref);
	if (info->compress)
		shipclear(fout, "<</Filter/FlateDecode/Length %d 0 R>>\n", nxref + 1);
	else
		shipclear(fout, "<</Length %d 0 R>>\n", nxref + 1);
	shipclear(fout, "stream\n");

	streamofs = outcount;

	ship(fout, "%f %f %f %f %f %f cm\n", dxx, dxy, dyx, dyy, origx, origy);
        return 0;

       try_error:
        return 1;
}

/* Return 0 on success or 1 on error with errno set. */
static int pdf_pageterm(std::vector<unsigned char> &fout) {
    
	size_t streamlen;

	shipclear(fout, "");

	streamlen = outcount - streamofs;
	shipclear(fout, "endstream\nendobj\n");
	
	TRY(intarray_set(&xref, nxref++, (int)outcount));
	shipclear(fout, "%d 0 obj\n%d\nendobj\n", nxref, streamlen);
        return 0;

       try_error:
        return 1;
}

PA_Picture page_pdf(std::vector<unsigned char> &buf, potrace_path_t *plist, imginfo_t *imginfo, info_t *info) {
    
    int r;
    
    color_t pdf_color = -1;
    
    pdf_callbacks(info);
    
    init_pdf(buf, info);
    
    TRY(pdf_pageinit(buf, imginfo, 0, info, &pdf_color));
    
    r = pdf_render(buf, plist, info, &pdf_color);
    if (r) {
        return 0;
    }
    
    TRY(pdf_pageterm(buf));
    
    term_pdf(buf, info);
    return PA_CreatePicture((void *)&buf[0], (PA_long32)buf.size());

// fflush(fout);
    
 try_error:
    
  return 0;
}

int page_pdfpage(std::vector<unsigned char> &buf, potrace_path_t *plist, imginfo_t *imginfo, info_t *info, color_t *pdf_color) {
    
  int r;

  pdf_callbacks(info);

  TRY(pdf_pageinit(buf, imginfo, 1, info, pdf_color));

  r = pdf_render(buf, plist, info, pdf_color);
  if (r) {
    return r;
  }

  TRY(pdf_pageterm(buf));

//  fflush(fout);
    
  return 0;

 try_error:
  return 1;
}
