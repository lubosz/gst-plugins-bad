/*
 * GStreamer
 * Copyright (C) 2014 Lubosz Sarnecki <lubosz@gmail.com>
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

/**
 * SECTION:element-gltransformation
 *
 * Transform video on the GPU.
 *
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch -v videotestsrc ! glupload ! GLTransformation ! glimagesink
 * ]| A pipeline to mpa textures on the 6 cube faces..
 * FBO is required.
 * |[
 * gst-launch -v videotestsrc ! glupload ! GLTransformation ! video/x-raw-gl, width=640, height=480 ! glimagesink
 * ]| Resize scene after drawing the cube.
 * The scene size is greater than the input video size.
  |[
 * gst-launch -v videotestsrc ! glupload ! video/x-raw-gl, width=640, height=480  ! GLTransformation ! glimagesink
 * ]| Resize scene before drawing the cube.
 * The scene size is greater than the input video size.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gl/gstglapi.h>
#include "gstgltransformation.h"

#define GST_CAT_DEFAULT gst_gl_transformation_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

enum
{
  PROP_0,
  PROP_RED,
  PROP_GREEN,
  PROP_BLUE,
  PROP_FOVY,
  PROP_ASPECT,
  PROP_ZNEAR,
  PROP_ZFAR,
  PROP_XTRANSLATION,
  PROP_YTRANSLATION,
  PROP_ZTRANSLATION,
  PROP_XROTATION,
  PROP_YROTATION,
  PROP_ZROTATION,
  PROP_XSCALE,
  PROP_YSCALE
};

#define DEBUG_INIT \
    GST_DEBUG_CATEGORY_INIT (gst_gl_transformation_debug, "GLTransformation", 0, "GLTransformation element");

G_DEFINE_TYPE_WITH_CODE (GstGLTransformation, gst_gl_transformation,
    GST_TYPE_GL_FILTER, DEBUG_INIT);

static void gst_gl_transformation_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_transformation_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_gl_transformation_set_caps (GstGLFilter * filter,
    GstCaps * incaps, GstCaps * outcaps);

static void gst_gl_transformation_reset (GstGLFilter * filter);
static gboolean gst_gl_transformation_init_shader (GstGLFilter * filter);
static void _callback_gles2 (gint width, gint height, guint texture,
    gpointer stuff);


static gboolean gst_gl_transformation_filter_texture (GstGLFilter * filter,
    guint in_tex, guint out_tex);

/* vertex source */
static const gchar *cube_v_src =
    "attribute vec4 a_position;                                   \n"
    "attribute vec2 a_texCoord;                                   \n"
    "uniform mat4 u_matrix;                                       \n"
    "uniform float xrot_degree, yrot_degree, zrot_degree;         \n"
    "uniform vec4 translation;         \n"
    "varying vec2 v_texCoord;                                     \n"
    "void main()                                                  \n"
    "{                                                            \n"
    "   float PI = 3.14159265;                                    \n"
    "   float xrot = xrot_degree*2.0*PI/360.0;                    \n"
    "   float yrot = yrot_degree*2.0*PI/360.0;                    \n"
    "   float zrot = zrot_degree*2.0*PI/360.0;                    \n"
    "   mat4 matX = mat4 (                                        \n"
    "            1.0,        0.0,        0.0, 0.0,                \n"
    "            0.0,  cos(xrot),  sin(xrot), 0.0,                \n"
    "            0.0, -sin(xrot),  cos(xrot), 0.0,                \n"
    "            0.0,        0.0,        0.0, 1.0 );              \n"
    "   mat4 matY = mat4 (                                        \n"
    "      cos(yrot),        0.0, -sin(yrot), 0.0,                \n"
    "            0.0,        1.0,        0.0, 0.0,                \n"
    "      sin(yrot),        0.0,  cos(yrot), 0.0,                \n"
    "            0.0,        0.0,       0.0,  1.0 );              \n"
    "   mat4 matZ = mat4 (                                        \n"
    "      cos(zrot),  sin(zrot),        0.0, 0.0,                \n"
    "     -sin(zrot),  cos(zrot),        0.0, 0.0,                \n"
    "            0.0,        0.0,        1.0, 0.0,                \n"
    "            0.0,        0.0,        0.0, 1.0 );              \n"
    "   gl_Position = u_matrix * matZ * matY * matX * (a_position + translation); \n"
    "   v_texCoord = a_texCoord;                                  \n"
    "}                                                            \n";

/* fragment source */
static const gchar *cube_f_src =
//    "precision mediump float;                            \n"
    "varying vec2 v_texCoord;                            \n"
    "uniform sampler2D s_texture;                        \n"
    "void main()                                         \n"
    "{                                                   \n"
    "  gl_FragColor = texture2D( s_texture, v_texCoord );\n"
    "}                                                   \n";

static void
gst_gl_transformation_class_init (GstGLTransformationClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  gobject_class = (GObjectClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_gl_transformation_set_property;
  gobject_class->get_property = gst_gl_transformation_get_property;


  GST_GL_FILTER_CLASS (klass)->onInitFBO = gst_gl_transformation_init_shader;
  GST_GL_FILTER_CLASS (klass)->onReset = gst_gl_transformation_reset;
  GST_GL_FILTER_CLASS (klass)->set_caps = gst_gl_transformation_set_caps;
  GST_GL_FILTER_CLASS (klass)->filter_texture =
      gst_gl_transformation_filter_texture;

  g_object_class_install_property (gobject_class, PROP_RED,
      g_param_spec_float ("red", "Red", "Background red color",
          0.0f, 1.0f, 0.0f, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_GREEN,
      g_param_spec_float ("green", "Green", "Background reen color",
          0.0f, 1.0f, 0.0f, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_BLUE,
      g_param_spec_float ("blue", "Blue", "Background blue color",
          0.0f, 1.0f, 0.0f, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_FOVY,
      g_param_spec_double ("fovy", "Fovy", "Field of view angle in degrees",
          0.0, 180.0, 45.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ASPECT,
      g_param_spec_double ("aspect", "Aspect",
          "Field of view in the x direction", 0.0, 100, 0.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ZNEAR,
      g_param_spec_double ("znear", "Znear",
          "Specifies the distance from the viewer to the near clipping plane",
          0.0, 100.0, 0.1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ZFAR,
      g_param_spec_double ("zfar", "Zfar",
          "Specifies the distance from the viewer to the far clipping plane",
          0.0, 1000.0, 100.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  // Rotation
  g_object_class_install_property (gobject_class, PROP_XROTATION,
      g_param_spec_float ("xrotation", "X Rotation",
          "Rotates the video around the X-Axis in degrees.",
          0.0, 360.0, 0.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_YROTATION,
      g_param_spec_float ("yrotation", "Y Rotation",
          "Rotates the video around the Y-Axis in degrees.",
          0.0, 360.0, 0.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ZROTATION,
      g_param_spec_float ("zrotation", "Z Rotation",
          "Rotates the video around the Z-Axis in degrees.",
          0.0, 360.0, 0.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  // Translation
  g_object_class_install_property (gobject_class, PROP_XTRANSLATION,
      g_param_spec_float ("xtranslation", "X Translation",
          "Translates the video at the X-Axis in percent.",
          -100.0, 100.0, 0.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_YTRANSLATION,
      g_param_spec_float ("ytranslation", "Y Translation",
          "Translates the video at the Y-Axis in percent.",
          -100.0, 100.0, 0.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ZTRANSLATION,
      g_param_spec_float ("ztranslation", "Z Translation",
          "Translates the video at the Z-Axis in percent.",
          -100.0, 100.0, 0.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  // Scale
  g_object_class_install_property (gobject_class, PROP_XSCALE,
      g_param_spec_float ("xscale", "X Scale",
          "Scales the video at the X-Axis in times.",
          0.0, 100.0, 1.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_YSCALE,
      g_param_spec_float ("yscale", "Y Scale",
          "Scales the video at the Y-Axis in times.",
          0.0, 100.0, 1.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_metadata (element_class, "OpenGL transformation filter",
      "Filter/Effect/Video", "Transform video on the GPU",
      "Lubosz Sarnecki <lubosz@gmail.com>");
}

static void
gst_gl_transformation_init (GstGLTransformation * filter)
{
  filter->shader = NULL;
  filter->fovy = 45;
  filter->aspect = 0;
  filter->znear = 0.1;
  filter->zfar = 100;
}

static void
gst_gl_transformation_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLTransformation *filter = GST_GL_TRANSFORMATION (object);

  switch (prop_id) {
    case PROP_RED:
      filter->red = g_value_get_float (value);
      break;
    case PROP_GREEN:
      filter->green = g_value_get_float (value);
      break;
    case PROP_BLUE:
      filter->blue = g_value_get_float (value);
      break;
    case PROP_FOVY:
      filter->fovy = g_value_get_double (value);
      break;
    case PROP_ASPECT:
      filter->aspect = g_value_get_double (value);
      break;
    case PROP_ZNEAR:
      filter->znear = g_value_get_double (value);
      break;
    case PROP_ZFAR:
      filter->zfar = g_value_get_double (value);
      break;
    case PROP_XTRANSLATION:
      filter->xtranslation = g_value_get_float (value);
      break;
    case PROP_YTRANSLATION:
      filter->ytranslation = g_value_get_float (value);
      break;
    case PROP_ZTRANSLATION:
      filter->ztranslation = g_value_get_float (value);
      break;
    case PROP_XROTATION:
      filter->xrotation = g_value_get_float (value);
      break;
    case PROP_YROTATION:
      filter->yrotation = g_value_get_float (value);
      break;
    case PROP_ZROTATION:
      filter->zrotation = g_value_get_float (value);
      break;
    case PROP_XSCALE:
      filter->xscale = g_value_get_float (value);
      break;
    case PROP_YSCALE:
      filter->yscale = g_value_get_float (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_transformation_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGLTransformation *filter = GST_GL_TRANSFORMATION (object);

  switch (prop_id) {
    case PROP_RED:
      g_value_set_float (value, filter->red);
      break;
    case PROP_GREEN:
      g_value_set_float (value, filter->green);
      break;
    case PROP_BLUE:
      g_value_set_float (value, filter->blue);
      break;
    case PROP_FOVY:
      g_value_set_double (value, filter->fovy);
      break;
    case PROP_ASPECT:
      g_value_set_double (value, filter->aspect);
      break;
    case PROP_ZNEAR:
      g_value_set_double (value, filter->znear);
      break;
    case PROP_ZFAR:
      g_value_set_double (value, filter->zfar);
      break;
    case PROP_XTRANSLATION:
      g_value_set_float (value, filter->xtranslation);
      break;
    case PROP_YTRANSLATION:
      g_value_set_float (value, filter->ytranslation);
      break;
    case PROP_ZTRANSLATION:
      g_value_set_float (value, filter->ztranslation);
      break;
    case PROP_XROTATION:
      g_value_set_float (value, filter->xrotation);
      break;
    case PROP_YROTATION:
      g_value_set_float (value, filter->yrotation);
      break;
    case PROP_ZROTATION:
      g_value_set_float (value, filter->zrotation);
      break;
    case PROP_XSCALE:
      g_value_set_float (value, filter->xscale);
      break;
    case PROP_YSCALE:
      g_value_set_float (value, filter->yscale);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_gl_transformation_set_caps (GstGLFilter * filter, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstGLTransformation *cube_filter = GST_GL_TRANSFORMATION (filter);

  if (cube_filter->aspect == 0)
    cube_filter->aspect = (gdouble) GST_VIDEO_INFO_WIDTH (&filter->out_info) /
        (gdouble) GST_VIDEO_INFO_HEIGHT (&filter->out_info);

  return TRUE;
}

static void
gst_gl_transformation_reset (GstGLFilter * filter)
{
  GstGLTransformation *cube_filter = GST_GL_TRANSFORMATION (filter);

  /* blocking call, wait the opengl thread has destroyed the shader */
  if (cube_filter->shader)
    gst_gl_context_del_shader (filter->context, cube_filter->shader);
  cube_filter->shader = NULL;
}

static gboolean
gst_gl_transformation_init_shader (GstGLFilter * filter)
{
  GstGLTransformation *cube_filter = GST_GL_TRANSFORMATION (filter);

  if (gst_gl_context_get_gl_api (filter->context)) {
    /* blocking call, wait the opengl thread has compiled the shader */
    return gst_gl_context_gen_shader (filter->context, cube_v_src, cube_f_src,
        &cube_filter->shader);
  }
  return TRUE;
}

static gboolean
gst_gl_transformation_filter_texture (GstGLFilter * filter, guint in_tex,
    guint out_tex)
{
  GstGLTransformation *cube_filter = GST_GL_TRANSFORMATION (filter);
  GLCB cb = NULL;
  GstGLAPI api;

  api = gst_gl_context_get_gl_api (GST_GL_FILTER (cube_filter)->context);

  if (api)
    cb = _callback_gles2;

  /* blocking call, use a FBO */
  gst_gl_context_use_fbo (filter->context,
      GST_VIDEO_INFO_WIDTH (&filter->out_info),
      GST_VIDEO_INFO_HEIGHT (&filter->out_info),
      filter->fbo, filter->depthbuffer, out_tex,
      cb,
      GST_VIDEO_INFO_WIDTH (&filter->in_info),
      GST_VIDEO_INFO_HEIGHT (&filter->in_info),
      in_tex, cube_filter->fovy, cube_filter->aspect,
      cube_filter->znear, cube_filter->zfar,
      GST_GL_DISPLAY_PROJECTION_PERSPECTIVE, (gpointer) cube_filter);

  return TRUE;
}

static void
_callback_gles2 (gint width, gint height, guint texture, gpointer stuff)
{
  GstGLFilter *filter = GST_GL_FILTER (stuff);
  GstGLTransformation *cube_filter = GST_GL_TRANSFORMATION (filter);
  GstGLFuncs *gl = filter->context->gl_vtable;

/* *INDENT-OFF* */
  const GLfloat v_vertices[] = {
    /* front face */
     1.0,  1.0, -1.0, 1.0, 0.0,
     1.0, -1.0, -1.0, 1.0, 1.0,
    -1.0, -1.0, -1.0, 0.0, 1.0,
    -1.0,  1.0, -1.0, 0.0, 0.0,
  };
/* *INDENT-ON* */

  GLushort indices[] = {
    0, 1, 2,
    0, 2, 3
  };

  GLint attr_position_loc = 0;
  GLint attr_texture_loc = 0;

  const GLfloat matrix[] = {
    0.5f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.5f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.5f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f
  };

  gl->Enable (GL_DEPTH_TEST);

  gl->ClearColor (cube_filter->red, cube_filter->green, cube_filter->blue, 0.0);
  gl->Clear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  gst_gl_shader_use (cube_filter->shader);

  attr_position_loc =
      gst_gl_shader_get_attribute_location (cube_filter->shader, "a_position");
  attr_texture_loc =
      gst_gl_shader_get_attribute_location (cube_filter->shader, "a_texCoord");

  /* Load the vertex position */
  gl->VertexAttribPointer (attr_position_loc, 3, GL_FLOAT,
      GL_FALSE, 5 * sizeof (GLfloat), v_vertices);

  /* Load the texture coordinate */
  gl->VertexAttribPointer (attr_texture_loc, 2, GL_FLOAT,
      GL_FALSE, 5 * sizeof (GLfloat), &v_vertices[3]);

  gl->EnableVertexAttribArray (attr_position_loc);
  gl->EnableVertexAttribArray (attr_texture_loc);

  gl->ActiveTexture (GL_TEXTURE0);
  gl->BindTexture (GL_TEXTURE_2D, texture);
  gst_gl_shader_set_uniform_1i (cube_filter->shader, "s_texture", 0);
  gst_gl_shader_set_uniform_1f (cube_filter->shader, "xrot_degree",
      cube_filter->xrotation);
  gst_gl_shader_set_uniform_1f (cube_filter->shader, "yrot_degree",
      cube_filter->yrotation);
  gst_gl_shader_set_uniform_1f (cube_filter->shader, "zrot_degree",
      cube_filter->zrotation);
  gst_gl_shader_set_uniform_matrix_4fv (cube_filter->shader, "u_matrix", 1,
      GL_FALSE, matrix);


  gst_gl_shader_set_uniform_4f (cube_filter->shader, "translation",
      cube_filter->xtranslation,
      cube_filter->ytranslation, cube_filter->ztranslation, 0);

  gl->DrawElements (GL_TRIANGLES, 36, GL_UNSIGNED_SHORT, indices);

  gl->DisableVertexAttribArray (attr_position_loc);
  gl->DisableVertexAttribArray (attr_texture_loc);

  gl->Disable (GL_DEPTH_TEST);
}
