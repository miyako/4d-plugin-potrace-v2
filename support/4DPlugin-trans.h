#ifndef POTRACE_TRANS_H
#define POTRACE_TRANS_H

#include "4DPlugin-Potrace.h"

void trans_scale_to_size(trans_t *r, double w, double h);
void trans_tighten(trans_t *r, potrace_path_t *plist);
void trans_rescale(trans_t *r, double sc);
void trans_from_rect(trans_t *r, double w, double h);
void trans_rotate(trans_t *r, double alpha);

#endif /* POTRACE_TRANS_H */
