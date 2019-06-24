#ifndef POTRACE_BBOX_H
#define POTRACE_BBOX_H

#include "4DPlugin-Potrace.h"

#include "bbox.h"

namespace bbox {
    
    /* extend the interval to include the number x */
    static inline void extend(interval_t *i, double x) {
        if (x < i->min) {
            i->min = x;
        } else if (x > i->max) {
            i->max = x;
        }
    }
    /* return a point on a 1-dimensional Bezier segment */
    static inline double bezier(double t, double x0, double x1, double x2, double x3) {
        double s = 1-t;
        return s*s*s*x0 + 3*(s*s*t)*x1 + 3*(t*t*s)*x2 + t*t*t*x3;
    }
    static inline int in_interval(interval_t *i, double x) {
        return i->min <= x && x <= i->max;
    }
    /* Extend the interval i to include the minimum and maximum of a
     1-dimensional Bezier segment given by control points x0..x3. For
     efficiency, x0 in i is assumed as a precondition. */
    static void bezier_limits(double x0, double x1, double x2, double x3, interval_t *i) {
        double a, b, c, d, r;
        double t, x;
        
        /* the min and max of a cubic curve segment are attained at one of
         at most 4 critical points: the 2 endpoints and at most 2 local
         extrema. We don't check the first endpoint, because all our
         curves are cyclic so it's more efficient not to check endpoints
         twice. */
        
        /* endpoint */
        bbox::extend(i, x3);
        
        /* optimization: don't bother calculating extrema if all control
         points are already in i */
        if (in_interval(i, x1) && in_interval(i, x2)) {
            return;
        }
        
        /* solve for extrema. at^2 + bt + c = 0 */
        a = -3*x0 + 9*x1 - 9*x2 + 3*x3;
        b = 6*x0 - 12*x1 + 6*x2;
        c = -3*x0 + 3*x1;
        d = b*b - 4*a*c;
        if (d > 0) {
            r = sqrt(d);
            t = (-b-r)/(2*a);
            if (t > 0 && t < 1) {
                x = bezier(t, x0, x1, x2, x3);
                bbox::extend(i, x);
            }
            t = (-b+r)/(2*a);
            if (t > 0 && t < 1) {
                x = bezier(t, x0, x1, x2, x3);
                bbox::extend(i, x);
            }
        }
        return;
    }
    static double iprod(dpoint_t a, dpoint_t b) {
        return a.x * b.x + a.y * b.y;
    }
    /* extend the interval i to include the inner product <v | dir> for
     all points v on the segment. Assume precondition a in i. */
    static inline void segment_limits(int tag, dpoint_t a, dpoint_t c[3], dpoint_t dir, interval_t *i) {
        switch (tag) {
            case POTRACE_CORNER:
            bbox::extend(i, bbox::iprod(c[1], dir));
            bbox::extend(i, bbox::iprod(c[2], dir));
            break;
            case POTRACE_CURVETO:
            bbox::bezier_limits(bbox::iprod(a, dir), bbox::iprod(c[0], dir), bbox::iprod(c[1], dir), bbox::iprod(c[2], dir), i);
            break;
        }
    }
    static inline double double_of_dim(dim_t d, double def)
    {
        if (d.d) {
            return d.x * d.d;
        } else {
            return d.x * def;
        }
    }
    /* initialize the interval to [min, max] */
    static void interval(interval_t *i, double min, double max) {
        i->min = min;
        i->max = max;
    }
    /* initialize the interval to [x, x] */
    static inline void singleton(interval_t *i, double x) {
        bbox::interval(i, x, x);
    }
    /* extend the interval i to include <v | dir> for all points v on the curve. */
    static void curve_limits(potrace_curve_t *curve, dpoint_t dir, interval_t *i) {
        int k;
        int n = curve->n;
        
        bbox::segment_limits(curve->tag[0], curve->c[n-1][2], curve->c[0], dir, i);
        for (k=1; k<n; k++) {
            bbox::segment_limits(curve->tag[k], curve->c[k-1][2], curve->c[k], dir, i);
        }
    }
}

/* determine the dimensions of the output based on command line and
 image dimensions, and optionally, based on the actual image outline. */
static void calc_dimensions(imginfo_t *imginfo, potrace_path_t *plist, info_s *info)
{
    double dim_def;
    double maxwidth, maxheight, sc;
    int default_scaling = 0;
    
    /* we take care of a special case: if one of the image dimensions is
     0, we change it to 1. Such an image is empty anyway, so there
     will be 0 paths in it. Changing the dimensions avoids division by
     0 error in calculating scaling factors, bounding boxes and
     such. This doesn't quite do the right thing in all cases, but it
     is better than causing overflow errors or "nan" output in
     backends.  Human users don't tend to process images of size 0
     anyway; they might occur in some pipelines. */
    if (imginfo->pixwidth == 0)
    {
        imginfo->pixwidth = 1;
    }
    if (imginfo->pixheight == 0)
    {
        imginfo->pixheight = 1;
    }
    
    /* set the default dimension for width, height, margins */
    if (info->backend->pixel)
    {
        dim_def = DIM_PT;
    } else {
        dim_def = DEFAULT_DIM;
    }
    
    /* apply default dimension to width, height, margins */
    imginfo->width = info->width_d.x == UNDEF ? UNDEF : bbox::double_of_dim(info->width_d, dim_def);
    imginfo->height = info->height_d.x == UNDEF ? UNDEF : bbox::double_of_dim(info->height_d, dim_def);
    imginfo->lmar = info->lmar_d.x == UNDEF ? UNDEF : bbox::double_of_dim(info->lmar_d, dim_def);
    imginfo->rmar = info->rmar_d.x == UNDEF ? UNDEF : bbox::double_of_dim(info->rmar_d, dim_def);
    imginfo->tmar = info->tmar_d.x == UNDEF ? UNDEF : bbox::double_of_dim(info->tmar_d, dim_def);
    imginfo->bmar = info->bmar_d.x == UNDEF ? UNDEF : bbox::double_of_dim(info->bmar_d, dim_def);
    
    /* start with a standard rectangle */
    trans_from_rect(&imginfo->trans, imginfo->pixwidth, imginfo->pixheight);
    
    /* if info.tight is set, tighten the bounding box */
    if (info->tight) {
        trans_tighten(&imginfo->trans, plist);
    }
    
    /* sx/rx is just an alternate way to specify width; sy/ry is just an
     alternate way to specify height. */
    if (info->backend->pixel) {
        if (imginfo->width == UNDEF && info->sx != UNDEF) {
            imginfo->width = imginfo->trans.bb[0] * info->sx;
        }
        if (imginfo->height == UNDEF && info->sy != UNDEF) {
            imginfo->height = imginfo->trans.bb[1] * info->sy;
        }
    } else {
        if (imginfo->width == UNDEF && info->rx != UNDEF) {
            imginfo->width = imginfo->trans.bb[0] / info->rx * 72;
        }
        if (imginfo->height == UNDEF && info->ry != UNDEF) {
            imginfo->height = imginfo->trans.bb[1] / info->ry * 72;
        }
    }
    
    /* if one of width/height is specified, use stretch to determine the
     other */
    if (imginfo->width == UNDEF && imginfo->height != UNDEF) {
        imginfo->width = imginfo->height / imginfo->trans.bb[1] * imginfo->trans.bb[0] / info->stretch;
    } else if (imginfo->width != UNDEF && imginfo->height == UNDEF) {
        imginfo->height = imginfo->width / imginfo->trans.bb[0] * imginfo->trans.bb[1] * info->stretch;
    }
    
    /* if width and height are still variable, tenatively use the
     default scaling factor of 72dpi (for dimension-based backends) or
     1 (for pixel-based backends). For fixed-size backends, this will
     be adjusted later to fit the page. */
    if (imginfo->width == UNDEF && imginfo->height == UNDEF) {
        imginfo->width = imginfo->trans.bb[0];
        imginfo->height = imginfo->trans.bb[1] * info->stretch;
        default_scaling = 1;
    }
    
    /* apply scaling */
    trans_scale_to_size(&imginfo->trans, imginfo->width, imginfo->height);
    
    /* apply rotation, and tighten the bounding box again, if necessary */
    if (info->angle != 0.0) {
        trans_rotate(&imginfo->trans, info->angle);
        if (info->tight) {
            trans_tighten(&imginfo->trans, plist);
        }
    }
    
    /* for fixed-size backends, if default scaling was in effect,
     further adjust the scaling to be the "best fit" for the given
     page size and margins. */
    if (default_scaling && info->backend->fixed) {
        
        /* try to squeeze it between margins */
        maxwidth = UNDEF;
        maxheight = UNDEF;
        
        if (imginfo->lmar != UNDEF && imginfo->rmar != UNDEF) {
            maxwidth = info->paperwidth - imginfo->lmar - imginfo->rmar;
        }
        if (imginfo->bmar != UNDEF && imginfo->tmar != UNDEF) {
            maxheight = info->paperheight - imginfo->bmar - imginfo->tmar;
        }
        if (maxwidth == UNDEF && maxheight == UNDEF) {
            maxwidth = max(info->paperwidth - 144, info->paperwidth * 0.75);
            maxheight = max(info->paperheight - 144, info->paperheight * 0.75);
        }
        
        if (maxwidth == UNDEF) {
            sc = maxheight / imginfo->trans.bb[1];
        } else if (maxheight == UNDEF) {
            sc = maxwidth / imginfo->trans.bb[0];
        } else {
            sc = min(maxwidth / imginfo->trans.bb[0], maxheight / imginfo->trans.bb[1]);
        }
        
        /* re-scale coordinate system */
        imginfo->width *= sc;
        imginfo->height *= sc;
        trans_rescale(&imginfo->trans, sc);
    }
    
    /* adjust margins */
    if (info->backend->fixed) {
        if (imginfo->lmar == UNDEF && imginfo->rmar == UNDEF) {
            imginfo->lmar = (info->paperwidth-imginfo->trans.bb[0])/2;
        } else if (imginfo->lmar == UNDEF) {
            imginfo->lmar = (info->paperwidth-imginfo->trans.bb[0]-imginfo->rmar);
        } else if (imginfo->lmar != UNDEF && imginfo->rmar != UNDEF) {
            imginfo->lmar += (info->paperwidth-imginfo->trans.bb[0]-imginfo->lmar-imginfo->rmar)/2;
        }
        if (imginfo->bmar == UNDEF && imginfo->tmar == UNDEF) {
            imginfo->bmar = (info->paperheight-imginfo->trans.bb[1])/2;
        } else if (imginfo->bmar == UNDEF) {
            imginfo->bmar = (info->paperheight-imginfo->trans.bb[1]-imginfo->tmar);
        } else if (imginfo->bmar != UNDEF && imginfo->tmar != UNDEF) {
            imginfo->bmar += (info->paperheight-imginfo->trans.bb[1]-imginfo->bmar-imginfo->tmar)/2;
        }
    } else
    {
        if (imginfo->lmar == UNDEF)
        {
            imginfo->lmar = 0;
        }
        if (imginfo->rmar == UNDEF)
        {
            imginfo->rmar = 0;
        }
        if (imginfo->bmar == UNDEF)
        {
            imginfo->bmar = 0;
        }
        if (imginfo->tmar == UNDEF)
        {
            imginfo->tmar = 0;
        }
    }
}

#endif /* POTRACE_BBOX_H */
