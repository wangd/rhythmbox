# Art Display Python Plugin

plugindir = $(PLUGINDIR)/artdisplay
plugindatadir = $(PLUGINDATADIR)/artdisplay
plugin_PYTHON = 			\
	EmbeddedCoverArtSearch.py	\
	PodcastCoverArtSearch.py	\
	LastFMCoverArtSearch.py		\
	LocalCoverArtSearch.py		\
	CoverArtDatabase.py		\
	MusicBrainzCoverArtSearch.py	\
	artdisplay.py

plugin_in_files = artdisplay.plugin.in
%.plugin: %.plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

plugin_DATA = $(plugin_in_files:.plugin.in=.plugin)

artworkdir = $(plugindatadir)
artwork_DATA = rhythmbox-missing-artwork.svg lastfm_red_small.png

EXTRA_DIST = $(plugin_in_files) $(artwork_DATA)

CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)
