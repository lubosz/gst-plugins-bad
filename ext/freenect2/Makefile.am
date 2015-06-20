plugin_LTLIBRARIES = libgstfreenect2.la

# sources used to compile this plug-in
libgstfreenect2_la_SOURCES = gstfreenect2.cpp \
                          gstfreenect2src.cpp

libgstfreenect2_la_CXXFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CXXFLAGS) $(FREENECT2_CFLAGS)

# flags used to compile this facedetect
# add other _CFLAGS and _LIBS as needed
#
libgstfreenect2_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
                       $(GST_CFLAGS) $(FREENECT2_CFLAGS) $(OPENCV_CFLAGS)

libgstfreenect2_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(FREENECT2_LIBS) \
                       $(GSTPB_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) $(OPENCV_LIBS)

libgstfreenect2_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstfreenect2_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

# headers we need but don't want installed
noinst_HEADERS = gstfreenect2src.h