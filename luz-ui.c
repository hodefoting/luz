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
 * Copyright 2014,2016 Øyvind Kolås <pippin@gimp.org>
 */

//#include "config.h"
//#include <glib/gi18n-lib.h>
#define _(a) (a)
#define N_(a) (a)
#define dgettext(a,b) (b)

#define USE_UI

#ifdef GEGL_PROPERTIES
//"black=rgb 0 0 0\n"

#define DEFAULT_CONFIG \
"\n"

enum_start (count_sim_mode)
  enum_value (GEGL_SSIM_PROOF,          "proof",          N_("Proof"))
  enum_value (GEGL_SSIM_SEPARATE,       "separate",       N_("Separate"))
  enum_value (GEGL_SSIM_SEPARATE_PROOF, "separate-proof", N_("Separate and proof"))
enum_end (GeglInkSimMode2)

property_enum (mode, _("Mode"), GeglInkSimMode2, count_sim_mode,
                 GEGL_SSIM_SEPARATE_PROOF)
  description (_("how the coat simulator is used"))

  property_int (coat_no, _("coat no"), 0)
              value_range (0, 5)
              description (_("0 means all coat, a specific number means show output for only that color - when separating this causes a grayscale to be produced"))

#ifdef USE_UI

property_color (substrate_color, _("Substrate color"), "#ffffff")
  description (_("paper/fabric/material color"))

property_color (coat1, _("coat 1"), "#ff000000")
  description (_("opacity of 0 means coat 100 means paint and 50 means unused"))
property_int (coat1_opaqueness, _("coat1 opaqueness"), 0)
   value_range (-1, 100)
   description (_("-1 means do not use coat, 0 means treat as coat 100 means treat as paint."))

property_color (coat2, _("coat 2"), "#00ff0000")
  description (_("opacity of 0 means coat 100 means paint and 50 means unused"))
property_int (coat2_opaqueness, _("coat2 opaqueness"), 0)
   value_range (-1, 100)
   description (_("-1 means do not use coat, 0 means treat as coat 100 means treat as paint."))

property_color (coat3, _("coat 3"), "#0000ff00")
  description (_("opacity of 0 means coat 100 means paint and 50 means unused"))
property_int (coat3_opaqueness, _("coat3 opaqueness"), -1)
   value_range (-1, 100)
   description (_("-1 means do not use coat, 0 means treat as coat 100 means treat as paint."))

property_color (coat4, _("coat 4"), "#000000ff")
  description (_("opacity of 0 means coat 100 means paint and 50 means unused"))
property_int (coat4_opaqueness, _("coat4 opaqueness"), -1)
   value_range (-1, 100)
   description (_("-1 means do not use coat, 0 means treat as coat 100 means treat as paint."))

property_color (coat5, _("coat 5"), "#00000077")
  description (_("opacity of 0 means coat 100 means paint and 50 means unused"))
property_int (coat5_opaqueness, _("coat5 opaqueness"), -1)
   value_range (-1, 100)
   description (_("-1 means do not use coat, 0 means treat as coat 100 means treat as paint."))

property_double (coverage_limit, _("Ink limit"), 4.0)
   description (_("1.0 is full coverage of one coat 2.0 full coverage of two or mixes of 3 not reaching 200%"))
property_double (iterations, _("Iterations"), 64.0)
   value_range (0.0, 2000.0)
   description (_("how many iterations to run"))
/*
5.0, 3.0,
                   _("maximum amount of coat for one pixel, 2.5 = 250%% coverage"))*/
#else
property_string (config, _("Ink configuration"), DEFAULT_CONFIG)
  description (_("Textual desciption of coats used for simulated print-job, "))
  ui_meta ("multiline", "true")


#endif

#else

#define GEGL_OP_POINT_FILTER
#define GEGL_OP_NAME     luz_ui
#define GEGL_OP_C_SOURCE luz-ui.c

#include "gegl-op.h"
#include <math.h>

#include "luz.h"

/*********************/

/*********************/

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
    case GEGL_SSIM_PROOF:
      gegl_operation_set_format (operation, "input",
        babl_format_n (babl_type("float"), input_components));
      gegl_operation_set_format (operation, "output", babl_format ("RGBA float"));
      break;
    case GEGL_SSIM_SEPARATE:
      //gegl_operation_set_format (operation, "output",
      //  babl_format_n (babl_type("float"), luz_get_coat_count (o->user_data)));
      gegl_operation_set_format (operation, "output", babl_format ("RGBA float"));

      gegl_operation_set_format (operation, "input",
          babl_format ("RGBA float"));
      break;
    default:
    case GEGL_SSIM_SEPARATE_PROOF:
      gegl_operation_set_format (operation, "output", babl_format ("RGB float"));

      gegl_operation_set_format (operation, "input",
          babl_format ("RGBA float"));
      break;
  }

#ifdef USE_UI
  {
  float color[4];
  GString *conf_str;
  conf_str = g_string_new ("\nilluminant=D65\n");
  g_string_append_printf (conf_str, "\n");

  gegl_color_get_pixel (o->substrate_color, babl_format ("RGBA float"), color);
  g_string_append_printf (conf_str, "substrate = rgb %f %f %f\n",
      color[0], color[1], color[2]);

  {
    float op = o->coat1_opaqueness;
    if (op >= 0)
    {
      gegl_color_get_pixel (o->coat1, babl_format ("RGBA float"), color);
      g_string_append_printf (conf_str, "coat1= rgb %f %f %f\n",
         color[0], color[1], color[2]);
      g_string_append_printf (conf_str, "coat1.opaqueness=%f\n", op/100.0);
    }
  }
  {
    float op = o->coat2_opaqueness;
    if (op >= 0)
    {
      gegl_color_get_pixel (o->coat2, babl_format ("RGBA float"), color);
      g_string_append_printf (conf_str, "coat2= rgb %f %f %f\n",
         color[0], color[1], color[2]);
      g_string_append_printf (conf_str, "coat2.opaqueness=%f\n", op/100.0);
    }
  }
  {
    float op = o->coat3_opaqueness;
    if (op >= 0)
    {
      gegl_color_get_pixel (o->coat3, babl_format ("RGBA float"), color);
      g_string_append_printf (conf_str, "coat3= rgb %f %f %f\n",
         color[0], color[1], color[2]);
      g_string_append_printf (conf_str, "coat3.opaqueness=%f\n", op/100.0);
    }
  }
  {
    float op = o->coat4_opaqueness;
    if (op >= 0)
    {
      gegl_color_get_pixel (o->coat4, babl_format ("RGBA float"), color);
      g_string_append_printf (conf_str, "coat4= rgb %f %f %f\n",
         color[0], color[1], color[2]);
      g_string_append_printf (conf_str, "coat4.opaqueness=%f\n", op/100.0);
    }
  }
  {
    float op = o->coat5_opaqueness;
    if (op >= 0)
    {
      gegl_color_get_pixel (o->coat5, babl_format ("RGBA float"), color);
      g_string_append_printf (conf_str, "coat5= rgb %f %f %f\n",
         color[0], color[1], color[2]);
      g_string_append_printf (conf_str, "coat5.opaqueness=%f\n", op/100.0);
    }
  }

  g_string_append_printf (conf_str, "limit=%f\n", o->coverage_limit);
  g_string_append_printf (conf_str, "iterations=%f\n", o->iterations);
  g_string_append_printf (conf_str, "\n");

  if (o->user_data)
    luz_destroy (o->user_data);
  o->user_data = luz_new (conf_str->str);

  g_string_free (conf_str, TRUE);
  }
#else
  if (o->user_data)
    luz_destroy (o->user_data);
  o->user_data = luz_new (o->config);
#endif
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
    case GEGL_SSIM_PROOF:
      {
        if (luz_get_coat_count (ssim) > 3)
        {
          while (samples--)
            {
              gfloat coats[4];
              int i;
              for (i = 0; i < 4; i++)
                coats[i] = in[i];
              luz_coats_to_rgb (ssim, coats, out);
              in  += in_components;
              out += 4;
            }
        }
        else
        {
          while (samples--)
            {
              luz_coats_to_rgb (ssim, in, out);
              in  += in_components;
              out += 4;
            }
        }
      }
      break;
    case GEGL_SSIM_SEPARATE:
      /* eeek hard coded for rgb output */
    if (o->coat_no == 0)
      while (samples--)
        {
          int i;
          int count_count = luz_get_coat_count (ssim);
          gfloat coats[LUZ_MAX_COATS];
          luz_rgb_to_coats (ssim, in, coats);
          for (i = 0; i < MIN(4, count_count); i++)
            out[i] = coats[i];
          if (count_count < 4)
            out[3] = 1.0;
          if (count_count < 3)
            out[2] = 0.0;
          if (count_count < 2)
            out[1] = 0.0;
          in  += in_components;
          out += 4;
        }
     else
       {
         int count_count = luz_get_coat_count (ssim);
         int count_no = o->coat_no-1;
         if (count_no > count_count - 1) count_no = count_count - 1;

         while (samples--)
           {
             int i;
             gfloat coats[LUZ_MAX_COATS];

             luz_rgb_to_coats (ssim, in, coats);
             out[0] = coats[count_no];
             out[1] = coats[count_no];
             out[2] = coats[count_no];
             out[3] = 1.0;
             in  += in_components;
             out += 4;
           }
        }
      break;
      case GEGL_SSIM_SEPARATE_PROOF:
        while (samples--)
        {
          gfloat coats[LUZ_MAX_COATS];
          luz_rgb_to_coats (ssim, in, coats);
          if (o->coat_no != 0)
          {
            int count_count = luz_get_coat_count (ssim);
            int i;
            for (i = 0; i < count_count; i++)
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

  gegl_operation_class_set_keys (operation_class,
  "name"        , "gegl:luz",
  "title"       , _("Luz"),
  "categories"  , "misc",
  "description" ,
        _("Layered spectral coat (print/ink/paint) simulator. Simulates various printing/painting/drawing technologies, with a softproofing/simulating physical color mixing and interactions."
          ""),
        NULL);
}


#endif
