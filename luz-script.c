/* This file is an image processing operation for GEGL
 *
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
 * Copyright 2014, 2018 Øyvind Kolås <pippin@gimp.org>
 */

//#include "config.h"
//#include <glib/gi18n-lib.h>
#define _(a) (a)
#define N_(a) (a)
#define dgettext(a,b) (b)


#ifdef GEGL_PROPERTIES

#define DEFAULT_CONFIG \
"\n"\
"coat1=rgb 0 1 1\n"\
"coat1.black=rgb 0 0 0\n"\
"coat2=rgb 1 0 1\n"\
"coat2.black=rgb 0 0 0\n"\
"coat3=rgb 1 1 0\n"\
"coat3.black=rgb 0 0 0\n"\
"iterations=100\n"\
"\n"

enum_start (coat_sim_mode)
  enum_value (GEGL_LUZ_PROOF,          "proof",          N_("Proof"))
  enum_value (GEGL_LUZ_SEPARATE,       "separate",       N_("Separate"))
  enum_value (GEGL_LUZ_SEPARATE_PROOF, "separate-proof", N_("Separate and proof"))
enum_end (GeglInkSimMode)

property_enum (mode, _("Mode"), GeglInkSimMode, coat_sim_mode,
                 GEGL_LUZ_SEPARATE_PROOF)
  description (_("how the coat simulator is used"))

property_int (coat_no, _("coat no"), 0)
              value_range (0, 5)
              description (_("0 means all coat, a specific number means show output for only that color - when separating this causes a grayscale to be produced"))

property_string (config, _("Ink configuration"), DEFAULT_CONFIG)
  description (_("Textual desciption of coats used for simulated print-job, "))
  ui_meta ("multiline", "true")

#if 0
property_int (debug_width, _("Debug width"), 100)
   value_range (0, 450)
   description (_("how wide peel off bands for coat order vis"))
#endif

#else

#define GEGL_OP_POINT_FILTER
#define GEGL_OP_NAME     luz_script
#define GEGL_OP_C_SOURCE luz-script.c

#include "gegl-op.h"
#include <math.h>
#include "luz.h"


static void
prepare (GeglOperation *operation)
{
  GeglProperties *o = GEGL_PROPERTIES (operation);
  const Babl *input_format = gegl_operation_get_source_format (operation, "input");
  gint input_components = 1;
  if (input_format)
    input_components = babl_format_get_n_components (input_format);

  switch (o->mode)
  {
    case GEGL_LUZ_PROOF:
      gegl_operation_set_format (operation, "input",
        babl_format_n (babl_type("float"), input_components));
      gegl_operation_set_format (operation, "output", babl_format ("RGBA float"));
      break;
    case GEGL_LUZ_SEPARATE:
      //gegl_operation_set_format (operation, "output",
      //  babl_format_n (babl_type("float"), luz_get_coat_count (o->user_data)));
      gegl_operation_set_format (operation, "output", babl_format ("RGBA float"));

      gegl_operation_set_format (operation, "input",
          babl_format ("RGBA float"));
      break;
    default:
    case GEGL_LUZ_SEPARATE_PROOF:
      gegl_operation_set_format (operation, "output", babl_format ("RGB float"));
      gegl_operation_set_format (operation, "input",
          babl_format ("RGBA float"));
      break;
  }

  if (o->user_data)
    luz_destroy (o->user_data);
  o->user_data = luz_new (o->config);
}

static gboolean
process (GeglOperation       *op,
         void                *in_buf,
         void                *out_buf,
         glong                samples,
         const GeglRectangle *roi,
         gint                 level)
{
  GeglProperties *o = GEGL_PROPERTIES (op);
  gfloat *in  = in_buf;
  gfloat *out = out_buf;
  Luz *ssim = o->user_data;
  int in_components =
    babl_format_get_n_components (gegl_operation_get_format (op, "input"));

  switch (o->mode)
  {
    case GEGL_LUZ_PROOF:
      {
        if (luz_get_coat_count (ssim) > 3)
        {
          while (samples--)
            {
              gfloat coats[4];
              int i;
              for (i = 0; i < 4; i++)
                coats[i] = in[i];
              luz_coats_to_rgb  (ssim, coats, out);
              in  += in_components;
              out += 4;
            }
        }
        else
        {
          while (samples--)
            {
              luz_coats_to_rgb  (ssim, in, out);
              in  += in_components;
              out += 4;
            }
        }
      }
      break;
    case GEGL_LUZ_SEPARATE:
      /* eeek hard coded for rgba output */
    if (o->coat_no == 0)
      while (samples--)
        {
          int i;
          int coat_count = luz_get_coat_count (ssim);
          gfloat coats[LUZ_MAX_COATS];
          luz_rgb_to_coats (ssim, in, coats);
          for (i = 0; i < MIN(4, coat_count); i++)
            out[i] = coats[i];
          if (coat_count < 4)
            out[3] = 1.0;
          if (coat_count < 3)
            out[2] = 0.0;
          if (coat_count < 2)
            out[1] = 0.0;
          in  += in_components;
          out += 4;
        }
     else
       {
         int coat_count = luz_get_coat_count (ssim);
         int coat_no = o->coat_no-1;
         if (coat_no > coat_count - 1) coat_no = coat_count - 1;
         if (coat_no < 0) coat_no = 0;

         while (samples--)
           {
             int i;
             gfloat coats[LUZ_MAX_COATS];

             luz_rgb_to_coats (ssim, in, coats);
             out[0] = coats[coat_no];
             out[1] = coats[coat_no];
             out[2] = coats[coat_no];
             out[3] = 1.0;
             in  += in_components;
             out += 4;
           }
        }
      break;
      case GEGL_LUZ_SEPARATE_PROOF:
        while (samples--)
        {
          gfloat coats[LUZ_MAX_COATS];
          luz_rgb_to_coats (ssim, in, coats);
          if (o->coat_no != 0)
          {
            int coat_count = luz_get_coat_count (ssim);
            int i;
            for (i = 0; i < coat_count; i++)
              if (i != o->coat_no - 1)
                coats[i] = 0;
          }
          luz_coats_to_rgb (ssim, coats, out);

          in  += in_components;
          out += 3;
        }
      break;
  }
  return TRUE;
}

static void
finalize (GObject *object)
{
  GeglProperties *o = GEGL_PROPERTIES (object);
  if (o->user_data)
  {
    luz_destroy (o->user_data);
    o->user_data = NULL;
  }
  G_OBJECT_CLASS (g_type_class_peek_parent (G_OBJECT_GET_CLASS (object)))->finalize (object);
}

static void
gegl_op_class_init (GeglOpClass *klass)
{
  GeglOperationClass            *operation_class;
  GeglOperationPointFilterClass *point_filter_class;
  GObjectClass                  *gobject_class;

  operation_class = GEGL_OPERATION_CLASS (klass);
  point_filter_class = GEGL_OPERATION_POINT_FILTER_CLASS (klass);
  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = finalize;
  point_filter_class->process = process;
  operation_class->prepare = prepare;
  operation_class->threaded = FALSE;

  gegl_operation_class_set_keys (operation_class,
  "name"        , "gegl:luz-script",
  "title"       , _("luz script"),
  "categories"  , "misc",
  "description" ,
        _("Spectral coat (coat and paint) simulator, for separating/softproofing/simulating physical color mixing and interactions. Configured by scripts."
          ""),
        NULL);
}

#endif
