/*
 * GStreamer Frenect2 device source element
 * Copyright (C) 2015 Lubosz Sarnecki <lubosz@gmail.com>

 * This library is free software; you can
 * redistribute it and/or modify it under the terms of the GNU Library
 * General Public License as published by the Free Software Foundation;
 * either version 2 of the License, or (at your option) any later version.
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library
 * General Public License for more details. You should have received a copy
 * of the GNU Library General Public License along with this library; if
 * not, write to the Free Software Foundation, Inc., 51 Franklin St,
 * Fifth Floor, Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-freenect2src
 *
 * <refsect2>
 * <title>Examples</title>
 * <para>
 * Some recorded .oni files are available at:
 * <programlisting>
 *  http://people.cs.pitt.edu/~chang/1635/proj11/kinectRecord
 * </programlisting>
 *
 * <programlisting>
  LD_LIBRARY_PATH=/usr/lib/OpenNI2/Drivers/ gst-launch-1.0 --gst-debug=freenect2src:5   freenect2src location='Downloads/mr.oni' sourcetype=depth ! videoconvert ! ximagesink
 * </programlisting>
 * <programlisting>
  LD_LIBRARY_PATH=/usr/lib/OpenNI2/Drivers/ gst-launch-1.0 --gst-debug=freenect2src:5   freenect2src location='Downloads/mr.oni' sourcetype=color ! videoconvert ! ximagesink
 * </programlisting>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstfreenect2src.h"



GST_DEBUG_CATEGORY_STATIC (freenect2src_debug);
#define GST_CAT_DEFAULT freenect2src_debug
static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{RGBA, RGB, GRAY16_LE}"))
    );

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_SOURCETYPE
};
typedef enum
{
  SOURCETYPE_DEPTH,
  SOURCETYPE_COLOR,
  SOURCETYPE_BOTH
} GstFreenect2SourceType;
#define DEFAULT_SOURCETYPE  SOURCETYPE_DEPTH

#define SAMPLE_READ_WAIT_TIMEOUT 2000   /* 2000ms */

#define GST_TYPE_FREENECT2_SRC_SOURCETYPE (gst_freenect2_src_sourcetype_get_type ())
static GType
gst_freenect2_src_sourcetype_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      {SOURCETYPE_DEPTH, "Get depth readings", "depth"},
      {SOURCETYPE_COLOR, "Get color readings", "color"},
      {SOURCETYPE_BOTH, "Get color and depth", "both"},
      {0, NULL, NULL},
    };
    etype = g_enum_register_static ("GstFreenect2SrcSourcetype", values);
  }
  return etype;
}

/* GObject methods */
static void gst_freenect2_src_dispose (GObject * object);
static void gst_freenect2_src_finalize (GObject * gobject);
static void gst_freenect2_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_freenect2_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

/* basesrc methods */
static gboolean gst_freenect2_src_start (GstBaseSrc * bsrc);
static gboolean gst_freenect2_src_stop (GstBaseSrc * bsrc);
static gboolean gst_freenect2_src_set_caps (GstBaseSrc * src, GstCaps * caps);
static GstCaps *gst_freenect2_src_get_caps (GstBaseSrc * src, GstCaps * filter);
static gboolean gst_freenect2src_decide_allocation (GstBaseSrc * bsrc,
    GstQuery * query);

/* element methods */
static GstStateChangeReturn gst_freenect2_src_change_state (GstElement * element,
    GstStateChange transition);

/* pushsrc method */
static GstFlowReturn gst_freenect2src_fill (GstPushSrc * src, GstBuffer * buf);

/* OpenNI2 interaction methods */
static gboolean freenect2_initialise_library (GstFreenect2Src * src);
static gboolean freenect2_initialise_devices (GstFreenect2Src * src);
static GstFlowReturn freenect2_read_gstbuffer (GstFreenect2Src * src,
    GstBuffer * buf);

#define parent_class gst_freenect2_src_parent_class
G_DEFINE_TYPE (GstFreenect2Src, gst_freenect2_src, GST_TYPE_PUSH_SRC);

static void
gst_freenect2_src_class_init (GstFreenect2SrcClass * klass)
{
  GObjectClass *gobject_class;
  GstPushSrcClass *pushsrc_class;
  GstBaseSrcClass *basesrc_class;
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gobject_class = (GObjectClass *) klass;
  basesrc_class = (GstBaseSrcClass *) klass;
  pushsrc_class = (GstPushSrcClass *) klass;

  gobject_class->dispose = gst_freenect2_src_dispose;
  gobject_class->finalize = gst_freenect2_src_finalize;
  gobject_class->set_property = gst_freenect2_src_set_property;
  gobject_class->get_property = gst_freenect2_src_get_property;
  g_object_class_install_property
      (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "Location",
          "Source uri, can be a file or a device.", "", (GParamFlags)
          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_SOURCETYPE,
      g_param_spec_enum ("sourcetype",
          "Device source type",
          "Type of readings to get from the source",
          GST_TYPE_FREENECT2_SRC_SOURCETYPE, DEFAULT_SOURCETYPE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));


  basesrc_class->start = GST_DEBUG_FUNCPTR (gst_freenect2_src_start);
  basesrc_class->stop = GST_DEBUG_FUNCPTR (gst_freenect2_src_stop);
  basesrc_class->get_caps = GST_DEBUG_FUNCPTR (gst_freenect2_src_get_caps);
  basesrc_class->set_caps = GST_DEBUG_FUNCPTR (gst_freenect2_src_set_caps);
  basesrc_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_freenect2src_decide_allocation);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&srctemplate));

  gst_element_class_set_static_metadata (element_class, "Freenect2 client source",
      "Source/Video",
      "Extract readings from an Kinect v2. ",
      "Lubosz Sarnecki <lubosz@gmail.com>");

  element_class->change_state = gst_freenect2_src_change_state;

  pushsrc_class->fill = GST_DEBUG_FUNCPTR (gst_freenect2src_fill);

  GST_DEBUG_CATEGORY_INIT (freenect2src_debug, "freenect2src", 0,
      "Freenect2 Device Source");


}

static void
gst_freenect2_src_init (GstFreenect2Src * freenect2src)
{
  gst_base_src_set_live (GST_BASE_SRC (freenect2src), TRUE);
  gst_base_src_set_format (GST_BASE_SRC (freenect2src), GST_FORMAT_TIME);
/*
  freenect2src->device = new openni::Device ();
  freenect2src->depth = new openni::VideoStream ();
  freenect2src->color = new openni::VideoStream ();
  freenect2src->depthFrame = new openni::VideoFrameRef ();
  freenect2src->colorFrame = new openni::VideoFrameRef ();
*/
  

  freenect2src->dev = NULL;
  freenect2src->pipeline = NULL;

  freenect2src->oni_start_ts = GST_CLOCK_TIME_NONE;
    /* Freenect2 initialisation inside this function */
  freenect2_initialise_library (freenect2src);
}

static void
gst_freenect2_src_dispose (GObject * object)
{
  GstFreenect2Src *freenect2src = GST_FREENECT2_SRC (object);

  if (freenect2src->gst_caps)
    gst_caps_unref (freenect2src->gst_caps);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_freenect2_src_finalize (GObject * gobject)
{
  GstFreenect2Src *freenect2src = GST_FREENECT2_SRC (gobject);

  if (freenect2src->uri_name) {
    g_free (freenect2src->uri_name);
    freenect2src->uri_name = NULL;
  }

  if (freenect2src->gst_caps) {
    gst_caps_unref (freenect2src->gst_caps);
    freenect2src->gst_caps = NULL;
  }
/*
  if (freenect2src->device) {
    delete freenect2src->device;
    freenect2src->device = NULL;
  }

  if (freenect2src->depth) {
    delete freenect2src->depth;
    freenect2src->depth = NULL;
  }

  if (freenect2src->color) {
    delete freenect2src->color;
    freenect2src->color = NULL;
  }

  if (freenect2src->depthFrame) {
    delete freenect2src->depthFrame;
    freenect2src->depthFrame = NULL;
  }

  if (freenect2src->colorFrame) {
    delete freenect2src->colorFrame;
    freenect2src->colorFrame = NULL;
  }
*/



  delete freenect2src->registration;

  G_OBJECT_CLASS (parent_class)->finalize (gobject);
}

static void
gst_freenect2_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstFreenect2Src *freenect2src = GST_FREENECT2_SRC (object);

  GST_OBJECT_LOCK (freenect2src);
  switch (prop_id) {
    case PROP_LOCATION:
      if (!g_value_get_string (value)) {
        GST_WARNING ("location property cannot be NULL");
        break;
      }

      if (freenect2src->uri_name != NULL) {
        g_free (freenect2src->uri_name);
        freenect2src->uri_name = NULL;
      }

      freenect2src->uri_name = g_value_dup_string (value);
      break;
    case PROP_SOURCETYPE:
      freenect2src->sourcetype = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (freenect2src);
}

static void
gst_freenect2_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstFreenect2Src *freenect2src = GST_FREENECT2_SRC (object);

  GST_OBJECT_LOCK (freenect2src);
  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, freenect2src->uri_name);
      break;
    case PROP_SOURCETYPE:
      g_value_set_enum (value, freenect2src->sourcetype);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (freenect2src);
}

/* Interesting info from gstv4l2src.c:
 * "start and stop are not symmetric -- start will open the device, but not
 * start capture. it's setcaps that will start capture, which is called via
 * basesrc's negotiate method. stop will both stop capture and close t device."
 */
static gboolean
gst_freenect2_src_start (GstBaseSrc * bsrc)
{
  /*
  GstFreenect2Src *src = GST_FREENECT2_SRC (bsrc);
  openni::Status rc = openni::STATUS_OK;

  if (src->depth->isValid ()) {
    rc = src->depth->start ();
    if (rc != openni::STATUS_OK) {
      GST_ERROR_OBJECT (src, "Couldn't start the depth stream\n%s\n",
          openni::OpenNI::getExtendedError ());
      return FALSE;
    }
  }

  if (src->color->isValid ()) {
    rc = src->color->start ();
    if (rc != openni::STATUS_OK) {
      GST_ERROR_OBJECT (src, "Couldn't start the color stream\n%s\n",
          openni::OpenNI::getExtendedError ());
      return FALSE;
    }
  }
  */
  return TRUE;
}

static gboolean
gst_freenect2_src_stop (GstBaseSrc * bsrc)
{
  GstFreenect2Src *src = GST_FREENECT2_SRC (bsrc);
/*
  if (src->depthFrame)
    src->depthFrame->release ();

  if (src->colorFrame)
    src->colorFrame->release ();

  if (src->depth->isValid ()) {
    src->depth->stop ();
    src->depth->destroy ();
  }

  if (src->color->isValid ()) {
    src->color->stop ();
    src->color->destroy ();
  }

  src->device->close ();
*/

  src->dev->stop();
  src->dev->close();

  return TRUE;
}

static GstCaps *
gst_freenect2_src_get_caps (GstBaseSrc * src, GstCaps * filter)
{
  GstFreenect2Src *freenect2src;
  GstCaps *caps;
  GstVideoInfo info;
  GstVideoFormat format;

  freenect2src = GST_FREENECT2_SRC (src);

  GST_OBJECT_LOCK (freenect2src);
  if (freenect2src->gst_caps)
    goto out;

  // If we are here, we need to compose the caps and return them.
/*
  if (freenect2src->depth->isValid () && freenect2src->color->isValid () &&
      freenect2src->sourcetype == SOURCETYPE_BOTH
      && freenect2src->colorpixfmt == openni::PIXEL_FORMAT_RGB888) {
    format = GST_VIDEO_FORMAT_RGBA;
  } else if (freenect2src->depth->isValid () &&
             freenect2src->sourcetype == SOURCETYPE_DEPTH) {
    format = GST_VIDEO_FORMAT_GRAY16_LE;
  } else if (freenect2src->color->isValid () && freenect2src->sourcetype == SOURCETYPE_COLOR
      && freenect2src->colorpixfmt == openni::PIXEL_FORMAT_RGB888) {
    format = GST_VIDEO_FORMAT_RGB;
  } else {
    goto out;
  }
*/

  format = GST_VIDEO_FORMAT_RGB;

  gst_video_info_init (&info);
  //gst_video_info_set_format (&info, format, freenect2src->width, freenect2src->height);
  //info.fps_n = freenect2src->fps;
  gst_video_info_set_format (&info, format, 1920, 1080);
  info.fps_n = 30;
  info.fps_d = 1;
  caps = gst_video_info_to_caps (&info);

  GST_ERROR_OBJECT (freenect2src, "probed caps: %" GST_PTR_FORMAT, caps);
  freenect2src->gst_caps = caps;

out:
  GST_OBJECT_UNLOCK (freenect2src);

  if (!freenect2src->gst_caps)
    return gst_pad_get_pad_template_caps (GST_BASE_SRC_PAD (freenect2src));

  return (filter)
      ? gst_caps_intersect_full (filter, freenect2src->gst_caps,
      GST_CAPS_INTERSECT_FIRST)
      : gst_caps_ref (freenect2src->gst_caps);
}

static gboolean
gst_freenect2_src_set_caps (GstBaseSrc * src, GstCaps * caps)
{
  GstFreenect2Src *freenect2src;

  freenect2src = GST_FREENECT2_SRC (src);

  return gst_video_info_from_caps (&freenect2src->info, caps);
}

static GstStateChangeReturn
gst_freenect2_src_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;
  GstFreenect2Src *src = GST_FREENECT2_SRC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      /* Action! */
      if (!freenect2_initialise_devices (src))
        return GST_STATE_CHANGE_FAILURE;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    return ret;
  }

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_freenect2_src_stop (GST_BASE_SRC (src));
      if (src->gst_caps) {
        gst_caps_unref (src->gst_caps);
        src->gst_caps = NULL;
      }
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      src->oni_start_ts = GST_CLOCK_TIME_NONE;
      break;
    default:
      break;
  }

  return ret;
}


static GstFlowReturn
gst_freenect2src_fill (GstPushSrc * src, GstBuffer * buf)
{
  GstFreenect2Src *freenect2src = GST_FREENECT2_SRC (src);
  return freenect2_read_gstbuffer (freenect2src, buf);
}

static gboolean
gst_freenect2src_decide_allocation (GstBaseSrc * bsrc, GstQuery * query)
{
  GstBufferPool *pool;
  guint size, min, max;
  gboolean update;
  GstStructure *config;
  GstCaps *caps;
  GstVideoInfo info;

  gst_query_parse_allocation (query, &caps, NULL);
  gst_video_info_from_caps (&info, caps);

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    update = TRUE;
  } else {
    pool = NULL;
    min = max = 0;
    size = info.size;
    update = FALSE;
  }

  GST_DEBUG_OBJECT (bsrc, "allocation: size:%u min:%u max:%u pool:%"
      GST_PTR_FORMAT " caps:%" GST_PTR_FORMAT, size, min, max, pool, caps);

  if (!pool)
    pool = gst_video_buffer_pool_new ();

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, size, min, max);

  if (gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL)) {
    GST_DEBUG_OBJECT (pool, "activate Video Meta");
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
  }

  gst_buffer_pool_set_config (pool, config);

  if (update)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  gst_object_unref (pool);

  return GST_BASE_SRC_CLASS (parent_class)->decide_allocation (bsrc, query);
}

gboolean
gst_freenect2src_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "freenect2src", GST_RANK_NONE,
      GST_TYPE_FREENECT2_SRC);
}


static gboolean
freenect2_initialise_library (GstFreenect2Src * src)
{
  /*
  openni::Status rc = openni::STATUS_OK;
  rc = openni::OpenNI::initialize ();
  if (rc != openni::STATUS_OK) {
    GST_ERROR ("Initialization failed: %s",
        openni::OpenNI::getExtendedError ());
    openni::OpenNI::shutdown ();
    return GST_FLOW_ERROR;
  }
  return (rc == openni::STATUS_OK);
  */

  GST_ERROR("init freenect! :)");
  src->freenect2 = new libfreenect2::Freenect2();

  return TRUE;
}

static gboolean
freenect2_initialise_devices (GstFreenect2Src * src)
{
  /*
  openni::Status rc = openni::STATUS_OK;
  const char *deviceURI = openni::ANY_DEVICE;

  if (src->uri_name)
    deviceURI = src->uri_name;

  rc = src->device->open (deviceURI);
  if (rc != openni::STATUS_OK) {
    GST_ERROR_OBJECT (src, "Device (%s) open failed: %s", deviceURI,
        openni::OpenNI::getExtendedError ());
    openni::OpenNI::shutdown ();
    return FALSE;
  }

  // depth sensor
  rc = src->depth->create (*src->device, openni::SENSOR_DEPTH);
  if (rc == openni::STATUS_OK) {
    rc = src->depth->start ();
    if (rc != openni::STATUS_OK) {
      GST_ERROR_OBJECT (src, "%s", openni::OpenNI::getExtendedError ());
      src->depth->destroy ();
    }
  } else {
    GST_WARNING_OBJECT (src, "Couldn't find depth stream: %s",
        openni::OpenNI::getExtendedError ());
  }

  // color sensor
  rc = src->color->create (*src->device, openni::SENSOR_COLOR);
  if (rc == openni::STATUS_OK) {
    rc = src->color->start ();
    if (rc != openni::STATUS_OK) {
      GST_ERROR_OBJECT (src, "Couldn't start color stream: %s ",
          openni::OpenNI::getExtendedError ());
      src->color->destroy ();
    }
  } else {
    GST_WARNING_OBJECT (src, "Couldn't find color stream: %s",
        openni::OpenNI::getExtendedError ());
  }

  if (!src->depth->isValid () && !src->color->isValid ()) {
    GST_ERROR_OBJECT (src, "No valid streams. Exiting\n");
    openni::OpenNI::shutdown ();
    return FALSE;
  }

  // Get resolution and make sure is valid
  if (src->depth->isValid () && src->color->isValid ()) {
    src->depthVideoMode = src->depth->getVideoMode ();
    src->colorVideoMode = src->color->getVideoMode ();

    int depthWidth = src->depthVideoMode.getResolutionX ();
    int depthHeight = src->depthVideoMode.getResolutionY ();
    int colorWidth = src->colorVideoMode.getResolutionX ();
    int colorHeight = src->colorVideoMode.getResolutionY ();

    if (depthWidth == colorWidth && depthHeight == colorHeight) {
      src->width = depthWidth;
      src->height = depthHeight;
      src->fps = src->depthVideoMode.getFps ();
      src->colorpixfmt = src->colorVideoMode.getPixelFormat ();
      src->depthpixfmt = src->depthVideoMode.getPixelFormat ();
    } else {
      GST_ERROR_OBJECT (src, "Error - expect color and depth to be"
          " in same resolution: D: %dx%d vs C: %dx%d",
          depthWidth, depthHeight, colorWidth, colorHeight);
      return FALSE;
    }
    GST_INFO_OBJECT (src, "DEPTH&COLOR resolution: %dx%d",
        src->width, src->height);
  } else if (src->depth->isValid ()) {
    src->depthVideoMode = src->depth->getVideoMode ();
    src->width = src->depthVideoMode.getResolutionX ();
    src->height = src->depthVideoMode.getResolutionY ();
    src->fps = src->depthVideoMode.getFps ();
    src->depthpixfmt = src->depthVideoMode.getPixelFormat ();
    GST_INFO_OBJECT (src, "DEPTH resolution: %dx%d", src->width, src->height);
  } else if (src->color->isValid ()) {
    src->colorVideoMode = src->color->getVideoMode ();
    src->width = src->colorVideoMode.getResolutionX ();
    src->height = src->colorVideoMode.getResolutionY ();
    src->fps = src->colorVideoMode.getFps ();
    src->colorpixfmt = src->colorVideoMode.getPixelFormat ();
    GST_INFO_OBJECT (src, "COLOR resolution: %dx%d", src->width, src->height);
  } else {
    GST_ERROR_OBJECT (src, "Expected at least one of the streams to be valid.");
    return FALSE;
  }
  */

  if(src->freenect2->enumerateDevices() == 0)
  {
    GST_ERROR("no device connected!");
    return FALSE;
  }

  std::string serial = src->freenect2->getDefaultDeviceSerialNumber();

  GST_ERROR("serial %s", serial.c_str());

  src->pipeline = new libfreenect2::CpuPacketPipeline();

  if(src->pipeline)
  {
    src->dev = src->freenect2->openDevice(serial, src->pipeline);
  }
  else
  {
    GST_ERROR("no pipeline!");
    src->dev = src->freenect2->openDevice(serial);
  }

  if(src->dev == 0)
  {
    GST_ERROR("failure opening device!");
    return FALSE;
  }

  src->listener = new libfreenect2::SyncMultiFrameListener(
    libfreenect2::Frame::Color | libfreenect2::Frame::Ir | libfreenect2::Frame::Depth);

  src->undistorted = new libfreenect2::Frame(512, 424, 4);
  src->registered = new libfreenect2::Frame(512, 424, 4);

  src->dev->setColorFrameListener(src->listener);
  src->dev->setIrAndDepthFrameListener(src->listener);
  src->dev->start();

  GST_ERROR("device serial: %s", src->dev->getSerialNumber().c_str());
  GST_ERROR("device firmware: %s", src->dev->getFirmwareVersion().c_str());

  src->registration = new libfreenect2::Registration(
    src->dev->getIrCameraParams(), 
    src->dev->getColorCameraParams());


  return TRUE;
}

static GstFlowReturn
freenect2_read_gstbuffer (GstFreenect2Src * src, GstBuffer * buf)
{

    src->listener->waitForNewFrame(src->frames);
    libfreenect2::Frame *rgb = src->frames[libfreenect2::Frame::Color];
    //libfreenect2::Frame *ir = src->frames[libfreenect2::Frame::Ir];
    //libfreenect2::Frame *depth = src->frames[libfreenect2::Frame::Depth];

/*
    cv::imshow("rgb", cv::Mat(rgb->height, rgb->width, CV_8UC4, rgb->data));
    cv::imshow("ir", cv::Mat(ir->height, ir->width, CV_32FC1, ir->data) / 20000.0f);
    cv::imshow("depth", cv::Mat(depth->height, depth->width, CV_32FC1, depth->data) / 4500.0f);
*/

GstVideoFrame vframe;

    // Copy colour information

//GST_ERROR_OBJECT (freenect2src, "probed caps: %" GST_PTR_FORMAT, caps);



//GST_ERROR("video info copy: %d %d", src->info.width, src->info.height);

    gst_video_frame_map (&vframe, &src->info, buf, GST_MAP_WRITE);


    guint8 *pData = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (&vframe, 0);
    guint8 *pColor = (guint8 *) rgb->data;
    // Add depth as 8bit alpha channel, depth is 16bit samples.
    //guint16 *pDepth = (guint16 *) src->depthFrame->getData ();

    for (unsigned j = 0; j < rgb->height; ++j) {
      //GST_ERROR("current line %d", j);
      for (unsigned i = 0; i < rgb->width; ++i) {

        pData[3 * i + 2] = pColor[4 * i + 0];
        pData[3 * i + 1] = pColor[4 * i + 1];
        pData[3 * i + 0] = pColor[4 * i + 2];
        //pData[4 * j + 3] = pDepth[j] >> 8;
      }
      pData += GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);
      pColor += rgb->bytes_per_pixel * rgb->width;
      //pColor += GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);
      //pColor += 512;
      //GST_ERROR("iterating by %ld", rgb->width);
      //GST_ERROR("GST_VIDEO_FRAME_PLANE_STRIDE %d", GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0));
      //pDepth += src->depthFrame->getStrideInBytes () / 2;
    }
    
    gst_video_frame_unmap (&vframe);
/*
    oni_ts = src->colorFrame->getTimestamp () * 1000;

    GST_LOG_OBJECT (src, "sending buffer (%d+%d)B",
        src->colorFrame->getDataSize (),
        src->depthFrame->getDataSize ());
*/

/*
    GST_ERROR("rgb buffer %ldx%ld", rgb->height, rgb->width);
    GST_ERROR("ir buffer %ldx%ld", ir->height, ir->width);
    GST_ERROR("depth buffer %ldx%ld", depth->height, depth->width);
*/
    //src->registration->apply(rgb,depth,src->undistorted,src->registered);
/*
    cv::imshow("undistorted", cv::Mat(freenect2src->undistorted.height, freenect2src->undistorted.width, CV_32FC1, freenect2src->undistorted.data) / 4500.0f);
    cv::imshow("registered", cv::Mat(freenect2src->registered.height, freenect2src->registered.width, CV_8UC4, freenect2src->registered.data));
*/
    //int key = cv::waitKey(1);
    //protonect_shutdown = protonect_shutdown || (key > 0 && ((key & 0xFF) == 27)); // shutdown on escape

    src->listener->release(src->frames);

  /*
  openni::Status rc = openni::STATUS_OK;
  openni::VideoStream * pStream = src->depth;
  int changedStreamDummy;
  GstVideoFrame vframe;
  uint64_t oni_ts;

  // Block until we get some data
  rc = openni::OpenNI::waitForAnyStream (&pStream, 1, &changedStreamDummy,
      SAMPLE_READ_WAIT_TIMEOUT);
  if (rc != openni::STATUS_OK) {
    GST_ERROR_OBJECT (src, "Frame read timeout: %s",
        openni::OpenNI::getExtendedError ());
    return GST_FLOW_ERROR;
  }

  if (src->depth->isValid () && src->color->isValid () &&
      src->sourcetype == SOURCETYPE_BOTH) {
    rc = src->depth->readFrame (src->depthFrame);
    if (rc != openni::STATUS_OK) {
      GST_ERROR_OBJECT (src, "Frame read error: %s",
          openni::OpenNI::getExtendedError ());
      return GST_FLOW_ERROR;
    }
    rc = src->color->readFrame (src->colorFrame);
    if (rc != openni::STATUS_OK) {
      GST_ERROR_OBJECT (src, "Frame read error: %s",
          openni::OpenNI::getExtendedError ());
      return GST_FLOW_ERROR;
    }

    // Copy colour information
    gst_video_frame_map (&vframe, &src->info, buf, GST_MAP_WRITE);

    guint8 *pData = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (&vframe, 0);
    guint8 *pColor = (guint8 *) src->colorFrame->getData ();
    // Add depth as 8bit alpha channel, depth is 16bit samples.
    guint16 *pDepth = (guint16 *) src->depthFrame->getData ();

    for (int i = 0; i < src->colorFrame->getHeight (); ++i) {
      for (int j = 0; j < src->colorFrame->getWidth (); ++j) {
        pData[4 * j + 0] = pColor[3 * j + 0];
        pData[4 * j + 1] = pColor[3 * j + 1];
        pData[4 * j + 2] = pColor[3 * j + 2];
        pData[4 * j + 3] = pDepth[j] >> 8;
      }
      pData += GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);
      pColor += src->colorFrame->getStrideInBytes ();
      pDepth += src->depthFrame->getStrideInBytes () / 2;
    }
    gst_video_frame_unmap (&vframe);

    oni_ts = src->colorFrame->getTimestamp () * 1000;

    GST_LOG_OBJECT (src, "sending buffer (%d+%d)B",
        src->colorFrame->getDataSize (),
        src->depthFrame->getDataSize ());
  } else if (src->depth->isValid () && src->sourcetype == SOURCETYPE_DEPTH) {
    rc = src->depth->readFrame (src->depthFrame);
    if (rc != openni::STATUS_OK) {
      GST_ERROR_OBJECT (src, "Frame read error: %s",
          openni::OpenNI::getExtendedError ());
      return GST_FLOW_ERROR;
    }

    // Copy depth information
    gst_video_frame_map (&vframe, &src->info, buf, GST_MAP_WRITE);

    guint16 *pData = (guint16 *) GST_VIDEO_FRAME_PLANE_DATA (&vframe, 0);
    guint16 *pDepth = (guint16 *) src->depthFrame->getData ();

    for (int i = 0; i < src->depthFrame->getHeight (); ++i) {
      memcpy (pData, pDepth, 2 * src->depthFrame->getWidth ());
      pDepth += src->depthFrame->getStrideInBytes () / 2;
      pData += GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0) / 2;
    }
    gst_video_frame_unmap (&vframe);

    oni_ts = src->depthFrame->getTimestamp () * 1000;

    GST_LOG_OBJECT (src, "sending buffer (%dx%d)=%dB",
        src->depthFrame->getWidth (),
        src->depthFrame->getHeight (),
        src->depthFrame->getDataSize ());
  } else if (src->color->isValid () && src->sourcetype == SOURCETYPE_COLOR) {
    rc = src->color->readFrame (src->colorFrame);
    if (rc != openni::STATUS_OK) {
      GST_ERROR_OBJECT (src, "Frame read error: %s",
          openni::OpenNI::getExtendedError ());
      return GST_FLOW_ERROR;
    }

    gst_video_frame_map (&vframe, &src->info, buf, GST_MAP_WRITE);

    guint8 *pData = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (&vframe, 0);
    guint8 *pColor = (guint8 *) src->colorFrame->getData ();

    for (int i = 0; i < src->colorFrame->getHeight (); ++i) {
      memcpy (pData, pColor, 3 * src->colorFrame->getWidth ());
      pColor += src->colorFrame->getStrideInBytes ();
      pData += GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);
    }
    gst_video_frame_unmap (&vframe);

    oni_ts = src->colorFrame->getTimestamp () * 1000;

    GST_LOG_OBJECT (src, "sending buffer (%dx%d)=%dB",
        src->colorFrame->getWidth (),
        src->colorFrame->getHeight (),
        src->colorFrame->getDataSize ());
  } else {
    g_return_val_if_reached (GST_FLOW_ERROR);
    return GST_FLOW_ERROR;
  }

  if (G_UNLIKELY (src->oni_start_ts == GST_CLOCK_TIME_NONE))
    src->oni_start_ts = oni_ts;

  GST_BUFFER_PTS (buf) = oni_ts - src->oni_start_ts;

  GST_LOG_OBJECT (src, "Calculated PTS as %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_PTS (buf)));
*/
  return GST_FLOW_OK;
}
