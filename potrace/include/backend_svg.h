/* Copyright (C) 2001-2017 Peter Selinger.
   This file is part of Potrace. It is free software and it is covered
   by the GNU General Public License. See the file COPYING for details. */


#ifndef BACKEND_SVG_H
#define BACKEND_SVG_H

#include "potracelib.h"

#include "4DPlugin-Potrace.h"

PA_Picture page_svg (potrace_path_t *plist, imginfo_t *imginfo, info_t *info);
PA_Picture page_gimp(potrace_path_t *plist, imginfo_t *imginfo, info_t *info);

#endif /* BACKEND_SVG_H */
