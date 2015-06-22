/*
 * GStreamer
 * Copyright (C) 2015 Lubosz Sarnecki <lubosz@gmail.com>
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
 * SECTION:element-glimagesink
 *
 * glimagesink renders video frames to a drawable on a local or remote
 * display using OpenGL. This element can receive a Window ID from the
 * application through the VideoOverlay interface and will then render video
 * frames in this drawable.
 * If no Window ID was provided by the application, the element will
 * create its own internal window and render into it.
 *
 * See the #GstGLDisplay documentation for a list of environment variables that
 * can override window/platform detection.
 *
 * <refsect2>
 * <title>Scaling</title>
 * <para>
 * Depends on the driver, OpenGL handles hardware accelerated
 * scaling of video frames. This means that the element will just accept
 * incoming video frames no matter their geometry and will then put them to the
 * drawable scaling them on the fly. Using the #GstVRSink:force-aspect-ratio
 * property it is possible to enforce scaling with a constant aspect ratio,
 * which means drawing black borders around the video frame.
 * </para>
 * </refsect2>
 * <refsect2>
 * <title>Events</title>
 * <para>
 * Through the gl thread, glimagesink handle some events coming from the drawable
 * to manage its appearance even when the data is not flowing (GST_STATE_PAUSED).
 * That means that even when the element is paused, it will receive expose events
 * from the drawable and draw the latest frame with correct borders/aspect-ratio.
 * </para>
 * </refsect2>
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch-1.0 -v videotestsrc ! video/x-raw ! glimagesink
 * ]| A pipeline to test hardware scaling.
 * No special opengl extension is used in this pipeline, that's why it should work
 * with OpenGL >= 1.1. That's the case if you are using the MESA3D driver v1.3.
 * |[
 * gst-launch-1.0 -v videotestsrc ! video/x-raw,format=I420 ! glimagesink
 * ]| A pipeline to test hardware scaling and hardware colorspace conversion.
 * When your driver supports GLSL (OpenGL Shading Language needs OpenGL >= 2.1),
 * the 4 following format YUY2, UYVY, I420, YV12 and AYUV are converted to RGB32
 * through some fragment shaders and using one framebuffer (FBO extension OpenGL >= 1.4).
 * If your driver does not support GLSL but supports MESA_YCbCr extension then
 * the you can use YUY2 and UYVY. In this case the colorspace conversion is automatically
 * made when loading the texture and therefore no framebuffer is used.
 * |[
 * gst-launch-1.0 -v gltestsrc ! glimagesink
 * ]| A pipeline 100% OpenGL.
 * No special opengl extension is used in this pipeline, that's why it should work
 * with OpenGL >= 1.1. That's the case if you are using the MESA3D driver v1.3.
 * |[
 * gst-plugins-bas/tests/examples/gl/generic/cube
 * ]| The graphic FPS scene can be greater than the input video FPS.
 * The graphic scene can be written from a client code through the
 * two glfilterapp properties.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvrsink.h"


#if GST_GL_HAVE_PLATFORM_EGL
#include <gst/gl/egl/gsteglimagememory.h>
#endif

#include <gst/gl/gstglviewconvert.h>

GST_DEBUG_CATEGORY (gst_debug_vr_sink);
#define GST_CAT_DEFAULT gst_debug_vr_sink

#define DEFAULT_SHOW_PREROLL_FRAME  TRUE
#define DEFAULT_HANDLE_EVENTS       TRUE
#define DEFAULT_FORCE_ASPECT_RATIO  TRUE
#define DEFAULT_IGNORE_ALPHA        TRUE

#define DEFAULT_MULTIVIEW_MODE GST_VIDEO_MULTIVIEW_MODE_MONO
#define DEFAULT_MULTIVIEW_FLAGS GST_VIDEO_MULTIVIEW_FLAGS_NONE
#define DEFAULT_MULTIVIEW_DOWNMIX GST_GL_STEREO_DOWNMIX_ANAGLYPH_GREEN_MAGENTA_DUBOIS

enum
{
  SIGNAL_BIN_0,
  SIGNAL_BIN_CLIENT_DRAW,
  SIGNAL_BIN_CLIENT_RESHAPE,
  SIGNAL_BIN_LAST,
};

static guint gst_vr_sink_bin_signals[SIGNAL_BIN_LAST] = { 0 };

#define GST_VR_SINK_GET_LOCK(glsink) \
  (GST_VR_SINK(glsink)->drawing_lock)
#define GST_VR_SINK_LOCK(glsink) \
  (g_mutex_lock(&GST_VR_SINK_GET_LOCK (glsink)))
#define GST_VR_SINK_UNLOCK(glsink) \
  (g_mutex_unlock(&GST_VR_SINK_GET_LOCK (glsink)))

#define USING_OPENGL(context) (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL, 1, 0))
#define USING_OPENGL3(context) (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL3, 3, 1))
#define USING_GLES(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES, 1, 0))
#define USING_GLES2(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 2, 0))
#define USING_GLES3(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 3, 0))

#define SUPPORTED_GL_APIS GST_GL_API_OPENGL | GST_GL_API_GLES2 | GST_GL_API_OPENGL3

static void gst_vr_sink_thread_init_redisplay (GstVRSink * vr_sink);
static void gst_vr_sink_cleanup_glthread (GstVRSink * vr_sink);
static void gst_vr_sink_on_close (GstVRSink * vr_sink);
static void gst_vr_sink_on_resize (GstVRSink * vr_sink,
    gint width, gint height);
static void gst_vr_sink_do_resize (GstVRSink * vr_sink,
    gint width, gint height);
static void gst_vr_sink_on_draw (GstVRSink * vr_sink);
static gboolean gst_vr_sink_redisplay (GstVRSink * vr_sink);

static void gst_vr_sink_finalize (GObject * object);
static void gst_vr_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * param_spec);
static void gst_vr_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * param_spec);

static gboolean gst_vr_sink_query (GstBaseSink * bsink, GstQuery * query);
static void gst_vr_sink_set_context (GstElement * element,
    GstContext * context);

static GstStateChangeReturn
gst_vr_sink_change_state (GstElement * element, GstStateChange transition);

static void gst_vr_sink_get_times (GstBaseSink * bsink, GstBuffer * buf,
    GstClockTime * start, GstClockTime * end);
static gboolean gst_vr_sink_set_caps (GstBaseSink * bsink, GstCaps * caps);
static GstCaps *gst_vr_sink_get_caps (GstBaseSink * bsink, GstCaps * filter);
static GstFlowReturn gst_vr_sink_prepare (GstBaseSink * bsink, GstBuffer * buf);
static GstFlowReturn gst_vr_sink_show_frame (GstVideoSink * bsink,
    GstBuffer * buf);
static gboolean gst_vr_sink_propose_allocation (GstBaseSink * bsink,
    GstQuery * query);

static gboolean update_output_format (GstVRSink * vr_sink);

gboolean
gst_vr_sink_shader_compile (GstGLShader * shader,
    GLint * pos_loc, GLint * tex_loc);

static GstStaticPadTemplate gst_vr_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_GL_MEMORY,
            "RGBA"))
    );

enum
{
  ARG_0,
  ARG_DISPLAY,
  PROP_FORCE_ASPECT_RATIO,
  PROP_PIXEL_ASPECT_RATIO,
  PROP_CONTEXT,
  PROP_HANDLE_EVENTS,
  PROP_IGNORE_ALPHA,
  PROP_OUTPUT_MULTIVIEW_LAYOUT,
  PROP_OUTPUT_MULTIVIEW_FLAGS,
  PROP_OUTPUT_MULTIVIEW_DOWNMIX_MODE
};

enum
{
  SIGNAL_0,
  CLIENT_DRAW_SIGNAL,
  CLIENT_RESHAPE_SIGNAL,
  LAST_SIGNAL
};

static guint gst_vr_sink_signals[LAST_SIGNAL] = { 0 };


#define gst_vr_sink_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVRSink, gst_vr_sink,
    GST_TYPE_VIDEO_SINK,
    GST_DEBUG_CATEGORY_INIT (gst_debug_vr_sink, "vrsink", 0,
        "Virtual Reality Video Sink"));

static void
gst_vr_sink_class_init (GstVRSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;
  GstVideoSinkClass *gstvideosink_class;
  GstElementClass *element_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;
  gstvideosink_class = (GstVideoSinkClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_vr_sink_set_property;
  gobject_class->get_property = gst_vr_sink_get_property;

  g_object_class_install_property (gobject_class, PROP_FORCE_ASPECT_RATIO,
      g_param_spec_boolean ("force-aspect-ratio", "Force aspect ratio",
          "When enabled, scaling will respect original aspect ratio",
          DEFAULT_FORCE_ASPECT_RATIO,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PIXEL_ASPECT_RATIO,
      gst_param_spec_fraction ("pixel-aspect-ratio", "Pixel Aspect Ratio",
          "The pixel aspect ratio of the device", 0, 1, G_MAXINT, 1, 1, 1,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CONTEXT,
      g_param_spec_object ("context", "OpenGL context", "Get OpenGL context",
          GST_GL_TYPE_CONTEXT, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_HANDLE_EVENTS,
      g_param_spec_boolean ("handle-events", "Handle XEvents",
          "When enabled, XEvents will be selected and handled",
          DEFAULT_HANDLE_EVENTS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_IGNORE_ALPHA,
      g_param_spec_boolean ("ignore-alpha", "Ignore Alpha",
          "When enabled, alpha will be ignored and converted to black",
          DEFAULT_IGNORE_ALPHA, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_OUTPUT_MULTIVIEW_LAYOUT,
      g_param_spec_enum ("output-multiview-mode",
          "Output Multiview Mode",
          "Choose output mode for multiview/3D video",
          GST_TYPE_VIDEO_MULTIVIEW_MODE, DEFAULT_MULTIVIEW_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_OUTPUT_MULTIVIEW_FLAGS,
      g_param_spec_flags ("output-multiview-flags",
          "Output Multiview Flags",
          "Output multiview layout modifier flags",
          GST_TYPE_VIDEO_MULTIVIEW_FLAGS, DEFAULT_MULTIVIEW_FLAGS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      PROP_OUTPUT_MULTIVIEW_DOWNMIX_MODE,
      g_param_spec_enum ("output-multiview-downmix-mode",
          "Mode for mono downmixed output",
          "Output anaglyph type to generate when downmixing to mono",
          GST_TYPE_GL_STEREO_DOWNMIX_MODE_TYPE, DEFAULT_MULTIVIEW_DOWNMIX,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_metadata (element_class, "Virtual Reality video sink",
      "Sink/Video", "An OpenGL videosink for VR",
      "Lubosz Sarnecki <lubosz@gmail.com>");

  /**
   * GstVRSink::client-draw:
   * @object: the #GstVRSink
   * @texture: the #guint id of the texture.
   * @width: the #guint width of the texture.
   * @height: the #guint height of the texture.
   *
   * Will be emitted before actually drawing the texture.  The client should
   * redraw the surface/contents with the @texture, @width and @height and
   * and return %TRUE.
   *
   * Returns: whether the texture was redrawn by the signal.  If not, a
   *          default redraw will occur.
   */
  gst_vr_sink_signals[CLIENT_DRAW_SIGNAL] =
      g_signal_new ("client-draw", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_BOOLEAN, 2, GST_GL_TYPE_CONTEXT, GST_TYPE_SAMPLE);

  /**
   * GstVRSink::client-reshape:
   * @object: the #GstVRSink
   * @width: the #guint width of the texture.
   * @height: the #guint height of the texture.
   *
   * The client should resize the surface/window/viewport with the @width and
   * @height and return %TRUE.
   *
   * Returns: whether the content area was resized by the signal.  If not, a
   *          default viewport resize will occur.
   */
  gst_vr_sink_signals[CLIENT_RESHAPE_SIGNAL] =
      g_signal_new ("client-reshape", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_BOOLEAN, 3, GST_GL_TYPE_CONTEXT, G_TYPE_UINT, G_TYPE_UINT);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_vr_sink_template));

  gobject_class->finalize = gst_vr_sink_finalize;

  gstelement_class->change_state = gst_vr_sink_change_state;
  gstelement_class->set_context = gst_vr_sink_set_context;
  gstbasesink_class->query = GST_DEBUG_FUNCPTR (gst_vr_sink_query);
  gstbasesink_class->set_caps = gst_vr_sink_set_caps;
  gstbasesink_class->get_caps = gst_vr_sink_get_caps;
  gstbasesink_class->get_times = gst_vr_sink_get_times;
  gstbasesink_class->prepare = gst_vr_sink_prepare;
  gstbasesink_class->propose_allocation = gst_vr_sink_propose_allocation;

  gstvideosink_class->show_frame = GST_DEBUG_FUNCPTR (gst_vr_sink_show_frame);
}

static void
gst_vr_sink_init (GstVRSink * vr_sink)
{
  vr_sink->window_id = 0;
  vr_sink->new_window_id = 0;
  vr_sink->display = NULL;
  vr_sink->keep_aspect_ratio = TRUE;
  vr_sink->par_n = 0;
  vr_sink->par_d = 1;
  vr_sink->redisplay_texture = 0;
  vr_sink->handle_events = TRUE;
  vr_sink->ignore_alpha = TRUE;

  vr_sink->mview_output_mode = DEFAULT_MULTIVIEW_MODE;
  vr_sink->mview_output_flags = DEFAULT_MULTIVIEW_FLAGS;
  vr_sink->mview_downmix_mode = DEFAULT_MULTIVIEW_DOWNMIX;

  g_mutex_init (&vr_sink->drawing_lock);
}

static void
gst_vr_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVRSink *vr_sink;

  g_return_if_fail (GST_IS_VR_SINK (object));

  vr_sink = GST_VR_SINK (object);

  switch (prop_id) {
    case PROP_FORCE_ASPECT_RATIO:
    {
      vr_sink->keep_aspect_ratio = g_value_get_boolean (value);
      break;
    }
    case PROP_PIXEL_ASPECT_RATIO:
    {
      vr_sink->par_n = gst_value_get_fraction_numerator (value);
      vr_sink->par_d = gst_value_get_fraction_denominator (value);
      break;
    }
    case PROP_IGNORE_ALPHA:
      vr_sink->ignore_alpha = g_value_get_boolean (value);
      break;
    case PROP_OUTPUT_MULTIVIEW_LAYOUT:
      GST_VR_SINK_LOCK (vr_sink);
      vr_sink->mview_output_mode = g_value_get_enum (value);
      vr_sink->output_mode_changed = TRUE;
      GST_VR_SINK_UNLOCK (vr_sink);
      break;
    case PROP_OUTPUT_MULTIVIEW_FLAGS:
      GST_VR_SINK_LOCK (vr_sink);
      vr_sink->mview_output_flags = g_value_get_flags (value);
      vr_sink->output_mode_changed = TRUE;
      GST_VR_SINK_UNLOCK (vr_sink);
      break;
    case PROP_OUTPUT_MULTIVIEW_DOWNMIX_MODE:
      GST_VR_SINK_LOCK (vr_sink);
      vr_sink->mview_downmix_mode = g_value_get_enum (value);
      vr_sink->output_mode_changed = TRUE;
      GST_VR_SINK_UNLOCK (vr_sink);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vr_sink_finalize (GObject * object)
{
  GstVRSink *vr_sink;

  g_return_if_fail (GST_IS_VR_SINK (object));

  vr_sink = GST_VR_SINK (object);

  g_mutex_clear (&vr_sink->drawing_lock);

  GST_DEBUG ("finalized");
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_vr_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVRSink *vr_sink;

  g_return_if_fail (GST_IS_VR_SINK (object));

  vr_sink = GST_VR_SINK (object);

  switch (prop_id) {
    case PROP_FORCE_ASPECT_RATIO:
      g_value_set_boolean (value, vr_sink->keep_aspect_ratio);
      break;
    case PROP_PIXEL_ASPECT_RATIO:
      gst_value_set_fraction (value, vr_sink->par_n, vr_sink->par_d);
      break;
    case PROP_CONTEXT:
      g_value_set_object (value, vr_sink->context);
      break;
    case PROP_HANDLE_EVENTS:
      g_value_set_boolean (value, vr_sink->handle_events);
      break;
    case PROP_IGNORE_ALPHA:
      g_value_set_boolean (value, vr_sink->ignore_alpha);
      break;
    case PROP_OUTPUT_MULTIVIEW_LAYOUT:
      g_value_set_enum (value, vr_sink->mview_output_mode);
      break;
    case PROP_OUTPUT_MULTIVIEW_FLAGS:
      g_value_set_flags (value, vr_sink->mview_output_flags);
      break;
    case PROP_OUTPUT_MULTIVIEW_DOWNMIX_MODE:
      g_value_set_enum (value, vr_sink->mview_downmix_mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vr_sink_key_event_cb (GstGLWindow * window, char *event_name, char
    *key_string, GstVRSink * vr_sink)
{
  GST_DEBUG_OBJECT (vr_sink, "glimagesink event %s key %s pressed", event_name,
      key_string);
}

static void
gst_vr_sink_mouse_event_cb (GstGLWindow * window, char *event_name,
    int button, double posx, double posy, GstVRSink * vr_sink)
{
  GST_DEBUG_OBJECT (vr_sink, "glimagesink event %s at %g, %g", event_name, posx,
      posy);
}

static gboolean
_ensure_gl_setup (GstVRSink * vr_sink)
{
  GError *error = NULL;

  GST_TRACE_OBJECT (vr_sink, "Ensuring setup");

  if (!vr_sink->context) {
    GST_OBJECT_LOCK (vr_sink->display);
    do {
      GstGLContext *other_context = NULL;
      GstGLWindow *window = NULL;

      if (vr_sink->context) {
        gst_object_unref (vr_sink->context);
        vr_sink->context = NULL;
      }

      GST_DEBUG_OBJECT (vr_sink,
          "No current context, creating one for %" GST_PTR_FORMAT,
          vr_sink->display);

      if (vr_sink->other_context) {
        other_context = gst_object_ref (vr_sink->other_context);
      } else {
        other_context =
            gst_gl_display_get_gl_context_for_thread (vr_sink->display, NULL);
      }

      if (!gst_gl_display_create_context (vr_sink->display,
              other_context, &vr_sink->context, &error)) {
        if (other_context)
          gst_object_unref (other_context);
        GST_OBJECT_UNLOCK (vr_sink->display);
        goto context_error;
      }

      GST_DEBUG_OBJECT (vr_sink,
          "created context %" GST_PTR_FORMAT " from other context %"
          GST_PTR_FORMAT, vr_sink->context, vr_sink->other_context);

      window = gst_gl_context_get_window (vr_sink->context);

      GST_DEBUG_OBJECT (vr_sink, "got window %" GST_PTR_FORMAT, window);

      GST_DEBUG_OBJECT (vr_sink,
          "window_id : %" G_GUINTPTR_FORMAT " , new_window_id : %"
          G_GUINTPTR_FORMAT, vr_sink->window_id, vr_sink->new_window_id);

      if (vr_sink->window_id != vr_sink->new_window_id) {
        vr_sink->window_id = vr_sink->new_window_id;
        GST_DEBUG_OBJECT (vr_sink, "Setting window handle on gl window");
        gst_gl_window_set_window_handle (window, vr_sink->window_id);
      }

      gst_gl_window_handle_events (window, vr_sink->handle_events);

      /* setup callbacks */
      gst_gl_window_set_resize_callback (window,
          GST_GL_WINDOW_RESIZE_CB (gst_vr_sink_on_resize),
          gst_object_ref (vr_sink), (GDestroyNotify) gst_object_unref);
      gst_gl_window_set_draw_callback (window,
          GST_GL_WINDOW_CB (gst_vr_sink_on_draw),
          gst_object_ref (vr_sink), (GDestroyNotify) gst_object_unref);
      gst_gl_window_set_close_callback (window,
          GST_GL_WINDOW_CB (gst_vr_sink_on_close),
          gst_object_ref (vr_sink), (GDestroyNotify) gst_object_unref);
      vr_sink->key_sig_id = g_signal_connect (window, "key-event", G_CALLBACK
          (gst_vr_sink_key_event_cb), vr_sink);
      vr_sink->mouse_sig_id =
          g_signal_connect (window, "mouse-event",
          G_CALLBACK (gst_vr_sink_mouse_event_cb), vr_sink);

      if (vr_sink->x >= 0 && vr_sink->y >= 0 && vr_sink->width > 0 &&
          vr_sink->height > 0) {
        gst_gl_window_set_render_rectangle (window, vr_sink->x, vr_sink->y,
            vr_sink->width, vr_sink->height);
      }

      if (other_context)
        gst_object_unref (other_context);
      gst_object_unref (window);
    } while (!gst_gl_display_add_context (vr_sink->display, vr_sink->context));
    GST_OBJECT_UNLOCK (vr_sink->display);
  } else
    GST_TRACE_OBJECT (vr_sink, "Already have a context");

  return TRUE;

context_error:
  {
    GST_ELEMENT_ERROR (vr_sink, RESOURCE, NOT_FOUND, ("%s", error->message),
        (NULL));

    if (vr_sink->context) {
      gst_object_unref (vr_sink->context);
      vr_sink->context = NULL;
    }

    if (error) {
      g_error_free (error);
      error = NULL;
    }

    return FALSE;
  }
}

static gboolean
gst_vr_sink_query (GstBaseSink * bsink, GstQuery * query)
{
  GstVRSink *vr_sink = GST_VR_SINK (bsink);
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
    {
      const gchar *context_type;
      GstContext *context, *old_context;

      res = gst_gl_handle_context_query ((GstElement *) vr_sink, query,
          &vr_sink->display, &vr_sink->other_context);
      if (vr_sink->display)
        gst_gl_display_filter_gl_api (vr_sink->display, SUPPORTED_GL_APIS);

      gst_query_parse_context_type (query, &context_type);

      if (g_strcmp0 (context_type, "gst.gl.local_context") == 0) {
        GstStructure *s;

        gst_query_parse_context (query, &old_context);

        if (old_context)
          context = gst_context_copy (old_context);
        else
          context = gst_context_new ("gst.gl.local_context", FALSE);

        s = gst_context_writable_structure (context);
        gst_structure_set (s, "context", GST_GL_TYPE_CONTEXT,
            vr_sink->context, NULL);
        gst_query_set_context (query, context);
        gst_context_unref (context);

        res = vr_sink->context != NULL;
      }
      GST_LOG_OBJECT (vr_sink, "context query of type %s %i", context_type,
          res);

      if (res)
        return res;
      break;
    }
    case GST_QUERY_DRAIN:
    {
      GstBuffer *buf[2];

      GST_VR_SINK_LOCK (vr_sink);
      vr_sink->redisplay_texture = 0;
      buf[0] = vr_sink->stored_buffer[0];
      buf[1] = vr_sink->stored_buffer[1];
      vr_sink->stored_buffer[0] = vr_sink->stored_buffer[1] = NULL;
      GST_VR_SINK_UNLOCK (vr_sink);

      gst_buffer_replace (buf, NULL);
      gst_buffer_replace (buf + 1, NULL);

      gst_buffer_replace (&vr_sink->input_buffer, NULL);
      gst_buffer_replace (&vr_sink->input_buffer2, NULL);
      gst_buffer_replace (&vr_sink->next_buffer, NULL);
      gst_buffer_replace (&vr_sink->next_buffer2, NULL);
      gst_buffer_replace (&vr_sink->next_sync, NULL);

      res = GST_BASE_SINK_CLASS (parent_class)->query (bsink, query);
      break;
    }
    default:
      res = GST_BASE_SINK_CLASS (parent_class)->query (bsink, query);
      break;
  }

  return res;
}

static void
gst_vr_sink_set_context (GstElement * element, GstContext * context)
{
  GstVRSink *vr_sink = GST_VR_SINK (element);

  gst_gl_handle_set_context (element, context, &vr_sink->display,
      &vr_sink->other_context);

  if (vr_sink->display)
    gst_gl_display_filter_gl_api (vr_sink->display, SUPPORTED_GL_APIS);
}

static GstStateChangeReturn
gst_vr_sink_change_state (GstElement * element, GstStateChange transition)
{
  GstVRSink *vr_sink;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  GST_DEBUG ("changing state: %s => %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  vr_sink = GST_VR_SINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_gl_ensure_element_data (vr_sink, &vr_sink->display,
              &vr_sink->other_context))
        return GST_STATE_CHANGE_FAILURE;

      gst_gl_display_filter_gl_api (vr_sink->display, SUPPORTED_GL_APIS);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!_ensure_gl_setup (vr_sink))
        return GST_STATE_CHANGE_FAILURE;

      g_atomic_int_set (&vr_sink->to_quit, 0);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    {
      GstBuffer *buf[2];

      GST_VR_SINK_LOCK (vr_sink);
      /* mark the redisplay_texture as unavailable (=0)
       * to avoid drawing
       */
      vr_sink->redisplay_texture = 0;
      buf[0] = vr_sink->stored_buffer[0];
      buf[1] = vr_sink->stored_buffer[1];
      vr_sink->stored_buffer[0] = vr_sink->stored_buffer[1] = NULL;

      if (vr_sink->stored_sync)
        gst_buffer_unref (vr_sink->stored_sync);
      vr_sink->stored_sync = NULL;

      GST_VR_SINK_UNLOCK (vr_sink);

      gst_buffer_replace (buf, NULL);
      gst_buffer_replace (buf + 1, NULL);

      gst_object_replace ((GstObject **) & vr_sink->convert_views, NULL);
      gst_buffer_replace (&vr_sink->input_buffer, NULL);
      gst_buffer_replace (&vr_sink->input_buffer2, NULL);
      gst_buffer_replace (&vr_sink->next_buffer, NULL);
      gst_buffer_replace (&vr_sink->next_buffer2, NULL);
      gst_buffer_replace (&vr_sink->next_sync, NULL);

      vr_sink->window_id = 0;
      /* but do not reset vr_sink->new_window_id */

      GST_VIDEO_SINK_WIDTH (vr_sink) = 1;
      GST_VIDEO_SINK_HEIGHT (vr_sink) = 1;
      /* Clear cached caps */
      if (vr_sink->out_caps) {
        gst_caps_unref (vr_sink->out_caps);
        vr_sink->out_caps = NULL;
      }

      if (vr_sink->context) {
        GstGLWindow *window = gst_gl_context_get_window (vr_sink->context);

        gst_gl_window_send_message (window,
            GST_GL_WINDOW_CB (gst_vr_sink_cleanup_glthread), vr_sink);

        gst_gl_window_set_resize_callback (window, NULL, NULL, NULL);
        gst_gl_window_set_draw_callback (window, NULL, NULL, NULL);
        gst_gl_window_set_close_callback (window, NULL, NULL, NULL);

        if (vr_sink->key_sig_id)
          g_signal_handler_disconnect (window, vr_sink->key_sig_id);
        vr_sink->key_sig_id = 0;
        if (vr_sink->mouse_sig_id)
          g_signal_handler_disconnect (window, vr_sink->mouse_sig_id);
        vr_sink->mouse_sig_id = 0;

        gst_object_unref (window);
        gst_object_unref (vr_sink->context);
        vr_sink->context = NULL;
      }
      break;
    }
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (vr_sink->other_context) {
        gst_object_unref (vr_sink->other_context);
        vr_sink->other_context = NULL;
      }

      if (vr_sink->display) {
        gst_object_unref (vr_sink->display);
        vr_sink->display = NULL;
      }
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_vr_sink_get_times (GstBaseSink * bsink, GstBuffer * buf,
    GstClockTime * start, GstClockTime * end)
{
  GstVRSink *glimagesink;

  glimagesink = GST_VR_SINK (bsink);

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    *start = GST_BUFFER_TIMESTAMP (buf);
    if (GST_BUFFER_DURATION_IS_VALID (buf))
      *end = *start + GST_BUFFER_DURATION (buf);
    else {
      if (GST_VIDEO_INFO_FPS_N (&glimagesink->out_info) > 0) {
        *end = *start +
            gst_util_uint64_scale_int (GST_SECOND,
            GST_VIDEO_INFO_FPS_D (&glimagesink->out_info),
            GST_VIDEO_INFO_FPS_N (&glimagesink->out_info));
      }
    }
  }
}

static GstCaps *
gst_vr_sink_get_caps (GstBaseSink * bsink, GstCaps * filter)
{
  GstCaps *tmp = NULL;
  GstCaps *result = NULL;

  tmp = gst_pad_get_pad_template_caps (GST_BASE_SINK_PAD (bsink));

  if (filter) {
    GST_DEBUG_OBJECT (bsink, "intersecting with filter caps %" GST_PTR_FORMAT,
        filter);

    result = gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (tmp);
  } else {
    result = tmp;
  }

  GST_DEBUG_OBJECT (bsink, "returning caps: %" GST_PTR_FORMAT, result);

  return result;
}

static gboolean
configure_display_from_info (GstVRSink * vr_sink, GstVideoInfo * vinfo)
{
  gint width;
  gint height;
  gboolean ok;
  gint par_n, par_d;
  gint display_par_n, display_par_d;
  guint display_ratio_num, display_ratio_den;

  width = GST_VIDEO_INFO_WIDTH (vinfo);
  height = GST_VIDEO_INFO_HEIGHT (vinfo);

  par_n = GST_VIDEO_INFO_PAR_N (vinfo);
  par_d = GST_VIDEO_INFO_PAR_D (vinfo);

  if (!par_n)
    par_n = 1;

  /* get display's PAR */
  if (vr_sink->par_n != 0 && vr_sink->par_d != 0) {
    display_par_n = vr_sink->par_n;
    display_par_d = vr_sink->par_d;
  } else {
    display_par_n = 1;
    display_par_d = 1;
  }

  ok = gst_video_calculate_display_ratio (&display_ratio_num,
      &display_ratio_den, width, height, par_n, par_d, display_par_n,
      display_par_d);

  if (!ok)
    return FALSE;

  GST_TRACE ("PAR: %u/%u DAR:%u/%u", par_n, par_d, display_par_n,
      display_par_d);

  if (height % display_ratio_den == 0) {
    GST_DEBUG ("keeping video height");
    GST_VIDEO_SINK_WIDTH (vr_sink) = (guint)
        gst_util_uint64_scale_int (height, display_ratio_num,
        display_ratio_den);
    GST_VIDEO_SINK_HEIGHT (vr_sink) = height;
  } else if (width % display_ratio_num == 0) {
    GST_DEBUG ("keeping video width");
    GST_VIDEO_SINK_WIDTH (vr_sink) = width;
    GST_VIDEO_SINK_HEIGHT (vr_sink) = (guint)
        gst_util_uint64_scale_int (width, display_ratio_den, display_ratio_num);
  } else {
    GST_DEBUG ("approximating while keeping video height");
    GST_VIDEO_SINK_WIDTH (vr_sink) = (guint)
        gst_util_uint64_scale_int (height, display_ratio_num,
        display_ratio_den);
    GST_VIDEO_SINK_HEIGHT (vr_sink) = height;
  }
  GST_DEBUG ("scaling to %dx%d", GST_VIDEO_SINK_WIDTH (vr_sink),
      GST_VIDEO_SINK_HEIGHT (vr_sink));

  return TRUE;
}

/* Called with GST_VR_SINK lock held, to
 * copy in_info to out_info and update out_caps */
static gboolean
update_output_format (GstVRSink * vr_sink)
{
  GstVideoInfo *out_info = &vr_sink->out_info;
  gboolean input_is_mono = FALSE;
  GstVideoMultiviewMode mv_mode;
  gboolean ret;

  *out_info = vr_sink->in_info;

  mv_mode = GST_VIDEO_INFO_MULTIVIEW_MODE (&vr_sink->in_info);

  if (mv_mode == GST_VIDEO_MULTIVIEW_MODE_NONE ||
      mv_mode == GST_VIDEO_MULTIVIEW_MODE_MONO ||
      mv_mode == GST_VIDEO_MULTIVIEW_MODE_LEFT ||
      mv_mode == GST_VIDEO_MULTIVIEW_MODE_RIGHT)
    input_is_mono = TRUE;

  if (input_is_mono == FALSE &&
      vr_sink->mview_output_mode != GST_VIDEO_MULTIVIEW_MODE_NONE) {
    /* Input is multiview, and output wants a conversion - configure 3d converter now,
     * otherwise defer it until either the caps or the 3D output mode changes */
    gst_video_multiview_video_info_change_mode (out_info,
        vr_sink->mview_output_mode, vr_sink->mview_output_flags);

    if (vr_sink->convert_views == NULL) {
      vr_sink->convert_views = gst_gl_view_convert_new ();
      gst_gl_view_convert_set_context (vr_sink->convert_views,
          vr_sink->context);
    }
  } else {
    if (vr_sink->convert_views) {
      gst_object_unref (vr_sink->convert_views);
      vr_sink->convert_views = NULL;
    }
  }

  ret = configure_display_from_info (vr_sink, out_info);

  if (vr_sink->convert_views) {
    /* Match actual output window size for pixel-aligned output,
     * even though we can't necessarily match the starting left/right
     * view parity properly */
    vr_sink->out_info.width = MAX (1, vr_sink->display_rect.w);
    vr_sink->out_info.height = MAX (1, vr_sink->display_rect.h);
    GST_LOG_OBJECT (vr_sink, "Set 3D output scale to %d,%d",
        vr_sink->display_rect.w, vr_sink->display_rect.h);

    GST_VR_SINK_UNLOCK (vr_sink);
    gst_gl_view_convert_set_format (vr_sink->convert_views,
        &vr_sink->in_info, &vr_sink->out_info);
    g_object_set (vr_sink->convert_views, "downmix-mode",
        vr_sink->mview_downmix_mode, NULL);
    GST_VR_SINK_LOCK (vr_sink);
  }

  vr_sink->output_mode_changed = FALSE;
  vr_sink->caps_change = TRUE;

  if (vr_sink->out_caps)
    gst_caps_unref (vr_sink->out_caps);
  vr_sink->out_caps = gst_video_info_to_caps (out_info);

  return ret;
}

static gboolean
gst_vr_sink_set_caps (GstBaseSink * bsink, GstCaps * caps)
{
  GstVRSink *vr_sink;
  gboolean ok;
  GstVideoInfo vinfo;

  GST_DEBUG_OBJECT (bsink, "set caps with %" GST_PTR_FORMAT, caps);

  vr_sink = GST_VR_SINK (bsink);

  ok = gst_video_info_from_caps (&vinfo, caps);
  if (!ok)
    return FALSE;

  if (!_ensure_gl_setup (vr_sink))
    return FALSE;

  GST_VR_SINK_LOCK (vr_sink);
  vr_sink->in_info = vinfo;
  ok = update_output_format (vr_sink);

  GST_VR_SINK_UNLOCK (vr_sink);

  return ok;
}

/* Take the input_buffer and run it through 3D conversion if needed.
 * Called with glimagesink lock, but might drop it temporarily */
static gboolean
prepare_next_buffer (GstVRSink * vr_sink)
{
  GstBuffer *in_buffer, *next_buffer, *old_buffer;
  GstBuffer *in_buffer2 = NULL, *next_buffer2 = NULL, *old_buffer2;
  GstBuffer *next_sync, *old_sync;
  GstGLSyncMeta *sync_meta;
  GstVideoFrame gl_frame;
  GstGLViewConvert *convert_views = NULL;
  GstVideoInfo *info;

  if (vr_sink->input_buffer == NULL)
    return TRUE;                /* No input buffer to process */

  if (GST_VIDEO_INFO_MULTIVIEW_MODE (&vr_sink->in_info) ==
      GST_VIDEO_MULTIVIEW_MODE_FRAME_BY_FRAME) {
    if (vr_sink->input_buffer2 == NULL)
      return TRUE;              /* Need 2nd input buffer to process */
    in_buffer2 = gst_buffer_ref (vr_sink->input_buffer2);
  }

  in_buffer = gst_buffer_ref (vr_sink->input_buffer);
  if (vr_sink->convert_views &&
      (GST_VIDEO_INFO_MULTIVIEW_MODE (&vr_sink->in_info) !=
          GST_VIDEO_INFO_MULTIVIEW_MODE (&vr_sink->out_info) ||
          GST_VIDEO_INFO_MULTIVIEW_FLAGS (&vr_sink->in_info) !=
          GST_VIDEO_INFO_MULTIVIEW_FLAGS (&vr_sink->out_info)))
    convert_views = gst_object_ref (vr_sink->convert_views);

  GST_VR_SINK_UNLOCK (vr_sink);

  if (convert_views) {
    info = &vr_sink->out_info;

    if (gst_gl_view_convert_submit_input_buffer (vr_sink->convert_views,
            GST_BUFFER_IS_DISCONT (in_buffer), in_buffer) != GST_FLOW_OK) {
      gst_buffer_replace (&in_buffer2, NULL);
      goto fail;
    }
    if (in_buffer2) {
      if (gst_gl_view_convert_submit_input_buffer (vr_sink->convert_views,
              GST_BUFFER_IS_DISCONT (in_buffer2), in_buffer2) != GST_FLOW_OK) {
        goto fail;
      }
    }

    if (gst_gl_view_convert_get_output (vr_sink->convert_views,
            &next_buffer) != GST_FLOW_OK)
      goto fail;
    if (GST_VIDEO_INFO_MULTIVIEW_MODE (info) ==
        GST_VIDEO_MULTIVIEW_MODE_FRAME_BY_FRAME) {
      if (gst_gl_view_convert_get_output (vr_sink->convert_views,
              &next_buffer2) != GST_FLOW_OK)
        goto fail;
    }
    gst_object_unref (convert_views);

    if (next_buffer == NULL) {
      /* Not ready to paint a buffer yet */
      GST_VR_SINK_LOCK (vr_sink);
      return TRUE;
    }
  } else {
    next_buffer = in_buffer;
    info = &vr_sink->in_info;
  }
  /* in_buffer invalid now */
  if (!gst_video_frame_map (&gl_frame, info, next_buffer,
          GST_MAP_READ | GST_MAP_GL)) {
    gst_buffer_unref (next_buffer);
    goto fail;
  }

  next_sync = gst_buffer_new ();
  sync_meta = gst_buffer_add_gl_sync_meta (vr_sink->context, next_sync);
  gst_gl_sync_meta_set_sync_point (sync_meta, vr_sink->context);

  GST_VR_SINK_LOCK (vr_sink);
  vr_sink->next_tex = *(guint *) gl_frame.data[0];

  old_buffer = vr_sink->next_buffer;
  vr_sink->next_buffer = next_buffer;
  old_buffer2 = vr_sink->next_buffer2;
  vr_sink->next_buffer2 = next_buffer2;

  old_sync = vr_sink->next_sync;
  vr_sink->next_sync = next_sync;

  /* Need to drop the lock again, to avoid a deadlock if we're
   * dropping the last ref on this buffer and it goes back to our
   * allocator */
  GST_VR_SINK_UNLOCK (vr_sink);

  if (old_buffer)
    gst_buffer_unref (old_buffer);
  if (old_buffer2)
    gst_buffer_unref (old_buffer2);
  if (old_sync)
    gst_buffer_unref (old_sync);
  gst_video_frame_unmap (&gl_frame);

  GST_VR_SINK_LOCK (vr_sink);

  return TRUE;

fail:
  GST_VR_SINK_LOCK (vr_sink);
  return FALSE;
}

static GstFlowReturn
gst_vr_sink_prepare (GstBaseSink * bsink, GstBuffer * buf)
{
  GstVRSink *vr_sink;
  GstBuffer **target;
  GstBuffer *old_input;

  vr_sink = GST_VR_SINK (bsink);

  GST_TRACE ("preparing buffer:%p", buf);

  if (GST_VIDEO_SINK_WIDTH (vr_sink) < 1 || GST_VIDEO_SINK_HEIGHT (vr_sink) < 1) {
    return GST_FLOW_NOT_NEGOTIATED;
  }

  if (!_ensure_gl_setup (vr_sink))
    return GST_FLOW_NOT_NEGOTIATED;

  GST_VR_SINK_LOCK (vr_sink);
  target = &vr_sink->input_buffer;
  if (GST_VIDEO_INFO_MULTIVIEW_MODE (&vr_sink->in_info) ==
      GST_VIDEO_MULTIVIEW_MODE_FRAME_BY_FRAME &&
      !GST_BUFFER_FLAG_IS_SET (buf, GST_VIDEO_BUFFER_FLAG_FIRST_IN_BUNDLE)) {
    target = &vr_sink->input_buffer2;
  }
  old_input = *target;
  *target = gst_buffer_ref (buf);

  if (vr_sink->output_mode_changed)
    update_output_format (vr_sink);

  if (!prepare_next_buffer (vr_sink)) {
    GST_VR_SINK_UNLOCK (vr_sink);
    if (old_input)
      gst_buffer_unref (old_input);
    goto convert_views_failed;
  }
  GST_VR_SINK_UNLOCK (vr_sink);

  if (old_input)
    gst_buffer_unref (old_input);

  if (vr_sink->window_id != vr_sink->new_window_id) {
    GstGLWindow *window = gst_gl_context_get_window (vr_sink->context);

    vr_sink->window_id = vr_sink->new_window_id;
    gst_gl_window_set_window_handle (window, vr_sink->window_id);

    gst_object_unref (window);
  }

  return GST_FLOW_OK;
convert_views_failed:
  {
    GST_ELEMENT_ERROR (vr_sink, RESOURCE, NOT_FOUND,
        ("%s", "Failed to convert multiview video buffer"), (NULL));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_vr_sink_show_frame (GstVideoSink * vsink, GstBuffer * buf)
{
  GstVRSink *vr_sink;

  GST_TRACE ("rendering buffer:%p", buf);

  vr_sink = GST_VR_SINK (vsink);

  GST_TRACE ("redisplay texture:%u of size:%ux%u, window size:%ux%u",
      vr_sink->next_tex, GST_VIDEO_INFO_WIDTH (&vr_sink->out_info),
      GST_VIDEO_INFO_HEIGHT (&vr_sink->out_info),
      GST_VIDEO_SINK_WIDTH (vr_sink), GST_VIDEO_SINK_HEIGHT (vr_sink));

  /* Ask the underlying window to redraw its content */
  if (!gst_vr_sink_redisplay (vr_sink))
    goto redisplay_failed;

  GST_TRACE ("post redisplay");

  if (g_atomic_int_get (&vr_sink->to_quit) != 0) {
    GST_ELEMENT_ERROR (vr_sink, RESOURCE, NOT_FOUND,
        ("%s", gst_gl_context_get_error ()), (NULL));
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;

/* ERRORS */
redisplay_failed:
  {
    GST_ELEMENT_ERROR (vr_sink, RESOURCE, NOT_FOUND,
        ("%s", gst_gl_context_get_error ()), (NULL));
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_vr_sink_propose_allocation (GstBaseSink * bsink, GstQuery * query)
{
  GstVRSink *vr_sink = GST_VR_SINK (bsink);
  GstStructure *config;
  GstCaps *caps;
  guint size;
  gboolean need_pool;

  if (!_ensure_gl_setup (vr_sink))
    return FALSE;

  gst_query_parse_allocation (query, &caps, &need_pool);

  if (caps == NULL)
    goto no_caps;

  if (need_pool) {
    GstBufferPool *pool;
    GstVideoInfo info;

    if (!gst_video_info_from_caps (&info, caps))
      goto invalid_caps;

    /* the normal size of a frame */
    size = info.size;

    GST_DEBUG_OBJECT (vr_sink, "create new pool");

    pool = gst_gl_buffer_pool_new (vr_sink->context);
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_set_params (config, caps, size, 0, 0);

    if (!gst_buffer_pool_set_config (pool, config))
      goto config_failed;

    /* we need at least 2 buffer because we hold on to the last one */
    gst_query_add_allocation_pool (query, pool, size, 2, 0);
  }

  if (vr_sink->context->gl_vtable->FenceSync)
    gst_query_add_allocation_meta (query, GST_GL_SYNC_META_API_TYPE, 0);

  return TRUE;

  /* ERRORS */
no_caps:
  {
    GST_WARNING_OBJECT (bsink, "no caps specified");
    return FALSE;
  }
invalid_caps:
  {
    GST_WARNING_OBJECT (bsink, "invalid caps specified");
    return FALSE;
  }
config_failed:
  {
    GST_WARNING_OBJECT (bsink, "failed setting config");
    return FALSE;
  }
}

/* *INDENT-OFF* */
static const GLfloat vertices[] = {
     1.0f,  1.0f, 0.0f, 1.0f, 0.0f,
    -1.0f,  1.0f, 0.0f, 0.0f, 0.0f,
    -1.0f, -1.0f, 0.0f, 0.0f, 1.0f,
     1.0f, -1.0f, 0.0f, 1.0f, 1.0f
};

static const GLushort indices[] = { 0, 1, 2, 0, 2, 3 };
/* *INDENT-ON* */

static void
_bind_buffer (GstVRSink * vr_sink)
{
  const GstGLFuncs *gl = vr_sink->context->gl_vtable;

  gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, vr_sink->vbo_indices);
  gl->BindBuffer (GL_ARRAY_BUFFER, vr_sink->vertex_buffer);

  /* Load the vertex position */
  gl->VertexAttribPointer (vr_sink->attr_position, 3, GL_FLOAT, GL_FALSE,
      5 * sizeof (GLfloat), (void *) 0);

  /* Load the texture coordinate */
  gl->VertexAttribPointer (vr_sink->attr_texture, 2, GL_FLOAT, GL_FALSE,
      5 * sizeof (GLfloat), (void *) (3 * sizeof (GLfloat)));

  gl->EnableVertexAttribArray (vr_sink->attr_position);
  gl->EnableVertexAttribArray (vr_sink->attr_texture);
}

static void
_unbind_buffer (GstVRSink * vr_sink)
{
  const GstGLFuncs *gl = vr_sink->context->gl_vtable;

  gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);
  gl->BindBuffer (GL_ARRAY_BUFFER, 0);

  gl->DisableVertexAttribArray (vr_sink->attr_position);
  gl->DisableVertexAttribArray (vr_sink->attr_texture);
}

/* *INDENT-OFF* */
static const gchar *simple_vertex_shader_str_gles2 =
      "attribute vec4 a_position;\n"
      "attribute vec2 a_texcoord;\n"
      "varying vec2 v_texcoord;\n"
      "void main()\n"
      "{\n"
      "   gl_Position = a_position;\n"
      "   v_texcoord = a_texcoord;\n"
      "}\n";

static const gchar *simple_fragment_shader_str_gles2 =
      "#ifdef GL_ES\n"
      "precision mediump float;\n"
      "#endif\n"
      "varying vec2 v_texcoord;\n"
      "uniform sampler2D tex;\n"
      "void main()\n"
      "{\n"
      "  gl_FragColor = texture2D(tex, v_texcoord);\n"
      "}";
/* *INDENT-ON* */

gboolean
gst_vr_sink_shader_compile (GstGLShader * shader,
    GLint * pos_loc, GLint * tex_loc)
{
  const gchar *attrib_names[2] = { "a_position", "a_texcoord" };
  GLint attrib_locs[2] = { 0 };
  gboolean ret = TRUE;

  ret = gst_gl_shader_compile_all_with_attribs_and_check (shader,
      simple_vertex_shader_str_gles2, simple_fragment_shader_str_gles2, 2,
      attrib_names, attrib_locs);

  if (ret) {
    *pos_loc = attrib_locs[0];
    *tex_loc = attrib_locs[1];
  }

  return ret;
}

/* Called in the gl thread */
static void
gst_vr_sink_thread_init_redisplay (GstVRSink * vr_sink)
{
  const GstGLFuncs *gl = vr_sink->context->gl_vtable;

  vr_sink->redisplay_shader = gst_gl_shader_new (vr_sink->context);

  if (!gst_vr_sink_shader_compile
      (vr_sink->redisplay_shader, &vr_sink->attr_position,
          &vr_sink->attr_texture))
    gst_vr_sink_cleanup_glthread (vr_sink);

  if (gl->GenVertexArrays) {
    gl->GenVertexArrays (1, &vr_sink->vao);
    gl->BindVertexArray (vr_sink->vao);
  }

  if (!vr_sink->vertex_buffer) {
    gl->GenBuffers (1, &vr_sink->vertex_buffer);
    gl->BindBuffer (GL_ARRAY_BUFFER, vr_sink->vertex_buffer);
    gl->BufferData (GL_ARRAY_BUFFER, 4 * 5 * sizeof (GLfloat), vertices,
        GL_STATIC_DRAW);
  }

  if (!vr_sink->vbo_indices) {
    gl->GenBuffers (1, &vr_sink->vbo_indices);
    gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, vr_sink->vbo_indices);
    gl->BufferData (GL_ELEMENT_ARRAY_BUFFER, sizeof (indices), indices,
        GL_STATIC_DRAW);
  }

  if (gl->GenVertexArrays) {
    _bind_buffer (vr_sink);
    gl->BindVertexArray (0);
  }

  gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);
  gl->BindBuffer (GL_ARRAY_BUFFER, 0);
}

static void
gst_vr_sink_cleanup_glthread (GstVRSink * vr_sink)
{
  const GstGLFuncs *gl = vr_sink->context->gl_vtable;

  if (vr_sink->redisplay_shader) {
    gst_object_unref (vr_sink->redisplay_shader);
    vr_sink->redisplay_shader = NULL;
  }

  if (vr_sink->vao) {
    gl->DeleteVertexArrays (1, &vr_sink->vao);
    vr_sink->vao = 0;
  }

  if (vr_sink->vertex_buffer) {
    gl->DeleteBuffers (1, &vr_sink->vertex_buffer);
    vr_sink->vertex_buffer = 0;
  }

  if (vr_sink->vbo_indices) {
    gl->DeleteBuffers (1, &vr_sink->vbo_indices);
    vr_sink->vbo_indices = 0;
  }
}

static void
gst_vr_sink_on_resize (GstVRSink * vr_sink, gint width, gint height)
{
  GST_DEBUG_OBJECT (vr_sink, "GL Window resized to %ux%u", width, height);

  GST_VR_SINK_LOCK (vr_sink);
  vr_sink->output_mode_changed = TRUE;
  gst_vr_sink_do_resize (vr_sink, width, height);
  GST_VR_SINK_UNLOCK (vr_sink);
}

/* Called with object lock held */
static void
gst_vr_sink_do_resize (GstVRSink * vr_sink, gint width, gint height)
{
  /* Here vr_sink members (ex:vr_sink->out_info) have a life time of set_caps.
   * It means that they cannot change between two set_caps
   */
  gboolean do_reshape;

  GST_VR_SINK_UNLOCK (vr_sink);
  /* check if a client reshape callback is registered */
  g_signal_emit (vr_sink, gst_vr_sink_signals[CLIENT_RESHAPE_SIGNAL], 0,
      vr_sink->context, width, height, &do_reshape);
  GST_VR_SINK_LOCK (vr_sink);

  width = MAX (1, width);
  height = MAX (1, height);

  vr_sink->window_width = width;
  vr_sink->window_height = height;

  /* default reshape */
  if (!do_reshape) {
    if (vr_sink->keep_aspect_ratio) {
      GstVideoRectangle src, dst, result;

      src.x = 0;
      src.y = 0;
      src.w = GST_VIDEO_SINK_WIDTH (vr_sink);
      src.h = GST_VIDEO_SINK_HEIGHT (vr_sink);

      dst.x = 0;
      dst.y = 0;
      dst.w = width;
      dst.h = height;

      gst_video_sink_center_rect (src, dst, &result, TRUE);
      vr_sink->output_mode_changed |= (result.w != vr_sink->display_rect.w);
      vr_sink->output_mode_changed |= (result.h != vr_sink->display_rect.h);
      vr_sink->display_rect = result;
    } else {
      vr_sink->output_mode_changed |= (width != vr_sink->display_rect.w);
      vr_sink->output_mode_changed |= (height != vr_sink->display_rect.h);

      vr_sink->display_rect.x = 0;
      vr_sink->display_rect.y = 0;
      vr_sink->display_rect.w = width;
      vr_sink->display_rect.h = height;
    }
    vr_sink->update_viewport = TRUE;

  }
}

static void
gst_vr_sink_on_draw (GstVRSink * vr_sink)
{
  /* Here vr_sink members (ex:vr_sink->out_info) have a life time of set_caps.
   * It means that they cannot not change between two set_caps as well as
   * for the redisplay_texture size.
   * Whereas redisplay_texture id changes every sink_render
   */

  const GstGLFuncs *gl = NULL;
  GstGLWindow *window = NULL;
  gboolean do_redisplay = FALSE;
  GstGLSyncMeta *sync_meta = NULL;
  GstSample *sample = NULL;

  g_return_if_fail (GST_IS_VR_SINK (vr_sink));

  gl = vr_sink->context->gl_vtable;

  GST_VR_SINK_LOCK (vr_sink);

  /* check if texture is ready for being drawn */
  if (!vr_sink->redisplay_texture) {
    GST_VR_SINK_UNLOCK (vr_sink);
    return;
  }

  window = gst_gl_context_get_window (vr_sink->context);
  window->is_drawing = TRUE;

  /* opengl scene */
  GST_TRACE ("redrawing texture:%u", vr_sink->redisplay_texture);

  if (vr_sink->caps_change && vr_sink->window_width > 0
      && vr_sink->window_height > 0) {
    gst_vr_sink_do_resize (vr_sink, vr_sink->window_width,
        vr_sink->window_height);
    vr_sink->caps_change = FALSE;
  }
  if (vr_sink->update_viewport == TRUE) {
    gl->Viewport (vr_sink->display_rect.x, vr_sink->display_rect.y,
        vr_sink->display_rect.w, vr_sink->display_rect.h);
    GST_DEBUG_OBJECT (vr_sink, "GL output area now %u,%u %ux%u",
        vr_sink->display_rect.x, vr_sink->display_rect.y,
        vr_sink->display_rect.w, vr_sink->display_rect.h);
    vr_sink->update_viewport = FALSE;
  }

  sync_meta = gst_buffer_get_gl_sync_meta (vr_sink->stored_sync);
  if (sync_meta)
    gst_gl_sync_meta_wait (sync_meta, gst_gl_context_get_current ());

  /* make sure that the environnement is clean */
  gst_gl_context_clear_shader (vr_sink->context);
  gl->BindTexture (GL_TEXTURE_2D, 0);
#if GST_GL_HAVE_OPENGL
  if (USING_OPENGL (vr_sink->context))
    gl->Disable (GL_TEXTURE_2D);
#endif

  sample = gst_sample_new (vr_sink->stored_buffer[0],
      vr_sink->out_caps, &GST_BASE_SINK (vr_sink)->segment, NULL);
  g_signal_emit (vr_sink, gst_vr_sink_signals[CLIENT_DRAW_SIGNAL], 0,
      vr_sink->context, sample, &do_redisplay);
  gst_sample_unref (sample);

  if (vr_sink->stored_buffer[1]) {
    sample = gst_sample_new (vr_sink->stored_buffer[1],
        vr_sink->out_caps, &GST_BASE_SINK (vr_sink)->segment, NULL);
    g_signal_emit (vr_sink, gst_vr_sink_signals[CLIENT_DRAW_SIGNAL], 0,
        vr_sink->context, sample, &do_redisplay);
    gst_sample_unref (sample);
  }

  if (!do_redisplay) {
    gfloat alpha = vr_sink->ignore_alpha ? 1.0f : 0.0f;

    gl->ClearColor (0.0, 0.0, 0.0, alpha);
    gl->Clear (GL_COLOR_BUFFER_BIT);

    if (vr_sink->ignore_alpha) {
      gl->BlendColor (0.0, 0.0, 0.0, alpha);
      gl->BlendFunc (GL_SRC_ALPHA, GL_CONSTANT_COLOR);
      gl->BlendEquation (GL_FUNC_ADD);
      gl->Enable (GL_BLEND);
    }

    gst_gl_shader_use (vr_sink->redisplay_shader);

    if (gl->GenVertexArrays)
      gl->BindVertexArray (vr_sink->vao);
    else
      _bind_buffer (vr_sink);

    gl->ActiveTexture (GL_TEXTURE0);
    gl->BindTexture (GL_TEXTURE_2D, vr_sink->redisplay_texture);
    gst_gl_shader_set_uniform_1i (vr_sink->redisplay_shader, "tex", 0);

    gl->DrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

    gst_gl_context_clear_shader (vr_sink->context);

    if (gl->GenVertexArrays)
      gl->BindVertexArray (0);
    else
      _unbind_buffer (vr_sink);

    if (vr_sink->ignore_alpha)
      gl->Disable (GL_BLEND);
  }
  /* end default opengl scene */
  window->is_drawing = FALSE;
  gst_object_unref (window);

  GST_VR_SINK_UNLOCK (vr_sink);
}

static void
gst_vr_sink_on_close (GstVRSink * vr_sink)
{
  GstGLWindow *window;

  gst_gl_context_set_error (vr_sink->context, "Output window was closed");

  window = gst_gl_context_get_window (vr_sink->context);

  if (vr_sink->key_sig_id)
    g_signal_handler_disconnect (window, vr_sink->key_sig_id);
  vr_sink->key_sig_id = 0;
  if (vr_sink->mouse_sig_id)
    g_signal_handler_disconnect (window, vr_sink->mouse_sig_id);
  vr_sink->mouse_sig_id = 0;

  g_atomic_int_set (&vr_sink->to_quit, 1);

  gst_object_unref (window);
}

static gboolean
gst_vr_sink_redisplay (GstVRSink * vr_sink)
{
  GstGLWindow *window;
  gboolean alive;
  GstBuffer *old_stored_buffer[2], *old_sync;

  window = gst_gl_context_get_window (vr_sink->context);
  if (!window)
    return FALSE;

  if (gst_gl_window_is_running (window)) {
    gulong handler_id =
        g_signal_handler_find (GST_ELEMENT_PARENT (vr_sink), G_SIGNAL_MATCH_ID,
        gst_vr_sink_bin_signals[SIGNAL_BIN_CLIENT_DRAW], 0,
        NULL, NULL, NULL);

    if (G_UNLIKELY (!vr_sink->redisplay_shader) && (!handler_id
            || !vr_sink->other_context)) {
      gst_gl_window_send_message (window,
          GST_GL_WINDOW_CB (gst_vr_sink_thread_init_redisplay), vr_sink);

      /* if the shader is still null it means it failed to be useable */
      if (G_UNLIKELY (!vr_sink->redisplay_shader)) {
        gst_object_unref (window);
        return FALSE;
      }

      gst_gl_window_set_preferred_size (window, GST_VIDEO_SINK_WIDTH (vr_sink),
          GST_VIDEO_SINK_HEIGHT (vr_sink));
      gst_gl_window_show (window);
    }

    /* Recreate the output texture if needed */
    GST_VR_SINK_LOCK (vr_sink);
    if (vr_sink->output_mode_changed && vr_sink->input_buffer != NULL) {
      GST_DEBUG ("Recreating output after mode/size change");
      update_output_format (vr_sink);
      prepare_next_buffer (vr_sink);
    }

    if (vr_sink->next_buffer == NULL) {
      /* Nothing to display yet */
      GST_VR_SINK_UNLOCK (vr_sink);
      return TRUE;
    }

    /* Avoid to release the texture while drawing */
    vr_sink->redisplay_texture = vr_sink->next_tex;
    old_stored_buffer[0] = vr_sink->stored_buffer[0];
    old_stored_buffer[1] = vr_sink->stored_buffer[1];
    vr_sink->stored_buffer[0] = gst_buffer_ref (vr_sink->next_buffer);
    if (vr_sink->next_buffer2)
      vr_sink->stored_buffer[1] = gst_buffer_ref (vr_sink->next_buffer2);
    else
      vr_sink->stored_buffer[1] = NULL;

    old_sync = vr_sink->stored_sync;
    vr_sink->stored_sync = gst_buffer_ref (vr_sink->next_sync);
    GST_VR_SINK_UNLOCK (vr_sink);

    gst_buffer_replace (old_stored_buffer, NULL);
    gst_buffer_replace (old_stored_buffer + 1, NULL);
    if (old_sync)
      gst_buffer_unref (old_sync);

    /* Drawing is asynchronous: gst_gl_window_draw is not blocking
     * It means that it does not wait for stuff to be executed in other threads
     */
    gst_gl_window_draw (window);
  }
  alive = gst_gl_window_is_running (window);
  gst_object_unref (window);

  return alive;
}
