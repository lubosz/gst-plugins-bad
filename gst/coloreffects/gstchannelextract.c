/* GStreamer
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
 * SECTION:element-channelextract
 * 
 * The channelextract element will replace all color channels by one channel,
 * except for the alpha channel, which will be full white.
 *
 * Sample pipeline:
 * |[
 * gst-launch videotestsrc pattern=smpte75 ! \
 *   channelextract channel=1 ! \
 *   videoconvert ! autovideosink     \
 * ]| This pipeline only keeps the red channel.
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstchannelextract.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

GST_DEBUG_CATEGORY_STATIC (gst_channel_extract_debug);
#define GST_CAT_DEFAULT gst_channel_extract_debug

#define DEFAULT_CHANNEL_NAME "A"
#define DEFAULT_CHANNEL_ENUM GST_VIDEO_COMP_A

enum
{
  PROP_0,
  PROP_CHANNEL,
  PROP_LAST
};

static GstStaticPadTemplate gst_channel_extract_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ GRAY8 }"))
    );

static GstStaticPadTemplate gst_channel_extract_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE
        ("{ ARGB, BGRA, ABGR, RGBA, xRGB, BGRx, xBGR, RGBx, GRAY8 }"))
    );

#define GST_CHANNEL_EXTRACT_LOCK(self) G_STMT_START { \
  GST_LOG_OBJECT (self, "Locking channelextract from thread %p", g_thread_self ()); \
  g_mutex_lock (&self->lock); \
  GST_LOG_OBJECT (self, "Locked channelextract from thread %p", g_thread_self ()); \
} G_STMT_END

#define GST_CHANNEL_EXTRACT_UNLOCK(self) G_STMT_START { \
  GST_LOG_OBJECT (self, "Unlocking channelextract from thread %p", \
      g_thread_self ()); \
  g_mutex_unlock (&self->lock); \
} G_STMT_END

static gboolean gst_channel_extract_start (GstBaseTransform * trans);
static gboolean gst_channel_extract_set_info (GstVideoFilter * vfilter,
    GstCaps * incaps, GstVideoInfo * in_info, GstCaps * outcaps,
    GstVideoInfo * out_info);
static GstFlowReturn gst_channel_extract_transform_frame (GstVideoFilter *
    vfilter, GstVideoFrame * src, GstVideoFrame * dest);
static void gst_channel_extract_before_transform (GstBaseTransform * btrans,
    GstBuffer * buf);

static gboolean gst_channel_extract_set_process_function (GstChannelExtract *
    self);

static void gst_channel_extract_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_channel_extract_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_channel_extract_finalize (GObject * object);

#define gst_channel_extract_parent_class parent_class
G_DEFINE_TYPE (GstChannelExtract, gst_channel_extract, GST_TYPE_VIDEO_FILTER);

static void
gst_channel_extract_class_init (GstChannelExtractClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstBaseTransformClass *btrans_class = (GstBaseTransformClass *) klass;
  GstVideoFilterClass *vfilter_class = (GstVideoFilterClass *) klass;

  gobject_class->set_property = gst_channel_extract_set_property;
  gobject_class->get_property = gst_channel_extract_get_property;
  gobject_class->finalize = gst_channel_extract_finalize;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_CHANNEL,
      g_param_spec_string ("channel", "Source Channel", "The channel to sample",
          DEFAULT_CHANNEL_NAME,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  btrans_class->start = GST_DEBUG_FUNCPTR (gst_channel_extract_start);
  btrans_class->before_transform =
      GST_DEBUG_FUNCPTR (gst_channel_extract_before_transform);

  vfilter_class->transform_frame =
      GST_DEBUG_FUNCPTR (gst_channel_extract_transform_frame);
  vfilter_class->set_info = GST_DEBUG_FUNCPTR (gst_channel_extract_set_info);

  gst_element_class_set_static_metadata (gstelement_class,
      "Channel extract filter", "Filter/Effect/Video",
      "Passes only one color channel", "Lubosz Sarnecki <lubosz@gmail.com>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_channel_extract_sink_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_channel_extract_src_template));

  GST_DEBUG_CATEGORY_INIT (gst_channel_extract_debug, "channelextract", 0,
      "channelextract - Passes only one color channel");
}

static void
gst_channel_extract_init (GstChannelExtract * self)
{
  self->channel_name = DEFAULT_CHANNEL_NAME;
  self->channel_enum = DEFAULT_CHANNEL_ENUM;

  g_mutex_init (&self->lock);
}

static void
gst_channel_extract_finalize (GObject * object)
{
  GstChannelExtract *self = GST_CHANNEL_EXTRACT (object);

  g_mutex_clear (&self->lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_channel_extract_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstChannelExtract *self = GST_CHANNEL_EXTRACT (object);

  GST_CHANNEL_EXTRACT_LOCK (self);
  switch (prop_id) {
    case PROP_CHANNEL:
      self->channel_name = g_value_get_string (value);
      switch (self->channel_name[0]) {
        case 'R':
        case 'r':
          self->channel_enum = GST_VIDEO_COMP_R;
          break;
        case 'G':
        case 'g':
          self->channel_enum = GST_VIDEO_COMP_G;
          break;
        case 'B':
        case 'b':
          self->channel_enum = GST_VIDEO_COMP_B;
          break;
        case 'A':
        case 'a':
          self->channel_enum = GST_VIDEO_COMP_A;
          break;
        default:
          G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
          break;
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_CHANNEL_EXTRACT_UNLOCK (self);
}

static void
gst_channel_extract_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstChannelExtract *self = GST_CHANNEL_EXTRACT (object);

  switch (prop_id) {
    case PROP_CHANNEL:
      g_value_set_string (value, self->channel_name);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_channel_extract_set_info (GstVideoFilter * vfilter, GstCaps * incaps,
    GstVideoInfo * in_info, GstCaps * outcaps, GstVideoInfo * out_info)
{
  GstChannelExtract *self = GST_CHANNEL_EXTRACT (vfilter);

  GST_CHANNEL_EXTRACT_LOCK (self);

  GST_DEBUG_OBJECT (self,
      "Setting caps %" GST_PTR_FORMAT " -> %" GST_PTR_FORMAT, incaps, outcaps);

  self->format = GST_VIDEO_INFO_FORMAT (in_info);
  self->width = GST_VIDEO_INFO_WIDTH (in_info);
  self->height = GST_VIDEO_INFO_HEIGHT (in_info);

  if (!gst_channel_extract_set_process_function (self)) {
    GST_WARNING_OBJECT (self, "No processing function for this caps");
    GST_CHANNEL_EXTRACT_UNLOCK (self);
    return FALSE;
  }

  GST_CHANNEL_EXTRACT_UNLOCK (self);

  return TRUE;
}

static void
gst_channel_extract_process_xrgb (GstChannelExtract * self, gint width,
    gint height, GstVideoFrame * src, GstVideoFrame * dest)
{
  gint i, j;
  gint p[4];
  gint row_wrap;
  guint8 *src_plane;
  guint8 *dest_plane;

  gint channel_color;

  gint channel_color_offset =
      GST_VIDEO_FRAME_COMP_POFFSET (src, self->channel_enum);

  src_plane = GST_VIDEO_FRAME_PLANE_DATA (src, 0);
  dest_plane = GST_VIDEO_FRAME_PLANE_DATA (dest, 0);
  p[0] = GST_VIDEO_FRAME_COMP_POFFSET (src, 3);
  p[1] = GST_VIDEO_FRAME_COMP_POFFSET (src, 0);
  p[2] = GST_VIDEO_FRAME_COMP_POFFSET (src, 1);
  p[3] = GST_VIDEO_FRAME_COMP_POFFSET (src, 2);
  row_wrap = GST_VIDEO_FRAME_PLANE_STRIDE (src, 0) - 4 * width;

  for (i = 0; i < height; i++) {
    for (j = 0; j < width; j++) {
      channel_color = src_plane[channel_color_offset];

      dest_plane[p[0]] = 255;
      dest_plane[p[1]] = channel_color;
      dest_plane[p[2]] = channel_color;
      dest_plane[p[3]] = channel_color;

      src_plane += 4;
      dest_plane += 4;
    }
    src_plane += row_wrap;
    dest_plane += row_wrap;
  }
}

static void
gst_channel_extract_process_gray (GstChannelExtract * self, gint width,
    gint height, GstVideoFrame * src, GstVideoFrame * dest)
{
  gint i, j;
  gint row_wrap;
  guint8 *src_plane;
  guint8 *dest_plane;

  src_plane = GST_VIDEO_FRAME_PLANE_DATA (src, 0);
  dest_plane = GST_VIDEO_FRAME_PLANE_DATA (dest, 0);
  row_wrap = GST_VIDEO_FRAME_PLANE_STRIDE (src, 0) - width;

  for (i = 0; i < height; i++) {
    for (j = 0; j < width; j++) {

      dest_plane[0] = src_plane[0];

      src_plane += 1;
      dest_plane += 1;
    }
    src_plane += row_wrap;
    dest_plane += row_wrap;
  }
}

/* Protected with the channel extract lock */
static gboolean
gst_channel_extract_set_process_function (GstChannelExtract * self)
{
  self->process = NULL;

  switch (self->format) {
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
      self->process = gst_channel_extract_process_xrgb;
      break;
    case GST_VIDEO_FORMAT_GRAY8:
      self->process = gst_channel_extract_process_gray;
      break;
    default:
      break;
  }
  return self->process != NULL;
}

static gboolean
gst_channel_extract_start (GstBaseTransform * btrans)
{
  return TRUE;
}

static void
gst_channel_extract_before_transform (GstBaseTransform * btrans,
    GstBuffer * buf)
{
  GstChannelExtract *self = GST_CHANNEL_EXTRACT (btrans);
  GstClockTime timestamp;

  timestamp = gst_segment_to_stream_time (&btrans->segment, GST_FORMAT_TIME,
      GST_BUFFER_TIMESTAMP (buf));
  GST_LOG ("Got stream time of %" GST_TIME_FORMAT, GST_TIME_ARGS (timestamp));
  if (GST_CLOCK_TIME_IS_VALID (timestamp))
    gst_object_sync_values (GST_OBJECT (self), timestamp);
}

static GstFlowReturn
gst_channel_extract_transform_frame (GstVideoFilter * vfilter,
    GstVideoFrame * src, GstVideoFrame * dest)
{
  GstChannelExtract *self = GST_CHANNEL_EXTRACT (vfilter);

  GST_CHANNEL_EXTRACT_LOCK (self);

  if (G_UNLIKELY (!self->process)) {
    GST_ERROR_OBJECT (self, "Not negotiated yet");
    GST_CHANNEL_EXTRACT_UNLOCK (self);
    return GST_FLOW_NOT_NEGOTIATED;
  }

  self->process (self, self->width, self->height, src, dest);

  GST_CHANNEL_EXTRACT_UNLOCK (self);

  return GST_FLOW_OK;
}
