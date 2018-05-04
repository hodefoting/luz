
/* Coat Color Simulator
 *
 * luz is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * luz is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with luz; if not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright 2014, 2016, 2018 Øyvind Kolås <pippin@gimp.org>
 */

#include <babl/babl.h>
#include "luz.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>

/* this defines the dimensions of the spectrums used for computations,
   when spectrums are defined in the text configuration environment, they
   are converted/resampled (currently nearest neighbour) to this internal
   resolution
 */
#define INCREMENT        0.1

#define SPECTRUM_DB_SIZE 384  /* number of named spectrums to store */
#define LUT_DIM          16

#include "luz-config.inc"


#ifndef CLAMP
#define CLAMP(a,b,c) ((a)<(b)?(b):(a)>(c)?(c):(c))
#endif

#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif

typedef struct _Coat     Coat;

enum {
  LUZ_COLOR_SUBSTRATE  = -1,
  LUZ_COLOR_ILLUMINANT = 0,
  LUZ_COLOR_INK1       = 1,
  LUZ_COLOR_INK2       = 2,
  LUZ_COLOR_INK3       = 3,
  LUZ_COLOR_INK4       = 4,
  LUZ_COLOR_INK5       = 5,
  LUZ_COLOR_INK6       = 6,
  LUZ_COLOR_INK7       = 7,
  LUZ_COLOR_INK8       = 8
};


struct _Coat {
  Spectrum on_white;   /* spectral energy of this coat; as reflected of a fully reflective background */
  Spectrum on_black;   /* spectral energy of this coat; as reflected of a fully reflective background */
  Spectrum opaqueness; /* (on_black/on_white) */

  float    scale;      /* scale factor; increasing the amount of spectral contribution */
  float    trc_gamma;
  int      levels;     /* 0/1 - means continous, 2 is binary.. and 1024 - 10bit is where
                          we consider even high iterations a too far wish */
};

typedef struct _InkMix InkMix;

struct _InkMix
{
  int32_t defined;
  float   level[LUZ_MAX_COATS];
};

typedef struct _SpectrumDb SpectrumDb;

struct _SpectrumDb
{
  Spectrum spectrum[SPECTRUM_DB_SIZE];
  char     name[SPECTRUM_DB_SIZE][32];
  int      count;
};

struct _Luz
{
  SpectrumDb db;

  Spectrum illuminant;
  float    rev_y_scale; /* computed when illuminant are set */
  Spectrum substrate;

  Coat     coat_def[LUZ_MAX_COATS];
  int32_t  coats;
  float    coverage_limit;
  InkMix   lut[LUT_DIM*LUT_DIM*LUT_DIM];
  int32_t  debug_width;
  char    *src; /* cached version of the source resulting in a configuration */


  Spectrum STANDARD_OBSERVER_X;
  Spectrum STANDARD_OBSERVER_Y;
  Spectrum STANDARD_OBSERVER_Z;

  int   STOCHASTIC_ITERATIONS;
  float STOCHASTIC_DIFFUSION0;
  float STOCHASTIC_DIFFUSION1;

};

/* need a spectral resampler,..
 *
 */

const Spectrum *luz_get_spectrum (Luz *luz, const char *name);

static inline int lut_indice (float  val, float *delta)
{
  /* LUT_DIM-1 to have both 0.0 and 1.0 values to interpolate from */
  int v = floor (val * (LUT_DIM - 1));
  if (v < 0)
    v = 0;
  if (v >= (LUT_DIM-1))
    v = LUT_DIM - 2;
  //if (delta)
     *delta = ((val * (LUT_DIM - 1)) - v);
  return v;
}

static inline int
lut_index (int ri,
           int gi,
           int bi)
{
  return ri * LUT_DIM * LUT_DIM + gi * LUT_DIM + bi;
}


static inline void
spectrum_scale (Spectrum       *s,
                const Spectrum *a,
                const Spectrum *b)
{
  int i;
  for (i = 0; i < LUZ_SPECTRUM_BANDS; i++)
    s->bands[i] = a->bands[i] * b->bands[i];
}


static inline void
spectrum_add_factor (Spectrum       *s,
                     const Spectrum *a,
                     const Spectrum *b,
                     float          factor)
{
  int i;
  for (i = 0; i < LUZ_SPECTRUM_BANDS; i++)
    s->bands[i] = a->bands[i] + b->bands[i] * factor;
}

static inline void
spectrum_set (Spectrum       *s,
              const Spectrum *a)
{
  int i;
  for (i = 0; i < LUZ_SPECTRUM_BANDS; i++)
    s->bands[i] = a->bands[i];
}


static inline void
add_coat (Spectrum       *s,
    //      const Spectrum *illuminant,
          const Spectrum *on_white,
          const Spectrum *on_black,
          const Spectrum *opaqueness,
          float           coverage,
          float           trc_gamma)
{
   int i;
   if (trc_gamma != 1.0)
    coverage = powf (coverage, trc_gamma);
   for (i = 0; i < LUZ_SPECTRUM_BANDS; i++)
   {
     float band_val = s->bands[i];
     float band_bc;
     float band_wc;
     float opacity = opaqueness->bands[i];
#define LERP(a,b,dt)    ((a) + ((b) - (a)) * (dt))

     /* what the result would be if we were fully acting subtractively
        like ink */
     band_bc = LERP(band_val, on_white->bands[i] * band_val, coverage);

     /* and what the result of a fully paint like opaque layer of given
        coverage would be */
     band_wc = LERP(band_val, on_white->bands[i], coverage);

     /* then we do a linear blend between behaving more like coat or more like
 * paint
        for each band, dependent on the computed opacity/paintness/coverage for
this band
      */
     s->bands[i] = LERP(band_bc, band_wc, opacity);
   }
}

static inline float spectrum_integrate (const Spectrum *s,
                                        const Spectrum *is)
{
  float result = 0.0;
  int i;
  for (i = 0; i < LUZ_SPECTRUM_BANDS; i++)
    result += s->bands[i] * is->bands[i];
  return (result / LUZ_SPECTRUM_BANDS);
}

static inline float
illuminant_to_rev_y_scale (Luz *luz,
                           const Spectrum *illuminant)
{
  return 1.0 / spectrum_integrate (illuminant, &luz->STANDARD_OBSERVER_Y);
}

static inline void
spectrum_to_xyz (Luz  *luz,
                 const Spectrum *observed,
                 float          *x,
                 float          *y,
                 float          *z)
{
  Spectrum rescaled = *observed;

  *x = spectrum_integrate (&rescaled, &luz->STANDARD_OBSERVER_X) *
         luz->rev_y_scale;
  *y = spectrum_integrate (&rescaled, &luz->STANDARD_OBSERVER_Y) *
         luz->rev_y_scale;
  *z = spectrum_integrate (&rescaled, &luz->STANDARD_OBSERVER_Z) *
         luz->rev_y_scale;
}

void
luz_spectrum_to_xyz (Luz  *luz,
                     const Spectrum *observed,
                     float          *x,
                     float          *y,
                     float          *z)
{
  spectrum_to_xyz (luz, observed, x, y, z);
}

static const Babl *fish = NULL;

static inline void
spectrum_to_rgb (Luz            *luz,
                 const Spectrum *observed,
                 float          *rgb)
{
  float xyz[3];
  spectrum_to_xyz (luz, observed, &xyz[0], &xyz[1], &xyz[2]);
  rgb[0] = xyz[0] * 3.134274799724 +
           xyz[1] * -1.617275708956 +
           xyz[2] * -0.490724283042;
  rgb[1] = xyz[0] * -0.978795575994 +
           xyz[1] * 1.916161689117 +
           xyz[2] * 0.033453331711;
  rgb[2] = xyz[0] * 0.071976988401 +
           xyz[1] * -0.228984974402 +
           xyz[2] * 1.405718224383;
}

void
luz_spectrum_to_rgb (Luz            *luz,
                     const Spectrum *observed,
                     float          *rgb)
{
  spectrum_to_rgb (luz, observed, rgb);
}


static void color_recompute (Luz *luz, Coat *coat)
{
  int i;
  float max = 0;
  for (i = 0; i < LUZ_SPECTRUM_BANDS; i++)
    {
      float white = coat->on_white.bands[i];
      float black = coat->on_black.bands[i];
      if (white <= 0.00001)
        white = 0.00001;
      coat->opaqueness.bands[i] = black/white;
      if (coat->opaqueness.bands[i] > 1.0)
        coat->opaqueness.bands[i] = 1.0;

      if (coat->opaqueness.bands[i]>max)
        max = coat->opaqueness.bands[i];
    }

#if 0
  /* set computed spectral opaqueness to half between max band and this bands
     opaquenes. NOTE: important tweak of implementation.
   */
  for (i = 0; i < LUZ_SPECTRUM_BANDS; i++)
    coat->opaqueness.bands[i] =
      coat->opaqueness.bands[i] * 0.5 +
      max * 0.5;
#endif
}

static inline Spectrum
coats_to_spectrum_continous (Luz  *luz,
                            const float *coat_levels)
{
  int i;
  Spectrum spec = luz->substrate;

  for (i = 0; i < luz->coats; i++)
    add_coat (&spec, //&luz->illuminant,
                     &luz->coat_def[i].on_white,
                     &luz->coat_def[i].on_black,
                     &luz->coat_def[i].opaqueness,
                     coat_levels[i] * luz->coat_def[i].scale,
                     luz->coat_def[i].trc_gamma);

  spectrum_scale (&spec, &spec, &luz->illuminant);
  return spec;
}

static inline Spectrum
coats_to_spectrum (Luz *luz,
                  const float  *coat_levels)
{
  return coats_to_spectrum_continous (luz, coat_levels);
}

void
luz_coats_to_rgb (Luz *luz,
                         const float  *coat_levels,
                         float  *rgb)
{
  Spectrum perceived_spec = coats_to_spectrum (luz, coat_levels);
  spectrum_to_rgb (luz, &perceived_spec, rgb);
}

void
luz_coats_to_xyz (Luz *luz,
                     const float  *coat_levels,
                     float  *xyz)
{
  Spectrum perceived_spec = coats_to_spectrum (luz, coat_levels);
  spectrum_to_xyz (luz, &perceived_spec, &xyz[0], &xyz[1], &xyz[2]);
}

static inline float
colordiff (float *rgb_a,
           float *rgb_b)
{
  /* using CIE Lab delta E here, would not improve things, and be a waste of cycles.
   */
  return
    (rgb_a[0]-rgb_b[0])*(rgb_a[0]-rgb_b[0])+
    (rgb_a[1]-rgb_b[1])*(rgb_a[1]-rgb_b[1]) * 1.3+ /* makes green a little more important - thus luminance contribution - bigger*/
    (rgb_a[2]-rgb_b[2])*(rgb_a[2]-rgb_b[2]);
}

static inline float
spec_diff_squared (const float *spec_a,
                   const float *spec_b,
                   int    bands)
{
  float sum = 0.0;
  int i;
  /* using CIE Lab delta E here, would not improve things, and be a waste of cycles.
   */
  for (i = 0; i < bands; i++)
    {
      sum += (spec_a[i] - spec_b[i]) *
             (spec_a[i] - spec_b[i]);
    }

  return sum;
}

static inline void
luz_rgb_to_coats_stochastic (Luz *luz,
                             const float  *rgb,
                             Spectrum *spectrum, // if passed rgb is ignored
                             float  *coat_levels,
                             int     iterations,
                             float   rrange0,
                             float   rrange1)
{
  float prev_best[LUZ_MAX_COATS] = {};
  float best[LUZ_MAX_COATS] = {};
  float bestdiff = 1000.0;
  float attempt[LUZ_MAX_COATS];
  int i;

  for (i = 0; i < luz->coats; i++)
    attempt[i] = prev_best[i] = best[i] = coat_levels[i];

  for (i = 0; i < iterations; i++)
  {
    int j;
    int   max_coatsum_attempts = 10000;
    float softrgb[4];
    float diff;
    float coatsum = 0;
    coatsum = 0.0;
    for (j = 0; j < luz->coats; j++)
      {
        coatsum += attempt[j];
      }

    if (spectrum)
    {
      Spectrum soft_spect = luz_coats_to_spectrum (luz, attempt);
      diff = spec_diff_squared (&spectrum->bands[0],
                                &soft_spect.bands[0],
                                LUZ_SPECTRUM_BANDS);
    }
    else
    {
      luz_coats_to_rgb (luz, attempt, softrgb);
      diff = spec_diff_squared (rgb, softrgb, 3);
    }
    if (diff < bestdiff)
    {
      bestdiff = diff;
      for (j = 0; j < luz->coats; j++)
      {
        prev_best[j] = best[j];
        best[j] = attempt[j];
      }
      if (diff < 0.0001) /* close enough */
        break;
    }


    do {
      coatsum = 0.0;
      for (j = 0; j < luz->coats; j++)
      {
        float dir = prev_best[j] - best[j];

        if (dir > 0.001)
          dir = 0.75;
        else if (dir < -0.001)
          dir = 1.25;
        else
          dir = 1.0;

        attempt[j] = best[j] + ((random()%10000)/5000.0-dir) *
            ((i * rrange1 / iterations) +
             ((iterations-i) * ( rrange0 / iterations)));
        attempt[j] = CLAMP(attempt[j],0,1);
        coatsum += attempt[j];
      }
    } while (coatsum > luz->coverage_limit && (--max_coatsum_attempts > 0));
  }

  for (i = 0; i < luz->coats; i++)
    coat_levels[i] = best[i];
}

static inline void
luz_rgb_to_coats_griddy (Luz   *luz,
                         const float *rgb,
                         Spectrum *spectrum, // if passed rgb is ignored
                         float *coat_levels)
{
  float prev_best[LUZ_MAX_COATS] = {};
  float best[LUZ_MAX_COATS] = {};
  float bestdiff = 1000.0;
  float attempt[LUZ_MAX_COATS]={0.,};
  int i;

  for (i = 0; i < luz->coats; i++)
    prev_best[i] = best[i] = coat_levels[i];

  do
  {
    int j;
    float softrgb[4];
    float diff;
    float coatsum = 0;


    coatsum = 0.0;
    for (j = 0; j < luz->coats; j++)
    {
      coatsum += attempt[j];
    }

    if (coatsum <= luz->coverage_limit)
    {
      if (spectrum)
      {
        Spectrum soft_spec = luz_coats_to_spectrum (luz, attempt);
        diff = spec_diff_squared (&spectrum->bands[0],
                                  &soft_spec.bands[0],
                                  LUZ_SPECTRUM_BANDS);
      }
      else
      {
        luz_coats_to_rgb (luz, attempt, softrgb);
        //diff = colordiff (rgb, softrgb) * coatsum;// + (coatsum / luz->coats / 200);
        diff = spec_diff_squared (rgb, softrgb, 3);
      }

    if (diff < bestdiff)
    {
      bestdiff = diff;
      for (j = 0; j < luz->coats; j++)
      {
        prev_best[j] = best[j];
        best[j] = attempt[j];
      }
      if (diff < 0.0001) /* close enough */
        break;
    }
    }

    /* update exhaustive attempts */
    attempt[luz->coats-1] += INCREMENT;
    for (int j = luz->coats-1; j>0;j--)
    {
      if (attempt[j]>1.0)
      {
        attempt[j] = 0;
        attempt[j-1] += INCREMENT;
      }

    }

  } while (attempt[0] <= 1.0 && luz->coats > 0);

  for (i = 0; i < luz->coats; i++)
    coat_levels[i] = best[i];
}

static inline void _rgb_to_coats (Luz  *luz, const float *rgb, Spectrum *spectrum, float *coat_levels)
{
  luz_rgb_to_coats_griddy (luz, rgb, spectrum, coat_levels);
  luz_rgb_to_coats_stochastic (luz, rgb, spectrum, coat_levels,
                               luz->STOCHASTIC_ITERATIONS,
                               luz->STOCHASTIC_DIFFUSION0,
                               luz->STOCHASTIC_DIFFUSION1);
}

static inline float *
ensure_lut (Luz *luz,
            int    ri,
            int    gi,
            int    bi)
{
  int l_index;
  l_index = lut_index (ri, gi, bi);
  if (!luz->lut[l_index].defined)
  {
    float trgb[3] = {(float)ri / LUT_DIM,
                     (float)gi / LUT_DIM,
                     (float)bi / LUT_DIM };
    luz->lut[l_index].defined = 2;
    _rgb_to_coats (luz, trgb, NULL, &luz->lut[l_index].level[0]);
    luz->lut[l_index].defined = 1;
  }

  while (luz->lut[l_index].defined == 2)
    usleep (3000); // another thread is computing it, sleep
  return &luz->lut[l_index].level[0];
}

static inline void
lerp_coats (int          coats,
                float       *coat_res,
                const float *coata,
                const float *coatb,
                float        delta)
{
  int i;
  for (i = 0; i < coats; i++)
    coat_res[i] = coata[i]  * (1.0 - delta) + coatb[i] * delta;
}

void luz_rgb_to_coats (Luz  *luz, const float *rgb, float *coat_levels)
{
  float rdelta, gdelta, bdelta;
  int ri = lut_indice (rgb[0], &rdelta);
  int gi = lut_indice (rgb[1], &gdelta);
  int bi = lut_indice (rgb[2], &bdelta);
  float *coat_corner[8];
  float  temp1[LUZ_MAX_COATS];
  float  temp2[LUZ_MAX_COATS];
  float  temp3[LUZ_MAX_COATS];
  float  temp4[LUZ_MAX_COATS];

/* numbering of corners, and positions of R,G,B axes
      6
      /\
    /   \
 7/   d  \5
  |\     /|
  |  \4/  |
 3\B G|2  |
   \  |  /1
     \|/R
      0       */

  coat_corner[0] = ensure_lut (luz, ri + 0, gi + 0, bi + 0);
  coat_corner[1] = ensure_lut (luz, ri + 1, gi + 0, bi + 0);
  coat_corner[2] = ensure_lut (luz, ri + 1, gi + 0, bi + 1);
  coat_corner[3] = ensure_lut (luz, ri + 0, gi + 0, bi + 1);

  coat_corner[4] = ensure_lut (luz, ri + 0, gi + 1, bi + 0);
  coat_corner[5] = ensure_lut (luz, ri + 1, gi + 1, bi + 0);
  coat_corner[6] = ensure_lut (luz, ri + 1, gi + 1, bi + 1);
  coat_corner[7] = ensure_lut (luz, ri + 0, gi + 1, bi + 1);

  lerp_coats (luz->coats, temp1, coat_corner[0], coat_corner[1], rdelta);
  lerp_coats (luz->coats, temp2, coat_corner[3], coat_corner[2], rdelta);
  lerp_coats (luz->coats, temp3, coat_corner[4], coat_corner[5], rdelta);
  lerp_coats (luz->coats, temp4, coat_corner[7], coat_corner[6], rdelta);
  lerp_coats (luz->coats, temp1, temp1, temp3, gdelta);
  lerp_coats (luz->coats, temp2, temp2, temp4, gdelta);
  lerp_coats (luz->coats, coat_levels, temp1, temp2, bdelta);

  if(1){
    int i;
    for (i = 0; i < luz->coats; i++)
    {
      int levels = luz->coat_def[i].levels;
      if (levels > 1)
        coat_levels[i] =
        (((int)(coat_levels[i] * levels)%(levels)) ) / (levels-1.000f);
    }
  }
}

/* FIXME: this can be improved to gain smoother spectrums by creating or
          finding some other basis functions. One can even have multiple
          different basises if some types are closer to some color mixing
          than others. */
static Spectrum _rgb_to_spectrum (Luz *luz, float r, float g, float b)
{
  Spectrum s;
  int i;
  Spectrum red   = *luz_get_spectrum (luz, "red");
  Spectrum green = *luz_get_spectrum (luz, "green");
  Spectrum blue  = *luz_get_spectrum (luz, "blue");

  for (i = 0; i<LUZ_SPECTRUM_BANDS; i++)
    s.bands[i] = red.bands[i] * powf(r,2.2) + green.bands[i] * powf(g,2.2) + blue.bands[i] * powf(b, 2.2);
  return s;
}

Spectrum
luz_rgb_to_spectrum (Luz *luz, float r, float g, float b)
{
  return _rgb_to_spectrum (luz, r, g, b);
}

Spectrum
luz_parse_spectrum (Luz *luz, char *spectrum)
{
  Spectrum s;
  char key[32];
  const Spectrum *tmp;
  int i;
  for (i = 0; i < LUZ_SPECTRUM_BANDS; i++)
    s.bands[i] = 0;

  if (!spectrum)
    return s;
  while (*spectrum == ' ') spectrum ++;
  for (i = 0; spectrum[i] && spectrum[i]!=' '; i++)
    key[i] = spectrum[i];
  key[i]=0;

  if (!strcmp (key, "rgb"))
  {
    float r = 0, g = 0, b = 0;
    char *p = spectrum + 3;

    r = strtod (p, &p);
    if (p) g = strtod (p, &p);
    if (p) b = strtod (p, &p);

    s = _rgb_to_spectrum (luz, r, g, b);

  }

  tmp = luz_get_spectrum (luz, key);
  if (tmp)
  {
    s = *tmp;
  }
  else
  {
    float num_array [100];
    int band;
    band = 0;
    do {
      num_array[band++] = strtod (spectrum, &spectrum);
    } while (spectrum && band <= 100);

    if (band > 3)
    {
      float nm_start = num_array[0];
      float nm_gap = num_array[1];
      float nm_scale = num_array[2];

      int i;
      int j;
      float nm = nm_start;
      for (i = 3; i < band; i++)
      {
        j = (int) ( (nm - LUZ_SPECTRUM_START) / LUZ_SPECTRUM_GAP);
        if (j >=0 && j < LUZ_SPECTRUM_BANDS)
          {
            int k;
            for (k = j; k < LUZ_SPECTRUM_BANDS; k++)
              s.bands[k] = num_array[i] * nm_scale;
          }
        nm += nm_gap;
      }

      j = (int) ( (nm - LUZ_SPECTRUM_START) / LUZ_SPECTRUM_GAP);
      if (j >=0 && j < LUZ_SPECTRUM_BANDS)
       {
         int k;
         for (k = j; k < LUZ_SPECTRUM_BANDS; k++)
          s.bands[k] = 0.0;
       }
    }
  }
  return s;
}

const Spectrum *luz_get_spectrum (Luz *luz, const char *name)
{
  int i;
  if (!strcmp (name, "illuminant")) return &luz->illuminant;
  if (!strcmp (name, "substrate")) return &luz->substrate;
  if (!strcmp (name, "observer_x")) { return &luz->STANDARD_OBSERVER_X; }
  if (!strcmp (name, "observer_y")) { return &luz->STANDARD_OBSERVER_Y; }
  if (!strcmp (name, "observer_z")) { return &luz->STANDARD_OBSERVER_Z; }

  for (i = 0; i < luz->db.count; i++)
  {
    if (!strcmp (name, luz->db.name[i]))
    {
      return &luz->db.spectrum[i];
    }
  }
  return NULL;
}

void luz_set_spectrum (Luz *luz, const char *name, Spectrum *spectrum)
{
  int i;

  if (!strcmp (name, "illuminant")) { luz->illuminant = *spectrum;
  luz->rev_y_scale = illuminant_to_rev_y_scale (luz, spectrum);
          return; }
  if (!strcmp (name, "substrate"))  { luz->substrate  = *spectrum; return; }
  if (!strcmp (name, "observer_x")) { luz->STANDARD_OBSERVER_X = *spectrum; return; }
  if (!strcmp (name, "observer_y")) { luz->STANDARD_OBSERVER_Y = *spectrum; return; }
  if (!strcmp (name, "observer_z")) { luz->STANDARD_OBSERVER_Z = *spectrum; return; }

  for (i = 0; i < luz->db.count; i++)
  {
    if (!strcmp (name, luz->db.name[i]))
    {
      luz->db.spectrum[i] = *spectrum;
      return;
    }
  }
  if (luz->db.count >= SPECTRUM_DB_SIZE-1)
  {
    /* eeek */
    return;
  }
  i = luz->db.count;
  strncpy (luz->db.name[i], name, 32);
  luz->db.spectrum[i] = *spectrum;
  luz->db.count++;
}

static void parse_config_line (Luz   *luz,
                               const char *line)
{
  char *key;
  char *p;
  char *rest;
  Spectrum s;
  int i;
  if (!line)
    return;

  if (!strchr (line, '=')) /* lines without = are simply skipped */
    return;
  while (*line == ' ') line++;

  key = strdup (line);
  p = rest = strchr (key, '=');

  while (*rest == '=' || *rest == ' ')
          rest++;
  while (*p == '=' || *p == ' ')
  {
    *p = '\0';
    p--;
  }

  if (!strcmp (key, "coatlimit"))
    {
      luz->coverage_limit = strchr(line, '=') ? strtod (strchr (line, '=')+1, NULL) : 3.0;
      if (luz->coverage_limit < 0.2)
        luz->coverage_limit = 0.2;
      free (key);
      return;
    }
  else if (!strcmp (key, "debugwidth"))
    {
      luz->debug_width= strchr(line, '=') ? strtod (strchr (line, '=')+1, NULL) : 25;
      free (key);
      return;
    }
  else if (!strcmp (key, "iterations"))
    {
      luz->STOCHASTIC_ITERATIONS = (strchr(line, '=') ? atoi (strchr (line, '=')+1) : 42);
      free (key);
      return;
    }
  else if (!strcmp (key, "diffusion"))
    {
      luz->STOCHASTIC_DIFFUSION0 = strchr(line, '=') ?
                 strtod (strchr (line, '=')+1, NULL) : 0;
      luz->STOCHASTIC_DIFFUSION1 = strchr(line, '=') ?
                 strtod (strchr (line, '=')+1, NULL) : 0;
      free (key);
      return;
    }

  s = luz_parse_spectrum (luz, strchr (line, '=') +1);
  {
    luz_set_spectrum (luz, key, &s);
    for (i = 0; i < LUZ_MAX_COATS; i++)
    {
      Coat *coat = &luz->coat_def[i];
      char prefix[40];
      sprintf (prefix, "coat%i", i+1);
      if (!strcmp (key, prefix))
        {
          coat->on_white = s;
          coat->on_black = s;
          memset (&coat->on_black, 0, sizeof (Spectrum)); /*default to black thus coats  */
          luz->coats = MAX(luz->coats, i+1);
          free (key);
          color_recompute (luz, coat);
          return;
        }

      sprintf (prefix, "coat%i.black", i+1);
      if (!strcmp (key, prefix))
        {
          coat->on_black = s;
          luz->coats = MAX(luz->coats, i+1);
          free (key);
          color_recompute (luz, coat);
          return;
        }

      sprintf (prefix, "coat%i.levels", i+1);
      if (!strcmp (key, prefix))
        {
          coat->levels = strtod (rest, NULL);
          free (key);
          return;
        }

      sprintf (prefix, "coat%i.gamma", i+1);
      if (!strcmp (key, prefix))
        {
          coat->trc_gamma = strtod (rest, NULL);
          free (key);
          return;
        }
      sprintf (prefix, "coat%i.scale", i+1);
      if (!strcmp (key, prefix))
        {
          coat->scale = strtod (rest, NULL);
          free (key);
          return;
        }
      sprintf (prefix, "coat%i.opaqueness", i+1);
      if (!strcmp (key, prefix))
        {
          int j;
          float opaqueness = strtod (rest, NULL);
          for (j = 0; j < LUZ_SPECTRUM_BANDS; j++)
          {
            luz->coat_def[i].on_black.bands[j] =
            luz->coat_def[i].on_white.bands[j] * opaqueness;
          }

          free (key);
      color_recompute (luz, coat);
          return;
        }
      color_recompute (luz, coat);
    }
  }
  free (key);
}

static void
luz_reset (Luz *luz)
{
  int i;
  memset (luz, 0, sizeof (Luz));
  for (i = 0; i < LUZ_MAX_COATS; i++)
    {
      luz->coat_def[i].scale = 1.0;
      luz->coat_def[i].trc_gamma = 1.0;
      luz->coat_def[i].levels = 0;
    }
  luz->coverage_limit = LUZ_MAX_COATS;
}

static void
luz_parse_int (Luz *luz, const char *p)
{
  char acc[4096]; /* XXX: not protected */
  int acci = 0;
  acci = 0;
  while (*p)
  {
    switch (*p)
    {
      case '\n':
        parse_config_line (luz, acc);
        acci = 0;
        acc[acci] = 0;
        break;
      default:
        acc[acci++] = *p;
        acc[acci] = 0;
        break;
    }
    p++;
  }
  parse_config_line (luz, acc);
}

static void
luz_parse_config (Luz       *luz,
                   const char *p)
{
  if (!p)
    return;

  if (luz->src)
    {
      if (!strcmp (luz->src, p))
        return;
      free (luz->src);
      luz->src = NULL;
    }

  luz_reset (luz);

  luz->src = strdup (p);

  luz_parse_int (luz, config_internal);
  luz_parse_int (luz, p);

  if (luz->STOCHASTIC_DIFFUSION0 < 0.03)
    luz->STOCHASTIC_DIFFUSION0 = 0.03;
  else if (luz->STOCHASTIC_DIFFUSION0 > 100.0)
    luz->STOCHASTIC_DIFFUSION0 = 100.0;
  if (luz->STOCHASTIC_DIFFUSION1 < 0.03)
    luz->STOCHASTIC_DIFFUSION1 = 0.03;
  else if (luz->STOCHASTIC_DIFFUSION1 > 100.0)
    luz->STOCHASTIC_DIFFUSION1 = 100.0;
}

Luz *
luz_new (const char *config)
{
  Luz *luz = calloc (sizeof (Luz), 1);
  luz_parse_config (luz, config);
  return luz;
}

void
luz_destroy (Luz *luz)
{
  if (luz->src)
    {
      free (luz->src);
      luz->src = NULL;
    }
  free (luz);
}

/* this API permits proofing with a lower amount of coats,
 * without writing a full new config for doing that, by
 * overriding the coat limit after loading the config
 */

int luz_get_coat_count (Luz *luz)
{
  return luz->coats;
}

void luz_set_coat_count (Luz *luz, int count)
{
  luz->coats = count;
}

float luz_get_coverage_limit (Luz *luz)
{
  return luz->coverage_limit;
}

void luz_set_coverage_limit (Luz *luz, float limit)
{
  luz->coverage_limit = limit;
}

Spectrum luz_coats_to_spectrum  (Luz         *luz,
                                 const float *coat_levels)
{
  return coats_to_spectrum (luz, coat_levels);
}
