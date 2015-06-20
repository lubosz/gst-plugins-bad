plugin_LTLIBRARIES = libgstvrsink.la

libgstvrsink_la_SOURCES = \
	gstvrsink.c

noinst_HEADERS = \
	gstvrsink.h

libgstvrsink_la_OBJCFLAGS = \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	$(GST_OBJCFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GL_OBJCFLAGS)

# check order of CFLAGS and LIBS, shouldn't the order be the other way around
# (like in AM_CFLAGS)?
libgstvrsink_la_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	$(GST_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GL_CFLAGS) \
	$(LIBPNG_CFLAGS) \
	$(GRAPHENE_CFLAGS)

libgstvrsink_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/gl/libgstgl-$(GST_API_VERSION).la \
	$(top_builddir)/gst-libs/gst/base/libgstbadbase-$(GST_API_VERSION).la \
	$(top_builddir)/gst-libs/gst/video/libgstbadvideo-$(GST_API_VERSION).la \
	$(GST_BASE_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) \
	-lgstpbutils-$(GST_API_VERSION) \
	$(GL_LIBS) \
	$(LIBPNG_LIBS) \
	$(JPEG_LIBS) \
	$(LIBM) \
	$(GRAPHENE_LIBS)

libgstvrsink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvrsink_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS) --tag=CC


