/* Copyright (C) 2001-2017 Peter Selinger.
   This file is part of Potrace. It is free software and it is covered
   by the GNU General Public License. See the file COPYING for details. */


#ifndef BACKEND_PDF_H
#define BACKEND_PDF_H

#include "4DPlugin-Potrace.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#include "flate.h"
#include "lists.h"
#include "potracelib.h"
#include "auxiliary.h"

int init_pdf(std::vector<unsigned char> &buf, info_t *info);
PA_Picture page_pdf(std::vector<unsigned char> &buf, potrace_path_t *plist, imginfo_t *imginfo, info_t *info);
int term_pdf(std::vector<unsigned char> &buf, info_t *info);

int page_pdfpage(std::vector<unsigned char> &buf, potrace_path_t *plist, imginfo_t *imginfo, info_t *info);

#endif /* BACKEND_PDF_H */

