#ifndef POTRACE_BMP_H
#define POTRACE_BMP_H

#include "4DPlugin-Potrace.h"

int bm_readbody_bmp(std::vector<unsigned char> &buf, double threshold, potrace_bitmap_t **bmp);

/* reset padding boundary */
void bmp_pad_reset(int *count);
int bmp_forward(std::vector<unsigned char> buf, int *pos, int *count, int newPos);
int bmp_pad(std::vector<unsigned char> &buf, int *pos, int *count);
int bmp_readint(std::vector<unsigned char> &buf, int *pos, int *count, int len, unsigned int *p);
int bm_readbody_bmp(std::vector<unsigned char> &buf, double threshold, potrace_bitmap_t **bmp);

#endif /* POTRACE_BMP_H */
