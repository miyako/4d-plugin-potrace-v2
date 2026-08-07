#include "4DPlugin-gm.h"

#define TRY_STD(x) if (x) goto std_error

static size_t outcount;  /* output file position */

size_t xship(std::vector<unsigned char> &fout, int filter, char *s, size_t len) {
    
    std::string bytes(s, len);
    
    std::copy(bytes.begin(), bytes.end(), std::back_inserter(fout));
    
    return len;
}

static int shipc(std::vector<unsigned char> &fout, int v) {

    static char buf[1];
    buf[0] = v;

    outcount += xship(fout, 1, buf, 1);
    return 0;
}

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

/* write a pgm stream, either P2 or (if raw != 0) P5 format. Include
 one-line comment if non-NULL. Mode determines how out-of-range
 color values are converted. Gamma is the desired gamma correction,
 if any (set to 2.2 if the image is to look optimal on a CRT monitor,
 2.8 for LCD). Set to 1.0 for no gamma correction */

int gm_writepgm(std::vector<unsigned char> &buf, greymap_t *gm, const char *comment, int raw, int mode, double gamma) {
    
    int x, y, v;
    int gammatable[256];
    
    /* prepare gamma correction lookup table */
    if (gamma != 1.0) {
        gammatable[0] = 0;
        for (v=1; v<256; v++) {
            gammatable[v] = (int)(255 * exp(log(v/255.0)/gamma) + 0.5);
        }
    } else {
        for (v=0; v<256; v++) {
            gammatable[v] = v;
        }
    }
    
    ship(buf, raw ? "P5\n" : "P2\n");
//    fprintf(f, raw ? "P5\n" : "P2\n");
    if (comment && *comment) {
        ship(buf, "# %s\n", comment);
//        fprintf(f, "# %s\n", comment);
    }
    ship(buf, "%d %d 255\n", gm->w, gm->h);
//    fprintf(f, "%d %d 255\n", gm->w, gm->h);
    for (y=gm->h-1; y>=0; y--) {
        for (x=0; x<gm->w; x++) {
            v = GM_UGET(gm, x, y);
            if (mode == GM_MODE_NONZERO) {
                if (v > 255) {
                    v = 510 - v;
                }
                if (v < 0) {
                    v = 0;
                }
            } else if (mode == GM_MODE_ODD) {
                v = mod(v, 510);
                if (v > 255) {
                    v = 510 - v;
                }
            } else if (mode == GM_MODE_POSITIVE) {
                if (v < 0) {
                    v = 0;
                } else if (v > 255) {
                    v = 255;
                }
            } else if (mode == GM_MODE_NEGATIVE) {
                v = 510 - v;
                if (v < 0) {
                    v = 0;
                } else if (v > 255) {
                    v = 255;
                }
            }
            v = gammatable[v];
            
            if (raw) {
                shipc(buf, v);
//                fputc(v, f);
            } else {
                ship(buf, x == gm->w-1 ? "%d\n" : "%d ", v);
//                fprintf(f, x == gm->w-1 ? "%d\n" : "%d ", v);
            }
        }
    }
    return 0;
}

/* output pbm format */

void bm_writepbm(std::vector<unsigned char> &buf, potrace_bitmap_t *bm) {
    
    int w, h, bpr, y, i, c;
    
    w = bm->w;
    h = bm->h;
    
    bpr = (w+7)/8;
    
    ship(buf, "P4\n%d %d\n", w, h);
//    fprintf(f, "P4\n%d %d\n", w, h);
    for (y=h-1; y>=0; y--) {
        for (i=0; i<bpr; i++) {
            c = (*bm_index(bm, i*8, y) >> (8*(BM_WORDSIZE-1-(i % BM_WORDSIZE)))) & 0xff;
            shipc(buf, c);
//            fputc(c, f);
        }
    }
     
    return;
}

/* we need this typedef so that the SAFE_CALLOC macro will work */
typedef double double4[4];

#define SAFE_CALLOC(var, n, typ) \
if ((var = (typ *)calloc(n, sizeof(typ))) == NULL) goto calloc_error

void *interpolate_cubic(greymap_t *gm, int s, int bilevel, double c) {
    
    int w, h;
    double4 *poly = NULL; /* poly[s][4]: fixed interpolation polynomials */
    double p[4];              /* four current points */
    double4 *window = NULL; /* window[s][4]: current state */
    double t, v;
    int k, l, i, j, x, y;
    double c1 = 0;
    greymap_t *gm_out = NULL;
    potrace_bitmap_t *bm_out = NULL;
    
    SAFE_CALLOC(poly, s, double4);
    SAFE_CALLOC(window, s, double4);
    
    w = gm->w;
    h = gm->h;
    
    /* allocate output bitmap/greymap */
    if (bilevel) {
        bm_out = bm_new(w*s, h*s);
        if (!bm_out) {
            goto calloc_error;
        }
        c1 = c * 255;
    } else {
        gm_out = gm_new(w*s, h*s);
        if (!gm_out) {
            goto calloc_error;
        }
    }
    
    /* pre-calculate interpolation polynomials */
    for (k=0; k<s; k++) {
        t = k/(double)s;
        poly[k][0] = 0.5 * t * (t-1) * (1-t);
        poly[k][1] = -(t+1) * (t-1) * (1-t) + 0.5 * (t-1) * (t-2) * t;
        poly[k][2] = 0.5 * (t+1) * t * (1-t) - t * (t-2) * t;
        poly[k][3] = 0.5 * t * (t-1) * t;
    }
    
    /* interpolate */
    for (y=0; y<h; y++) {
        x=0;
        for (i=0; i<4; i++) {
            for (j=0; j<4; j++) {
                p[j] = GM_BGET(gm, x+i-1, y+j-1);
            }
            for (k=0; k<s; k++) {
                window[k][i] = 0.0;
                for (j=0; j<4; j++) {
                    window[k][i] += poly[k][j] * p[j];
                }
            }
        }
        while (1) {
            for (l=0; l<s; l++) {
                for (k=0; k<s; k++) {
                    v = 0.0;
                    for (i=0; i<4; i++) {
                        v += window[k][i] * poly[l][i];
                    }
                    if (bilevel) {
                        BM_PUT(bm_out, x*s+l, y*s+k, v < c1);
                    } else {
                        GM_PUT(gm_out, x*s+l, y*s+k, v);
                    }
                }
            }
            x++;
            if (x>=w) {
                break;
            }
            for (i=0; i<3; i++) {
                for (k=0; k<s; k++) {
                    window[k][i] = window[k][i+1];
                }
            }
            i=3;
            for (j=0; j<4; j++) {
                p[j] = GM_BGET(gm, x+i-1, y+j-1);
            }
            for (k=0; k<s; k++) {
                window[k][i] = 0.0;
                for (j=0; j<4; j++) {
                    window[k][i] += poly[k][j] * p[j];
                }
            }
        }
    }
    
    free(poly);
    free(window);
    
    if (bilevel) {
        return (void *)bm_out;
    } else {
        return (void *)gm_out;
    }
    
calloc_error:
    free(poly);
    free(window);
    return NULL;
}

/* scale greymap by factor s, using linear interpolation. If
 bilevel=0, return a pointer to a greymap_t. If bilevel=1, return a
 pointer to a potrace_bitmap_t and use cutoff threshold c (0=black,
 1=white).  On error, return NULL with errno set. */

void *interpolate_linear(greymap_t *gm, int s, int bilevel, double c) {
    
    int p00, p01, p10, p11;
    int i, j, x, y;
    double xx, yy, av;
    double c1 = 0;
    int w, h;
    double p0, p1;
    greymap_t *gm_out = NULL;
    potrace_bitmap_t *bm_out = NULL;
    
    w = gm->w;
    h = gm->h;
    
    /* allocate output bitmap/greymap */
    if (bilevel) {
        bm_out = bm_new(w*s, h*s);
        if (!bm_out) {
            return NULL;
        }
        c1 = c * 255;
    } else {
        gm_out = gm_new(w*s, h*s);
        if (!gm_out) {
            return NULL;
        }
    }
    
    /* interpolate */
    for (i=0; i<w; i++) {
        for (j=0; j<h; j++) {
            p00 = GM_BGET(gm, i, j);
            p01 = GM_BGET(gm, i, j+1);
            p10 = GM_BGET(gm, i+1, j);
            p11 = GM_BGET(gm, i+1, j+1);
            
            if (bilevel) {
                /* treat two special cases which are very common */
                if (p00 < c1 && p01 < c1 && p10 < c1 && p11 < c1) {
                    for (x=0; x<s; x++) {
                        for (y=0; y<s; y++) {
                            BM_UPUT(bm_out, i*s+x, j*s+y, 1);
                        }
                    }
                    continue;
                }
                if (p00 >= c1 && p01 >= c1 && p10 >= c1 && p11 >= c1) {
                    continue;
                }
            }
            
            /* the general case */
            for (x=0; x<s; x++) {
                xx = x/(double)s;
                p0 = p00*(1-xx) + p10*xx;
                p1 = p01*(1-xx) + p11*xx;
                for (y=0; y<s; y++) {
                    yy = y/(double)s;
                    av = p0*(1-yy) + p1*yy;
                    if (bilevel) {
                        BM_UPUT(bm_out, i*s+x, j*s+y, av < c1);
                    } else {
                        GM_UPUT(gm_out, i*s+x, j*s+y, av);
                    }
                }
            }
        }
    }
    if (bilevel) {
        return (void *)bm_out;
    } else {
        return (void *)gm_out;
    }
}

/* Convert greymap to bitmap by using cutoff threshold c (0=black,
 1=white). On error, return NULL with errno set. */
potrace_bitmap_t *threshold(greymap_t *gm, double c) {
    
    int w, h;
    potrace_bitmap_t *bm_out = NULL;
    double c1;
    int x, y;
    double p;
    
    w = gm->w;
    h = gm->h;
    
    /* allocate output bitmap */
    bm_out = bm_new(w, h);
    if (!bm_out) {
        return NULL;
    }
    
    /* thresholding */
    c1 = c * 255;
    
    for (y=0; y<h; y++) {
        for (x=0; x<w; x++) {
            p = GM_UGET(gm, x, y);
            BM_UPUT(bm_out, x, y, p < c1);
        }
    }
    return bm_out;
}

/* apply lowpass filter (an approximate Gaussian blur) to greymap.
 Lambda is the standard deviation of the kernel of the filter (i.e.,
 the approximate filter radius). */
void lowpass(greymap_t *gm, double lambda) {
    
    double f, g;
    double c, d;
    double B;
    int x, y;
    
    if (gm->h == 0 || gm->w == 0) {
        return;
    }
    
    /* calculate filter coefficients from given lambda */
    B = 1+2/(lambda*lambda);
    c = B-sqrt(B*B-1);
    d = 1-c;
    
    for (y=0; y<gm->h; y++) {
        /* apply low-pass filter to row y */
        /* left-to-right */
        f = g = 0;
        for (x=0; x<gm->w; x++) {
            f = f*c + GM_UGET(gm, x, y)*d;
            g = g*c + f*d;
            GM_UPUT(gm, x, y, g);
        }
        
        /* right-to-left */
        for (x=gm->w-1; x>=0; x--) {
            f = f*c + GM_UGET(gm, x, y)*d;
            g = g*c + f*d;
            GM_UPUT(gm, x, y, g);
        }
        
        /* left-to-right mop-up */
        for (x=0; x<gm->w; x++) {
            f = f*c;
            g = g*c + f*d;
            if (f+g < 1/255.0) {
                break;
            }
            GM_UPUT(gm, x, y, GM_UGET(gm, x, y)+g);
        }
    }
    
    for (x=0; x<gm->w; x++) {
        /* apply low-pass filter to column x */
        /* bottom-to-top */
        f = g = 0;
        for (y=0; y<gm->h; y++) {
            f = f*c + GM_UGET(gm, x, y)*d;
            g = g*c + f*d;
            GM_UPUT(gm, x, y, g);
        }
        
        /* top-to-bottom */
        for (y=gm->h-1; y>=0; y--) {
            f = f*c + GM_UGET(gm, x, y)*d;
            g = g*c + f*d;
            GM_UPUT(gm, x, y, g);
        }
        
        /* bottom-to-top mop-up */
        for (y=0; y<gm->h; y++) {
            f = f*c;
            g = g*c + f*d;
            if (f+g < 1/255.0) {
                break;
            }
            GM_UPUT(gm, x, y, GM_UGET(gm, x, y)+g);
        }
    }
}

/* apply highpass filter to greymap. Return 0 on success, 1 on error
 with errno set. */
int highpass(greymap_t *gm, double lambda) {
    
    greymap_t *gm1;
    double f;
    int x, y;
    
    if (gm->h == 0 || gm->w == 0) {
        return 0;
    }
    
    /* create a copy */
    gm1 = gm_dup(gm);
    if (!gm1) {
        return 1;
    }
    
    /* apply lowpass filter to the copy */
    lowpass(gm1, lambda);
    
    /* subtract copy from original */
    for (y=0; y<gm->h; y++) {
        for (x=0; x<gm->w; x++) {
            f = GM_UGET(gm, x, y);
            f -= GM_UGET(gm1, x, y);
            f += 128;    /* normalize! */
            GM_UPUT(gm, x, y, f);
        }
    }
    gm_free(gm1);
    return 0;
}

/* duplicate the given greymap. Return NULL on error with errno set. */
greymap_t *gm_dup(greymap_t *gm) {
    
    greymap_t *gm1 = gm_new(gm->w, gm->h);
    int y;
    
    if (!gm1) {
        return NULL;
    }
    for (y=0; y<gm->h; y++) {
        memcpy(gm_scanline(gm1, y), gm_scanline(gm, y), (size_t)gm1->dy * sizeof(gm_sample_t));
    }
    return gm1;
}

/* return new greymap initialized to 0. NULL with errno on error.
 Assumes w, h >= 0. */
greymap_t *gm_new(int w, int h) {
    
    greymap_t *gm;
    int dy = w;
    ptrdiff_t size;
    
    size = getsize(dy, h);
    if (size < 0) {
        errno = ENOMEM;
        return NULL;
    }
    if (size == 0) {
        size = 1; /* make surecmalloc() doesn't return NULL */
    }
    
    gm = (greymap_t *) malloc(sizeof(greymap_t));
    if (!gm) {
        return NULL;
    }
    gm->w = w;
    gm->h = h;
    gm->dy = dy;
    gm->base = (gm_sample_t *) calloc(1, size);
    if (!gm->base) {
        free(gm);
        return NULL;
    }
    gm->map = gm->base;
    return gm;
}

/* free the given greymap */
void gm_free(greymap_t *gm) {
    
    if (gm) {
        free(gm->base);
    }
    free(gm);
}

/* turn the given greymap upside down. This does not move the pixel
 data or change the base address. */
static inline void gm_flip(greymap_t *gm) {
    
    int dy = gm->dy;
    
    if (gm->h == 0 || gm->h == 1) {
        return;
    }
    
    gm->map = gm_scanline(gm, gm->h - 1);
    gm->dy = -dy;
}

/* resize the greymap to the given new height. The pixel data remains
 bottom-aligned (truncated at the top) when dy >= 0 and top-aligned
 (truncated at the bottom) when dy < 0. Return 0 on success, or 1 on
 error with errno set. If the new height is <= the old one, no error
 should occur. If the new height is larger, the additional pixel
 data is *not* initialized. */
static inline int gm_resize(greymap_t *gm, int h) {
    
    int dy = gm->dy;
    ptrdiff_t newsize;
    gm_sample_t *newbase;
    
    if (dy < 0) {
        gm_flip(gm);
    }
    
    newsize = getsize(dy, h);
    if (newsize < 0) {
        errno = ENOMEM;
        goto error;
    }
    if (newsize == 0) {
        newsize = 1; /* make sure realloc() doesn't return NULL */
    }
    
    newbase = (gm_sample_t *)realloc(gm->base, newsize);
    if (newbase == NULL) {
        goto error;
    }
    gm->base = newbase;
    gm->map = newbase;
    gm->h = h;
    
    if (dy < 0) {
        gm_flip(gm);
    }
    return 0;
    
error:
    if (dy < 0) {
        gm_flip(gm);
    }
    return 1;
}

int gm_readbody_bmp(std::vector<unsigned char> &buf, greymap_t **gmp) {

    bmp_info_t bmpinfo;
    int *coltable;
    unsigned int b, c;
    unsigned int i, j;
    greymap_t *gm;
    unsigned int x, y;
    int col[2];
    unsigned int bitbuf;
    unsigned int n;
    unsigned int redshift, greenshift, blueshift;
    int realheight;  /* in case of incomplete file, keeps track of how
                      many scan lines actually contain data */
    
    int bmp_count = 0; /* counter for byte padding */
    int bmp_pos = 2;  /* set file position */
    
//    gm_read_error = NULL;
    
    gm = NULL;
    coltable = NULL;
    
    bmp_pos = 2;  /* set file position */
    
    /* file header (minus magic number) */
    TRY(bmp_readint(buf, &bmp_pos, &bmp_count, 4, &bmpinfo.FileSize));
    TRY(bmp_readint(buf, &bmp_pos, &bmp_count, 4, &bmpinfo.reserved));
    TRY(bmp_readint(buf, &bmp_pos, &bmp_count, 4, &bmpinfo.DataOffset));
    
    /* info header */
    TRY(bmp_readint(buf, &bmp_pos, &bmp_count, 4, &bmpinfo.InfoSize));
    if (bmpinfo.InfoSize == 40 || bmpinfo.InfoSize == 64
        || bmpinfo.InfoSize == 108 || bmpinfo.InfoSize == 124) {
        /* Windows or new OS/2 format */
        bmpinfo.ctbits = 32; /* sample size in color table */
        TRY(bmp_readint(buf, &bmp_pos, &bmp_count, 4, &bmpinfo.w));
        TRY(bmp_readint(buf, &bmp_pos, &bmp_count, 4, &bmpinfo.h));
        TRY(bmp_readint(buf, &bmp_pos, &bmp_count, 2, &bmpinfo.Planes));
        TRY(bmp_readint(buf, &bmp_pos, &bmp_count, 2, &bmpinfo.bits));
        TRY(bmp_readint(buf, &bmp_pos, &bmp_count, 4, &bmpinfo.comp));
        TRY(bmp_readint(buf, &bmp_pos, &bmp_count, 4, &bmpinfo.ImageSize));
        TRY(bmp_readint(buf, &bmp_pos, &bmp_count, 4, &bmpinfo.XpixelsPerM));
        TRY(bmp_readint(buf, &bmp_pos, &bmp_count, 4, &bmpinfo.YpixelsPerM));
        TRY(bmp_readint(buf, &bmp_pos, &bmp_count, 4, &bmpinfo.ncolors));
        TRY(bmp_readint(buf, &bmp_pos, &bmp_count, 4, &bmpinfo.ColorsImportant));
        if (bmpinfo.InfoSize >= 108) { /* V4 and V5 bitmaps */
            TRY(bmp_readint(buf, &bmp_pos, &bmp_count, 4, &bmpinfo.RedMask));
            TRY(bmp_readint(buf, &bmp_pos, &bmp_count, 4, &bmpinfo.GreenMask));
            TRY(bmp_readint(buf, &bmp_pos, &bmp_count, 4, &bmpinfo.BlueMask));
            TRY(bmp_readint(buf, &bmp_pos, &bmp_count, 4, &bmpinfo.AlphaMask));
        }
        if (bmpinfo.w > 0x7fffffff) {
            goto format_error;
        }
        if (bmpinfo.h > 0x7fffffff) {
            bmpinfo.h = (-bmpinfo.h) & 0xffffffff;
            bmpinfo.topdown = 1;
        } else {
            bmpinfo.topdown = 0;
        }
        if (bmpinfo.h > 0x7fffffff) {
            goto format_error;
        }
    } else if (bmpinfo.InfoSize == 12) {
        /* old OS/2 format */
        bmpinfo.ctbits = 24; /* sample size in color table */
        TRY(bmp_readint(buf, &bmp_pos, &bmp_count, 2, &bmpinfo.w));
        TRY(bmp_readint(buf, &bmp_pos, &bmp_count, 2, &bmpinfo.h));
        TRY(bmp_readint(buf, &bmp_pos, &bmp_count, 2, &bmpinfo.Planes));
        TRY(bmp_readint(buf, &bmp_pos, &bmp_count, 2, &bmpinfo.bits));
        bmpinfo.comp = 0;
        bmpinfo.ncolors = 0;
        bmpinfo.topdown = 0;
    } else {
        goto format_error;
    }
    
    if (bmpinfo.comp == 3 && bmpinfo.InfoSize < 108) {
        /* bitfield feature is only understood with V4 and V5 format */
        goto format_error;
    }
    
    if (bmpinfo.comp > 3 || bmpinfo.bits > 32) {
        goto format_error;
    }
    
    /* forward to color table (e.g., if bmpinfo.InfoSize == 64) */
    TRY(bmp_forward(buf, &bmp_pos, &bmp_count, 14+bmpinfo.InfoSize));
    
    if (bmpinfo.Planes != 1) {
//        gm_read_error = "cannot handle bmp planes";
        goto format_error;  /* can't handle planes */
    }
    
    if (bmpinfo.ncolors == 0 && bmpinfo.bits <= 8) {
        bmpinfo.ncolors = 1 << bmpinfo.bits;
    }
    
    /* color table, present only if bmpinfo.bits <= 8. */
    if (bmpinfo.bits <= 8) {
        coltable = (int *) calloc(bmpinfo.ncolors, sizeof(int));
        if (!coltable) {
            goto std_error;
        }
        /* NOTE: since we are reading a greymap, we can immediately convert
         the color table entries to grey values. */
        for (i=0; i<bmpinfo.ncolors; i++) {
            TRY(bmp_readint(buf, &bmp_pos, &bmp_count, bmpinfo.ctbits/8, &c));
            c = ((c>>16) & 0xff) + ((c>>8) & 0xff) + (c & 0xff);
            coltable[i] = c/3;
        }
    }
    
    /* forward to data */
    if (bmpinfo.InfoSize != 12) { /* not old OS/2 format */
        TRY(bmp_forward(buf, &bmp_pos, &bmp_count, bmpinfo.DataOffset));
    }
    
    /* allocate greymap */
    gm = gm_new(bmpinfo.w, bmpinfo.h);
    if (!gm) {
        goto std_error;
    }
    
    realheight = 0;
    
    switch (bmpinfo.bits + 0x100*bmpinfo.comp) {
            
        default:
            goto format_error;
            break;
            
        case 0x001:  /* monochrome palette */
            
            /* raster data */
            for (y=0; y<bmpinfo.h; y++) {
                realheight = y+1;
                bmp_pad_reset(&bmp_count);
                for (i=0; 8*i<bmpinfo.w; i++) {
                    TRY_EOF(bmp_readint(buf, &bmp_pos, &bmp_count, 1, &b));
                    for (j=0; j<8; j++) {
                        GM_PUT(gm, i*8+j, y, b & (0x80 >> j) ? COLTABLE(1) : COLTABLE(0));
                    }
                }
                TRY(bmp_pad(buf, &bmp_pos, &bmp_count));
            }
            break;
            
        case 0x002:  /* 2-bit to 8-bit palettes */
        case 0x003:
        case 0x004:
        case 0x005:
        case 0x006:
        case 0x007:
        case 0x008:
            for (y=0; y<bmpinfo.h; y++) {
                realheight = y+1;
                bmp_pad_reset(&bmp_count);
                bitbuf = 0;  /* bit buffer: bits in buffer are high-aligned */
                n = 0;       /* number of bits currently in bitbuffer */
                for (x=0; x<bmpinfo.w; x++) {
                    if (n < bmpinfo.bits) {
                        TRY_EOF(bmp_readint(buf, &bmp_pos, &bmp_count, 1, &b));
                        bitbuf |= b << (INTBITS - 8 - n);
                        n += 8;
                    }
                    b = bitbuf >> (INTBITS - bmpinfo.bits);
                    bitbuf <<= bmpinfo.bits;
                    n -= bmpinfo.bits;
                    GM_UPUT(gm, x, y, COLTABLE(b));
                }
                TRY(bmp_pad(buf, &bmp_pos, &bmp_count));
            }
            break;
            
        case 0x010:  /* 16-bit encoding */
            /* can't do this format because it is not well-documented and I
             don't have any samples */
//            gm_read_error = "cannot handle bmp 16-bit coding";
            goto format_error;
            break;
            
        case 0x018:  /* 24-bit encoding */
        case 0x020:  /* 32-bit encoding */
            for (y=0; y<bmpinfo.h; y++) {
                realheight = y+1;
                bmp_pad_reset(&bmp_count);
                for (x=0; x<bmpinfo.w; x++) {
                    TRY_EOF(bmp_readint(buf, &bmp_pos, &bmp_count, bmpinfo.bits/8, &c));
                    c = ((c>>16) & 0xff) + ((c>>8) & 0xff) + (c & 0xff);
                    GM_UPUT(gm, x, y, c/3);
                }
                TRY(bmp_pad(buf, &bmp_pos, &bmp_count));
            }
            break;
            
        case 0x320:  /* 32-bit encoding with bitfields */
            redshift = lobit(bmpinfo.RedMask);
            greenshift = lobit(bmpinfo.GreenMask);
            blueshift = lobit(bmpinfo.BlueMask);
            
            for (y=0; y<bmpinfo.h; y++) {
                realheight = y+1;
                bmp_pad_reset(&bmp_count);
                for (x=0; x<bmpinfo.w; x++) {
                    TRY_EOF(bmp_readint(buf, &bmp_pos, &bmp_count, bmpinfo.bits/8, &c));
                    c = ((c & bmpinfo.RedMask) >> redshift) + ((c & bmpinfo.GreenMask) >> greenshift) + ((c & bmpinfo.BlueMask) >> blueshift);
                    GM_UPUT(gm, x, y, c/3);
                }
                TRY(bmp_pad(buf, &bmp_pos, &bmp_count));
            }
            break;
            
        case 0x204:  /* 4-bit runlength compressed encoding (RLE4) */
            x = 0;
            y = 0;
            while (1) {
                TRY_EOF(bmp_readint(buf, &bmp_pos, &bmp_count, 1, &b)); /* opcode */
                TRY_EOF(bmp_readint(buf, &bmp_pos, &bmp_count, 1, &c)); /* argument */
                if (b>0) {
                    /* repeat count */
                    col[0] = COLTABLE((c>>4) & 0xf);
                    col[1] = COLTABLE(c & 0xf);
                    for (i=0; i<b && x<bmpinfo.w; i++) {
                        if (x>=bmpinfo.w) {
                            x=0;
                            y++;
                        }
                        if (x>=bmpinfo.w || y>=bmpinfo.h) {
                            break;
                        }
                        realheight = y+1;
                        GM_PUT(gm, x, y, col[i&1]);
                        x++;
                    }
                } else if (c == 0) {
                    /* end of line */
                    y++;
                    x = 0;
                } else if (c == 1) {
                    /* end of greymap */
                    break;
                } else if (c == 2) {
                    /* "delta": skip pixels in x and y directions */
                    TRY_EOF(bmp_readint(buf, &bmp_pos, &bmp_count, 1, &b)); /* x offset */
                    TRY_EOF(bmp_readint(buf, &bmp_pos, &bmp_count, 1, &c)); /* y offset */
                    x += b;
                    y += c;
                } else {
                    /* verbatim segment */
                    for (i=0; i<c; i++) {
                        if ((i&1)==0) {
                            TRY_EOF(bmp_readint(buf, &bmp_pos, &bmp_count, 1, &b));
                        }
                        if (x>=bmpinfo.w) {
                            x=0;
                            y++;
                        }
                        if (x>=bmpinfo.w || y>=bmpinfo.h) {
                            break;
                        }
                        realheight = y+1;
                        GM_PUT(gm, x, y, COLTABLE((b>>(4-4*(i&1))) & 0xf));
                        x++;
                    }
                    if ((c+1) & 2) {
                        /* pad to 16-bit boundary */
                        TRY_EOF(bmp_readint(buf, &bmp_pos, &bmp_count, 1, &b));
                    }
                }
            }
            break;
            
        case 0x108:  /* 8-bit runlength compressed encoding (RLE8) */
            x = 0;
            y = 0;
            while (1) {
                TRY_EOF(bmp_readint(buf, &bmp_pos, &bmp_count, 1, &b)); /* opcode */
                TRY_EOF(bmp_readint(buf, &bmp_pos, &bmp_count, 1, &c)); /* argument */
                if (b>0) {
                    /* repeat count */
                    for (i=0; i<b; i++) {
                        if (x>=bmpinfo.w) {
                            x=0;
                            y++;
                        }
                        if (x>=bmpinfo.w || y>=bmpinfo.h) {
                            break;
                        }
                        realheight = y+1;
                        GM_PUT(gm, x, y, COLTABLE(c));
                        x++;
                    }
                } else if (c == 0) {
                    /* end of line */
                    y++;
                    x = 0;
                } else if (c == 1) {
                    /* end of greymap */
                    break;
                } else if (c == 2) {
                    /* "delta": skip pixels in x and y directions */
                    TRY_EOF(bmp_readint(buf, &bmp_pos, &bmp_count, 1, &b)); /* x offset */
                    TRY_EOF(bmp_readint(buf, &bmp_pos, &bmp_count, 1, &c)); /* y offset */
                    x += b;
                    y += c;
                } else {
                    /* verbatim segment */
                    for (i=0; i<c; i++) {
                        TRY_EOF(bmp_readint(buf, &bmp_pos, &bmp_count, 1, &b));
                        if (x>=bmpinfo.w) {
                            x=0;
                            y++;
                        }
                        if (x>=bmpinfo.w || y>=bmpinfo.h) {
                            break;
                        }
                        realheight = y+1;
                        GM_PUT(gm, x, y, COLTABLE(b));
                        x++;
                    }
                    if (c & 1) {
                        /* pad input to 16-bit boundary */
                        TRY_EOF(bmp_readint(buf, &bmp_pos, &bmp_count, 1, &b));
                    }
                }
            }
            break;
            
    } /* switch */
    
    /* skip any potential junk after the data section, but don't
     complain in case EOF is encountered */
    bmp_forward(buf, &bmp_pos, &bmp_count, bmpinfo.FileSize);
    
    free(coltable);
    if (bmpinfo.topdown) {
        gm_flip(gm);
    }
    *gmp = gm;
    return 0;
    
eof:
    TRY_STD(gm_resize(gm, realheight));
    free(coltable);
    if (bmpinfo.topdown) {
        gm_flip(gm);
    }
    *gmp = gm;
    return 1;
    
format_error:
try_error:
    free(coltable);
    gm_free(gm);
//    if (!gm_read_error) {
//        gm_read_error = "invalid bmp file";
//    }
    return -2;
    
std_error:
    free(coltable);
    gm_free(gm);
    return -1;
}

int gm_read(std::vector<unsigned char> &buf, greymap_t **gmp) {
    
    /* read magic number. We ignore whitespace and comments before the
     magic, for the benefit of concatenated files in P1-P3 format.
     Multiple P1-P3 images in a single file are not formally allowed
     by the PNM standard, but there is no harm in being lenient. */
    
    /* Bitmap file header */
    if (buf.size() < 2)
        return -3;
    
    if ((buf[0] == 'B' && buf[1] == 'M'))
    {
        return gm_readbody_bmp(buf, gmp);
    }
    
    /*
     if ((buf[0] == 'P' && buf[1] >= '1' && buf[1] <= '6'))
     {
     return gm_readbody_pnm(buf, gmp, magic[1]);
     }
     */

    return -4;
}
