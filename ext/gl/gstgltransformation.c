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
 * Transforms video on the GPU.
 *
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch gltestsrc ! gltransformation xrotate=45 ! glimagesink
 * ]| A pipeline to rotate by 45 degrees
 * |[
 * gst-launch gltestsrc ! gltransformation xtranslate=4 ! video/x-raw, width=640, height=480 ! glimagesink
 * ]| Resize scene after drawing.
 * The scene size is greater than the input video size.
  |[
 * gst-launch gltestsrc ! video/x-raw, width=1280, height=720 ! gltransformation xscale=1.5 ! glimagesink
 * ]| Resize scene before drawing the cube.
 * The scene size is greater than the input video size.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gl/gstglapi.h>
#include "gstgltransformation.h"

#include <graphene-1.0/graphene.h>

#define GST_CAT_DEFAULT gst_gl_transformation_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

enum
{
  PROP_0,
  PROP_FOVY,
  PROP_ORTHO,
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
static void gst_gl_transformation_callback (gpointer stuff);


static gboolean gst_gl_transformation_filter_texture (GstGLFilter * filter,
    guint in_tex, guint out_tex);

/* vertex source */
static const gchar *cube_v_src =
    "attribute vec4 position;                     \n"
    "attribute vec2 uv;                           \n"
    "uniform mat4 mvp;                            \n"
    "varying vec2 out_uv;                         \n"
    "void main()                                  \n"
    "{                                            \n"
    "   gl_Position = mvp * position;             \n"
    "   out_uv = uv;                              \n"
    "}                                            \n";

/* fragment source */
static const gchar *cube_f_src =
    "varying vec2 out_uv;                         \n"
    "uniform sampler2D texture;                   \n"
    "void main()                                  \n"
    "{                                            \n"
    "  gl_FragColor = texture2D (texture, out_uv);\n"
    "}                                            \n";

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

  g_object_class_install_property (gobject_class, PROP_FOVY,
      g_param_spec_double ("fovy", "Fovy", "Field of view angle in degrees",
          0.0, G_MAXDOUBLE, 90.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ORTHO,
      g_param_spec_boolean ("ortho", "Orthographic",
          "Use orthographic projection", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  // Rotation
  g_object_class_install_property (gobject_class, PROP_XROTATION,
      g_param_spec_float ("xrotation", "X Rotation",
          "Rotates the video around the X-Axis in degrees.",
          -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_YROTATION,
      g_param_spec_float ("yrotation", "Y Rotation",
          "Rotates the video around the Y-Axis in degrees.",
          -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ZROTATION,
      g_param_spec_float ("zrotation", "Z Rotation",
          "Rotates the video around the Z-Axis in degrees.",
          -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  // Translation
  g_object_class_install_property (gobject_class, PROP_XTRANSLATION,
      g_param_spec_float ("xtranslation", "X Translation",
          "Translates the video at the X-Axis.",
          -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_YTRANSLATION,
      g_param_spec_float ("ytranslation", "Y Translation",
          "Translates the video at the Y-Axis.",
          -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ZTRANSLATION,
      g_param_spec_float ("ztranslation", "Z Translation",
          "Translates the video at the Z-Axis.",
          -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  // Scale
  g_object_class_install_property (gobject_class, PROP_XSCALE,
      g_param_spec_float ("xscale", "X Scale",
          "Scales the video at the X-Axis in times.",
          0.0, G_MAXDOUBLE, 1.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_YSCALE,
      g_param_spec_float ("yscale", "Y Scale",
          "Scales the video at the Y-Axis in times.",
          0.0, G_MAXDOUBLE, 1.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_metadata (element_class, "OpenGL transformation filter",
      "Filter/Effect/Video", "Transform video on the GPU",
      "Lubosz Sarnecki <lubosz@gmail.com>");
}

static void
gst_gl_transformation_init (GstGLTransformation * filter)
{
  filter->shader = NULL;
  filter->fovy = 90;
  filter->aspect = 0;
  filter->znear = 0.1;
  filter->zfar = 100;

  filter->xscale = 1.0;
  filter->yscale = 1.0;

  filter->in_tex = 0;
}

static void
gst_gl_transformation_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLTransformation *filter = GST_GL_TRANSFORMATION (object);

  switch (prop_id) {
    case PROP_FOVY:
      filter->fovy = g_value_get_double (value);
      break;
    case PROP_ORTHO:
      filter->ortho = g_value_get_boolean (value);
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
    case PROP_FOVY:
      g_value_set_double (value, filter->fovy);
      break;
    case PROP_ORTHO:
      g_value_set_boolean (value, filter->ortho);
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
  GstGLTransformation *transformation = GST_GL_TRANSFORMATION (filter);

  if (transformation->aspect == 0)
    transformation->aspect =
        (gdouble) GST_VIDEO_INFO_WIDTH (&filter->out_info) /
        (gdouble) GST_VIDEO_INFO_HEIGHT (&filter->out_info);

  return TRUE;
}

static void
gst_gl_transformation_reset (GstGLFilter * filter)
{
  GstGLTransformation *transformation = GST_GL_TRANSFORMATION (filter);

  /* blocking call, wait the opengl thread has destroyed the shader */
  if (transformation->shader)
    gst_gl_context_del_shader (filter->context, transformation->shader);
  transformation->shader = NULL;
}

static gboolean
gst_gl_transformation_init_shader (GstGLFilter * filter)
{
  GstGLTransformation *transformation = GST_GL_TRANSFORMATION (filter);

  if (gst_gl_context_get_gl_api (filter->context)) {
    /* blocking call, wait the opengl thread has compiled the shader */
    return gst_gl_context_gen_shader (filter->context, cube_v_src, cube_f_src,
        &transformation->shader);
  }
  return TRUE;
}

static gboolean
gst_gl_transformation_filter_texture (GstGLFilter * filter, guint in_tex,
    guint out_tex)
{
  GstGLTransformation *transformation = GST_GL_TRANSFORMATION (filter);

  transformation->in_tex = in_tex;

  /* blocking call, use a FBO */
  gst_gl_context_use_fbo_v2 (filter->context,
      GST_VIDEO_INFO_WIDTH (&filter->out_info),
      GST_VIDEO_INFO_HEIGHT (&filter->out_info),
      filter->fbo, filter->depthbuffer,
      out_tex, gst_gl_transformation_callback, (gpointer) transformation);

  return TRUE;
}

static void
gst_gl_transformation_callback (gpointer stuff)
{
  GstGLFilter *filter = GST_GL_FILTER (stuff);
  GstGLTransformation *transformation = GST_GL_TRANSFORMATION (filter);
  GstGLFuncs *gl = filter->context->gl_vtable;

/* *INDENT-OFF* */

  const GLfloat positions[] = {
     -1.0 * transformation->aspect,  1.0,  0.0, 1.0,
      1.0 * transformation->aspect,  1.0,  0.0, 1.0,
      1.0 * transformation->aspect, -1.0,  0.0, 1.0,
     -1.0 * transformation->aspect, -1.0,  0.0, 1.0,
  };

  const GLfloat texture_coordinates[] = {
     0.0,  1.0, 
     1.0,  1.0,
     1.0,  0.0,
     0.0,  0.0, 
  };

/* *INDENT-ON* */

  GLushort indices[] = { 0, 1, 2, 3, 0 };

  GLint attr_position_loc = 0;
  GLint attr_texture_loc = 0;

  GLfloat temp_matrix[16];

  graphene_point3d_t translation_vector =
      GRAPHENE_POINT3D_INIT (transformation->xtranslation,
      transformation->ytranslation,
      transformation->ztranslation);

  graphene_matrix_t model_matrix;
  graphene_matrix_t projection_matrix;
  graphene_matrix_t view_matrix;
  graphene_matrix_t mvp_matrix;
  graphene_matrix_t vp_matrix;

  graphene_vec3_t eye;
  graphene_vec3_t center;
  graphene_vec3_t up;

  graphene_vec3_init (&eye, 0.f, 0.f, 1.f);
  graphene_vec3_init (&center, 0.f, 0.f, 0.f);
  graphene_vec3_init (&up, 0.f, 1.f, 0.f);

  graphene_matrix_init_rotate (&model_matrix,
      transformation->xrotation, graphene_vec3_x_axis ());
  graphene_matrix_rotate (&model_matrix,
      transformation->yrotation, graphene_vec3_y_axis ());
  graphene_matrix_rotate (&model_matrix,
      transformation->zrotation, graphene_vec3_z_axis ());
  graphene_matrix_scale (&model_matrix,
      transformation->xscale, transformation->yscale, 1.0f);
  graphene_matrix_translate (&model_matrix, &translation_vector);

  if (transformation->ortho) {
    graphene_matrix_init_ortho (&projection_matrix,
        -1 * transformation->aspect, 1 * transformation->aspect,
        -1, 1, transformation->znear, transformation->zfar);
  } else {
    graphene_matrix_init_perspective (&projection_matrix,
        transformation->fovy,
        transformation->aspect, transformation->znear, transformation->zfar);

  }

  graphene_matrix_init_look_at (&view_matrix, &eye, &center, &up);

  graphene_matrix_multiply (&projection_matrix, &view_matrix, &vp_matrix);
  graphene_matrix_multiply (&vp_matrix, &model_matrix, &mvp_matrix);

  gst_gl_context_clear_shader (filter->context);
  gl->BindTexture (GL_TEXTURE_2D, 0);
  gl->Disable (GL_TEXTURE_2D);

  gl->Enable (GL_DEPTH_TEST);

  gl->ClearColor (0.f, 0.f, 0.f, 0.f);
  gl->Clear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  gst_gl_shader_use (transformation->shader);

  attr_position_loc =
      gst_gl_shader_get_attribute_location (transformation->shader, "position");

  attr_texture_loc =
      gst_gl_shader_get_attribute_location (transformation->shader, "uv");

  /* Load the vertex position */
  gl->VertexAttribPointer (attr_position_loc, 4, GL_FLOAT,
      GL_FALSE, 0, positions);

  /* Load the texture coordinate */
  gl->VertexAttribPointer (attr_texture_loc, 2, GL_FLOAT,
      GL_FALSE, 0, texture_coordinates);

  gl->EnableVertexAttribArray (attr_position_loc);
  gl->EnableVertexAttribArray (attr_texture_loc);

  gl->ActiveTexture (GL_TEXTURE0);
  gl->BindTexture (GL_TEXTURE_2D, transformation->in_tex);
  gst_gl_shader_set_uniform_1i (transformation->shader, "texture", 0);

  graphene_matrix_to_float (&mvp_matrix, temp_matrix);
  gst_gl_shader_set_uniform_matrix_4fv (transformation->shader, "mvp",
      1, GL_FALSE, temp_matrix);

  gl->DrawElements (GL_TRIANGLE_STRIP, 5, GL_UNSIGNED_SHORT, indices);

  gl->DisableVertexAttribArray (attr_position_loc);
  gl->DisableVertexAttribArray (attr_texture_loc);

  gl->Disable (GL_DEPTH_TEST);

  gst_gl_context_clear_shader (filter->context);
}
