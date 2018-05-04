/*
 * GEGL is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * GEGL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GEGL; if not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright 2014,2016,2018 Øyind Kolås <pippin@gimp.org>
 */

// special case black coat in some cmyk's and use configurable undercolor
// removal instead?

#ifndef LUZSIM_H_
#define LUZSIM_H_

#include <stdint.h>

typedef struct _Luz Luz;
#define LUZ_MAX_COATS   16
#define LUZ_SPECTRUM_START   390  /*                 380nm */
#define LUZ_SPECTRUM_GAP     10
#define LUZ_SPECTRUM_BANDS   31   /* 380 + 10 * 31 = 790nm */
// START + GAP * BANDS should be around 700 to cover visual range

Luz    *luz_new                (const char  *config);
void    luz_destroy            (Luz         *luz);


void    luz_coats_to_xyz       (Luz         *luz,
                                const float *coat_levels,
                                float       *xyz);
void    luz_coats_to_rgb       (Luz         *luz,
                                const float *coat_levels,
                                float       *rgb);
void    luz_rgb_to_coats       (Luz         *luz,
                                const float *rgb,
                                float       *coat_levels);
void    luz_xyz_to_coats       (Luz         *luz,
                                const float *xyz,
                                float       *coat_levels);
float   luz_get_coverage_limit (Luz         *luz);
void    luz_set_coverage_limit (Luz         *luz, float limit);
void    luz_set_coat_count     (Luz         *luz, int count);
int     luz_get_coat_count     (Luz         *luz);

typedef struct _Spectrum Spectrum;

Spectrum luz_parse_spectrum (Luz *luz, char *spectrum);
const Spectrum *luz_get_spectrum (Luz *luz, const char *name);
void            luz_set_spectrum (Luz *luz, const char *name, Spectrum *spectrum);

Spectrum luz_coats_to_spectrum  (Luz         *luz,
                                 const float *coat_levels);

Spectrum luz_rgb_to_spectrum (Luz *luz, float r, float g, float b);

void luz_spectrum_to_rgb (Luz            *luz,
                          const Spectrum *spectrum,
                          float          *rgb);

void luz_spectrum_to_xyz (Luz            *luz,
                          const Spectrum *spectrum,
                          float          *x,
                          float          *y,
                          float          *z);

struct _Spectrum {
  float bands[LUZ_SPECTRUM_BANDS];
};


#endif
