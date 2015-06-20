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

#ifndef _GLVRSINK_H_
#define _GLVRSINK_H_

#include <gst/gst.h>
#include <gst/video/gstvideosink.h>
#include <gst/video/video.h>

#include <gst/gl/gl.h>

G_BEGIN_DECLS

GST_DEBUG_CATEGORY_EXTERN (gst_debug_vr_sink);

#define GST_TYPE_VR_SINK \
    (gst_vr_sink_get_type())
#define GST_VR_SINK(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VR_SINK,GstVRSink))
#define GST_VR_SINK_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VR_SINK,GstVRSinkClass))
#define GST_IS_VR_SINK(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VR_SINK))
#define GST_IS_VR_SINK_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VR_SINK))

typedef struct _GstVRSink GstVRSink;
typedef struct _GstVRSinkClass GstVRSinkClass;

struct _GstVRSink
{
    GstVideoSink video_sink;

    guintptr window_id;
    guintptr new_window_id;
    gulong mouse_sig_id;
    gulong key_sig_id;

    /* GstVideoOverlay::set_render_rectangle() cache */
    gint x;
    gint y;
    gint width;
    gint height;

    /* Input info before 3d stereo output conversion, if any */
    GstVideoInfo in_info;

    /* format/caps we actually hand off to the app */
    GstVideoInfo out_info;
    GstCaps *out_caps;

    GstGLDisplay *display;
    GstGLContext *context;
    GstGLContext *other_context;
    gboolean handle_events;
    gboolean ignore_alpha;

    GstGLViewConvert *convert_views;

    /* Original input RGBA buffer, ready for display,
     * or possible reconversion through the views filter */
    GstBuffer *input_buffer;
    /* Secondary view buffer - when operating in frame-by-frame mode */
    GstBuffer *input_buffer2;

    guint      next_tex;
    GstBuffer *next_buffer;
    GstBuffer *next_buffer2; /* frame-by-frame 2nd view */
    GstBuffer *next_sync;

    volatile gint to_quit;
    gboolean keep_aspect_ratio;
    gint par_n, par_d;

    /* avoid replacing the stored_buffer while drawing */
    GMutex drawing_lock;
    GstBuffer *stored_buffer[2];
    GstBuffer *stored_sync;
    GLuint redisplay_texture;

    gboolean caps_change;
    guint window_width;
    guint window_height;
    gboolean update_viewport;

    GstVideoRectangle display_rect;

    GstGLShader *redisplay_shader;
    GLuint vao;
    GLuint vbo_indices;
    GLuint vertex_buffer;
    GLint  attr_position;
    GLint  attr_texture;

    GstVideoMultiviewMode mview_output_mode;
    GstVideoMultiviewFlags mview_output_flags;
    gboolean output_mode_changed;
    GstGLStereoDownmix mview_downmix_mode;
};

struct _GstVRSinkClass
{
    GstVideoSinkClass video_sink_class;
};

GType gst_vr_sink_get_type(void);
GType gst_vr_sink_bin_get_type(void);

G_END_DECLS

#endif

