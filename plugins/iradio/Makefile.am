NULL =

plugindir = $(PLUGINDIR)/iradio
plugindatadir = $(PLUGINDATADIR)/iradio
plugin_LTLIBRARIES = libiradio.la

libiradio_la_SOURCES = 					\
	rb-iradio-plugin.c				\
	rb-iradio-source.c				\
	rb-iradio-source.h				\
	rb-iradio-source-search.c			\
	rb-iradio-source-search.h			\
	rb-station-properties-dialog.c			\
	rb-station-properties-dialog.h

libiradio_la_LDFLAGS = $(PLUGIN_LIBTOOL_FLAGS)
libiradio_la_LIBTOOLFLAGS = --tag=disable-static
libiradio_la_LIBADD = 					\
	$(top_builddir)/shell/librhythmbox-core.la	\
	$(TOTEM_PLPARSER_LIBS)				\
	$(NULL)

INCLUDES = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib                        	\
	-I$(top_srcdir)/metadata                       	\
	-I$(top_srcdir)/rhythmdb                       	\
	-I$(top_srcdir)/widgets                    	\
	-I$(top_srcdir)/sources                    	\
	-I$(top_srcdir)/plugins				\
	-I$(top_srcdir)/shell				\
	-I$(top_srcdir)/backends			\
	-DPIXMAP_DIR=\""$(datadir)/pixmaps"\"		\
	-DSHARE_DIR=\"$(pkgdatadir)\"                   \
	-DDATADIR=\""$(datadir)"\"			\
	$(RHYTHMBOX_CFLAGS)				\
	$(TOTEM_PLPARSER_CFLAGS)			\
	-D_XOPEN_SOURCE -D_BSD_SOURCE

gtkbuilderdir = $(plugindatadir)
gtkbuilder_DATA = \
	station-properties.ui

uixmldir = $(plugindatadir)
uixml_DATA = iradio-ui.xml

xspfdir = $(plugindatadir)
xspf_DATA = iradio-initial.xspf

plugin_in_files = iradio.plugin.in

%.plugin: %.plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

BUILT_SOURCES =							\
	$(plugin_in_files:.plugin.in=.plugin) 	\
	$(NULL)

plugin_DATA = 			\
	$(BUILT_SOURCES)	\
	$(NULL)

EXTRA_DIST = 			\
	$(gtkbuilder_DATA)	\
	$(uixml_DATA)		\
	$(plugin_in_files)	\
	$(xspf_DATA)		\
	$(NULL)

CLEANFILES = 			\
	$(BUILT_SOURCES)	\
	$(NULL)

DISTCLEANFILES =		\
	$(BUILT_SOURCES)	\
	$(NULL)

