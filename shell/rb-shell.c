/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2002, 2003 Jorn Baayen
 *  Copyright (C) 2003, 2004 Colin Walters <walters@gnome.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

/**
 * SECTION:rb-shell
 * @short_description: holds the Rhythmbox main window and everything else
 *
 * RBShell is the main application class in Rhythmbox.  It creates and holds
 * references to the other main objects (#RBShellPlayer, #RhythmDB, #RBDisplayPageTree),
 * constructs the main window UI, and provides the basic DBus interface.
 */

#include <config.h>

#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <sys/stat.h>

#include <glib/gi18n.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <girepository.h>

#include <libpeas/peas.h>
#include <libpeas-gtk/peas-gtk.h>

#ifdef HAVE_MMKEYS
#include <X11/XF86keysym.h>
#endif /* HAVE_MMKEYS */

#include "rb-shell.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#ifdef WITH_RHYTHMDB_TREE
#include "rhythmdb-tree.h"
#else
#error "no database specified. configure broken?"
#endif
#include "rb-stock-icons.h"
#include "rb-display-page-tree.h"
#include "rb-display-page-group.h"
#include "rb-file-helpers.h"
#include "rb-source.h"
#include "rb-playlist-manager.h"
#include "rb-removable-media-manager.h"
#include "rb-track-transfer-queue.h"
#include "rb-shell-clipboard.h"
#include "rb-shell-player.h"
#include "rb-source-header.h"
#include "rb-statusbar.h"
#include "rb-shell-preferences.h"
#include "rb-library-source.h"
#include "rb-podcast-source.h"
#include "totem-pl-parser.h"
#include "rb-shell-preferences.h"
#include "rb-playlist-source.h"
#include "rb-static-playlist-source.h"
#include "rb-play-queue-source.h"
#include "rb-missing-files-source.h"
#include "rb-import-errors-source.h"
#include "rb-util.h"
#include "rb-display-page-model.h"
#include "rb-song-info.h"
#include "rb-marshal.h"
#include "rb-missing-plugins.h"
#include "rb-podcast-manager.h"
#include "rb-podcast-main-source.h"
#include "rb-podcast-entry-types.h"

#include "eggsmclient.h"

#define UNINSTALLED_PLUGINS_LOCATION "plugins"

#define PLAYING_ENTRY_NOTIFY_TIME 4

static void rb_shell_class_init (RBShellClass *klass);
static void rb_shell_init (RBShell *shell);
static void rb_shell_constructed (GObject *object);
static void rb_shell_finalize (GObject *object);
static void rb_shell_set_property (GObject *object,
				   guint prop_id,
				   const GValue *value,
				   GParamSpec *pspec);
static void rb_shell_get_property (GObject *object,
				   guint prop_id,
				   GValue *value,
				   GParamSpec *pspec);
static gboolean rb_shell_get_visibility (RBShell *shell);
static gboolean rb_shell_window_state_cb (GtkWidget *widget,
					  GdkEventWindowState *event,
					  RBShell *shell);
static gboolean rb_shell_window_configure_cb (GtkWidget *win,
					      GdkEventConfigure*event,
					      RBShell *shell);
static gboolean rb_shell_window_delete_cb (GtkWidget *win,
			                   GdkEventAny *event,
			                   RBShell *shell);
static gboolean rb_shell_key_press_event_cb (GtkWidget *win,
					     GdkEventKey *event,
					     RBShell *shell);
static void rb_shell_sync_window_state (RBShell *shell, gboolean dont_maximise);
static void rb_shell_sync_paned (RBShell *shell);
static void rb_shell_sync_party_mode (RBShell *shell);
static void rb_shell_select_page (RBShell *shell, RBDisplayPage *display_page);
static void display_page_selected_cb (RBDisplayPageTree *display_page_tree,
				      RBDisplayPage *page,
				      RBShell *shell);
static void rb_shell_playing_source_changed_cb (RBShellPlayer *player,
						RBSource *source,
						RBShell *shell);
static void rb_shell_playing_from_queue_cb (RBShellPlayer *player,
					    GParamSpec *arg,
					    RBShell *shell);
static void rb_shell_db_save_error_cb (RhythmDB *db,
				       const char *uri, const GError *error,
				       RBShell *shell);

static void rb_shell_playlist_added_cb (RBPlaylistManager *mgr, RBSource *source, RBShell *shell);
static void rb_shell_playlist_created_cb (RBPlaylistManager *mgr, RBSource *source, RBShell *shell);
static void rb_shell_medium_added_cb (RBRemovableMediaManager *mgr, RBSource *source, RBShell *shell);
static void rb_shell_display_page_deleted_cb (RBDisplayPage *page, RBShell *shell);
static void rb_shell_set_window_title (RBShell *shell, const char *window_title);
static void rb_shell_player_window_title_changed_cb (RBShellPlayer *player,
					             const char *window_title,
					             RBShell *shell);
static void rb_shell_cmd_about (GtkAction *action,
		                RBShell *shell);
static void rb_shell_cmd_contents (GtkAction *action,
				   RBShell *shell);
static void rb_shell_cmd_quit (GtkAction *action,
			       RBShell *shell);
static void rb_shell_cmd_preferences (GtkAction *action,
		                      RBShell *shell);
static void rb_shell_cmd_plugins (GtkAction *action,
		                  RBShell *shell);
static void rb_shell_cmd_add_folder_to_library (GtkAction *action,
						RBShell *shell);
static void rb_shell_cmd_add_file_to_library (GtkAction *action,
					      RBShell *shell);

static void rb_shell_cmd_current_song (GtkAction *action,
				       RBShell *shell);
static void rb_shell_jump_to_current (RBShell *shell);
static void rb_shell_jump_to_entry_with_source (RBShell *shell, RBSource *source,
						RhythmDBEntry *entry);
static void rb_shell_play_entry (RBShell *shell, RhythmDBEntry *entry);
static void rb_shell_cmd_view_all (GtkAction *action,
				   RBShell *shell);
static void rb_shell_view_party_mode_changed_cb (GtkAction *action,
						 RBShell *shell);
static void rb_shell_view_smalldisplay_changed_cb (GtkAction *action,
						 RBShell *shell);
static void rb_shell_view_statusbar_changed_cb (GtkAction *action,
						RBShell *shell);
static void rb_shell_view_queue_as_sidebar_changed_cb (GtkAction *action,
						       RBShell *shell);
static void rb_shell_load_complete_cb (RhythmDB *db, RBShell *shell);
static void rb_shell_sync_toolbar_state (RBShell *shell);
static void rb_shell_sync_smalldisplay (RBShell *shell);
static void rb_shell_sync_pane_visibility (RBShell *shell);
static void rb_shell_sync_statusbar_visibility (RBShell *shell);
static void rb_shell_set_visibility (RBShell *shell,
				     gboolean initial,
				     gboolean visible);
static void display_page_tree_drag_received_cb (RBDisplayPageTree *display_page_tree,
						RBDisplayPage *page,
						GtkSelectionData *data,
						RBShell *shell);

static void paned_size_allocate_cb (GtkWidget *widget,
				    GtkAllocation *allocation,
				    RBShell *shell);
static void rb_shell_volume_widget_changed_cb (GtkScaleButton *vol,
					       gdouble volume,
					       RBShell *shell);
static void rb_shell_player_volume_changed_cb (RBShellPlayer *player,
					       GParamSpec *arg,
					       RBShell *shell);
static void settings_changed_cb (GSettings *settings, const char *key, RBShell *shell);

static void rb_shell_session_init (RBShell *shell);

static gboolean rb_shell_visibility_changing (RBShell *shell, gboolean initial, gboolean visible);

enum
{
	PROP_NONE,
	PROP_NO_REGISTRATION,
	PROP_NO_UPDATE,
	PROP_DRY_RUN,
	PROP_RHYTHMDB_FILE,
	PROP_PLAYLISTS_FILE,
	PROP_SELECTED_PAGE,
	PROP_DB,
	PROP_UI_MANAGER,
	PROP_CLIPBOARD,
	PROP_PLAYLIST_MANAGER,
	PROP_REMOVABLE_MEDIA_MANAGER,
	PROP_SHELL_PLAYER,
	PROP_WINDOW,
	PROP_PREFS,
	PROP_QUEUE_SOURCE,
	PROP_PROXY_CONFIG,
	PROP_LIBRARY_SOURCE,
	PROP_DISPLAY_PAGE_MODEL,
	PROP_DISPLAY_PAGE_TREE,
	PROP_SOURCE_HEADER,
	PROP_VISIBILITY,
	PROP_TRACK_TRANSFER_QUEUE,
	PROP_AUTOSTARTED,
	PROP_DISABLE_PLUGINS
};

enum
{
	VISIBILITY_CHANGED,
	VISIBILITY_CHANGING,
	CREATE_SONG_INFO,
	REMOVABLE_MEDIA_SCAN_FINISHED,
	NOTIFY_PLAYING_ENTRY,
	NOTIFY_CUSTOM,
	DATABASE_LOAD_COMPLETE,
	LAST_SIGNAL
};

static guint rb_shell_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (RBShell, rb_shell, G_TYPE_OBJECT)

struct _RBShellPrivate
{
	GtkWidget *window;
	gboolean iconified;

	GtkUIManager *ui_manager;
	GtkActionGroup *actiongroup;
	guint source_ui_merge_id;

	GtkWidget *main_vbox;
	GtkWidget *paned;
	GtkWidget *right_paned;
	RBDisplayPageTree *display_page_tree;
	GtkWidget *notebook;
	GtkWidget *queue_paned;
	GtkWidget *queue_sidebar;

	GtkBox *sidebar_container;
	GtkBox *right_sidebar_container;
	GtkBox *top_container;
	GtkBox *bottom_container;
	guint right_sidebar_widget_count;

	RBDisplayPageModel *display_page_model;
	GList *sources;				/* kill? */
	GHashTable *sources_hash;		/* kill? */

	guint async_state_save_id;
	guint save_playlist_id;
	guint save_db_id;

	gboolean shutting_down;
	gboolean load_complete;

	gboolean no_registration;
	gboolean no_update;
	gboolean dry_run;
	gboolean autostarted;
	gboolean disable_plugins;
	char *rhythmdb_file;
	char *playlists_file;

	RhythmDB *db;

	RBShellPlayer *player_shell;
	RBShellClipboard *clipboard_shell;
	RBSourceHeader *source_header;
	RBStatusbar *statusbar;
	RBPlaylistManager *playlist_manager;
	RBRemovableMediaManager *removable_media_manager;
	RBTrackTransferQueue *track_transfer_queue;
	RBPodcastManager *podcast_manager;

	RBLibrarySource *library_source;
	RBPodcastSource *podcast_source;
	RBPlaylistSource *queue_source;
	RBSource *missing_files_source;
	RBSource *import_errors_source;

	RBDisplayPage *selected_page;

	GtkWidget *prefs;
	GtkWidget *plugins;

	GtkWidget *volume_button;
	gboolean syncing_volume;

	char *cached_title;
	gboolean cached_playing;

	guint sidepane_visibility_notify_id;
	guint toolbar_visibility_notify_id;
	guint toolbar_style_notify_id;
	guint smalldisplay_notify_id;

	glong last_small_time; /* when we last changed small mode */
	gboolean party_mode;

	GSettings *settings;

	GSettings *plugin_settings;
	PeasEngine *plugin_engine;
	PeasExtensionSet *activatable;
};


static GtkActionEntry rb_shell_actions [] =
{
	{ "Music", NULL, N_("_Music") },
	{ "Edit", NULL, N_("_Edit") },
	{ "View", NULL, N_("_View") },
	{ "Control", NULL, N_("_Control") },
	{ "Tools", NULL, N_("_Tools") },
	{ "Help", NULL, N_("_Help") },

	{ "MusicImportFolder", GTK_STOCK_OPEN, N_("_Import Folder..."), "<control>O",
	  N_("Choose folder to be added to the Library"),
	  G_CALLBACK (rb_shell_cmd_add_folder_to_library) },
	{ "MusicImportFile", GTK_STOCK_FILE, N_("Import _File..."), NULL,
	  N_("Choose file to be added to the Library"),
	  G_CALLBACK (rb_shell_cmd_add_file_to_library) },
	{ "HelpAbout", GTK_STOCK_ABOUT, N_("_About"), NULL,
	  N_("Show information about Rhythmbox"),
	  G_CALLBACK (rb_shell_cmd_about) },
	{ "HelpContents", GTK_STOCK_HELP, N_("_Contents"), "F1",
	  N_("Display Rhythmbox help"),
	  G_CALLBACK (rb_shell_cmd_contents) },
	{ "MusicQuit", GTK_STOCK_QUIT, N_("_Quit"), "<control>Q",
	  N_("Quit Rhythmbox"),
	  G_CALLBACK (rb_shell_cmd_quit) },
	{ "EditPreferences", GTK_STOCK_PREFERENCES, N_("Prefere_nces"), NULL,
	  N_("Edit Rhythmbox preferences"),
	  G_CALLBACK (rb_shell_cmd_preferences) },
	{ "EditPlugins", NULL, N_("Plu_gins"), NULL,
	  N_("Change and configure plugins"),
	  G_CALLBACK (rb_shell_cmd_plugins) },
	{ "ViewAll", NULL, N_("Show _All Tracks"), "<control>Y",
	  N_("Show all tracks in this music source"),
	  G_CALLBACK (rb_shell_cmd_view_all) },
	{ "ViewJumpToPlaying", GTK_STOCK_JUMP_TO, N_("_Jump to Playing Song"), "<control>J",
	  N_("Scroll the view to the currently playing song"),
	  G_CALLBACK (rb_shell_cmd_current_song) },
};
static guint rb_shell_n_actions = G_N_ELEMENTS (rb_shell_actions);

static GtkToggleActionEntry rb_shell_toggle_entries [] =
{
	{ "ViewSidePane", NULL, N_("Side _Pane"), "F9",
	  N_("Change the visibility of the side pane"),
	  NULL, TRUE },
	{ "ViewToolbar", NULL, N_("T_oolbar"), NULL,
	  N_("Change the visibility of the toolbar"),
	  NULL, TRUE },
	{ "ViewSmallDisplay", NULL, N_("_Small Display"), "<control>D",
	  N_("Make the main window smaller"),
	  G_CALLBACK (rb_shell_view_smalldisplay_changed_cb), },
	{ "ViewPartyMode", NULL, N_("Party _Mode"), "F11",
	  N_("Change the status of the party mode"),
	  G_CALLBACK (rb_shell_view_party_mode_changed_cb), FALSE },
	{ "ViewQueueAsSidebar", NULL, N_("Play _Queue as Side Pane"), "<control>K",
	  N_("Change whether the queue is visible as a source or a sidebar"),
	  G_CALLBACK (rb_shell_view_queue_as_sidebar_changed_cb) },
        { "ViewStatusbar", NULL, N_("S_tatusbar"), NULL,
	  N_("Change the visibility of the statusbar"),
	  G_CALLBACK (rb_shell_view_statusbar_changed_cb), TRUE }
};
static guint rb_shell_n_toggle_entries = G_N_ELEMENTS (rb_shell_toggle_entries);

static void
rb_shell_class_init (RBShellClass *klass)
{
        GObjectClass *object_class = (GObjectClass *) klass;

	object_class->set_property = rb_shell_set_property;
	object_class->get_property = rb_shell_get_property;
        object_class->finalize = rb_shell_finalize;
	object_class->constructed = rb_shell_constructed;

	klass->visibility_changing = rb_shell_visibility_changing;

	/**
	 * RBShell:no-registration:
	 *
	 * If %TRUE, disable single-instance features.
	 */
	g_object_class_install_property (object_class,
					 PROP_NO_REGISTRATION,
					 g_param_spec_boolean ("no-registration",
							       "no-registration",
							       "Whether or not to register",
							       FALSE,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	/**
	 * RBShell:no-update:
	 *
	 * If %TRUE, don't update the database.
	 */
	g_object_class_install_property (object_class,
					 PROP_NO_UPDATE,
					 g_param_spec_boolean ("no-update",
							       "no-update",
							       "Whether or not to update the library",
							       FALSE,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	/**
	 * RBShell:dry-run:
	 *
	 * If TRUE, don't write back file metadata changes.
	 */
	g_object_class_install_property (object_class,
					 PROP_DRY_RUN,
					 g_param_spec_boolean ("dry-run",
							       "dry-run",
							       "Whether or not this is a dry run",
							       FALSE,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	/**
	 * RBShell:rhythmdb-file:
	 *
	 * The path to the rhythmdb file
	 */
	g_object_class_install_property (object_class,
					 PROP_RHYTHMDB_FILE,
					 g_param_spec_string ("rhythmdb-file",
							      "rhythmdb-file",
							      "The RhythmDB file to use",
#ifdef WITH_RHYTHMDB_TREE
							      "rhythmdb.xml",
#elif defined(WITH_RHYTHMDB_GDA)
							      "rhythmdb.sqlite", /* FIXME: correct extension? */
#endif
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	/**
	 * RBShell:playlists-file:
	 *
	 * The path to the playlist file
	 */
	g_object_class_install_property (object_class,
					 PROP_PLAYLISTS_FILE,
					 g_param_spec_string ("playlists-file",
							      "playlists-file",
							      "The playlists file to use",
							      "playlists.xml",
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));


	/**
	 * RBShell:selected-page:
	 *
	 * The currently selected display page
	 */
	g_object_class_install_property (object_class,
					 PROP_SELECTED_PAGE,
					 g_param_spec_object ("selected-page",
							      "selected-page",
							      "Display page which is currently selected",
							      RB_TYPE_DISPLAY_PAGE,
							      G_PARAM_READABLE));
	/**
	 * RBShell:db:
	 *
	 * The #RhythmDB instance
	 */
	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_object ("db",
							      "RhythmDB",
							      "RhythmDB object",
							      RHYTHMDB_TYPE,
							      G_PARAM_READABLE));
	/**
	 * RBShell:ui-manager:
	 *
	 * The #GtkUIManager instance
	 */
	g_object_class_install_property (object_class,
					 PROP_UI_MANAGER,
					 g_param_spec_object ("ui-manager",
							      "GtkUIManager",
							      "GtkUIManager object",
							      GTK_TYPE_UI_MANAGER,
							      G_PARAM_READABLE));
	/**
	 * RBShell:clipboard:
	 *
	 * The #RBShellClipboard instance
	 */
	g_object_class_install_property (object_class,
					 PROP_CLIPBOARD,
					 g_param_spec_object ("clipboard",
							      "RBShellClipboard",
							      "RBShellClipboard object",
							      RB_TYPE_SHELL_CLIPBOARD,
							      G_PARAM_READABLE));
	/**
	 * RBShell:playlist-manager:
	 *
	 * The #RBPlaylistManager instance
	 */
	g_object_class_install_property (object_class,
					 PROP_PLAYLIST_MANAGER,
					 g_param_spec_object ("playlist-manager",
							      "RBPlaylistManager",
							      "RBPlaylistManager object",
							      RB_TYPE_PLAYLIST_MANAGER,
							      G_PARAM_READABLE));
	/**
	 * RBShell:shell-player:
	 *
	 * The #RBShellPlayer instance
	 */
	g_object_class_install_property (object_class,
					 PROP_SHELL_PLAYER,
					 g_param_spec_object ("shell-player",
							      "RBShellPlayer",
							      "RBShellPlayer object",
							      RB_TYPE_SHELL_PLAYER,
							      G_PARAM_READABLE));
	/**
	 * RBShell:removable-media-manager:
	 *
	 * The #RBRemovableMediaManager instance
	 */
	g_object_class_install_property (object_class,
					 PROP_REMOVABLE_MEDIA_MANAGER,
					 g_param_spec_object ("removable-media-manager",
							      "RBRemovableMediaManager",
							      "RBRemovableMediaManager object",
							      RB_TYPE_REMOVABLE_MEDIA_MANAGER,
							      G_PARAM_READABLE));
	/**
	 * RBShell:window:
	 *
	 * The main Rhythmbox window.
	 */
	g_object_class_install_property (object_class,
					 PROP_WINDOW,
					 g_param_spec_object ("window",
							      "GtkWindow",
							      "GtkWindow object",
							      GTK_TYPE_WINDOW,
							      G_PARAM_READABLE));
	/**
	 * RBShell:prefs:
	 *
	 * The #RBShellPreferences instance
	 */
	g_object_class_install_property (object_class,
					 PROP_PREFS,
					 g_param_spec_object ("prefs",
							      "RBShellPreferences",
							      "RBShellPreferences object",
							      RB_TYPE_SHELL_PREFERENCES,
							      G_PARAM_READABLE));
	/**
	 * RBShell:queue-source:
	 *
	 * The play queue source
	 */
	g_object_class_install_property (object_class,
					 PROP_QUEUE_SOURCE,
					 g_param_spec_object ("queue-source",
							      "queue-source",
							      "Queue source",
							      RB_TYPE_PLAY_QUEUE_SOURCE,
							      G_PARAM_READABLE));
	/**
	 * RBShell:library-source:
	 *
	 * The library source
	 */
	g_object_class_install_property (object_class,
					 PROP_LIBRARY_SOURCE,
					 g_param_spec_object ("library-source",
							      "library-source",
							      "Library source",
							      RB_TYPE_LIBRARY_SOURCE,
							      G_PARAM_READABLE));
	/**
	 * RBShell:display-page-model:
	 *
	 * The model underlying the display page tree
	 */
	g_object_class_install_property (object_class,
					 PROP_DISPLAY_PAGE_MODEL,
					 g_param_spec_object ("display-page-model",
							      "display-page-model",
							      "RBDisplayPageModel",
							      RB_TYPE_DISPLAY_PAGE_MODEL,
							      G_PARAM_READABLE));

	/**
	 * RBShell:display-page-tree:
	 *
	 * The #RBDisplayPageTree instance
	 */
	g_object_class_install_property (object_class,
					 PROP_DISPLAY_PAGE_TREE,
					 g_param_spec_object ("display-page-tree",
							      "display-page-tree",
							      "RBDisplayPageTree",
							      RB_TYPE_DISPLAY_PAGE_TREE,
							      G_PARAM_READABLE));

	/**
	 * RBShell:visibility:
	 *
	 * Whether the main window is currently visible.
	 */
	g_object_class_install_property (object_class,
					 PROP_VISIBILITY,
					 g_param_spec_boolean ("visibility",
							       "visibility",
							       "Current window visibility",
							       TRUE,
							       G_PARAM_READWRITE));
	/**
	 * RBShell:source-header:
	 *
	 * The #RBSourceHeader instance
	 */
	g_object_class_install_property (object_class,
					 PROP_SOURCE_HEADER,
					 g_param_spec_object ("source-header",
							      "source header widget",
							      "RBSourceHeader",
							      RB_TYPE_SOURCE_HEADER,
							      G_PARAM_READABLE));

	/**
	 * RBShell:track-transfer-queue:
	 *
	 * The #RBTrackTransferQueue instance
	 */
	g_object_class_install_property (object_class,
					 PROP_TRACK_TRANSFER_QUEUE,
					 g_param_spec_object ("track-transfer-queue",
							      "RBTrackTransferQueue",
							      "RBTrackTransferQueue object",
							      RB_TYPE_TRACK_TRANSFER_QUEUE,
							      G_PARAM_READABLE));
	/**
	 * RBShell:autostarted:
	 *
	 * Whether Rhythmbox was automatically started by the session manager
	 */
	g_object_class_install_property (object_class,
					 PROP_AUTOSTARTED,
					 g_param_spec_boolean ("autostarted",
							       "autostarted",
							       "TRUE if autostarted",
							       FALSE,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	/**
	 * RBShell:disable-plugins:
	 *
	 * If %TRUE, disable plugins
	 */
	g_object_class_install_property (object_class,
					 PROP_DISABLE_PLUGINS,
					 g_param_spec_boolean ("disable-plugins",
							       "disable-plugins",
							       "Whether or not to disable plugins",
							       FALSE,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	/**
	 * RBShell::visibility-changed:
	 * @shell: the #RBShell
	 * @visibile: new visibility
	 *
	 * Emitted after the visibility of the main Rhythmbox window has
	 * changed.
	 */
	rb_shell_signals[VISIBILITY_CHANGED] =
		g_signal_new ("visibility_changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBShellClass, visibility_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_BOOLEAN);
	/**
	 * RBShell::visibility-changing:
	 * @shell: the #RBShell
	 * @initial: if %TRUE, this is the initial visibility for the window
	 * @visible: new shell visibility
	 *
	 * Emitted before the visibility of the main window changes.  The return
	 * value overrides the visibility setting.  If multiple signal handlers
	 * are attached, the last one wins.
	 */
	rb_shell_signals[VISIBILITY_CHANGING] =
		g_signal_new ("visibility_changing",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBShellClass, visibility_changing),
			      NULL, NULL,
			      rb_marshal_BOOLEAN__BOOLEAN_BOOLEAN,
			      G_TYPE_BOOLEAN,
			      2,
			      G_TYPE_BOOLEAN,
			      G_TYPE_BOOLEAN);

	/**
	 * RBShell::create-song-info:
	 * @shell: the #RBShell
	 * @song_info: the new #RBSongInfo window
	 * @multi: if %TRUE, the song info window is for multiple entries
	 *
	 * Emitted when creating a new #RBSongInfo window.  Signal handlers can
	 * add pages to the song info window notebook to display additional
	 * information.
	 */
	rb_shell_signals[CREATE_SONG_INFO] =
		g_signal_new ("create_song_info",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBShellClass, create_song_info),
			      NULL, NULL,
			      rb_marshal_VOID__OBJECT_BOOLEAN,
			      G_TYPE_NONE,
			      2,
			      RB_TYPE_SONG_INFO, G_TYPE_BOOLEAN);
	/**
	 * RBShell::removable-media-scan-finished:
	 * @shell: the #RBShell
	 *
	 * Emitted when the initial scan for removable media devices is
	 * complete.  This is intended to allow plugins to request a
	 * device scan only if the scan on startup has already been done,
	 * but it isn't very useful for that.
	 * See #RBRemovableMediaManager:scanned for a better approach to
	 * this problem.
	 */
	rb_shell_signals[REMOVABLE_MEDIA_SCAN_FINISHED] =
		g_signal_new ("removable_media_scan_finished",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBShellClass, removable_media_scan_finished),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
	/**
	 * RBShell::notify-playing-entry:
	 * @shell: the #RBShell
	 *
	 * Emitted when a notification should be displayed showing the current
	 * playing entry.
	 */
	rb_shell_signals[NOTIFY_PLAYING_ENTRY] =
		g_signal_new ("notify-playing-entry",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_BOOLEAN);
	/**
	 * RBShell::notify-custom:
	 * @shell: the #RBShell
	 * @timeout: length of time (in seconds) to display the notification
	 * @primary: main notification text
	 * @secondary: secondary notification text
	 * @image_uri: URI for an image to include in the notification (optional)
	 * @requested: if %TRUE, the notification was triggered by an explicit user action
	 *
	 * Emitted when a custom notification should be displayed to the user.
	 */
	rb_shell_signals[NOTIFY_CUSTOM] =
		g_signal_new ("notify-custom",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      rb_marshal_VOID__UINT_STRING_STRING_STRING_BOOLEAN,
			      G_TYPE_NONE,
			      5,
			      G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);
	/**
	 * RBShell::database-load-complete:
	 * @shell: the #RBShell
	 *
	 * Emitted when the database has been loaded.  This is intended to allow
	 * DBus clients that start a new instance of the application to wait until
	 * a reasonable amount of state has been loaded before making further requests.
	 */
	rb_shell_signals[DATABASE_LOAD_COMPLETE] =
		g_signal_new ("database-load-complete",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBShellClass, database_load_complete),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);

	g_type_class_add_private (klass, sizeof (RBShellPrivate));
}

static void
rb_shell_init (RBShell *shell)
{
	shell->priv = G_TYPE_INSTANCE_GET_PRIVATE (shell, RB_TYPE_SHELL, RBShellPrivate);

	rb_user_data_dir ();

        rb_shell_session_init (shell);
}

static void
rb_shell_set_property (GObject *object,
		       guint prop_id,
		       const GValue *value,
		       GParamSpec *pspec)
{
	RBShell *shell = RB_SHELL (object);

	switch (prop_id)
	{
	case PROP_NO_REGISTRATION:
		shell->priv->no_registration = g_value_get_boolean (value);
		break;
	case PROP_NO_UPDATE:
		shell->priv->no_update = g_value_get_boolean (value);
		break;
	case PROP_DRY_RUN:
		shell->priv->dry_run = g_value_get_boolean (value);
		if (shell->priv->dry_run)
			shell->priv->no_registration = TRUE;
		break;
	case PROP_RHYTHMDB_FILE:
		g_free (shell->priv->rhythmdb_file);
		shell->priv->rhythmdb_file = g_value_dup_string (value);
		break;
	case PROP_PLAYLISTS_FILE:
		g_free (shell->priv->playlists_file);
		shell->priv->playlists_file = g_value_dup_string (value);
		break;
	case PROP_VISIBILITY:
		rb_shell_set_visibility (shell, FALSE, g_value_get_boolean (value));
		break;
	case PROP_AUTOSTARTED:
		shell->priv->autostarted = g_value_get_boolean (value);
		break;
	case PROP_DISABLE_PLUGINS:
		shell->priv->disable_plugins = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_shell_get_property (GObject *object,
		       guint prop_id,
		       GValue *value,
		       GParamSpec *pspec)
{
	RBShell *shell = RB_SHELL (object);

	switch (prop_id)
	{
	case PROP_NO_REGISTRATION:
		g_value_set_boolean (value, shell->priv->no_registration);
		break;
	case PROP_NO_UPDATE:
		g_value_set_boolean (value, shell->priv->no_update);
		break;
	case PROP_DRY_RUN:
		g_value_set_boolean (value, shell->priv->dry_run);
		break;
	case PROP_RHYTHMDB_FILE:
		g_value_set_string (value, shell->priv->rhythmdb_file);
		break;
	case PROP_PLAYLISTS_FILE:
		g_value_set_string (value, shell->priv->playlists_file);
		break;
	case PROP_DB:
		g_value_set_object (value, shell->priv->db);
		break;
	case PROP_UI_MANAGER:
		g_value_set_object (value, shell->priv->ui_manager);
		break;
	case PROP_CLIPBOARD:
		g_value_set_object (value, shell->priv->clipboard_shell);
		break;
	case PROP_PLAYLIST_MANAGER:
		g_value_set_object (value, shell->priv->playlist_manager);
		break;
	case PROP_SHELL_PLAYER:
		g_value_set_object (value, shell->priv->player_shell);
		break;
	case PROP_REMOVABLE_MEDIA_MANAGER:
		g_value_set_object (value, shell->priv->removable_media_manager);
		break;
	case PROP_SELECTED_PAGE:
		g_value_set_object (value, shell->priv->selected_page);
		break;
	case PROP_WINDOW:
		g_value_set_object (value, shell->priv->window);
		break;
	case PROP_PREFS:
		/* create the preferences window the first time we need it */
		if (shell->priv->prefs == NULL) {
			GtkWidget *content;

			shell->priv->prefs = rb_shell_preferences_new (shell->priv->sources);

			gtk_window_set_transient_for (GTK_WINDOW (shell->priv->prefs),
						      GTK_WINDOW (shell->priv->window));
			content = gtk_dialog_get_content_area (GTK_DIALOG (shell->priv->prefs));
			gtk_widget_show_all (content);
		}
		g_value_set_object (value, shell->priv->prefs);
		break;
	case PROP_QUEUE_SOURCE:
		g_value_set_object (value, shell->priv->queue_source);
		break;
	case PROP_LIBRARY_SOURCE:
		g_value_set_object (value, shell->priv->library_source);
		break;
	case PROP_DISPLAY_PAGE_MODEL:
		g_value_set_object (value, shell->priv->display_page_model);
		break;
	case PROP_DISPLAY_PAGE_TREE:
		g_value_set_object (value, shell->priv->display_page_tree);
		break;
	case PROP_VISIBILITY:
		g_value_set_boolean (value, rb_shell_get_visibility (shell));
		break;
	case PROP_SOURCE_HEADER:
		g_value_set_object (value, shell->priv->source_header);
		break;
	case PROP_TRACK_TRANSFER_QUEUE:
		g_value_set_object (value, shell->priv->track_transfer_queue);
		break;
	case PROP_AUTOSTARTED:
		g_value_set_boolean (value, shell->priv->autostarted);
		break;
	case PROP_DISABLE_PLUGINS:
		g_value_set_boolean (value, shell->priv->disable_plugins);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static gboolean
rb_shell_sync_state (RBShell *shell)
{
	if (shell->priv->dry_run) {
		rb_debug ("in dry-run mode, not syncing state");
		return FALSE;
	}

	if (!shell->priv->load_complete) {
		rb_debug ("load incomplete, not syncing state");
		return FALSE;
	}

	rb_debug ("saving playlists");
	rb_playlist_manager_save_playlists (shell->priv->playlist_manager,
					    TRUE);

	rb_debug ("saving db");
	rhythmdb_save (shell->priv->db);
	return FALSE;
}

static gboolean
idle_save_rhythmdb (RBShell *shell)
{
	rhythmdb_save (shell->priv->db);

	shell->priv->save_db_id = 0;

	return FALSE;
}

static gboolean
idle_save_playlist_manager (RBShell *shell)
{
	GDK_THREADS_ENTER ();
	rb_playlist_manager_save_playlists (shell->priv->playlist_manager,
					    FALSE);
	GDK_THREADS_LEAVE ();

	return TRUE;
}

static void
rb_shell_shutdown (RBShell *shell)
{
	GdkDisplay *display;

	if (shell->priv->shutting_down)
		return;
	shell->priv->shutting_down = TRUE;

	/* Hide the main window and tray icon as soon as possible */
	display = gtk_widget_get_display (shell->priv->window);
	gtk_widget_hide (shell->priv->window);
	gdk_display_sync (display);

	if (shell->priv->plugin_engine != NULL) {
		g_object_unref (shell->priv->plugin_engine);
		shell->priv->plugin_engine = NULL;
	}
	if (shell->priv->activatable != NULL) {
		g_object_unref (shell->priv->activatable);
		shell->priv->activatable = NULL;
	}
	if (shell->priv->plugin_settings != NULL) {
		g_object_unref (shell->priv->plugin_settings);
		shell->priv->plugin_settings = NULL;
	}
}

static void
rb_shell_finalize (GObject *object)
{
        RBShell *shell = RB_SHELL (object);

	rb_debug ("Finalizing shell");

	rb_shell_player_stop (shell->priv->player_shell);

	if (shell->priv->settings != NULL) {
		rb_settings_delayed_sync (shell->priv->settings, NULL, NULL, NULL);
		g_object_unref (shell->priv->settings);
	}

	g_free (shell->priv->cached_title);

	if (shell->priv->save_playlist_id > 0) {
		g_source_remove (shell->priv->save_playlist_id);
		shell->priv->save_playlist_id = 0;
	}

	if (shell->priv->save_db_id > 0) {
		g_source_remove (shell->priv->save_db_id);
		shell->priv->save_db_id = 0;
	}

	if (shell->priv->queue_sidebar != NULL) {
		g_object_unref (shell->priv->queue_sidebar);
	}

	rb_debug ("shutting down playlist manager");
	rb_playlist_manager_shutdown (shell->priv->playlist_manager);

	rb_debug ("unreffing playlist manager");
	g_object_unref (shell->priv->playlist_manager);

	rb_debug ("unreffing removable media manager");
	g_object_unref (shell->priv->removable_media_manager);
	g_object_unref (shell->priv->track_transfer_queue);

	rb_debug ("unreffing podcast manager");
	g_object_unref (shell->priv->podcast_manager);

	rb_debug ("unreffing clipboard shell");
	g_object_unref (shell->priv->clipboard_shell);

	rb_debug ("destroying prefs");
	if (shell->priv->prefs != NULL)
		gtk_widget_destroy (shell->priv->prefs);

	g_free (shell->priv->rhythmdb_file);

	g_free (shell->priv->playlists_file);

	rb_debug ("destroying window");
	gtk_widget_destroy (shell->priv->window);

	g_list_free (shell->priv->sources);
	shell->priv->sources = NULL;

	g_hash_table_destroy (shell->priv->sources_hash);

	rb_debug ("shutting down DB");
	rhythmdb_shutdown (shell->priv->db);

	rb_debug ("unreffing DB");
	g_object_unref (shell->priv->db);

        G_OBJECT_CLASS (rb_shell_parent_class)->finalize (object);

	rb_debug ("shell shutdown complete");
}

/**
 * rb_shell_new:
 * @no_registration: if %TRUE, single-instance features are disabled
 * @no_update: if %TRUE, don't update the database file
 * @dry_run: if %TRUE, don't write back file metadata changes
 * @autostarted: %TRUE if autostarted by the session manager
 * @disable_plugins: %TRUE if the plugins should be disabled
 * @rhythmdb: path to the database file
 * @playlists: path to the playlist file
 *
 * Creates the Rhythmbox shell.  This is effectively a singleton, so it doesn't
 * make sense to call this from anywhere other than main.c.
 *
 * Return value: the #RBShell instance
 */
RBShell *
rb_shell_new (gboolean no_registration,
	      gboolean no_update,
	      gboolean dry_run,
	      gboolean autostarted,
	      gboolean disable_plugins,
	      char *rhythmdb,
	      char *playlists)
{
	return g_object_new (RB_TYPE_SHELL,
			  "no-registration", no_registration,
			  "no-update", no_update,
			  "dry-run", dry_run, "rhythmdb-file", rhythmdb,
			  "playlists-file", playlists,
			  "autostarted", autostarted,
			  "disable-plugins", disable_plugins,
			  NULL);
}

static GMountOperation *
rb_shell_create_mount_op_cb (RhythmDB *db, RBShell *shell)
{
	/* we don't want the operation to be modal, so we don't associate it with the window. */
	GMountOperation *op = gtk_mount_operation_new (NULL);
	gtk_mount_operation_set_screen (GTK_MOUNT_OPERATION (op),
					gtk_window_get_screen (GTK_WINDOW (shell->priv->window)));
	return op;
}

static void
construct_db (RBShell *shell)
{
	GError *error = NULL;
	char *pathname;

	/* Initialize the database */
	rb_debug ("creating database object");
	rb_profile_start ("creating database object");

	if (shell->priv->rhythmdb_file) {
		pathname = g_strdup (shell->priv->rhythmdb_file);
	} else {
		pathname = rb_find_user_data_file ("rhythmdb.xml", &error);
		if (error != NULL) {
			rb_error_dialog (GTK_WINDOW (shell->priv->window),
					 _("Unable to move user data files"),
					 "%s", error->message);
			g_error_free (error);
		}
	}

#ifdef WITH_RHYTHMDB_TREE
	shell->priv->db = rhythmdb_tree_new (pathname);
#elif defined(WITH_RHYTHMDB_GDA)
	shell->priv->db = rhythmdb_gda_new (pathname);
#endif
	g_free (pathname);

	if (shell->priv->dry_run)
		g_object_set (shell->priv->db, "dry-run", TRUE, NULL);
	if (shell->priv->no_update)
		g_object_set (shell->priv->db, "no-update", TRUE, NULL);

	g_signal_connect_object (G_OBJECT (shell->priv->db), "load-complete",
				 G_CALLBACK (rb_shell_load_complete_cb), shell,
				 0);
	g_signal_connect_object (G_OBJECT (shell->priv->db), "create-mount-op",
				 G_CALLBACK (rb_shell_create_mount_op_cb), shell,
				 0);

	rb_profile_end ("creating database object");
}

static void
construct_widgets (RBShell *shell)
{
	GtkWindow *win;

	rb_profile_start ("constructing widgets");

	/* initialize UI */
	win = GTK_WINDOW (gtk_window_new (GTK_WINDOW_TOPLEVEL));
	gtk_window_set_title (win, _("Rhythmbox"));

	shell->priv->window = GTK_WIDGET (win);
	shell->priv->iconified = FALSE;
	g_signal_connect_object (G_OBJECT (win), "window-state-event",
				 G_CALLBACK (rb_shell_window_state_cb),
				 shell, 0);

	g_signal_connect_object (G_OBJECT (win), "configure-event",
				 G_CALLBACK (rb_shell_window_configure_cb),
				 shell, 0);

	/* connect after, so that things can affect behaviour */
	g_signal_connect_object (G_OBJECT (win), "delete_event",
				 G_CALLBACK (rb_shell_window_delete_cb),
				 shell, G_CONNECT_AFTER);

	gtk_widget_add_events (GTK_WIDGET (win), GDK_KEY_PRESS_MASK);
	g_signal_connect_object (G_OBJECT(win), "key_press_event",
				 G_CALLBACK (rb_shell_key_press_event_cb), shell, 0);

	rb_debug ("shell: initializing shell services");

	shell->priv->podcast_manager = rb_podcast_manager_new (shell->priv->db);
	shell->priv->track_transfer_queue = rb_track_transfer_queue_new (shell);
	shell->priv->ui_manager = gtk_ui_manager_new ();
	shell->priv->source_ui_merge_id = gtk_ui_manager_new_merge_id (shell->priv->ui_manager);

	shell->priv->player_shell = rb_shell_player_new (shell->priv->db,
							 shell->priv->ui_manager,
							 shell->priv->actiongroup);
	g_signal_connect_object (G_OBJECT (shell->priv->player_shell),
				 "playing-source-changed",
				 G_CALLBACK (rb_shell_playing_source_changed_cb),
				 shell, 0);
	g_signal_connect_object (G_OBJECT (shell->priv->player_shell),
				 "notify::playing-from-queue",
				 G_CALLBACK (rb_shell_playing_from_queue_cb),
				 shell, 0);
	g_signal_connect_object (G_OBJECT (shell->priv->player_shell),
				 "window_title_changed",
				 G_CALLBACK (rb_shell_player_window_title_changed_cb),
				 shell, 0);
	shell->priv->clipboard_shell = rb_shell_clipboard_new (shell->priv->actiongroup,
							       shell->priv->ui_manager,
							       shell->priv->db);
	shell->priv->source_header = rb_source_header_new (shell->priv->ui_manager,
							   shell->priv->actiongroup);
	gtk_widget_show_all (GTK_WIDGET (shell->priv->source_header));

	shell->priv->display_page_tree = rb_display_page_tree_new (shell);
	gtk_widget_show_all (GTK_WIDGET (shell->priv->display_page_tree));
	g_signal_connect_object (shell->priv->display_page_tree, "drop-received",
				 G_CALLBACK (display_page_tree_drag_received_cb), shell, 0);
	g_object_get (shell->priv->display_page_tree, "model", &shell->priv->display_page_model, NULL);
	rb_display_page_group_add_core_groups (G_OBJECT (shell), shell->priv->display_page_model);

	shell->priv->statusbar = rb_statusbar_new (shell->priv->db,
						   shell->priv->ui_manager,
						   shell->priv->track_transfer_queue);
	g_object_set (shell->priv->player_shell, "statusbar", shell->priv->statusbar, NULL);
	gtk_widget_show (GTK_WIDGET (shell->priv->statusbar));

	g_signal_connect_object (shell->priv->display_page_tree, "selected",
				 G_CALLBACK (display_page_selected_cb), shell, 0);

	shell->priv->notebook = gtk_notebook_new ();
	gtk_widget_show (shell->priv->notebook);
	gtk_notebook_set_show_tabs (GTK_NOTEBOOK (shell->priv->notebook), FALSE);
	gtk_notebook_set_show_border (GTK_NOTEBOOK (shell->priv->notebook), FALSE);
	g_signal_connect_object (shell->priv->display_page_tree,
				 "size-allocate",
				 G_CALLBACK (paned_size_allocate_cb),
				 shell, 0);

	shell->priv->queue_source = RB_PLAYLIST_SOURCE (rb_play_queue_source_new (shell));
	g_object_set (shell->priv->player_shell, "queue-source", shell->priv->queue_source, NULL);
	g_object_set (shell->priv->clipboard_shell, "queue-source", shell->priv->queue_source, NULL);
	rb_shell_append_display_page (shell, RB_DISPLAY_PAGE (shell->priv->queue_source), RB_DISPLAY_PAGE_GROUP_LIBRARY);
	g_object_get (shell->priv->queue_source, "sidebar", &shell->priv->queue_sidebar, NULL);
	gtk_widget_show_all (shell->priv->queue_sidebar);
	gtk_widget_set_no_show_all (shell->priv->queue_sidebar, TRUE);

	/* places for plugins to put UI */
	shell->priv->top_container = GTK_BOX (gtk_vbox_new (FALSE, 0));
	shell->priv->bottom_container = GTK_BOX (gtk_vbox_new (FALSE, 0));
	shell->priv->sidebar_container = GTK_BOX (gtk_vbox_new (FALSE, 0));
	shell->priv->right_sidebar_container = GTK_BOX (gtk_vbox_new (FALSE, 0));

	/* set up sidebars */
	shell->priv->paned = gtk_hpaned_new ();
	shell->priv->right_paned = gtk_hpaned_new ();
	gtk_widget_show_all (shell->priv->right_paned);
	g_signal_connect_object (G_OBJECT (shell->priv->right_paned),
				 "size-allocate",
				 G_CALLBACK (paned_size_allocate_cb),
				 shell, 0);
	gtk_widget_set_no_show_all (shell->priv->right_paned, TRUE);
	{
		GtkWidget *vbox2 = gtk_vbox_new (FALSE, 0);

		shell->priv->queue_paned = gtk_vpaned_new ();
		gtk_paned_pack1 (GTK_PANED (shell->priv->queue_paned),
				 GTK_WIDGET (shell->priv->display_page_tree),
				 FALSE, TRUE);
		gtk_paned_pack2 (GTK_PANED (shell->priv->queue_paned),
				 shell->priv->queue_sidebar,
				 TRUE, TRUE);
		gtk_container_child_set (GTK_CONTAINER (shell->priv->queue_paned),
					 GTK_WIDGET (shell->priv->display_page_tree),
					 "resize", FALSE,
					 NULL);

		gtk_box_pack_start (GTK_BOX (vbox2),
				    GTK_WIDGET (shell->priv->source_header),
				    FALSE, FALSE, 3);
		gtk_box_pack_start (GTK_BOX (vbox2),
				    shell->priv->notebook,
				    TRUE, TRUE, 0);
		gtk_box_pack_start (GTK_BOX (vbox2),
				    GTK_WIDGET (shell->priv->bottom_container),
				    FALSE, FALSE, 0);

		gtk_paned_pack1 (GTK_PANED (shell->priv->right_paned),
				 vbox2, TRUE, TRUE);
		gtk_paned_pack2 (GTK_PANED (shell->priv->right_paned),
				 GTK_WIDGET (shell->priv->right_sidebar_container),
				 FALSE, FALSE);
		gtk_widget_hide (GTK_WIDGET(shell->priv->right_sidebar_container));

		gtk_box_pack_start (shell->priv->sidebar_container,
				    shell->priv->queue_paned,
				    TRUE, TRUE, 0);
		gtk_paned_pack1 (GTK_PANED (shell->priv->paned),
				 GTK_WIDGET (shell->priv->sidebar_container),
				 FALSE, TRUE);
		gtk_paned_pack2 (GTK_PANED (shell->priv->paned),
				 shell->priv->right_paned,
				 TRUE, TRUE);
		gtk_widget_show (vbox2);
	}

	g_signal_connect_object (G_OBJECT (shell->priv->queue_paned),
				 "size-allocate",
				 G_CALLBACK (paned_size_allocate_cb),
				 shell, 0);
	gtk_widget_show (shell->priv->paned);

	shell->priv->main_vbox = gtk_vbox_new (FALSE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (shell->priv->main_vbox), 0);
	gtk_box_pack_start (GTK_BOX (shell->priv->main_vbox), GTK_WIDGET (shell->priv->player_shell), FALSE, TRUE, 6);
	gtk_widget_show (GTK_WIDGET (shell->priv->player_shell));

	gtk_box_pack_start (GTK_BOX (shell->priv->main_vbox), GTK_WIDGET (shell->priv->top_container), FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (shell->priv->main_vbox), shell->priv->paned, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (shell->priv->main_vbox), GTK_WIDGET (shell->priv->statusbar), FALSE, TRUE, 0);
	gtk_widget_show_all (shell->priv->main_vbox);

	gtk_container_add (GTK_CONTAINER (win), shell->priv->main_vbox);

	rb_profile_end ("constructing widgets");
}

static void
construct_sources (RBShell *shell)
{
	RBDisplayPage *page_group;
	GError *error = NULL;
	char *pathname;

	rb_profile_start ("constructing sources");

	page_group = RB_DISPLAY_PAGE_GROUP_LIBRARY;
	shell->priv->library_source = RB_LIBRARY_SOURCE (rb_library_source_new (shell));
	shell->priv->podcast_source = RB_PODCAST_SOURCE (rb_podcast_main_source_new (shell, shell->priv->podcast_manager));
	shell->priv->missing_files_source = rb_missing_files_source_new (shell, shell->priv->library_source);

	shell->priv->import_errors_source = rb_import_errors_source_new (shell,
									 RHYTHMDB_ENTRY_TYPE_IMPORT_ERROR,
									 RHYTHMDB_ENTRY_TYPE_SONG,
									 RHYTHMDB_ENTRY_TYPE_IGNORE);

	rb_shell_append_display_page (shell, RB_DISPLAY_PAGE (shell->priv->library_source), page_group);
	rb_shell_append_display_page (shell, RB_DISPLAY_PAGE (shell->priv->podcast_source), page_group);
	rb_shell_append_display_page (shell, RB_DISPLAY_PAGE (shell->priv->missing_files_source), page_group);
	rb_shell_append_display_page (shell, RB_DISPLAY_PAGE (shell->priv->import_errors_source), page_group);

	rb_podcast_main_source_add_subsources (RB_PODCAST_MAIN_SOURCE (shell->priv->podcast_source));

	/* Find the playlist name if none supplied */
	if (shell->priv->playlists_file) {
		pathname = g_strdup (shell->priv->playlists_file);
	} else {
		pathname = rb_find_user_data_file ("playlists.xml", &error);
		if (error != NULL) {
			rb_error_dialog (GTK_WINDOW (shell->priv->window),
					 _("Unable to move user data files"),
					 "%s", error->message);
			g_error_free (error);
		}
	}

	/* Initialize playlist manager */
	rb_debug ("shell: creating playlist manager");
	shell->priv->playlist_manager = rb_playlist_manager_new (shell,
								 shell->priv->display_page_model,
								 shell->priv->display_page_tree,
								 pathname);

	g_object_set (shell->priv->clipboard_shell,
		      "playlist-manager", shell->priv->playlist_manager,
		      NULL);

	g_signal_connect_object (G_OBJECT (shell->priv->playlist_manager), "playlist_added",
				 G_CALLBACK (rb_shell_playlist_added_cb), shell, 0);
	g_signal_connect_object (G_OBJECT (shell->priv->playlist_manager), "playlist_created",
				 G_CALLBACK (rb_shell_playlist_created_cb), shell, 0);

	/* Initialize removable media manager */
	rb_debug ("shell: creating removable media manager");
	shell->priv->removable_media_manager = rb_removable_media_manager_new (shell);

	g_signal_connect_object (G_OBJECT (shell->priv->removable_media_manager), "medium_added",
				 G_CALLBACK (rb_shell_medium_added_cb), shell, 0);


	g_free (pathname);

	rb_profile_end ("constructing sources");
}

static void
construct_load_ui (RBShell *shell)
{
	GtkWidget *menubar;
	GtkWidget *toolbar;
	GtkToolItem *tool_item;
	GError *error = NULL;

	rb_debug ("shell: loading ui");
	rb_profile_start ("loading ui");

	gtk_ui_manager_insert_action_group (shell->priv->ui_manager,
					    shell->priv->actiongroup, 0);
	gtk_ui_manager_add_ui_from_file (shell->priv->ui_manager,
					 rb_file ("rhythmbox-ui.xml"), &error);

	gtk_ui_manager_ensure_update (shell->priv->ui_manager);
	gtk_window_add_accel_group (GTK_WINDOW (shell->priv->window),
				    gtk_ui_manager_get_accel_group (shell->priv->ui_manager));
	menubar = gtk_ui_manager_get_widget (shell->priv->ui_manager, "/MenuBar");

	gtk_box_pack_start (GTK_BOX (shell->priv->main_vbox), menubar, FALSE, FALSE, 0);
	gtk_box_reorder_child (GTK_BOX (shell->priv->main_vbox), menubar, 0);

	toolbar = gtk_ui_manager_get_widget (shell->priv->ui_manager, "/ToolBar");
	gtk_style_context_add_class (gtk_widget_get_style_context (toolbar),
				     GTK_STYLE_CLASS_PRIMARY_TOOLBAR);
	g_settings_bind (shell->priv->settings, "toolbar-visible",
			 toolbar, "visible",
			 G_SETTINGS_BIND_DEFAULT);
	gtk_box_pack_start (GTK_BOX (shell->priv->main_vbox), toolbar, FALSE, FALSE, 0);
	gtk_box_reorder_child (GTK_BOX (shell->priv->main_vbox), toolbar, 1);
	gtk_widget_show (toolbar);

	shell->priv->volume_button = gtk_volume_button_new ();
	g_signal_connect (shell->priv->volume_button, "value-changed",
			  G_CALLBACK (rb_shell_volume_widget_changed_cb),
			  shell);
	g_signal_connect (shell->priv->player_shell, "notify::volume",
			  G_CALLBACK (rb_shell_player_volume_changed_cb),
			  shell);
	rb_shell_player_volume_changed_cb (shell->priv->player_shell, NULL, shell);

	tool_item = gtk_tool_item_new ();
	gtk_tool_item_set_expand (tool_item, TRUE);
	gtk_widget_show (GTK_WIDGET (tool_item));
	gtk_toolbar_insert (GTK_TOOLBAR (toolbar), tool_item, -1);

	tool_item = gtk_tool_item_new ();
	gtk_container_add (GTK_CONTAINER (tool_item), shell->priv->volume_button);
	gtk_widget_show_all (GTK_WIDGET (tool_item));
	gtk_toolbar_insert (GTK_TOOLBAR (toolbar), tool_item, -1);

	gtk_widget_set_tooltip_text (shell->priv->volume_button,
				     _("Change the music volume"));

	if (error != NULL) {
		g_warning ("Couldn't merge %s: %s",
			   rb_file ("rhythmbox-ui.xml"), error->message);
		g_clear_error (&error);
	}

	rb_profile_end ("loading ui");
}

static void
extension_added_cb (PeasExtensionSet *set, PeasPluginInfo *info, PeasExtension *extension, RBShell *shell)
{
	rb_debug ("activating extension %s", peas_plugin_info_get_name (info));
	peas_extension_call (extension, "activate");
}

static void
extension_removed_cb (PeasExtensionSet *set, PeasPluginInfo *info, PeasExtension *extension, RBShell *shell)
{
	rb_debug ("deactivating extension %s", peas_plugin_info_get_name (info));
	peas_extension_call (extension, "deactivate");
}

static void
construct_plugins (RBShell *shell)
{
	char *typelib_dir;
	char *plugindir;
	char *plugindatadir;
	const GList *plugins;
	const GList *l;
	GError *error = NULL;

	if (shell->priv->disable_plugins) {
		return;
	}

	rb_profile_start ("loading plugins");
	shell->priv->plugin_settings = g_settings_new ("org.gnome.rhythmbox.plugins");

	shell->priv->plugin_engine = peas_engine_new ();
	/* need an #ifdef for this? */
	peas_engine_enable_loader (shell->priv->plugin_engine, "python");

	typelib_dir = g_build_filename (LIBDIR,
					"girepository-1.0",
					NULL);
	if (g_irepository_require_private (g_irepository_get_default (),
					   typelib_dir, "MPID", "3.0", 0, &error) == FALSE) {
		g_clear_error (&error);
		if (g_irepository_require (g_irepository_get_default (), "MPID", "3.0", 0, &error) == FALSE) {
			g_warning ("Could not load MPID typelib: %s", error->message);
			g_clear_error (&error);
		}
	}

	if (g_irepository_require_private (g_irepository_get_default (),
					   typelib_dir, "RB", "3.0", 0, &error) == FALSE) {
		g_clear_error (&error);
		if (g_irepository_require (g_irepository_get_default (), "RB", "3.0", 0, &error) == FALSE) {
			g_warning ("Could not load RB typelib: %s", error->message);
			g_clear_error (&error);
		}
	}
	g_free (typelib_dir);

	if (g_irepository_require (g_irepository_get_default (), "Peas", "1.0", 0, &error) == FALSE) {
		g_warning ("Could not load Peas typelib: %s", error->message);
		g_clear_error (&error);
	}

	if (g_irepository_require (g_irepository_get_default (), "PeasGtk", "1.0", 0, &error) == FALSE) {
		g_warning ("Could not load PeasGtk typelib: %s", error->message);
		g_clear_error (&error);
	}

	plugindir = g_build_filename (rb_user_data_dir (), "plugins", NULL);
	rb_debug ("plugin search path: %s", plugindir);
	peas_engine_add_search_path (shell->priv->plugin_engine,
				     plugindir,
				     plugindir);
	g_free (plugindir);

	plugindir = g_build_filename (LIBDIR, "rhythmbox", "plugins", NULL);
	plugindatadir = g_build_filename (DATADIR, "rhythmbox", "plugins", NULL);
	rb_debug ("plugin search path: %s / %s", plugindir, plugindatadir);
	peas_engine_add_search_path (shell->priv->plugin_engine,
				     plugindir,
				     plugindatadir);
	g_free (plugindir);
	g_free (plugindatadir);

#ifdef USE_UNINSTALLED_DIRS
	plugindir = g_build_filename (SHARE_UNINSTALLED_BUILDDIR, "..", UNINSTALLED_PLUGINS_LOCATION, NULL);
	rb_debug ("plugin search path: %s", plugindir);
	peas_engine_add_search_path (shell->priv->plugin_engine,
				     plugindir,
				     plugindir);
	g_free (plugindir);
#endif

	shell->priv->activatable = peas_extension_set_new (shell->priv->plugin_engine,
							   PEAS_TYPE_ACTIVATABLE,
							   "object", shell,
							   NULL);
	g_signal_connect (shell->priv->activatable, "extension-added", G_CALLBACK (extension_added_cb), shell);
	g_signal_connect (shell->priv->activatable, "extension-removed", G_CALLBACK (extension_removed_cb), shell);

	g_settings_bind (shell->priv->plugin_settings,
			 "active-plugins",
			 shell->priv->plugin_engine,
			 "loaded-plugins",
			 G_SETTINGS_BIND_DEFAULT);

	/* load builtin plugins */
	plugins = peas_engine_get_plugin_list (shell->priv->plugin_engine);
	for (l = plugins; l != NULL; l = l->next) {
		PeasPluginInfo *info = PEAS_PLUGIN_INFO (l->data);
		if (peas_plugin_info_is_builtin (info) &&
		    g_strcmp0 (peas_plugin_info_get_module_name (info), "rb") != 0) {
			peas_engine_load_plugin (shell->priv->plugin_engine, info);
		}
	}

	rb_profile_end ("loading plugins");
}

static gboolean
_scan_idle (RBShell *shell)
{
	GDK_THREADS_ENTER ();
	rb_removable_media_manager_scan (shell->priv->removable_media_manager);
	GDK_THREADS_LEAVE ();
	g_signal_emit (shell, rb_shell_signals[REMOVABLE_MEDIA_SCAN_FINISHED], 0);
	return FALSE;
}

static void
rb_shell_constructed (GObject *object)
{
	RBShell *shell;
	GtkAction *action;

	RB_CHAIN_GOBJECT_METHOD (rb_shell_parent_class, constructed, object);

	shell = RB_SHELL (object);

	rb_debug ("Constructing shell");
	rb_profile_start ("constructing shell");

	shell->priv->settings = g_settings_new ("org.gnome.rhythmbox");

	shell->priv->actiongroup = gtk_action_group_new ("MainActions");
	gtk_action_group_set_translation_domain (shell->priv->actiongroup,
						 GETTEXT_PACKAGE);
	gtk_action_group_add_actions (shell->priv->actiongroup,
				      rb_shell_actions,
				      rb_shell_n_actions, shell);
	gtk_action_group_add_toggle_actions (shell->priv->actiongroup,
					     rb_shell_toggle_entries,
					     rb_shell_n_toggle_entries,
					     shell);

	construct_db (shell);

	/* initialize shell services */
	construct_widgets (shell);

	g_signal_connect_object (shell->priv->settings, "changed", G_CALLBACK (settings_changed_cb), shell, 0);

	action = gtk_action_group_get_action (shell->priv->actiongroup, "ViewSidePane");
	g_settings_bind (shell->priv->settings, "display-page-tree-visible",
			 action, "active",
			 G_SETTINGS_BIND_DEFAULT);
	g_settings_bind (shell->priv->settings, "display-page-tree-visible",
			 shell->priv->sidebar_container, "visible",
			 G_SETTINGS_BIND_DEFAULT);

	action = gtk_action_group_get_action (shell->priv->actiongroup, "ViewToolbar");
	g_settings_bind (shell->priv->settings, "toolbar-visible",
			 action, "active",
			 G_SETTINGS_BIND_DEFAULT);

	rb_debug ("shell: syncing with settings");
	rb_shell_sync_pane_visibility (shell);

	g_signal_connect_object (G_OBJECT (shell->priv->db), "save-error",
				 G_CALLBACK (rb_shell_db_save_error_cb), shell, 0);

	construct_sources (shell);

	construct_load_ui (shell);

	construct_plugins (shell);

	rb_shell_sync_window_state (shell, FALSE);
	rb_shell_sync_smalldisplay (shell);
	rb_shell_sync_party_mode (shell);
	rb_shell_sync_toolbar_state (shell);

	rb_shell_select_page (shell, RB_DISPLAY_PAGE (shell->priv->library_source));

	/* by now we've added the built in sources and any sources from plugins,
	 * so we can consider the fixed page groups loaded
	 */
	rb_display_page_group_loaded (RB_DISPLAY_PAGE_GROUP (RB_DISPLAY_PAGE_GROUP_LIBRARY));
	rb_display_page_group_loaded (RB_DISPLAY_PAGE_GROUP (RB_DISPLAY_PAGE_GROUP_STORES));

	rb_missing_plugins_init (GTK_WINDOW (shell->priv->window));

	g_idle_add ((GSourceFunc)_scan_idle, shell);

	/* GO GO GO! */
	rb_debug ("loading database");
	rhythmdb_load (shell->priv->db);

	rb_debug ("shell: syncing window state");
	rb_shell_sync_paned (shell);

	/* set initial visibility */
	rb_shell_set_visibility (shell, TRUE, TRUE);

	gdk_notify_startup_complete ();

	/* focus play if small, the entry view if not */
	if (g_settings_get_boolean (shell->priv->settings, "small-display")) {
		GtkWidget *play_button;

		play_button = gtk_ui_manager_get_widget (shell->priv->ui_manager, "/ToolBar/Play");
		gtk_widget_grab_focus (play_button);
	} else {
		RBEntryView *view;

		view = rb_source_get_entry_view (RB_SOURCE (shell->priv->library_source));
		if (view != NULL) {
			gtk_widget_grab_focus (GTK_WIDGET (view));
		}
	}

	rb_profile_end ("constructing shell");
}

static gboolean
rb_shell_window_state_cb (GtkWidget *widget,
			  GdkEventWindowState *event,
			  RBShell *shell)
{
	shell->priv->iconified = ((event->new_window_state & GDK_WINDOW_STATE_ICONIFIED) != 0);

	if (event->changed_mask & (GDK_WINDOW_STATE_WITHDRAWN | GDK_WINDOW_STATE_ICONIFIED)) {
		g_signal_emit (shell, rb_shell_signals[VISIBILITY_CHANGED], 0,
			       rb_shell_get_visibility (shell));
	}

	/* don't save maximized state when is hidden */
	if (!gtk_widget_get_visible (shell->priv->window))
		return FALSE;

	if (event->changed_mask & GDK_WINDOW_STATE_MAXIMIZED) {
		gboolean maximised = ((event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) != 0);

		if (!g_settings_get_boolean (shell->priv->settings, "small-display")) {
			g_settings_set_boolean (shell->priv->settings,
						"maximized",
						maximised);
		}
		rb_shell_sync_window_state (shell, TRUE);
		rb_shell_sync_paned (shell);
	}

	return FALSE;
}

static gboolean
rb_shell_visibility_changing (RBShell *shell, gboolean initial, gboolean visible)
{
	return visible;
}

static gboolean
rb_shell_get_visibility (RBShell *shell)
{
	GdkWindowState state;

	if (!gtk_widget_get_realized (shell->priv->window))
		return FALSE;
	if (shell->priv->iconified)
		return FALSE;

	state = gdk_window_get_state (gtk_widget_get_window (GTK_WIDGET (shell->priv->window)));
	if (state & (GDK_WINDOW_STATE_WITHDRAWN | GDK_WINDOW_STATE_ICONIFIED))
		return FALSE;

	return TRUE;
}

static void
rb_shell_set_visibility (RBShell *shell,
			 gboolean initial,
			 gboolean visible)
{
	gboolean really_visible;

	rb_profile_start ("changing shell visibility");

	if (visible == rb_shell_get_visibility (shell)) {
		rb_profile_end ("changing shell visibility");
		return;
	}

	really_visible = visible;
	g_signal_emit (shell, rb_shell_signals[VISIBILITY_CHANGING], 0, initial, visible, &really_visible);

	if (really_visible) {
		rb_debug ("showing main window");
		rb_shell_sync_window_state (shell, FALSE);

		gtk_widget_show (GTK_WIDGET (shell->priv->window));
		gtk_window_deiconify (GTK_WINDOW (shell->priv->window));

		if (gtk_widget_get_realized (GTK_WIDGET (shell->priv->window)))
			rb_shell_present (shell, gtk_get_current_event_time (), NULL);
		else
			gtk_widget_show_all (GTK_WIDGET (shell->priv->window));

		g_signal_emit (shell, rb_shell_signals[VISIBILITY_CHANGED], 0, visible);
	} else {
		rb_debug ("hiding main window");
		shell->priv->iconified = TRUE;
		gtk_window_iconify (GTK_WINDOW (shell->priv->window));

		g_signal_emit (shell, rb_shell_signals[VISIBILITY_CHANGED], 0, FALSE);
	}

	rb_profile_end ("changing shell visibility");
}

static void
sync_window_settings (GSettings *settings, RBShell *shell)
{
	int width, height;
	int oldx, oldy;
	int x, y;
	int pos;

	gtk_window_get_size (GTK_WINDOW (shell->priv->window), &width, &height);
	if (g_settings_get_boolean (shell->priv->settings, "small-display")) {
		if (width != g_settings_get_int (shell->priv->settings, "small-width")) {
			rb_debug ("storing small window width of %d", width);
			g_settings_set_int (shell->priv->settings, "small-width", width);
		}
	} else {
		int oldwidth, oldheight;

		g_settings_get (shell->priv->settings, "size", "(ii)", &oldwidth, &oldheight);
		if ((width != oldwidth) || (height != oldheight)) {
			rb_debug ("storing window size of %d:%d", width, height);
			g_settings_set (shell->priv->settings, "size", "(ii)", width, height);
		}
	}

	gtk_window_get_position (GTK_WINDOW(shell->priv->window), &x, &y);
	g_settings_get (shell->priv->settings, "position", "(ii)", &oldx, &oldy);
	if ((x != oldx) || (y != oldy)) {
		rb_debug ("storing window position of %d:%d", x, y);
		g_settings_set (shell->priv->settings, "position", "(ii)", x, y);
	}

	pos = gtk_paned_get_position (GTK_PANED (shell->priv->paned));
	rb_debug ("paned position %d", pos);

	if (pos != g_settings_get_int (shell->priv->settings, "paned-position")) {
		g_settings_set_int (shell->priv->settings, "paned-position", pos);
	}

	pos = gtk_paned_get_position (GTK_PANED (shell->priv->right_paned));
	rb_debug ("right_paned position %d", pos);

	if (pos != g_settings_get_int (shell->priv->settings, "right-paned-position")) {
		g_settings_set_int (shell->priv->settings, "right-paned-position", pos);
	}

	pos = gtk_paned_get_position (GTK_PANED (shell->priv->queue_paned));
	rb_debug ("sidebar paned position %d", pos);

	if (pos != g_settings_get_int (shell->priv->settings, "display-page-tree-height")) {
		g_settings_set_int (shell->priv->settings, "display-page-tree-height", pos);
	}
}

static gboolean
rb_shell_window_configure_cb (GtkWidget *win,
			      GdkEventConfigure *event,
			      RBShell *shell)
{
	if (g_settings_get_boolean (shell->priv->settings, "maximized") || shell->priv->iconified)
		return FALSE;

	rb_settings_delayed_sync (shell->priv->settings,
				  (RBDelayedSyncFunc) sync_window_settings,
				  g_object_ref (shell),
				  g_object_unref);
	return FALSE;
}

static gboolean
rb_shell_window_delete_cb (GtkWidget *win,
			   GdkEventAny *event,
			   RBShell *shell)
{
	if (shell->priv->party_mode) {
		return TRUE;
	}

	rb_shell_quit (shell, NULL);

	return TRUE;
}

static gboolean
rb_shell_key_press_event_cb (GtkWidget *win,
			     GdkEventKey *event,
			     RBShell *shell)
{
#ifndef HAVE_MMKEYS
	return FALSE;
#else

	gboolean retval = TRUE;

	switch (event->keyval) {
	case XF86XK_Back:
		rb_shell_player_do_previous (shell->priv->player_shell, NULL);
		break;
	case XF86XK_Forward:
		rb_shell_player_do_next (shell->priv->player_shell, NULL);
		break;
	default:
		retval = FALSE;
	}

	return retval;
#endif /* !HAVE_MMKEYS */
}

static void
rb_shell_sync_window_state (RBShell *shell,
			    gboolean dont_maximise)
{
	GdkGeometry hints;
	int x, y;

	rb_profile_start ("syncing window state");

	if (g_settings_get_boolean (shell->priv->settings, "small-display")) {
		int width;

		width = g_settings_get_int (shell->priv->settings, "small-width");
		hints.min_height = -1;
		hints.min_width = -1;
		hints.max_height = -1;
		hints.max_width = 3000;
		gtk_window_set_default_size (GTK_WINDOW (shell->priv->window), width, 0);
		gtk_window_resize (GTK_WINDOW (shell->priv->window), width, 1);
		gtk_window_set_geometry_hints (GTK_WINDOW (shell->priv->window),
						NULL,
						&hints,
						GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE);
		gtk_window_unmaximize (GTK_WINDOW (shell->priv->window));
		rb_debug ("syncing small window width to %d", width);
	} else {
		int width, height;
		if (!dont_maximise) {
			if (g_settings_get_boolean (shell->priv->settings, "maximized"))
				gtk_window_maximize (GTK_WINDOW (shell->priv->window));
			else
				gtk_window_unmaximize (GTK_WINDOW (shell->priv->window));
		}

		g_settings_get (shell->priv->settings, "size", "(ii)", &width, &height);

		gtk_window_set_default_size (GTK_WINDOW (shell->priv->window), width, height);
		gtk_window_resize (GTK_WINDOW (shell->priv->window), width, height);
		gtk_window_set_geometry_hints (GTK_WINDOW (shell->priv->window),
						NULL,
						&hints,
						0);
	}

	g_settings_get (shell->priv->settings, "position", "(ii)", &x, &y);
	gtk_window_move (GTK_WINDOW (shell->priv->window), x, y);
	rb_profile_end ("syncing window state");
}

static void
display_page_selected_cb (RBDisplayPageTree *display_page_tree,
			  RBDisplayPage *page,
			  RBShell *shell)
{
	rb_debug ("page selected");
	rb_shell_select_page (shell, page);
}

gboolean
rb_shell_activate_source (RBShell *shell, RBSource *source, guint play, GError **error)
{
	RhythmDBEntry *entry;
	/* FIXME
	 *
	 * this doesn't work correctly yet, but it's still an improvement on the
	 * previous behaviour.
	 *
	 * with crossfading enabled, this fades out the current song, but
	 * doesn't start the new one.
	 */

	/* Select the new one, and optionally start it playing */
	rb_shell_select_page (shell, RB_DISPLAY_PAGE (source));

	switch (play) {
	case RB_SHELL_ACTIVATION_SELECT:
		return TRUE;

	case RB_SHELL_ACTIVATION_PLAY:
		entry = rb_shell_player_get_playing_entry (shell->priv->player_shell);
		if (entry != NULL) {
			rhythmdb_entry_unref (entry);
			return TRUE;
		}
		/* fall through */
	case RB_SHELL_ACTIVATION_ALWAYS_PLAY:
		rb_shell_player_set_playing_source (shell->priv->player_shell, source);
		return rb_shell_player_playpause (shell->priv->player_shell, FALSE, error);

	default:
		return FALSE;
	}
}

static void
rb_shell_db_save_error_cb (RhythmDB *db,
			   const char *uri, const GError *error,
			   RBShell *shell)
{
	rb_error_dialog (GTK_WINDOW (shell->priv->window),
			 _("Error while saving song information"),
			 "%s", error->message);
}

/**
 * rb_shell_get_source_by_entry_type:
 * @shell: the #RBShell
 * @type: entry type for which to find a source
 *
 * Looks up and returns the source that owns entries of the specified
 * type.
 *
 * Return value: (transfer none): source instance, if any
 */
RBSource *
rb_shell_get_source_by_entry_type (RBShell *shell,
				   RhythmDBEntryType *type)
{
	return g_hash_table_lookup (shell->priv->sources_hash, type);
}

/**
 * rb_shell_register_entry_type_for_source:
 * @shell: the #RBShell
 * @source: the #RBSource to register
 * @type: the #RhythmDBEntryType to register for
 *
 * Registers a source as the owner of entries of the specified type.
 * The main effect of this is that calling #rb_shell_get_source_by_entry_type
 * with the same entry type will return the source.  A source should only
 * be registered as the owner of a single entry type.
 */
void
rb_shell_register_entry_type_for_source (RBShell *shell,
					 RBSource *source,
					 RhythmDBEntryType *type)
{
	if (shell->priv->sources_hash == NULL) {
		shell->priv->sources_hash = g_hash_table_new (g_direct_hash,
							      g_direct_equal);
	}
	g_assert (g_hash_table_lookup (shell->priv->sources_hash, type) == NULL);
	g_hash_table_insert (shell->priv->sources_hash, type, source);
}

/**
 * rb_shell_append_display_page:
 * @shell: the #RBShell
 * @page: the new #RBDisplayPage
 * @parent: (allow-none): the parent page for the new page
 *
 * Adds a new display page to the shell.
 */
void
rb_shell_append_display_page (RBShell *shell, RBDisplayPage *page, RBDisplayPage *parent)
{
	if (RB_IS_SOURCE (page)) {
		shell->priv->sources = g_list_append (shell->priv->sources, RB_SOURCE (page));
	}

	g_signal_connect_object (G_OBJECT (page), "deleted",
				 G_CALLBACK (rb_shell_display_page_deleted_cb), shell, 0);

	gtk_notebook_append_page (GTK_NOTEBOOK (shell->priv->notebook),
				  GTK_WIDGET (page),
				  gtk_label_new (""));
	gtk_widget_show (GTK_WIDGET (page));

	rb_display_page_model_add_page (shell->priv->display_page_model, page, parent);
}

static void
rb_shell_playlist_added_cb (RBPlaylistManager *mgr,
			    RBSource *source,
			    RBShell *shell)
{
	rb_shell_append_display_page (shell, RB_DISPLAY_PAGE (source), RB_DISPLAY_PAGE_GROUP_PLAYLISTS);
}

static void
rb_shell_playlist_created_cb (RBPlaylistManager *mgr,
			      RBSource *source,
			      RBShell *shell)
{
	g_settings_set_boolean (shell->priv->settings, "small-display", FALSE);
	g_settings_set_boolean (shell->priv->settings, "sourcelist-hidden", FALSE);

	rb_shell_sync_window_state (shell, FALSE);
}

static void
rb_shell_medium_added_cb (RBRemovableMediaManager *mgr,
			  RBSource *source,
			  RBShell *shell)
{
	rb_shell_append_display_page (shell, RB_DISPLAY_PAGE (source), RB_DISPLAY_PAGE_GROUP_DEVICES);
}

static void
rb_shell_display_page_deleted_cb (RBDisplayPage *page, RBShell *shell)
{

	rb_debug ("display page deleted");

	if (RB_IS_SOURCE (page)) {
		RhythmDBEntryType *entry_type;
		RBSource *source = RB_SOURCE (page);

		/* remove from the map if the source owns the type */
		g_object_get (source, "entry-type", &entry_type, NULL);
		if (rb_shell_get_source_by_entry_type (shell, entry_type) == source) {
			g_hash_table_remove (shell->priv->sources_hash, entry_type);
		}
		g_object_unref (entry_type);

		if (source == rb_shell_player_get_playing_source (shell->priv->player_shell) ||
		    source == rb_shell_player_get_active_source (shell->priv->player_shell)) {
			rb_shell_player_stop (shell->priv->player_shell);
		}

		shell->priv->sources = g_list_remove (shell->priv->sources, source);
	}

	if (page == shell->priv->selected_page) {
		if (page != RB_DISPLAY_PAGE (shell->priv->library_source)) {
			rb_shell_select_page (shell, RB_DISPLAY_PAGE (shell->priv->library_source));
		} else {
			rb_shell_select_page (shell, NULL);
		}
	}

	rb_display_page_model_remove_page (shell->priv->display_page_model, page);
	gtk_notebook_remove_page (GTK_NOTEBOOK (shell->priv->notebook),
				  gtk_notebook_page_num (GTK_NOTEBOOK (shell->priv->notebook),
							 GTK_WIDGET (page)));
}

static void
rb_shell_playing_source_changed_cb (RBShellPlayer *player,
				    RBSource *source,
				    RBShell *shell)
{
	rb_debug ("playing source changed");
	if (source != RB_SOURCE (shell->priv->queue_source)) {
		rb_display_page_model_set_playing_source (shell->priv->display_page_model, RB_DISPLAY_PAGE (source));
	}
}

static void
rb_shell_playing_from_queue_cb (RBShellPlayer *player,
				GParamSpec *param,
				RBShell *shell)
{
	gboolean from_queue;

	g_object_get (player, "playing-from-queue", &from_queue, NULL);
	if (!g_settings_get_boolean (shell->priv->settings, "queue-as-sidebar")) {
		RBSource *source;
		source = rb_shell_player_get_playing_source (shell->priv->player_shell);
		rb_display_page_model_set_playing_source (shell->priv->display_page_model, RB_DISPLAY_PAGE (source));
	} else {
		RBSource *source;
		RhythmDBEntry *entry;
		RhythmDBEntryType *entry_type;

		/* if playing from the queue, show the playing entry as playing in the
		 * registered source for its type, so it makes sense when 'jump to current'
		 * jumps to it there.
		 */
		entry = rb_shell_player_get_playing_entry (shell->priv->player_shell);
		if (entry == NULL)
			return;

		entry_type = rhythmdb_entry_get_entry_type (entry);
		source = rb_shell_get_source_by_entry_type (shell, entry_type);
		if (source != NULL) {
			RBEntryViewState state;
			RBEntryView *songs;

			songs = rb_source_get_entry_view (source);
			if (songs != NULL) {
				state = from_queue ? RB_ENTRY_VIEW_PLAYING : RB_ENTRY_VIEW_NOT_PLAYING;
				rb_entry_view_set_state (songs, state);
			}
		}
		rhythmdb_entry_unref (entry);

		source = rb_shell_player_get_active_source (shell->priv->player_shell);
		rb_display_page_model_set_playing_source (shell->priv->display_page_model, RB_DISPLAY_PAGE (source));
	}
}

static void
merge_source_ui_cb (const char *action,
		    RBShell *shell)
{
	gtk_ui_manager_add_ui (shell->priv->ui_manager,
			       shell->priv->source_ui_merge_id,
			       "/ToolBar",
			       action,
			       action,
			       GTK_UI_MANAGER_AUTO,
			       FALSE);
}

static void
rb_shell_select_page (RBShell *shell, RBDisplayPage *page)
{
	GList *actions;
	int pagenum;

	if (shell->priv->selected_page == page)
		return;

	rb_debug ("selecting page %p", page);

	if (shell->priv->selected_page) {
		rb_display_page_deselected (shell->priv->selected_page);
		gtk_ui_manager_remove_ui (shell->priv->ui_manager, shell->priv->source_ui_merge_id);
	}

	shell->priv->selected_page = page;
	rb_display_page_selected (shell->priv->selected_page);

	/* show page */
	pagenum = gtk_notebook_page_num (GTK_NOTEBOOK (shell->priv->notebook),
					 GTK_WIDGET (page));
	gtk_notebook_set_current_page (GTK_NOTEBOOK (shell->priv->notebook), pagenum);

	g_signal_handlers_block_by_func (shell->priv->display_page_tree,
					 G_CALLBACK (display_page_selected_cb),
					 shell);
	rb_display_page_tree_select (shell->priv->display_page_tree, page);
	g_signal_handlers_unblock_by_func (shell->priv->display_page_tree,
					   G_CALLBACK (display_page_selected_cb),
					   shell);

	/* update services */
	if (RB_IS_SOURCE (page)) {
		RBSource *source = RB_SOURCE (page);
		rb_shell_clipboard_set_source (shell->priv->clipboard_shell, source);
		rb_shell_player_set_selected_source (shell->priv->player_shell, source);
		rb_source_header_set_source (shell->priv->source_header, source);
		g_object_set (shell->priv->playlist_manager, "source", source, NULL);
		g_object_set (shell->priv->removable_media_manager, "source", source, NULL);
	} else {
		rb_shell_clipboard_set_source (shell->priv->clipboard_shell, NULL);
		rb_shell_player_set_selected_source (shell->priv->player_shell, NULL);	/* ? */
		rb_source_header_set_source (shell->priv->source_header, NULL);

		/* clear playlist-manager:source? */
		/* clear removable-media-manager:source? */
	}
	rb_statusbar_set_page (shell->priv->statusbar, page);

	/* merge the page-specific UI */
	actions = rb_display_page_get_ui_actions (page);
	g_list_foreach (actions, (GFunc)merge_source_ui_cb, shell);
	rb_list_deep_free (actions);

	g_object_notify (G_OBJECT (shell), "selected-page");
}

static void
rb_shell_player_window_title_changed_cb (RBShellPlayer *player,
					 const char *window_title,
					 RBShell *shell)
{
	rb_shell_set_window_title (shell, window_title);
}

static void
rb_shell_set_window_title (RBShell *shell,
			   const char *window_title)
{
	if (window_title == NULL) {
		rb_debug ("clearing title");

		g_free (shell->priv->cached_title);
		shell->priv->cached_title = NULL;

		gtk_window_set_title (GTK_WINDOW (shell->priv->window),
				      _("Rhythmbox"));
	}
	else {
		gboolean playing;
		char *title;

		rb_shell_player_get_playing (shell->priv->player_shell, &playing, NULL);

		if (shell->priv->cached_title &&
		    !strcmp (shell->priv->cached_title, window_title) &&
		    playing == shell->priv->cached_playing) {
			return;
		}
		g_free (shell->priv->cached_title);
		shell->priv->cached_title = g_strdup (window_title);
		shell->priv->cached_playing = playing;

		rb_debug ("setting title to \"%s\"", window_title);
		if (!playing) {
			/* Translators: %s is the song name */
			title = g_strdup_printf (_("%s (Paused)"), window_title);
			gtk_window_set_title (GTK_WINDOW (shell->priv->window),
					      title);
			g_free (title);
		} else {
			gtk_window_set_title (GTK_WINDOW (shell->priv->window),
					      window_title);
		}
	}
}

static void
rb_shell_view_smalldisplay_changed_cb (GtkAction *action,
				       RBShell *shell)
{
	GTimeVal time;

	/* don't change more than once per second, it causes weirdness */
	g_get_current_time (&time);
	if (time.tv_sec == shell->priv->last_small_time)
		return;

	shell->priv->last_small_time = time.tv_sec;

	g_settings_set_boolean (shell->priv->settings,
				"small-display",
				gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action)));
}

static void
rb_shell_view_statusbar_changed_cb (GtkAction *action,
				    RBShell *shell)
{
	g_settings_set_boolean (shell->priv->settings,
				"statusbar-hidden",
				!gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action)));

	rb_shell_sync_statusbar_visibility (shell);
}

static void
rb_shell_view_queue_as_sidebar_changed_cb (GtkAction *action,
					   RBShell *shell)
{
	gboolean queue_as_sidebar = gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action));
	/* maybe use a settings binding? */
	g_settings_set_boolean (shell->priv->settings,
				"queue-as-sidebar",
				queue_as_sidebar);

	if (queue_as_sidebar &&
	    shell->priv->selected_page == RB_DISPLAY_PAGE (shell->priv->queue_source)) {
		/* queue no longer exists as a source, so change to the library */
		rb_shell_select_page (shell, RB_DISPLAY_PAGE (shell->priv->library_source));
	}

	rb_shell_playing_from_queue_cb (shell->priv->player_shell, NULL, shell);

	rb_shell_sync_pane_visibility (shell);
}

static void
rb_shell_view_party_mode_changed_cb (GtkAction *action,
				     RBShell *shell)
{
	shell->priv->party_mode = gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action));
	rb_shell_sync_party_mode (shell);
}

static void
rb_shell_cmd_about (GtkAction *action,
		    RBShell *shell)
{
	const char **tem;
	GString *comment;

	const char *authors[] = {
		"",
#include "MAINTAINERS.tab"
		"",
		NULL,
#include "MAINTAINERS.old.tab"
		"",
		NULL,
#include "AUTHORS.tab"
		NULL
	};

	const char *documenters[] = {
#include "DOCUMENTERS.tab"
		NULL
	};

	const char *translator_credits = _("translator-credits");

	const char *license[] = {
		N_("Rhythmbox is free software; you can redistribute it and/or modify\n"
		   "it under the terms of the GNU General Public License as published by\n"
		   "the Free Software Foundation; either version 2 of the License, or\n"
		   "(at your option) any later version.\n"),
		N_("Rhythmbox is distributed in the hope that it will be useful,\n"
		   "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
		   "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
		   "GNU General Public License for more details.\n"),
		N_("You should have received a copy of the GNU General Public License\n"
		   "along with Rhythmbox; if not, write to the Free Software Foundation, Inc.,\n"
		   "51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA\n")
	};

	char *license_trans;

	authors[0] = _("Maintainers:");
	for (tem = authors; *tem != NULL; tem++)
		;
	*tem = _("Former Maintainers:");
	for (; *tem != NULL; tem++)
		;
	*tem = _("Contributors:");

	comment = g_string_new (_("Music management and playback software for GNOME."));

	license_trans = g_strconcat (_(license[0]), "\n", _(license[1]), "\n",
				     _(license[2]), "\n", NULL);

	gtk_show_about_dialog (GTK_WINDOW (shell->priv->window),
			       "version", VERSION,
			       "copyright", "Copyright \xc2\xa9 2005 - 2009 The Rhythmbox authors\nCopyright \xc2\xa9 2003 - 2005 Colin Walters\nCopyright \xc2\xa9 2002, 2003 Jorn Baayen",
			       "license", license_trans,
			       "website-label", _("Rhythmbox Website"),
			       "website", "http://www.gnome.org/projects/rhythmbox",
			       "comments", comment->str,
			       "authors", (const char **) authors,
			       "documenters", (const char **) documenters,
			       "translator-credits", strcmp (translator_credits, "translator-credits") != 0 ? translator_credits : NULL,
			       "logo-icon-name", "rhythmbox",
			       NULL);
	g_string_free (comment, TRUE);
	g_free (license_trans);
}

/**
 * rb_shell_toggle_visibility:
 * @shell: the #RBShell
 *
 * Toggles the visibility of the main Rhythmbox window.
 */
void
rb_shell_toggle_visibility (RBShell *shell)
{
	gboolean visible;

	visible = rb_shell_get_visibility (shell);

	rb_shell_set_visibility (shell, FALSE, !visible);
}

static void
rb_shell_cmd_quit (GtkAction *action,
		   RBShell *shell)
{
	rb_shell_quit (shell, NULL);
}

static void
rb_shell_cmd_contents (GtkAction *action,
		       RBShell *shell)
{
	GError *error = NULL;

	gtk_show_uri (gtk_widget_get_screen (shell->priv->window),
		      "ghelp:rhythmbox",
		      gtk_get_current_event_time (),
		      &error);

	if (error != NULL) {
		rb_error_dialog (NULL, _("Couldn't display help"),
				 "%s", error->message);
		g_error_free (error);
	}
}

static void
rb_shell_cmd_preferences (GtkAction *action,
		          RBShell *shell)
{
	RBShellPreferences *prefs;

	g_object_get (shell, "prefs", &prefs, NULL);

	gtk_window_present (GTK_WINDOW (prefs));
	g_object_unref (prefs);
}

static gboolean
rb_shell_plugins_window_delete_cb (GtkWidget *window,
				   GdkEventAny *event,
				   gpointer data)
{
	gtk_widget_hide (window);

	return TRUE;
}

static void
rb_shell_plugins_response_cb (GtkDialog *dialog,
			      int response_id,
			      gpointer data)
{
	if (response_id == GTK_RESPONSE_CLOSE)
		gtk_widget_hide (GTK_WIDGET (dialog));
}

static void
rb_shell_cmd_plugins (GtkAction *action,
		      RBShell *shell)
{
	if (shell->priv->plugins == NULL) {
		GtkWidget *content_area;
		GtkWidget *manager;

		shell->priv->plugins = gtk_dialog_new_with_buttons (_("Configure Plugins"),
								    GTK_WINDOW (shell->priv->window),
								    GTK_DIALOG_DESTROY_WITH_PARENT,
								    GTK_STOCK_CLOSE,
								    GTK_RESPONSE_CLOSE,
								    NULL);
		content_area = gtk_dialog_get_content_area (GTK_DIALOG (shell->priv->plugins));
		gtk_container_set_border_width (GTK_CONTAINER (shell->priv->plugins), 5);
		gtk_box_set_spacing (GTK_BOX (content_area), 2);

		g_signal_connect_object (G_OBJECT (shell->priv->plugins),
					 "delete_event",
					 G_CALLBACK (rb_shell_plugins_window_delete_cb),
					 NULL, 0);
		g_signal_connect_object (G_OBJECT (shell->priv->plugins),
					 "response",
					 G_CALLBACK (rb_shell_plugins_response_cb),
					 NULL, 0);

		manager = peas_gtk_plugin_manager_new (NULL);
		gtk_widget_show_all (GTK_WIDGET (manager));
		gtk_container_add (GTK_CONTAINER (content_area),
				   manager);
	}

	gtk_window_present (GTK_WINDOW (shell->priv->plugins));
}

static void
add_to_library_response_cb (GtkDialog *dialog,
			    int response_id,
			    RBShell *shell)
{

	char *current_dir = NULL;
	GSList *uri_list = NULL, *uris = NULL;
	GSettings *library_settings;

	if (response_id != GTK_RESPONSE_ACCEPT) {
		gtk_widget_destroy (GTK_WIDGET (dialog));
		return;
	}

	library_settings = g_settings_new ("org.gnome.rhythmbox.library");
	current_dir = gtk_file_chooser_get_current_folder_uri (GTK_FILE_CHOOSER (dialog));
	g_settings_set_string (library_settings, "add-dir", current_dir);
	g_object_unref (library_settings);

	uri_list = gtk_file_chooser_get_uris (GTK_FILE_CHOOSER (dialog));
	if (uri_list == NULL) {
		uri_list = g_slist_prepend (uri_list, g_strdup (current_dir));
	}

	for (uris = uri_list; uris; uris = uris->next) {
		rb_shell_load_uri (shell, (char *)uris->data, FALSE, NULL);
		g_free (uris->data);
	}
	g_slist_free (uri_list);
	g_free (current_dir);
	gtk_widget_destroy (GTK_WIDGET (dialog));

	if (shell->priv->save_db_id > 0) {
		g_source_remove (shell->priv->save_db_id);
	}
	shell->priv->save_db_id = g_timeout_add_seconds (10, (GSourceFunc) idle_save_rhythmdb, shell);
}

static void
set_current_folder_uri (RBShell *shell, GtkWidget *dialog)
{
	GSettings *settings;
	char *dir;

	settings = g_settings_new ("org.gnome.rhythmbox.library");
	dir = g_settings_get_string (settings, "add-dir");
	if (dir && dir[0] != '\0') {
		gtk_file_chooser_set_current_folder_uri (GTK_FILE_CHOOSER (dialog),
							 dir);
	}
	g_free (dir);
	g_object_unref (settings);
}

static void
rb_shell_cmd_add_folder_to_library (GtkAction *action,
				    RBShell *shell)
{
	GtkWidget *dialog;

	dialog = rb_file_chooser_new (_("Import Folder into Library"),
			              GTK_WINDOW (shell->priv->window),
				      GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
				      FALSE);
	gtk_file_chooser_set_select_multiple (GTK_FILE_CHOOSER (dialog), TRUE);
	set_current_folder_uri (shell, dialog);

	g_signal_connect_object (G_OBJECT (dialog),
				 "response",
				 G_CALLBACK (add_to_library_response_cb),
				 shell, 0);
}

static void
rb_shell_cmd_add_file_to_library (GtkAction *action,
				  RBShell *shell)
{
	GtkWidget *dialog;

	dialog = rb_file_chooser_new (_("Import File into Library"),
			              GTK_WINDOW (shell->priv->window),
				      GTK_FILE_CHOOSER_ACTION_OPEN,
				      FALSE);
	gtk_file_chooser_set_select_multiple (GTK_FILE_CHOOSER (dialog), TRUE);
	set_current_folder_uri (shell, dialog);

	g_signal_connect_object (G_OBJECT (dialog),
				 "response",
				 G_CALLBACK (add_to_library_response_cb),
				 shell, 0);
}

static gboolean
quit_timeout (gpointer dummy)
{
	GDK_THREADS_ENTER ();
	rb_debug ("quit damn you");
	gtk_main_quit ();
	GDK_THREADS_LEAVE ();
	return FALSE;
}

/**
 * rb_shell_quit:
 * @shell: the #RBShell
 * @error: not used
 *
 * Begins the process of shutting down Rhythmbox.  This function will
 * return.  The error parameter and return value only exist because this
 * function is part of the DBus interface.
 *
 * Return value: not important
 */
gboolean
rb_shell_quit (RBShell *shell,
	       GError **error)
{
	rb_debug ("Quitting");

	/* Stop the playing source, if any */
	rb_shell_player_stop (shell->priv->player_shell);

	rb_podcast_manager_shutdown (shell->priv->podcast_manager);

	rb_shell_shutdown (shell);
	rb_shell_sync_state (shell);
	g_object_unref (G_OBJECT (shell));

	g_timeout_add_seconds (10, quit_timeout, NULL);
	return TRUE;
}

static gboolean
idle_handle_load_complete (RBShell *shell)
{
	GDK_THREADS_ENTER ();

	rb_debug ("load complete");

	rb_playlist_manager_load_playlists (shell->priv->playlist_manager);
	rb_display_page_group_loaded (RB_DISPLAY_PAGE_GROUP (RB_DISPLAY_PAGE_GROUP_PLAYLISTS));
	shell->priv->load_complete = TRUE;
	shell->priv->save_playlist_id = g_timeout_add_seconds (10, (GSourceFunc) idle_save_playlist_manager, shell);

	g_signal_emit (shell, rb_shell_signals[DATABASE_LOAD_COMPLETE], 0);

	rhythmdb_start_action_thread (shell->priv->db);

	GDK_THREADS_LEAVE ();

	return FALSE;
}

static void
rb_shell_load_complete_cb (RhythmDB *db,
			   RBShell *shell)
{
	g_idle_add ((GSourceFunc) idle_handle_load_complete, shell);
}

static void
rb_shell_sync_pane_visibility (RBShell *shell)
{
	GtkAction *action;
	gboolean queue_as_sidebar = g_settings_get_boolean (shell->priv->settings, "queue-as-sidebar");

	if (shell->priv->queue_source != NULL) {
		g_object_set (shell->priv->queue_source, "visibility", !queue_as_sidebar, NULL);
	}

	if (queue_as_sidebar) {
		gtk_widget_show (shell->priv->queue_sidebar);
	} else {
		gtk_widget_hide (shell->priv->queue_sidebar);
	}

	action = gtk_action_group_get_action (shell->priv->actiongroup,
					      "ViewQueueAsSidebar");
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), queue_as_sidebar);
}

static void
rb_shell_sync_toolbar_state (RBShell *shell)
{
	GtkWidget *toolbar;
	guint toolbar_style;

	toolbar = gtk_ui_manager_get_widget (shell->priv->ui_manager, "/ToolBar");

	/* icons-only in small mode */
	if (g_settings_get_boolean (shell->priv->settings, "small-display"))
		toolbar_style = 3;
	else
		toolbar_style = g_settings_get_int (shell->priv->settings, "toolbar-style");

	switch (toolbar_style) {
	case 0:
		/* default*/
		gtk_toolbar_unset_style (GTK_TOOLBAR (toolbar));
		break;
	case 1:
		/* text below icons */
		gtk_toolbar_set_style (GTK_TOOLBAR (toolbar), GTK_TOOLBAR_BOTH);
		break;
	case 2:
		/* text beside icons */
		gtk_toolbar_set_style (GTK_TOOLBAR (toolbar), GTK_TOOLBAR_BOTH_HORIZ);
		break;
	case 3:
		/* icons only */
		gtk_toolbar_set_style (GTK_TOOLBAR (toolbar), GTK_TOOLBAR_ICONS);
		break;
	case 4:
		/* text only */
		gtk_toolbar_set_style (GTK_TOOLBAR (toolbar), GTK_TOOLBAR_TEXT);
		break;
	default:
		g_warning ("unknown toolbar style type");
		gtk_toolbar_unset_style (GTK_TOOLBAR (toolbar));
	}
}

static gboolean
window_state_event_cb (GtkWidget           *widget,
		       GdkEventWindowState *event,
		       RBShell             *shell)
{
	if (event->changed_mask & GDK_WINDOW_STATE_ICONIFIED) {
		rb_shell_present (shell, gtk_get_current_event_time (), NULL);
	}

	return TRUE;
}

static void
rb_shell_sync_party_mode (RBShell *shell)
{
	GtkAction *action;

	/* party mode does not use gsettings as a model since it
	   should not be persistent */

	/* disable/enable quit action */
	action = gtk_action_group_get_action (shell->priv->actiongroup, "MusicQuit");
	g_object_set (action, "sensitive", !shell->priv->party_mode, NULL);
	action = gtk_action_group_get_action (shell->priv->actiongroup, "ViewSmallDisplay");
	g_object_set (action, "sensitive", !shell->priv->party_mode, NULL);

	/* show/hide queue as sidebar ? */

	g_object_set (shell->priv->player_shell, "queue-only", shell->priv->party_mode, NULL);

	/* Set playlist manager source to the current source to update properties */
	if (shell->priv->selected_page && RB_IS_SOURCE (shell->priv->selected_page)) {
		RBSource *source = RB_SOURCE (shell->priv->selected_page);
		g_object_set (shell->priv->playlist_manager, "source", source, NULL);
		rb_shell_clipboard_set_source (shell->priv->clipboard_shell, source);
	}

	gtk_window_set_keep_above (GTK_WINDOW (shell->priv->window), shell->priv->party_mode);
	if (shell->priv->party_mode) {
		gtk_window_fullscreen (GTK_WINDOW (shell->priv->window));
		gtk_window_stick (GTK_WINDOW (shell->priv->window));
		g_signal_connect (shell->priv->window, "window-state-event", G_CALLBACK (window_state_event_cb), shell);
	} else {
		gtk_window_unstick (GTK_WINDOW (shell->priv->window));
		gtk_window_unfullscreen (GTK_WINDOW (shell->priv->window));
		g_signal_handlers_disconnect_by_func (shell->priv->window, window_state_event_cb, shell);
	}
}

static void
rb_shell_sync_smalldisplay (RBShell *shell)
{
	GtkAction *action;
	GtkAction *queue_action;
	GtkAction *party_mode_action;
	GtkAction *jump_to_playing_action;

	rb_shell_sync_window_state (shell, FALSE);

	action = gtk_action_group_get_action (shell->priv->actiongroup,
					      "ViewSidePane");
	queue_action = gtk_action_group_get_action (shell->priv->actiongroup,
						    "ViewQueueAsSidebar");
	party_mode_action = gtk_action_group_get_action (shell->priv->actiongroup,
							 "ViewPartyMode");
	jump_to_playing_action = gtk_action_group_get_action (shell->priv->actiongroup,
							      "ViewJumpToPlaying");

	if (g_settings_get_boolean (shell->priv->settings, "small-display")) {
		g_object_set (action, "sensitive", FALSE, NULL);
		g_object_set (queue_action, "sensitive", FALSE, NULL);
		g_object_set (party_mode_action, "sensitive", FALSE, NULL);
		g_object_set (jump_to_playing_action, "sensitive", FALSE, NULL);

		gtk_widget_hide (GTK_WIDGET (shell->priv->paned));
	} else {
		RhythmDBEntry *playing;

		g_object_set (action, "sensitive", TRUE, NULL);
		g_object_set (queue_action, "sensitive", TRUE, NULL);
		g_object_set (party_mode_action, "sensitive", TRUE, NULL);

		playing = rb_shell_player_get_playing_entry (shell->priv->player_shell);
		g_object_set (jump_to_playing_action, "sensitive", playing != NULL, NULL);
		if (playing)
			rhythmdb_entry_unref (playing);

		gtk_widget_show (GTK_WIDGET (shell->priv->paned));
	}
	rb_shell_sync_statusbar_visibility (shell);
	rb_shell_sync_toolbar_state (shell);

	rb_source_header_sync_control_state (shell->priv->source_header);

	action = gtk_action_group_get_action (shell->priv->actiongroup,
					      "ViewSmallDisplay");
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action),
				      g_settings_get_boolean (shell->priv->settings, "small-display"));
}

static void
rb_shell_sync_statusbar_visibility (RBShell *shell)
{
	gboolean visible;
	GtkAction *action;

	visible = !g_settings_get_boolean (shell->priv->settings, "statusbar-hidden");

	action = gtk_action_group_get_action (shell->priv->actiongroup, "ViewStatusbar");
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), visible);

	gtk_widget_set_visible (GTK_WIDGET (shell->priv->statusbar),
				visible && !g_settings_get_boolean (shell->priv->settings, "small-display"));
}

static void
settings_changed_cb (GSettings *settings, const char *key, RBShell *shell)
{
	if (g_strcmp0 (key, "toolbar-style") == 0) {
		rb_debug ("toolbar state changed");
		rb_shell_sync_toolbar_state (shell);
	} else if (g_strcmp0 (key, "small-display") == 0) {
		rb_debug ("small display mode changed");
		rb_shell_sync_smalldisplay (shell);
	}
}

static void
rb_shell_sync_paned (RBShell *shell)
{
	gtk_paned_set_position (GTK_PANED (shell->priv->right_paned),
				g_settings_get_int (shell->priv->settings, "right-paned-position"));
	gtk_paned_set_position (GTK_PANED (shell->priv->paned),
				g_settings_get_int (shell->priv->settings, "paned-position"));
	gtk_paned_set_position (GTK_PANED (shell->priv->queue_paned),
				g_settings_get_int (shell->priv->settings, "display-page-tree-height"));
}

static void
paned_size_allocate_cb (GtkWidget *widget,
			GtkAllocation *allocation,
		        RBShell *shell)
{
	rb_settings_delayed_sync (shell->priv->settings,
				  (RBDelayedSyncFunc) sync_window_settings,
				  g_object_ref (shell),
				  g_object_unref);
}

static void
display_page_tree_drag_received_cb (RBDisplayPageTree *display_page_tree,
				    RBDisplayPage *page,
				    GtkSelectionData *data,
				    RBShell *shell)
{
        if (page == NULL) {
		RBSource *source;
		source = rb_playlist_manager_new_playlist_from_selection_data (shell->priv->playlist_manager,
									       data);
		page = RB_DISPLAY_PAGE (source);
        }

        if (page != NULL) {
                rb_display_page_receive_drag (page, data);
        }

}

static void
rb_shell_cmd_current_song (GtkAction *action,
			   RBShell *shell)
{
	rb_debug ("current song");

	rb_shell_jump_to_current (shell);
}

static void
rb_shell_cmd_view_all (GtkAction *action,
		       RBShell *shell)
{
	if (RB_IS_SOURCE (shell->priv->selected_page)) {
		RBSource *source = RB_SOURCE (shell->priv->selected_page);
		rb_debug ("view all");

		rb_source_reset_filters (source);
		rb_source_header_clear_search (shell->priv->source_header);
		rb_source_header_focus_search_box (shell->priv->source_header);
	}
}

static void
rb_shell_jump_to_entry_with_source (RBShell *shell,
				    RBSource *source,
				    RhythmDBEntry *entry)
{
	RBEntryView *songs;

	g_return_if_fail (entry != NULL);

	if ((source == RB_SOURCE (shell->priv->queue_source) &&
	     g_settings_get_boolean (shell->priv->settings, "queue-as-sidebar")) ||
	     source == NULL) {
		RhythmDBEntryType *entry_type;
		entry_type = rhythmdb_entry_get_entry_type (entry);
		source = rb_shell_get_source_by_entry_type (shell, entry_type);
	}
	if (source == NULL)
		return;

	songs = rb_source_get_entry_view (source);
	rb_shell_select_page (shell, RB_DISPLAY_PAGE (source));

	if (songs != NULL) {
		rb_entry_view_scroll_to_entry (songs, entry);
		rb_entry_view_select_entry (songs, entry);
	}
}

static void
rb_shell_play_entry (RBShell *shell,
		     RhythmDBEntry *entry)
{
	rb_shell_player_stop (shell->priv->player_shell);
	rb_shell_jump_to_entry_with_source (shell, NULL, entry);
	rb_shell_player_play_entry (shell->priv->player_shell, entry, NULL);
}

static void
rb_shell_jump_to_current (RBShell *shell)
{
	RBSource *source;
	RhythmDBEntry *playing;

	source = rb_shell_player_get_playing_source (shell->priv->player_shell);

	g_return_if_fail (source != NULL);

	playing = rb_shell_player_get_playing_entry (shell->priv->player_shell);

	rb_shell_jump_to_entry_with_source (shell, source, playing);
	rhythmdb_entry_unref (playing);
}

void
rb_shell_notify_custom (RBShell *shell,
			guint timeout,
			const char *primary,
			const char *secondary,
			const char *image_uri,
			gboolean requested)
{
	g_signal_emit (shell, rb_shell_signals[NOTIFY_CUSTOM], 0, timeout, primary, secondary, image_uri, requested);
}

/**
 * rb_shell_do_notify:
 * @shell: the #RBShell
 * @requested: if %TRUE, the notification was requested by some explicit user action
 * @error: not used
 *
 * Displays a notification of the current playing track.
 *
 * Return value: not important
 */
gboolean
rb_shell_do_notify (RBShell *shell, gboolean requested, GError **error)
{
	g_signal_emit (shell, rb_shell_signals[NOTIFY_PLAYING_ENTRY], 0, requested);
	return TRUE;
}

/**
 * rb_shell_error_quark:
 *
 * Returns the #GQuark used for #RBShell errors
 *
 * Return value: shell error #GQuark
 */
GQuark
rb_shell_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("rb_shell_error");

	return quark;
}

static void
session_save_state_cb (EggSMClient *client,
		       GKeyFile *key_file,
		       RBShell *shell)
{
	rb_debug ("session save-state");
	rb_shell_sync_state (shell);
}

static void
session_quit_cb (EggSMClient *client,
		 RBShell *shell)
{
	rb_debug ("session quit");
	rb_shell_quit (shell, NULL);
}

static void
rb_shell_session_init (RBShell *shell)
{
	EggSMClient *sm_client;

	sm_client = egg_sm_client_get ();
	g_signal_connect (sm_client, "save-state", G_CALLBACK (session_save_state_cb), shell);
	g_signal_connect (sm_client, "quit", G_CALLBACK (session_quit_cb), shell);
}

/**
 * rb_shell_guess_source_for_uri:
 * @shell: the #RBSource
 * @uri: the URI to guess a source for
 *
 * Attempts to locate the source that should handle the specified URI.
 * This iterates through all sources, calling #rb_source_want_uri,
 * returning the source that returns the highest value.
 *
 * Return value: (transfer none): the most appropriate #RBSource for the uri
 */
RBSource *
rb_shell_guess_source_for_uri (RBShell *shell,
			       const char *uri)
{
	GList *t;
	RBSource *best = NULL;
	guint strength = 0;

	for (t = shell->priv->sources; t != NULL; t = t->next) {
		guint s;
		RBSource *source;

		source = (RBSource *)t->data;
		s = rb_source_want_uri (source, uri);
		if (s > strength) {
			gchar *name;

			g_object_get (source, "name", &name, NULL);
			rb_debug ("source %s returned strength %u for uri %s",
				  name, s, uri);
			g_free (name);

			strength = s;
			best = source;
		}
	}

	return best;
}

/* Load a URI representing an element of the given type, with
 * optional metadata
 */
/**
 * rb_shell_add_uri:
 * @shell: the #RBShell
 * @uri: the URI to add
 * @title: optional title value for the URI
 * @genre: optional genre value for the URI
 * @error: returns error information
 *
 * Adds the specified URI to the Rhythmbox database.  Whether the
 * title and genre specified are actually used is up to the source
 * that handles the URI
 *
 * Return value: TRUE if the URI was added successfully
 */
gboolean
rb_shell_add_uri (RBShell *shell,
		  const char *uri,
		  const char *title,
		  const char *genre,
		  GError **error)
{
	RBSource *source;

	source = rb_shell_guess_source_for_uri (shell, uri);
	if (source == NULL) {
		g_set_error (error,
			     RB_SHELL_ERROR,
			     RB_SHELL_ERROR_NO_SOURCE_FOR_URI,
			     _("No registered source can handle URI %s"),
			     uri);
		return FALSE;
	}

	rb_source_add_uri (source, uri, title, genre, NULL, NULL, NULL);
	return TRUE;
}

typedef struct {
	RBShell *shell;
	char *uri;
	gboolean play;
	RBSource *playlist_source;
	gboolean can_use_playlist;
	gboolean source_is_entry;
} PlaylistParseData;

static void
handle_playlist_entry_cb (TotemPlParser *playlist,
			  const char *uri,
			  GHashTable *metadata,
			  PlaylistParseData *data)
{
	RBSource *source;

	/*
	 * Track whether the same playlist-handling source
	 * wants all the URIs from the playlist; if it does,
	 * then we'll just give the playlist URI to the source.
	 */
	if (data->can_use_playlist == FALSE)
		return;

	source = rb_shell_guess_source_for_uri (data->shell, uri);
	if (data->playlist_source == NULL) {
		if (source != NULL && rb_source_try_playlist (source)) {
			data->playlist_source = RB_SOURCE (g_object_ref (source));
			data->source_is_entry = rb_source_uri_is_source (source, uri);
		} else {
			data->can_use_playlist = FALSE;
		}
	} else if (data->playlist_source != source) {
		g_object_unref (data->playlist_source);
		data->playlist_source = NULL;
		data->can_use_playlist = FALSE;
		data->source_is_entry = FALSE;
	}
}

static void
shell_load_uri_done (RBSource *source, const char *uri, RBShell *shell)
{
	RhythmDBEntry *entry;

	entry = rhythmdb_entry_lookup_by_location (shell->priv->db, uri);
	if (entry) {
		rb_shell_play_entry (shell, entry);
	} else {
		rb_debug ("unable to find entry for uri %s", uri);
	}
}

static void
load_uri_finish (RBShell *shell, RBSource *entry_source, RhythmDBEntry *entry, gboolean play)
{
	if (play == FALSE) {
		rb_debug ("didn't want to do anything anyway");
	} else if (entry != NULL) {
		rb_debug ("found an entry to play");
		rb_shell_play_entry (shell, entry);
	} else if (entry_source != NULL) {
		char *name;
		GError *error = NULL;

		g_object_get (entry_source, "name", &name, NULL);
		if (rb_shell_activate_source (shell, entry_source, RB_SHELL_ACTIVATION_ALWAYS_PLAY, &error) == FALSE) {
			rb_debug ("couldn't activate source %s: %s", name, error->message);
			g_clear_error (&error);
		} else {
			rb_debug ("activated source '%s'", name);
		}
		g_free (name);
	} else {
		rb_debug ("couldn't do anything");
	}
}

static void
load_uri_parser_finished_cb (GObject *parser, GAsyncResult *res, PlaylistParseData *data)
{
	TotemPlParserResult result;
	RBSource *entry_source = NULL;
	GError *error = NULL;

	result = totem_pl_parser_parse_finish (TOTEM_PL_PARSER (parser), res, &error);
	g_object_unref (parser);

	if (error != NULL) {
		rb_debug ("parsing %s as a playlist failed: %s", data->uri, error->message);
		g_clear_error (&error);
	} else if (result == TOTEM_PL_PARSER_RESULT_UNHANDLED) {
		rb_debug ("%s unhandled", data->uri);
	} else if (result == TOTEM_PL_PARSER_RESULT_IGNORED) {
		rb_debug ("%s ignored", data->uri);
	}

	if (result == TOTEM_PL_PARSER_RESULT_SUCCESS) {

		if (data->can_use_playlist && data->playlist_source) {
			rb_debug ("adding playlist %s to source", data->uri);
			rb_source_add_uri (data->playlist_source, data->uri, NULL, NULL, NULL, NULL, NULL);

			/* FIXME: We need some way to determine whether the URI as
			 * given will appear in the db, or whether something else will.
			 * This hack assumes we'll never add local playlists to the db
			 * directly.
			 */
			if (rb_uri_is_local (data->uri) && (data->source_is_entry == FALSE)) {
				data->play = FALSE;
			}

			if (data->source_is_entry != FALSE) {
				entry_source = data->playlist_source;
			}
		} else {
			rb_debug ("adding %s as a static playlist", data->uri);
			if (!rb_playlist_manager_parse_file (data->shell->priv->playlist_manager,
							     data->uri,
							     &error)) {
				rb_debug ("unable to parse %s as a static playlist: %s", data->uri, error->message);
				g_clear_error (&error);
			}
			data->play = FALSE;		/* maybe we should play the new playlist? */
		}
	} else {
		RBSource *source;

		source = rb_shell_guess_source_for_uri (data->shell, data->uri);
		if (source != NULL) {
			char *name;
			g_object_get (source, "name", &name, NULL);
			if (rb_source_uri_is_source (source, data->uri)) {
				rb_debug ("%s identifies source %s", data->uri, name);
				entry_source = source;
			} else if (data->play) {
				rb_debug ("adding %s to source %s, will play it when it shows up", data->uri, name);
				rb_source_add_uri (source, data->uri, NULL, NULL, (RBSourceAddCallback) shell_load_uri_done, g_object_ref (data->shell), g_object_unref);
				data->play = FALSE;
			} else {
				rb_debug ("just adding %s to source %s", data->uri, name);
				rb_source_add_uri (source, data->uri, NULL, NULL, NULL, NULL, NULL);
			}
			g_free (name);
		} else {
			rb_debug ("couldn't find a source for %s, trying to add it anyway", data->uri);
			if (!rb_shell_add_uri (data->shell, data->uri, NULL, NULL, &error)) {
				rb_debug ("couldn't do it: %s", error->message);
				g_clear_error (&error);
			}
		}
	}

	load_uri_finish (data->shell, entry_source, NULL, data->play);

	if (data->playlist_source != NULL) {
		g_object_unref (data->playlist_source);
	}
	g_object_unref (data->shell);
	g_free (data->uri);
	g_free (data);
}

/**
 * rb_shell_load_uri:
 * @shell: the #RBShell
 * @uri: the URI to load
 * @play: if TRUE, start playing the URI (if possible)
 * @error: returns error information
 *
 * Loads a URI representing a single song, a directory, a playlist, or
 * an internet radio station, and optionally starts playing it.
 *
 * For playlists containing only stream URLs, we either add the playlist
 * itself (if it's remote) or each URL from it (if it's local).  The main
 * reason for this is so clicking on stream playlist links in web browsers
 * works properly - the playlist file will be downloaded to /tmp/, and
 * we can't add that to the database, so we need to add the stream URLs
 * instead.
 *
 * Return value: TRUE if the URI was added successfully
 */
gboolean
rb_shell_load_uri (RBShell *shell,
		   const char *uri,
		   gboolean play,
		   GError **error)
{
	RhythmDBEntry *entry;

	/* If the URI points to a Podcast, pass it on to
	 * the Podcast source */
	if (rb_uri_could_be_podcast (uri, NULL)) {
		rb_podcast_manager_subscribe_feed (shell->priv->podcast_manager, uri, FALSE);
		rb_shell_select_page (shell, RB_DISPLAY_PAGE (shell->priv->podcast_source));
		return TRUE;
	}

	entry = rhythmdb_entry_lookup_by_location (shell->priv->db, uri);

	if (entry == NULL) {
		TotemPlParser *parser;
		PlaylistParseData *data;

		data = g_new0 (PlaylistParseData, 1);
		data->shell = g_object_ref (shell);
		data->uri = g_strdup (uri);
		data->play = play;
		data->can_use_playlist = TRUE;
		data->source_is_entry = FALSE;
		data->playlist_source = NULL;

		rb_debug ("adding uri %s, play %d", uri, play);
		parser = totem_pl_parser_new ();

		g_signal_connect_data (parser, "entry-parsed",
				       G_CALLBACK (handle_playlist_entry_cb),
				       data, NULL, 0);

		totem_pl_parser_add_ignored_mimetype (parser, "x-directory/normal");
		totem_pl_parser_add_ignored_mimetype (parser, "inode/directory");
		totem_pl_parser_add_ignored_scheme (parser, "cdda");
		g_object_set (parser, "recurse", FALSE, NULL);
		if (rb_debug_matches ("totem_pl_parser_parse_async", "totem-pl-parser.c")) {
			g_object_set (parser, "debug", TRUE, NULL);
		}

		totem_pl_parser_parse_async (parser, uri, FALSE, NULL, (GAsyncReadyCallback)load_uri_parser_finished_cb, data);
	} else {
		load_uri_finish (shell, NULL, entry, play);
	}

	return TRUE;
}

/**
 * rb_shell_get_party_mode:
 * @shell: the #RBShell
 *
 * Returns %TRUE if the shell is in party mode
 *
 * Return value: %TRUE if the shell is in party mode
 */
gboolean
rb_shell_get_party_mode (RBShell *shell)
{
	return shell->priv->party_mode;
}

/**
 * rb_shell_get_player:
 * @shell: the #RBShell
 *
 * Returns the #RBShellPlayer object
 *
 * Return value: (transfer none): the #RBShellPlayer object
 */
GObject *
rb_shell_get_player (RBShell *shell)
{
	return G_OBJECT (shell->priv->player_shell);
}

/**
 * rb_shell_get_player_path:
 * @shell: the #RBShell
 *
 * Returns the DBus object path for the #RBShellPlayer
 *
 * Return value: the DBus object path for the #RBShellPlayer
 */
const char *
rb_shell_get_player_path (RBShell *shell)
{
	return "/org/gnome/Rhythmbox/Player";
}

/**
 * rb_shell_get_playlist_manager:
 * @shell: the #RBShell
 *
 * Returns the #RBPlaylistManager object
 *
 * Return value: (transfer none): the #RBPlaylistManager object
 */
GObject *
rb_shell_get_playlist_manager (RBShell *shell)
{
	return G_OBJECT (shell->priv->playlist_manager);
}

/**
 * rb_shell_get_playlist_manager_path:
 * @shell: the #RBShell
 *
 * Returns the DBus path for the #RBPlaylistManager object
 *
 * Return value: the DBus object path for the #RBPlaylistManager
 */
const char *
rb_shell_get_playlist_manager_path (RBShell *shell)
{
	return "/org/gnome/Rhythmbox/PlaylistManager";
}

/**
 * rb_shell_get_ui_manager:
 * @shell: the #RBShell
 *
 * Returns the main #GtkUIManager object
 *
 * Return value: (transfer none): the main #GtkUIManager object
 */
GObject *
rb_shell_get_ui_manager (RBShell *shell)
{
	return G_OBJECT (shell->priv->ui_manager);
}

/**
 * rb_shell_add_to_queue:
 * @shell: the #RBShell
 * @uri: the URI to add to the play queue
 * @error: not used
 *
 * Adds the specified URI to the play queue.  This only works if URI is already
 * in the database.
 *
 * Return value: not used
 */
gboolean
rb_shell_add_to_queue (RBShell *shell,
		       const gchar *uri,
		       GError **error)
{
	RhythmDBEntry *entry;

	entry = rhythmdb_entry_lookup_by_location (shell->priv->db, uri);
	if (entry == NULL) {
		RBSource *source;
		source = rb_shell_guess_source_for_uri (shell, uri);
		if (source != NULL) {
			rb_source_add_uri (source, uri, NULL, NULL, NULL, NULL, NULL);
		} else {
			g_set_error (error,
				     RB_SHELL_ERROR,
				     RB_SHELL_ERROR_NO_SOURCE_FOR_URI,
				     _("No registered source can handle URI %s"),
				     uri);
			return FALSE;
		}
	}
	rb_static_playlist_source_add_location (RB_STATIC_PLAYLIST_SOURCE (shell->priv->queue_source),
						uri, -1);
	return TRUE;
}

/**
 * rb_shell_remove_from_queue:
 * @shell: the #RBShell
 * @uri: the URI to remove from the play queue
 * @error: not used
 *
 * Removes the specified URI from the play queue.  If the URI is not
 * in the play queue, nothing happens.
 *
 * Return value: not used.
 */
gboolean
rb_shell_remove_from_queue (RBShell *shell,
			    const gchar *uri,
			    GError **error)
{
	if (rb_playlist_source_location_in_map (RB_PLAYLIST_SOURCE (shell->priv->queue_source), uri))
		rb_static_playlist_source_remove_location (RB_STATIC_PLAYLIST_SOURCE (shell->priv->queue_source),
							   uri);
	return TRUE;
}

/**
 * rb_shell_clear_queue:
 * @shell: the #RBShell
 * @error: not used
 *
 * Removes all entries from the play queue.
 *
 * Return value: not used
 */
gboolean
rb_shell_clear_queue (RBShell *shell,
		      GError **error)
{
	rb_play_queue_source_clear_queue (RB_PLAY_QUEUE_SOURCE (shell->priv->queue_source));
	return TRUE;
}

/**
 * rb_shell_present:
 * @shell: the #RBShell
 * @timestamp: GTK timestamp to use (for focus-stealing prevention)
 * @error: not used
 *
 * Attempts to display the main window to the user.  See #gtk_window_present for details.
 *
 * Return value: not used.
 */
gboolean
rb_shell_present (RBShell *shell,
		  guint32 timestamp,
		  GError **error)
{
	rb_profile_start ("presenting shell");

	rb_debug ("presenting with timestamp %u", timestamp);
	gtk_widget_show (GTK_WIDGET (shell->priv->window));
	gtk_window_present_with_time (GTK_WINDOW (shell->priv->window), timestamp);
	gtk_window_set_skip_taskbar_hint (GTK_WINDOW (shell->priv->window), FALSE);

	rb_profile_end ("presenting shell");

	return TRUE;
}

/**
 * rb_shell_activate_source_by_uri:
 * @shell: the #RBShell
 * @source_uri: URI for the source to activate
 * @play: 0: select source, 1: play source if not playing, 2: play source
 * @error: returns error information
 *
 * Searches for a source matching @source_uri and if found, selects it,
 * and depending on the value of @play, may start playing from it.
 * Device-based sources will match the device node or mount point URI.
 * Other types of sources may have their own URI scheme or format.
 * This is part of the DBus interface.
 *
 * Return value: %TRUE if successful
 */
gboolean
rb_shell_activate_source_by_uri (RBShell *shell,
				 const char *source_uri,
				 guint play,
				 GError **error)
{
	GList *t;
	GFile *f;
	char *uri;

	/* ensure the argument is actually a URI */
	f = g_file_new_for_commandline_arg (source_uri);
	uri = g_file_get_uri (f);
	g_object_unref (f);

	for (t = shell->priv->sources; t != NULL; t = t->next) {
		RBSource *source;

		source = (RBSource *)t->data;
		if (rb_source_uri_is_source (source, uri)) {
			rb_debug ("found source for uri %s", uri);
			g_free (uri);
			return rb_shell_activate_source (shell, source, play, error);
		}
	}

	g_set_error (error,
		     RB_SHELL_ERROR,
		     RB_SHELL_ERROR_NO_SOURCE_FOR_URI,
		     _("No registered source matches URI %s"),
		     uri);
	g_free (uri);
	return FALSE;
}

/**
 * rb_shell_get_song_properties:
 * @shell: the #RBShell
 * @uri: the URI to query
 * @properties: (out callee-allocates) (element-type utf8 GObject.Value) returns the properties of the specified URI
 * @error: returns error information
 *
 * Gathers and returns all metadata (including extra metadata such as album
 * art URIs and lyrics) for the specified URI.
 *
 * Return value: %TRUE if the URI is found in the database
 */
gboolean
rb_shell_get_song_properties (RBShell *shell,
			      const char *uri,
			      GHashTable **properties,
			      GError **error)
{
	RhythmDBEntry *entry;
	RBStringValueMap *map;

	entry = rhythmdb_entry_lookup_by_location (shell->priv->db, uri);

	if (entry == NULL) {
		g_set_error (error,
			     RB_SHELL_ERROR,
			     RB_SHELL_ERROR_NO_SUCH_URI,
			     _("Unknown song URI: %s"),
			     uri);
		return FALSE;
	}

	map = rhythmdb_entry_gather_metadata (shell->priv->db, entry);
	*properties = rb_string_value_map_steal_hashtable (map);
	g_object_unref (map);

	return (*properties != NULL);
}

/**
 * rb_shell_set_song_property:
 * @shell: the #RBShell
 * @uri: the URI to modify
 * @propname: the name of the property to modify
 * @value: the new value to set
 * @error: returns error information
 *
 * Attempts to set a property of a database entry identified by its URI.
 * If the URI identifies a file and the property is one associated with a
 * file metadata tag, the new value will be written to the file.
 *
 * Return value: %TRUE if the property was set successfully.
 */
gboolean
rb_shell_set_song_property (RBShell *shell,
			    const char *uri,
			    const char *propname,
			    const GValue *value,
			    GError **error)
{
	RhythmDBEntry *entry;
	GType proptype;
	int propid;

	entry = rhythmdb_entry_lookup_by_location (shell->priv->db, uri);

	if (entry == NULL) {
		g_set_error (error,
			     RB_SHELL_ERROR,
			     RB_SHELL_ERROR_NO_SUCH_URI,
			     _("Unknown song URI: %s"),
			     uri);
		return FALSE;
	}

	if ((propid = rhythmdb_propid_from_nice_elt_name (shell->priv->db, (guchar *) propname)) < 0) {
		g_set_error (error,
			     RB_SHELL_ERROR,
			     RB_SHELL_ERROR_NO_SUCH_PROPERTY,
			     _("Unknown property %s"),
			     propname);
		return FALSE;
	}

	proptype = rhythmdb_get_property_type (shell->priv->db, propid);
	if (G_VALUE_TYPE (value) != proptype) {
		GValue convert = {0,};
		g_value_init (&convert, proptype);
		if (g_value_transform (value, &convert) == FALSE) {
			g_value_unset (&convert);
			g_set_error (error,
				     RB_SHELL_ERROR,
				     RB_SHELL_ERROR_INVALID_PROPERTY_TYPE,
				     _("Invalid property type %s for property %s"),
				     g_type_name (G_VALUE_TYPE (value)),
				     propname);
			return FALSE;
		} else {
			rhythmdb_entry_set (shell->priv->db, entry, propid, &convert);
			g_value_unset (&convert);
		}
	} else {
		rhythmdb_entry_set (shell->priv->db, entry, propid, value);
	}
	rhythmdb_commit (shell->priv->db);
	return TRUE;
}

static void
rb_shell_volume_widget_changed_cb (GtkScaleButton *vol,
				   gdouble volume,
				   RBShell *shell)
{
	if (!shell->priv->syncing_volume) {
		g_object_set (shell->priv->player_shell, "volume", volume, NULL);
	}
}

static void
rb_shell_player_volume_changed_cb (RBShellPlayer *player,
				   GParamSpec *arg,
				   RBShell *shell)
{
	float volume;

	g_object_get (player, "volume", &volume, NULL);
	shell->priv->syncing_volume = TRUE;
	gtk_scale_button_set_value (GTK_SCALE_BUTTON (shell->priv->volume_button), volume);
	shell->priv->syncing_volume = FALSE;

}

static GtkBox*
rb_shell_get_box_for_ui_location (RBShell *shell, RBShellUILocation location)
{
	GtkBox *box = NULL;

	switch (location) {
	case RB_SHELL_UI_LOCATION_SIDEBAR:
		box = shell->priv->sidebar_container;
		break;
	case RB_SHELL_UI_LOCATION_RIGHT_SIDEBAR:
		box = shell->priv->right_sidebar_container;
		break;
	case RB_SHELL_UI_LOCATION_MAIN_TOP:
		box = shell->priv->top_container;
		break;
	case RB_SHELL_UI_LOCATION_MAIN_BOTTOM:
		box = shell->priv->bottom_container;
		break;
	default:
		break;
	}

	return box;
}

/**
 * rb_shell_add_widget:
 * @shell: the #RBShell
 * @widget: the #GtkWidget to insert into the main window
 * @location: the location at which to insert the widget
 * @expand: whether the widget should be given extra space
 * @fill: whether the widget should fill all space allocated to it
 *
 * Adds a widget to the main Rhythmbox window.  See #gtk_box_pack_start for
 * details on how the expand and fill parameters work.
 */
void
rb_shell_add_widget (RBShell *shell, GtkWidget *widget, RBShellUILocation location, gboolean expand, gboolean fill)
{
	GtkBox *box;

	switch (location) {
	case RB_SHELL_UI_LOCATION_RIGHT_SIDEBAR:
		if (!shell->priv->right_sidebar_widget_count)
			gtk_widget_show (GTK_WIDGET (shell->priv->right_sidebar_container));
		shell->priv->right_sidebar_widget_count++;
	default:
		box = rb_shell_get_box_for_ui_location (shell, location);
		g_return_if_fail (box != NULL);

		gtk_box_pack_start (box, widget, expand, fill, 0);
		break;
	}
}

/**
 * rb_shell_remove_widget:
 * @shell: the #RBShell
 * @widget: the #GtkWidget to remove from the main window
 * @location: the UI location to which the widget was originally added
 *
 * Removes a widget added with #rb_shell_add_widget from the main window.
 */
void
rb_shell_remove_widget (RBShell *shell, GtkWidget *widget, RBShellUILocation location)
{
	GtkBox *box;

	switch (location) {
	case RB_SHELL_UI_LOCATION_RIGHT_SIDEBAR:
		shell->priv->right_sidebar_widget_count--;
		if (!shell->priv->right_sidebar_widget_count)
			gtk_widget_hide (GTK_WIDGET (shell->priv->right_sidebar_container));
	default:
		box = rb_shell_get_box_for_ui_location (shell, location);
		g_return_if_fail (box != NULL);

		gtk_container_remove (GTK_CONTAINER (box), widget);
		break;
	}
}

/* This should really be standard. */
#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
rb_shell_activation_type_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)	{
		static const GEnumValue values[] = {
			ENUM_ENTRY (RB_SHELL_ACTIVATION_SELECT, "select"),
			ENUM_ENTRY (RB_SHELL_ACTIVATION_PLAY, "play"),
			ENUM_ENTRY (RB_SHELL_ACTIVATION_ALWAYS_PLAY, "always-play"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RBShellActivationType", values);
	}

	return etype;
}

GType
rb_shell_ui_location_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)	{
		static const GEnumValue values[] = {
			ENUM_ENTRY (RB_SHELL_UI_LOCATION_SIDEBAR, "sidebar"),
			ENUM_ENTRY (RB_SHELL_UI_LOCATION_RIGHT_SIDEBAR, "right-sidebar"),
			ENUM_ENTRY (RB_SHELL_UI_LOCATION_MAIN_TOP, "main-top"),
			ENUM_ENTRY (RB_SHELL_UI_LOCATION_MAIN_BOTTOM, "main-bottom"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RBShellUILocation", values);
	}

	return etype;
}

GType
rb_shell_error_get_type (void)
{
	static GType etype = 0;

	if (etype == 0) {
		static const GEnumValue values[] = {
			ENUM_ENTRY(RB_SHELL_ERROR_NO_SUCH_URI, "no-such-uri"),
			ENUM_ENTRY(RB_SHELL_ERROR_NO_SUCH_PROPERTY, "no-such-property"),
			ENUM_ENTRY(RB_SHELL_ERROR_IMMUTABLE_PROPERTY, "immutable-property"),
			ENUM_ENTRY(RB_SHELL_ERROR_INVALID_PROPERTY_TYPE, "invalid-property-type"),
			ENUM_ENTRY(RB_SHELL_ERROR_NO_SOURCE_FOR_URI, "no-source-for-uri"),
			{ 0, 0, 0 }
		};
		etype = g_enum_register_static ("RBShellErrorType", values);
	}

	return etype;
}
