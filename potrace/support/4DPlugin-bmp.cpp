#include "4DPlugin-bmp.h"

/* reset padding boundary */
void bmp_pad_reset(int *count) {
    
    *count = 0;
}

/* forward to the new file position. Return 1 on EOF or error, else 0 */
int bmp_forward(std::vector<unsigned char> buf, int *pos, int *count, int newPos) {
    
    if(newPos > buf.size()) return 1;
    
    unsigned int distance = (newPos - (*pos));
    
    *pos += distance;
    *count += distance;
    
    return 0;
}

/* read padding bytes to 4-byte boundary. Return 1 on EOF or error, else 0. */
int bmp_pad(std::vector<unsigned char> &buf, int *pos, int *count) {
    
    unsigned int start = *pos;
    int bmp_count = *count;
    int len = (-bmp_count) & 3;
    
    if((start + len) > buf.size()) return 1;
    
    *pos += len;
    *count = 0;
    
    return 0;
}

/* read n-byte little-endian integer. Return 1 on EOF or error, else 0. Assume n<=4. */
int bmp_readint(std::vector<unsigned char> &buf, int *pos, int *count, int len, unsigned int *p) {
    
    unsigned int start = *pos;
    unsigned int sum = 0;
    
    if((start + len) > buf.size()) return 1;
    
    for (unsigned int i = 0; i < len; ++i)
    {
        unsigned char byte = buf.at(start + i);
        sum += byte << (8 * i);
    }
    
    *pos += len;
    *count += len;
    *p = sum;
    
    return 0;
}

int bm_readbody_bmp(std::vector<unsigned char> &buf, double threshold, potrace_bitmap_t **bmp) {
    
    /* Bitmap file header */
    if (buf.size() < 2)
    return -3;
    
    if (!(buf[0] == 'B' && buf[1] == 'M'))
    return -4;
    
    bmp_info_t bmpinfo;
    int *coltable = NULL;
    unsigned int b, c;
    unsigned int i;
    potrace_bitmap_t *bm = NULL;
    int mask;
    unsigned int x, y;
    int col[2];
    unsigned int bitbuf;
    unsigned int n;
    unsigned int redshift, greenshift, blueshift;
    int col1[2];
    int bmp_count = 0; /* counter for byte padding */
    int bmp_pos = 2;  /* set file position */
    
    /* Bitmap file header */
    TRY(bmp_readint(buf, &bmp_pos, &bmp_count, 4, &bmpinfo.FileSize));
    TRY(bmp_readint(buf, &bmp_pos, &bmp_count, 4, &bmpinfo.reserved));
    TRY(bmp_readint(buf, &bmp_pos, &bmp_count, 4, &bmpinfo.DataOffset));
    
    /* DIB header (bitmap information header) */
    TRY(bmp_readint(buf, &bmp_pos, &bmp_count, 4, &bmpinfo.InfoSize));
    if (   bmpinfo.InfoSize == BITMAPINFOHEADER
        || bmpinfo.InfoSize == OS22XBITMAPHEADER
        || bmpinfo.InfoSize == BITMAPV4HEADER
        || bmpinfo.InfoSize == BITMAPV5HEADER)
    {
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
        
        if (bmpinfo.InfoSize == BITMAPV4HEADER || bmpinfo.InfoSize == BITMAPV5HEADER)
        {
            TRY(bmp_readint(buf, &bmp_pos, &bmp_count, 4, &bmpinfo.RedMask));
            TRY(bmp_readint(buf, &bmp_pos, &bmp_count, 4, &bmpinfo.GreenMask));
            TRY(bmp_readint(buf, &bmp_pos, &bmp_count, 4, &bmpinfo.BlueMask));
            TRY(bmp_readint(buf, &bmp_pos, &bmp_count, 4, &bmpinfo.AlphaMask));
        }
        if (bmpinfo.w > 0x7fffffff)
        {
            goto format_error;
        }
        if (bmpinfo.h > 0x7fffffff)
        {
            bmpinfo.h = (-bmpinfo.h) & 0xffffffff;
            bmpinfo.topdown = 1;
        } else {
            bmpinfo.topdown = 0;
        }
        if (bmpinfo.h > 0x7fffffff)
        {
            goto format_error;
        }
    } else if (bmpinfo.InfoSize == BITMAPCOREHEADER)
    {
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
    
    if (bmpinfo.comp == 3 && bmpinfo.InfoSize < BITMAPV4HEADER)
    {
        /* bitfield feature is only understood with V4 and V5 format */
        goto format_error;
    }
    
    if (bmpinfo.comp > 3 || bmpinfo.bits > 32)
    {
        goto format_error;
    }
    
    /* forward to color table (e.g., if bmpinfo.InfoSize == 64) */
    TRY(bmp_forward(buf, &bmp_pos, &bmp_count, 14+bmpinfo.InfoSize));
    
    if (bmpinfo.Planes != 1)
    {
        goto format_error;  /* can't handle planes */
    }
    
    if (bmpinfo.ncolors == 0)
    {
        bmpinfo.ncolors = 1 << bmpinfo.bits;
    }
    
    /* color table, present only if bmpinfo.bits <= 8. */
    if (bmpinfo.bits <= 8)
    {
        coltable = (int *) calloc(bmpinfo.ncolors, sizeof(int));
        if (!coltable)
        {
            goto std_error;
        }
        /* NOTE: since we are reading a bitmap, we can immediately convert
         the color table entries to bits. */
        for (i=0; i<bmpinfo.ncolors; i++)
        {
            TRY(bmp_readint(buf, &bmp_pos, &bmp_count, bmpinfo.ctbits/8, &c));
            c = ((c>>16) & 0xff) + ((c>>8) & 0xff) + (c & 0xff);
            
            if(threshold > 0)
            {
                coltable[i] = (c > 3 * threshold * 255 ? 0 : 1);
                if (i<2) {
                    col1[i] = c;
                }
            }else
            {
                coltable[i] = c/3;
            }
        }
    }
    
    /* forward to data */
    if (bmpinfo.InfoSize != 12)
    { /* not old OS/2 format */
        TRY(bmp_forward(buf, &bmp_pos, &bmp_count, bmpinfo.DataOffset));
    }
    
    /* allocate bitmap */
    bm = bm_new(bmpinfo.w, bmpinfo.h);
    
    if (!bm)
    {
        goto std_error;
    }
    
    /* zero it out */
    bm_clear(bm, 0);
    
    switch (bmpinfo.bits + 0x100*bmpinfo.comp)
    {
        
        default:
        goto format_error;
        break;
        
        case 0x001:  /* monochrome palette */
        if (col1[0] < col1[1]) { /* make the darker color black */
            mask = 0xff;
        } else {
            mask = 0;
        }
        
        /* raster data */
        for (y=0; y<bmpinfo.h; y++) {
            //            PA_YieldAbsolute();
            bmp_pad_reset(&bmp_count);
            for (i=0; 8*i<bmpinfo.w; i++) {
                TRY_EOF(bmp_readint(buf, &bmp_pos, &bmp_count, 1, &b));
                b ^= mask;
                *bm_index(bm, i*8, ycorr(y)) |= ((potrace_word)b) << (8*(BM_WORDSIZE-1-(i % BM_WORDSIZE)));
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
            //            PA_YieldAbsolute();
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
                BM_UPUT(bm, x, ycorr(y), COLTABLE(b));
            }
            TRY(bmp_pad(buf, &bmp_pos, &bmp_count));
        }
        break;
        
        case 0x010:  /* 16-bit encoding */
        /* can't do this format because it is not well-documented and I
         don't have any samples */
        goto format_error;
        break;
        
        case 0x018:  /* 24-bit encoding */
        case 0x020:  /* 32-bit encoding */
        for (y=0; y<bmpinfo.h; y++) {
            //            PA_YieldAbsolute();
            bmp_pad_reset(&bmp_count);
            for (x=0; x<bmpinfo.w; x++) {
                TRY_EOF(bmp_readint(buf, &bmp_pos, &bmp_count, bmpinfo.bits/8, &c));
                c = ((c>>16) & 0xff) + ((c>>8) & 0xff) + (c & 0xff);
                BM_UPUT(bm, x, ycorr(y), c > 3 * threshold * 255 ? 0 : 1);
            }
            TRY(bmp_pad(buf, &bmp_pos, &bmp_count));
        }
        break;
        
        case 0x320:  /* 32-bit encoding with bitfields */
        redshift = lobit(bmpinfo.RedMask);
        greenshift = lobit(bmpinfo.GreenMask);
        blueshift = lobit(bmpinfo.BlueMask);
        
        for (y=0; y<bmpinfo.h; y++) {
            //            PA_YieldAbsolute();
            bmp_pad_reset(&bmp_count);
            for (x=0; x<bmpinfo.w; x++) {
                TRY_EOF(bmp_readint(buf, &bmp_pos, &bmp_count, bmpinfo.bits/8, &c));
                c = ((c & bmpinfo.RedMask) >> redshift) + ((c & bmpinfo.GreenMask) >> greenshift) + ((c & bmpinfo.BlueMask) >> blueshift);
                BM_UPUT(bm, x, ycorr(y), c > 3 * threshold * 255 ? 0 : 1);
            }
            TRY(bmp_pad(buf, &bmp_pos, &bmp_count));
        }
        break;
        
        case 0x204:  /* 4-bit runlength compressed encoding (RLE4) */
        x = 0;
        y = 0;
        while (1) {
            //            PA_YieldAbsolute();
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
                    if (y>=bmpinfo.h) {
                        break;
                    }
                    BM_UPUT(bm, x, ycorr(y), col[i&1]);
                    x++;
                }
            } else if (c == 0) {
                /* end of line */
                y++;
                x = 0;
            } else if (c == 1) {
                /* end of bitmap */
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
                    if (y>=bmpinfo.h) {
                        break;
                    }
                    BM_PUT(bm, x, ycorr(y), COLTABLE((b>>(4-4*(i&1))) & 0xf));
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
            //            PA_YieldAbsolute();
            TRY_EOF(bmp_readint(buf, &bmp_pos, &bmp_count, 1, &b)); /* opcode */
            TRY_EOF(bmp_readint(buf, &bmp_pos, &bmp_count, 1, &c)); /* argument */
            if (b>0) {
                /* repeat count */
                for (i=0; i<b; i++) {
                    if (x>=bmpinfo.w) {
                        x=0;
                        y++;
                    }
                    if (y>=bmpinfo.h) {
                        break;
                    }
                    BM_UPUT(bm, x, ycorr(y), COLTABLE(c));
                    x++;
                }
            } else if (c == 0) {
                /* end of line */
                y++;
                x = 0;
            } else if (c == 1) {
                /* end of bitmap */
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
                    if (y>=bmpinfo.h) {
                        break;
                    }
                    BM_PUT(bm, x, ycorr(y), COLTABLE(b));
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
    *bmp = bm;
    return 0;
    
eof:
    if(coltable) free(coltable);
    *bmp = bm;
    return 1;
    
format_error:
try_error:
    if(coltable) free(coltable);
    if(bm) bm_free(bm);
    return -2;
    
std_error:
    if(coltable) free(coltable);
    if(bm) bm_free(bm);
    return -1;
}
