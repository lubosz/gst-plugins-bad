/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* non-GST-specific stuff */

#include "gltestsrc.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <gio/gio.h>

#ifndef M_PI
#define M_PI  3.14159265358979323846
#endif

enum
{
  COLOR_WHITE = 0,
  COLOR_YELLOW,
  COLOR_CYAN,
  COLOR_GREEN,
  COLOR_MAGENTA,
  COLOR_RED,
  COLOR_BLUE,
  COLOR_BLACK,
  COLOR_NEG_I,
  COLOR_POS_Q,
  COLOR_SUPER_BLACK,
  COLOR_DARK_GREY
};

static const struct vts_color_struct vts_colors[] = {
  /* 100% white */
  {1.0f, 1.0f, 1.0f},
  /* yellow */
  {1.0f, 1.0f, 0.0f},
  /* cyan */
  {0.0f, 1.0f, 1.0f},
  /* green */
  {0.0f, 1.0f, 0.0f},
  /* magenta */
  {1.0f, 0.0f, 1.0f},
  /* red */
  {1.0f, 0.0f, 0.0f},
  /* blue */
  {0.0f, 0.0f, 1.0f},
  /* black */
  {0.0f, 0.0f, 0.0f},
  /* -I */
  {0.0, 0.0f, 0.5f},
  /* +Q */
  {0.0f, 0.5, 1.0f},
  /* superblack */
  {0.0f, 0.0f, 0.0f},
  /* 7.421875% grey */
  {19 / 256.0f, 19 / 256.0f, 19 / 256.0},
};

static void
gst_gl_test_src_unicolor (GstGLTestSrc * v, GstBuffer * buffer, int w,
    int h, const struct vts_color_struct *color);

/* *INDENT-OFF* */
static const GLfloat positions_fullscreen[] = {
     -1.0,  1.0, 0.0, 1.0,
      1.0,  1.0, 0.0, 1.0,
      1.0, -1.0, 0.0, 1.0,
     -1.0, -1.0, 0.0, 1.0,
};

static const GLfloat positions_snow[] = {
    0.5, 1.0, 0.0, 1.0,
    1.0, 1.0, 0.0, 1.0,
    0.5, 0.5, 0.0, 1.0,
    1.0, 0.5, 0.0, 1.0
  };

static const GLushort indices[] = { 0, 1, 2, 3, 0 };

static const GLfloat identitiy_matrix[] = {
      1.0, 0.0, 0.0, 0.0,
      0.0, 1.0, 0.0, 0.0,
      0.0, 0.0, 1.0, 0.0,
      0.0, 0.0, 0.0, 1.0,
};

static const GLfloat uvs[] = {
     0.0, 1.0,
     1.0, 1.0,
     1.0, 0.0,
     0.0, 0.0,
};
/* *INDENT-ON* */

static void
gst_gl_test_src_position_buffer (GstGLTestSrc * v,
    GstGLShader * shader,
    GLuint * vertex_array, GLuint index_buffer, const GLfloat * positions)
{
  GstGLFuncs *gl = v->context->gl_vtable;
  GLuint position_buffer;
  GLint attr_position_loc =
      gst_gl_shader_get_attribute_location (shader, "position");

  gl->GenVertexArrays (1, vertex_array);
  gl->BindVertexArray (*vertex_array);

  /* upload vertex buffer */
  gl->GenBuffers (1, &position_buffer);
  gl->BindBuffer (GL_ARRAY_BUFFER, position_buffer);
  gl->VertexAttribPointer (attr_position_loc, 4, GL_FLOAT, GL_FALSE, 0, 0);
  gl->EnableVertexAttribArray (attr_position_loc);
  gl->BufferData (GL_ARRAY_BUFFER, 16 * sizeof (GLfloat), positions,
      GL_STATIC_DRAW);

  /* bind index buffer */
  gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, index_buffer);
}

static void
gst_gl_test_src_position_uv_buffer (GstGLTestSrc * v,
    GstGLShader * shader,
    GLuint * vertex_array, GLuint index_buffer, const GLfloat * positions)
{
  GLuint uv_buffer;
  GstGLFuncs *gl = v->context->gl_vtable;
  GLuint attr_uv_loc = gst_gl_shader_get_attribute_location (shader, "uv");
  gst_gl_test_src_position_buffer (v, shader, vertex_array, index_buffer,
      positions);

  /* upload uv buffer */
  gl->GenBuffers (1, &uv_buffer);
  gl->BindBuffer (GL_ARRAY_BUFFER, uv_buffer);
  gl->VertexAttribPointer (attr_uv_loc, 2, GL_FLOAT, GL_FALSE, 0, 0);
  gl->EnableVertexAttribArray (attr_uv_loc);
  gl->BufferData (GL_ARRAY_BUFFER, 8 * sizeof (GLfloat), uvs, GL_STATIC_DRAW);
}

void
gst_gl_test_src_uv_plane (GstGLTestSrc * v, GstBuffer * buffer, int w, int h)
{
  GstGLFuncs *gl = v->context->gl_vtable;

  GLuint vertex_array;
  GLuint index_buffer;

  if (gst_gl_context_get_gl_api (v->context)) {
    gst_gl_context_clear_shader (v->context);
    gst_gl_shader_use (v->shader);

    // index
    glGenBuffers (1, &index_buffer);
    glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, index_buffer);
    glBufferData (GL_ELEMENT_ARRAY_BUFFER, 5 * sizeof (GLuint), indices,
        GL_STATIC_DRAW);

    gst_gl_test_src_position_uv_buffer (v,
        v->shader, &vertex_array, index_buffer, positions_fullscreen);

    gst_gl_shader_set_uniform_1f (v->shader, "time",
        (gfloat) v->running_time / GST_SECOND);

    gst_gl_shader_set_uniform_1f (v->shader, "aspect_ratio",
        (gfloat) w / (gfloat) h);

    gl->DrawElements (GL_TRIANGLE_STRIP, 5, GL_UNSIGNED_SHORT, 0);

    gst_gl_context_clear_shader (v->context);
  }
}

static void
gst_gl_test_src_smpte_init_shader (gpointer shaderp)
{
  GstGLShader *shader = (GstGLShader *) shaderp;
  GError *error = NULL;
  gst_gl_shader_compile (shader, &error);
  if (error) {
    gst_gl_context_set_error (shader->context, "%s", error->message);
    g_error_free (error);
    gst_gl_context_clear_shader (shader->context);
    return;
  }
}

const char *
gst_gl_test_src_read_shader (const char *file)
{
  GBytes *bytes = NULL;
  GError *error = NULL;
  const char *shader;

  char *path = g_strjoin ("", "/glsl/", file, NULL);
  bytes = g_resources_lookup_data (path, 0, &error);
  g_free (path);

  if (bytes) {
    shader = (const gchar *) g_bytes_get_data (bytes, NULL);
    g_bytes_unref (bytes);
  } else {
    if (error != NULL) {
      GST_ERROR ("Unable to read file: %s", error->message);
      g_error_free (error);
    }
    return "";
  }
  return shader;
}

static void
gst_gl_test_src_smpte_init (GstGLTestSrc * v)
{
  GstGLShader *color_shader, *snow_shader;
  GstGLFuncs *gl = v->context->gl_vtable;
  GLuint index_buffer;

  const char *color_vertex = gst_gl_test_src_read_shader ("color.vert");
  const char *color_fragment = gst_gl_test_src_read_shader ("color.frag");
  const char *snow_vertex = gst_gl_test_src_read_shader ("snow.vert");
  const char *snow_fragment = gst_gl_test_src_read_shader ("snow.frag");

  v->vertex_arrays = malloc (21 * sizeof (GLuint));

  color_shader = gst_gl_shader_new (v->context);
  gst_gl_shader_set_vertex_source (color_shader, color_vertex);
  gst_gl_shader_set_fragment_source (color_shader, color_fragment);
  v->shaders = g_list_append (v->shaders, color_shader);

  snow_shader = gst_gl_shader_new (v->context);
  gst_gl_shader_set_vertex_source (snow_shader, snow_vertex);
  gst_gl_shader_set_fragment_source (snow_shader, snow_fragment);
  v->shaders = g_list_append (v->shaders, snow_shader);

  g_list_foreach (v->shaders, (GFunc) gst_gl_test_src_smpte_init_shader, NULL);

  /* make an index buffer for all planes */
  gl->GenBuffers (1, &index_buffer);
  gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, index_buffer);
  gl->BufferData (GL_ELEMENT_ARRAY_BUFFER, 5 * sizeof (GLuint), indices,
      GL_STATIC_DRAW);

  for (int i = 0; i < 7; i++) {
    /* *INDENT-OFF* */
    GLfloat color_plane_positions[] = {
      -1.0f +       i * (2.0f / 7.0f), 1.0f / 3.0f, 0, 1.0f,
      -1.0f + (i + 1) * (2.0f / 7.0f), 1.0f / 3.0f, 0, 1.0f,
      -1.0f +       i * (2.0f / 7.0f),       -1.0f, 0, 1.0f,
      -1.0f + (i + 1) * (2.0f / 7.0f),       -1.0f, 0, 1.0f,
    };
    /* *INDENT-ON* */
    gst_gl_test_src_position_buffer (v, color_shader,
        &v->vertex_arrays[i], index_buffer, color_plane_positions);
  }

  for (int i = 0; i < 7; i++) {
    /* *INDENT-OFF* */
    GLfloat color_plane_positions[] = {
      -1.0f + i       * (2.0f / 7.0f),        0.5f, 0, 1.0f,
      -1.0f + (i + 1) * (2.0f / 7.0f),        0.5f, 0, 1.0f,
      -1.0f + i       * (2.0f / 7.0f), 1.0f / 3.0f, 0, 1.0f,
      -1.0f + (i + 1) * (2.0f / 7.0f), 1.0f / 3.0f, 0, 1.0f
    };
    /* *INDENT-ON* */
    gst_gl_test_src_position_buffer (v, color_shader,
        &v->vertex_arrays[i + 7], index_buffer, color_plane_positions);
  }

  for (int i = 0; i < 3; i++) {
    /* *INDENT-OFF* */
    GLfloat color_plane_positions[] = {
      -1.0f +       i / 3.0f, 1.0f, 0.0f, 1.0f,
      -1.0f + (i + 1) / 3.0f, 1.0f, 0.0f, 1.0f,
      -1.0f +       i / 3.0f, 0.5f, 0.0f, 1.0f,
      -1.0f + (i + 1) / 3.0f, 0.5f, 0.0f, 1.0f
    };
    /* *INDENT-ON* */
    gst_gl_test_src_position_buffer (v, color_shader,
        &v->vertex_arrays[i + 14], index_buffer, color_plane_positions);
  }

  for (int i = 0; i < 3; i++) {
    /* *INDENT-OFF* */
    GLfloat color_plane_positions[] = {
            i / 6.0f, 1.0f, 0.0f, 1.0f,
      (i + 1) / 6.0f, 1.0f, 0.0f, 1.0f,
            i / 6.0f, 0.5f, 0.0f, 1.0f,
      (i + 1) / 6.0f, 0.5f, 0.0f, 1.0f
    };
    /* *INDENT-ON* */
    gst_gl_test_src_position_buffer (v, color_shader,
        &v->vertex_arrays[i + 17], index_buffer, color_plane_positions);
  }
  gst_gl_test_src_position_uv_buffer (v, snow_shader,
      &v->vertex_arrays[20], index_buffer, positions_snow);
}

void
gst_gl_test_src_smpte (GstGLTestSrc * v, GstBuffer * buffer, int w, int h)
{
  GstGLFuncs *gl = v->context->gl_vtable;

  if (v->shaders == NULL)
    gst_gl_test_src_smpte_init (v);

  if (gst_gl_context_get_gl_api (v->context)) {
    GstGLShader *color_shader = (GstGLShader *) g_list_nth_data (v->shaders, 0);
    GstGLShader *snow_shader = (GstGLShader *) g_list_nth_data (v->shaders, 1);

    gst_gl_context_clear_shader (v->context);
    gst_gl_shader_use (color_shader);

    for (int i = 0; i < 20; i++) {
      int k;
      if (i < 7) {
        k = i;
      } else if ((i - 7) & 1) {
        k = COLOR_BLACK;
      } else {
        k = 13 - i;
      }

      if (i == 14) {
        k = COLOR_NEG_I;
      } else if (i == 15) {
        k = COLOR_WHITE;
      } else if (i == 16) {
        k = COLOR_POS_Q;
      } else if (i == 17) {
        k = COLOR_SUPER_BLACK;
      } else if (i == 18) {
        k = COLOR_BLACK;
      } else if (i == 19) {
        k = COLOR_DARK_GREY;
      }

      glBindVertexArray (v->vertex_arrays[i]);
      gst_gl_shader_set_uniform_4f (color_shader, "color",
          vts_colors[k].R, vts_colors[k].G, vts_colors[k].B, 1.0f);

      gl->DrawElements (GL_TRIANGLE_STRIP, 5, GL_UNSIGNED_SHORT, 0);
    }

    gst_gl_context_clear_shader (v->context);

    gst_gl_shader_use (snow_shader);
    glBindVertexArray (v->vertex_arrays[20]);

    gst_gl_shader_set_uniform_1f (snow_shader, "time",
        (gfloat) v->running_time / GST_SECOND);

    gl->DrawElements (GL_TRIANGLE_STRIP, 5, GL_UNSIGNED_SHORT, 0);

    gst_gl_context_clear_shader (v->context);
  }
}

static void
gst_gl_test_src_unicolor (GstGLTestSrc * v, GstBuffer * buffer, int w,
    int h, const struct vts_color_struct *color)
{
#if GST_GL_HAVE_OPENGL
  if (gst_gl_context_get_gl_api (v->context)) {
    glClearColor (color->R, color->G, color->B, 1.0f);
    glClear (GL_COLOR_BUFFER_BIT);
  }
#endif
}

void
gst_gl_test_src_black (GstGLTestSrc * v, GstBuffer * buffer, int w, int h)
{
  gst_gl_test_src_unicolor (v, buffer, w, h, vts_colors + COLOR_BLACK);
}

void
gst_gl_test_src_white (GstGLTestSrc * v, GstBuffer * buffer, int w, int h)
{
  gst_gl_test_src_unicolor (v, buffer, w, h, vts_colors + COLOR_WHITE);
}

void
gst_gl_test_src_red (GstGLTestSrc * v, GstBuffer * buffer, int w, int h)
{
  gst_gl_test_src_unicolor (v, buffer, w, h, vts_colors + COLOR_RED);
}

void
gst_gl_test_src_green (GstGLTestSrc * v, GstBuffer * buffer, int w, int h)
{
  gst_gl_test_src_unicolor (v, buffer, w, h, vts_colors + COLOR_GREEN);
}

void
gst_gl_test_src_blue (GstGLTestSrc * v, GstBuffer * buffer, int w, int h)
{
  gst_gl_test_src_unicolor (v, buffer, w, h, vts_colors + COLOR_BLUE);
}

static void
gst_gl_test_src_checkers (GstGLTestSrc * v, gint checker_width, int w, int h)
{
  GstGLFuncs *gl = v->context->gl_vtable;

  GLuint vertex_array;
  GLuint index_buffer;
  GLuint uv_buffer;
  GLuint attr_uv_loc = gst_gl_shader_get_attribute_location (v->shader, "uv");

  /* *INDENT-OFF* */
  GLfloat pixel_coords[] = {
       0, h,
       w, h,
       w, 0,
       0, 0,
  };
  /* *INDENT-ON* */

  if (gst_gl_context_get_gl_api (v->context)) {
    gst_gl_context_clear_shader (v->context);
    gl->BindTexture (GL_TEXTURE_2D, 0);

    gst_gl_shader_use (v->shader);

    // index
    glGenBuffers (1, &index_buffer);
    glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, index_buffer);
    glBufferData (GL_ELEMENT_ARRAY_BUFFER, 5 * sizeof (GLuint), indices,
        GL_STATIC_DRAW);

    gst_gl_test_src_position_buffer (v, v->shader, &vertex_array, index_buffer,
        positions_fullscreen);

    /* upload uv buffer */
    gl->GenBuffers (1, &uv_buffer);
    gl->BindBuffer (GL_ARRAY_BUFFER, uv_buffer);
    gl->VertexAttribPointer (attr_uv_loc, 2, GL_FLOAT, GL_FALSE, 0, 0);
    gl->EnableVertexAttribArray (attr_uv_loc);
    gl->BufferData (GL_ARRAY_BUFFER, 8 * sizeof (GLfloat), pixel_coords,
        GL_STATIC_DRAW);

    gst_gl_shader_set_uniform_1f (v->shader, "checker_width", checker_width);

    gl->DrawElements (GL_TRIANGLE_STRIP, 5, GL_UNSIGNED_SHORT, 0);

    gst_gl_context_clear_shader (v->context);
  }
}


void
gst_gl_test_src_checkers1 (GstGLTestSrc * v, GstBuffer * buffer, int w, int h)
{
  gst_gl_test_src_checkers (v, 1, w, h);
}


void
gst_gl_test_src_checkers2 (GstGLTestSrc * v, GstBuffer * buffer, int w, int h)
{
  gst_gl_test_src_checkers (v, 2, w, h);
}

void
gst_gl_test_src_checkers4 (GstGLTestSrc * v, GstBuffer * buffer, int w, int h)
{
  gst_gl_test_src_checkers (v, 4, w, h);

}

void
gst_gl_test_src_checkers8 (GstGLTestSrc * v, GstBuffer * buffer, int w, int h)
{
  gst_gl_test_src_checkers (v, 8, w, h);
}

void
gst_gl_test_src_circular (GstGLTestSrc * v, GstBuffer * buffer, int w, int h)
{
#if 0
  int i;
  int j;
  paintinfo pi = { NULL, };
  paintinfo *p = &pi;
  struct fourcc_list_struct *fourcc;
  struct vts_color_struct color;
  static uint8_t sine_array[256];
  static int sine_array_inited = FALSE;
  double freq[8];

#ifdef SCALE_AMPLITUDE
  double ampl[8];
#endif
  int d;

  if (!sine_array_inited) {
    for (i = 0; i < 256; i++) {
      sine_array[i] =
          floor (255 * (0.5 + 0.5 * sin (i * 2 * M_PI / 256)) + 0.5);
    }
    sine_array_inited = TRUE;
  }

  p->width = w;
  p->height = h;
  fourcc = v->fourcc;
  if (fourcc == NULL)
    return;

  fourcc->paint_setup (p, dest);
  p->paint_hline = fourcc->paint_hline;

  color = vts_colors[COLOR_BLACK];
  p->color = &color;

  for (i = 1; i < 8; i++) {
    freq[i] = 200 * pow (2.0, -(i - 1) / 4.0);
#ifdef SCALE_AMPLITUDE
    {
      double x;

      x = 2 * M_PI * freq[i] / w;
      ampl[i] = sin (x) / x;
    }
#endif
  }

  for (i = 0; i < w; i++) {
    for (j = 0; j < h; j++) {
      double dist;
      int seg;

      dist =
          sqrt ((2 * i - w) * (2 * i - w) + (2 * j - h) * (2 * j -
              h)) / (2 * w);
      seg = floor (dist * 16);
      if (seg == 0 || seg >= 8) {
        color.Y = 255;
      } else {
#ifdef SCALE_AMPLITUDE
        double a;
#endif
        d = floor (256 * dist * freq[seg] + 0.5);
#ifdef SCALE_AMPLITUDE
        a = ampl[seg];
        if (a < 0)
          a = 0;
        color.Y = 128 + a * (sine_array[d & 0xff] - 128);
#else
        color.Y = sine_array[d & 0xff];
#endif
      }
      color.R = color.Y;
      color.G = color.Y;
      color.B = color.Y;
      p->paint_hline (p, i, j, 1);
    }
  }
#endif
}
