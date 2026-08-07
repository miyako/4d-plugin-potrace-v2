#include "4DPlugin-bbox.h"

/* compute the interval i to be the smallest interval including all <v
 | dir> for points in the pathlist. If the pathlist is empty, return
 the singleton interval [0,0]. */
void path_limits(potrace_path_t *path, dpoint_t dir, interval_t *i) {
    potrace_path_t *p;
    
    /* empty image? */
    if (path == NULL) {
        bbox::interval(i, 0, 0);
        return;
    }
    
    /* initialize interval to a point on the first curve */
    bbox::singleton(i, bbox::iprod(path->curve.c[0][2], dir));
    
    /* iterate */
    list_forall(p, path) {
        bbox::curve_limits(&p->curve, dir, i);
    }
}
