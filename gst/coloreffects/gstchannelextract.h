/* GStreamer
 *
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

#ifndef __GST_CHANNEL_EXTRACT_H__
#define __GST_CHANNEL_EXTRACT_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>
#include <gst/base/gstcollectpads.h>

G_BEGIN_DECLS
#define GST_TYPE_CHANNEL_EXTRACT \
  (gst_channel_extract_get_type())
#define GST_CHANNEL_EXTRACT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CHANNEL_EXTRACT,GstChannelExtract))
#define GST_CHANNEL_EXTRACT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CHANNEL_EXTRACT,GstChannelExtractClass))
#define GST_IS_CHANNEL_EXTRACT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CHANNEL_EXTRACT))
#define GST_IS_CHANNEL_EXTRACT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CHANNEL_EXTRACT))
typedef struct _GstChannelExtract GstChannelExtract;
typedef struct _GstChannelExtractClass GstChannelExtractClass;

struct _GstChannelExtract
{
  GstVideoFilter parent;

  /* <private> */

  /* caps */
  GMutex lock;

  GstVideoFormat format;
  gint width, height;

  guint channel_enum;
  const gchar* channel_name;

  /* processing function */
  void (*process) (GstChannelExtract * channel_extract, gint width, gint height,
      GstVideoFrame * src, GstVideoFrame * dest);

};

struct _GstChannelExtractClass
{
  GstVideoFilterClass parent_class;
};

GType gst_channel_extract_get_type (void);

G_END_DECLS
#endif /* __GST_CHANNEL_EXTRACT_H__ */
