/*
 * This file is a part of Poly2Tri-C
 * (c) Barak Itkin <lightningismyname@gmail.com>
 * http://code.google.com/p/poly2tri-c/
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * * Neither the name of Poly2Tri nor the names of its contributors may be
 *   used to endorse or promote products derived from this software without specific
 *   prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <glib.h>

#include <poly2tri-c/p2t/poly2tri.h>

#include <poly2tri-c/refine/refine.h>
#include <poly2tri-c/render/svg-plot.h>
#include <poly2tri-c/render/mesh-render.h>

#include <string.h>


static gint refine_max_steps = 1000;
static gboolean debug_print = TRUE;
static gboolean verbose = TRUE;
static gchar *input_file = NULL;
static gchar *output_file = NULL;
static gboolean render_mesh = FALSE;
static gboolean render_svg = FALSE;

static GOptionEntry entries[] =
{
  { "refine-max-steps", 'r', 0, G_OPTION_ARG_INT,      &refine_max_steps, "Set maximal refinement steps to N", "N" },
  { "verbose",          'v', 0, G_OPTION_ARG_NONE,     &verbose,          "Print output?",                     NULL },
  { "debug",            'd', 0, G_OPTION_ARG_NONE,     &debug_print,      "Enable debug printing",             NULL },
  { "input",            'i', 0, G_OPTION_ARG_FILENAME, &input_file,       "Use input file at FILE_IN",         "FILE_IN" },
  { "output",           'o', 0, G_OPTION_ARG_FILENAME, &output_file,      "Use output file at FILE_OUT",       "FILE_OUT" },
  { "render-mesh",      'm', 0, G_OPTION_ARG_NONE,     &render_mesh,      "Render a color mesh of the result", NULL },
  { "render-svg",       's', 0, G_OPTION_ARG_NONE,     &render_svg,       "Render an outline of the result",   NULL },
  { NULL }
};

typedef gfloat Color3f[3];
typedef gfloat Point2f[2];

/**
 * read_points_file:
 * @path: The path to the points & colors file
 * @points: An pointer to an array of pointers to #P2RrPoint will be returned
 *          here. NULL can be passed.
 * @colors: An pointer to an array of colors will be returned here. NULL can be
 *          passed.
 *
 *
 */
void
read_points_file (const gchar       *path,
                  P2tPointPtrArray  *points,
                  GArray           **colors)
{
  FILE *f = fopen (path, "r");
  gint countPts = 0, countCls = 0;

  if (f == NULL)
    {
      g_print ("Error! Could not read input file!");
      exit (1);
    }

  if (verbose)
    g_print ("Now parsing \"%s\"\n", path);

  if (points != NULL) *points = g_ptr_array_new ();
  if (colors != NULL) *colors = g_array_new (FALSE, FALSE, sizeof (Color3f));

  if (debug_print && points == NULL) g_print ("Points will not be kept\n");
  if (debug_print && colors == NULL) g_print ("Colors will not be kept\n");

  while (! feof (f))
    {
      Color3f col = { 0, 0, 0 };
      Point2f ptc = { 0, 0 };
      gboolean foundCol = FALSE, foundPt = FALSE;

      /* Read only while we have valid points */
      gint readSize = fscanf (f, "@ %f %f ", &ptc[0], &ptc[1]);

      if (readSize > 0)
        {
          if (readSize != 2)
            {
              g_error ("Error! %d is an unexpected number of floats after point '@' declaration!\n", readSize);
              exit (1);
            }

          foundPt = TRUE;

          if (points != NULL)
            {
              g_ptr_array_add (*points, p2t_point_new_dd (ptc[0], ptc[1]));
              countPts++;
            }
        }

     readSize = fscanf (f, "# %f %f %f ", &col[0], &col[1], &col[2]);

     if (readSize > 0)
       {
          if (readSize != 1 && readSize != 3)
            {
              g_error ("Error! %d is an unexpected number of floats after color '#' declaration!\n", readSize);
              exit (1);
            }

          foundCol = TRUE;

          /* Did we read Gray color information? */
          if (readSize == 1)
            col[1] = col[2] = col[0];

          if (colors != NULL)
            {
              g_array_append_val (*colors, ptc);
              countCls++;
            }
       }
     
     if (!foundCol && !foundPt)
       break;
    }

  fclose (f);

  if (verbose)
    g_print ("Read %d points and %d colors\n", countPts, countCls);
}

void
free_read_results (P2tPointPtrArray  *points,
                   GArray           **colors)
{
  gint i;

  if (points != NULL)
    {
      for (i = 0; i < (*points)->len; i++)
        p2t_point_free (point_index (*points, i));
      g_ptr_array_free (*points, TRUE);
    }

  if (colors != NULL)
    g_array_free (*colors, TRUE);
}

/* Calculate a "deterministic random" color for each point
 * based on its memory address. Since we know that least-significant bytes
 * of the point address will change more than the mor-important ones, we
 * make sure to take them into consideration in all the color channels.
 */
static void
test_point_to_color (P2trPoint* point, gfloat *dest, gpointer user_data)
{
  gulong value = (gulong) point;
  guchar b1 = value & 0xff, b2 = (value & 0xff00) >> 2, b3 = (value & 0xff0000) >> 4;
  dest[0] = b1 / 255.0f;
  dest[1] = (b1 ^ b2) / 255.0f;
  dest[2] = (b1 ^ b2 ^ b3) / 255.0f;
}

gint main (int argc, char *argv[])
{
  FILE *svg_out = NULL, *mesh_out = NULL;
  gchar *svg_out_path, *mesh_out_path;

  GError *error = NULL;
  GOptionContext *context;

  GPtrArray *pts;
  GArray    *colors;

  P2tCDT *cdt;
  P2trCDT *rcdt;
  P2trDelaunayTerminator *dt;

  context = g_option_context_new ("- Create a fine mesh from a given PSLG");
  g_option_context_add_main_entries (context, entries, NULL);

  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_print ("option parsing failed: %s\n", error->message);
      exit (1);
    }

  g_option_context_free (context);

  if (input_file == NULL)
    {
      g_print ("No input file given. Stop.");
      exit (1);
    }

  if (! g_file_test (input_file, G_FILE_TEST_EXISTS))
    {
      g_print ("Input file does not exist. Stop.");
      exit (1);
    }

  if (output_file == NULL && (render_svg || render_mesh))
    {
      g_print ("No output file given. Stop.");
      exit (1);
    }

  if (render_svg)
    {
      svg_out_path = g_newa (gchar, strlen (output_file) + 4);
      sprintf (svg_out_path, "%s.svg", output_file);

      if ((svg_out = fopen (svg_out_path, "w")) == NULL)
        {
          g_print ("Can't open the svg output file. Stop.");
          exit (1);
        }
    }

  if (render_mesh)
    {
      mesh_out_path = g_newa (gchar, strlen (output_file) + 4);
      sprintf (mesh_out_path, "%s.ppm", output_file);

      if ((mesh_out = fopen (mesh_out_path, "w")) == NULL)
        {
          g_print ("Can't open the mesh output file. Stop.");
          exit (1);
        }
    }

  read_points_file (input_file, &pts, &colors);

  cdt = p2t_cdt_new (pts);
  p2t_cdt_triangulate (cdt);

  rcdt = p2tr_cdt_new (cdt);
  p2t_cdt_free (cdt);

  if (refine_max_steps > 0)
    {
      g_print ("Refining the mesh!\n"); 
      dt = p2tr_dt_new (G_PI / 6, p2tr_dt_false_too_big, rcdt);
      p2tr_dt_refine (dt, refine_max_steps);
      p2tr_dt_free (dt);
    }

  if (render_svg)
    {
      g_print ("Rendering SVG outline!");
      p2tr_plot_svg (rcdt->mesh, svg_out);
      fclose (svg_out);
    }

  if (render_mesh)
    {
      P2trImageConfig imc;
      gfloat *im;

      g_print ("Rendering color interpolation!");

      imc.cpp = 4;
      imc.min_x = imc.min_y = 0;
      imc.step_x = imc.step_y = 0.2;
      imc.x_samples = imc.y_samples = 500;

      im = g_new (gfloat, imc.cpp * imc.x_samples * imc.y_samples);

      p2tr_mesh_render_scanline (rcdt->mesh, im, &imc, test_point_to_color, NULL);

      p2tr_write_ppm (mesh_out, im, &imc);
      fclose (mesh_out);

      g_free (im);
    }

  p2tr_cdt_free (rcdt);
  free_read_results (&pts, &colors);

  return 0;
}
