/* GStreamer
 *
 * Copyright (C) 1999 Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2007 Wim Taymans <wim.taymans@collabora.co.uk>
 * Copyright (C) 2007 Edward Hervey <edward.hervey@collabora.co.uk>
 * Copyright (C) 2007 Jan Schmidt <thaytan@noraisin.net>
 * Copyright (C) 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

  guint channel;

  /* processing function */
  void (*process) (GstVideoFrame * frame, gint width, gint height,
      GstChannelExtract * channel_extract);

  /* pre-calculated values */
  gint hue;
};

struct _GstChannelExtractClass
{
  GstVideoFilterClass parent_class;
};

GType gst_channel_extract_get_type (void);

G_END_DECLS
#endif /* __GST_CHANNEL_EXTRACT_H__ */
