/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003,2004 Colin Walters <walters@verbum.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
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
 * SECTION:rb-library-source
 * @short_description: main library source, containing all local songs
 *
 * The library source contains all local songs that have been imported
 * into the database.
 *
 * It provides a preferences page for configuring the library location,
 * the directory structure to use when transferring new files into
 * the library from another source, and the preferred audio encoding
 * to use.
 *
 * If multiple library locations are configured, the library source
 * creates a child source for each location, which will only show
 * files found under that location.
 */

#include "config.h"

#include <string.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include "rb-track-transfer-batch.h"
#include "rb-track-transfer-queue.h"
#include <libgnome-media-profiles/gnome-media-profiles.h>
#include <libgnome-media-profiles/audio-profile-choose.h>

#include "rhythmdb.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#include "rb-builder-helpers.h"
#include "rb-file-helpers.h"
#include "rb-util.h"
#include "rb-library-source.h"
#include "rb-auto-playlist-source.h"
#include "rb-encoder.h"
#include "rb-missing-plugins.h"

static void rb_library_source_class_init (RBLibrarySourceClass *klass);
static void rb_library_source_init (RBLibrarySource *source);
static void rb_library_source_constructed (GObject *object);
static void rb_library_source_dispose (GObject *object);
static void rb_library_source_finalize (GObject *object);

static gboolean impl_show_popup (RBDisplayPage *source);
static GtkWidget *impl_get_config_widget (RBDisplayPage *source, RBShellPreferences *prefs);
static gboolean impl_receive_drag (RBDisplayPage *source, GtkSelectionData *data);
static void impl_get_status (RBDisplayPage *source, char **text, char **progress_text, float *progress);

static gboolean impl_can_paste (RBSource *asource);
static RBTrackTransferBatch *impl_paste (RBSource *source, GList *entries);
static guint impl_want_uri (RBSource *source, const char *uri);
static void impl_add_uri (RBSource *source,
			  const char *uri,
			  const char *title,
			  const char *genre,
			  RBSourceAddCallback callback,
			  gpointer data,
			  GDestroyNotify destroy_data);

static void library_settings_changed_cb (GSettings *settings, const char *key, RBLibrarySource *source);
static void db_settings_changed_cb (GSettings *settings, const char *key, RBLibrarySource *source);
static void rb_library_source_edit_profile_clicked_cb (GtkButton *button,
						       RBLibrarySource *source);
static gboolean rb_library_source_library_location_cb (GtkEntry *entry,
						       GdkEventFocus *event,
						       RBLibrarySource *source);
static void rb_library_source_sync_child_sources (RBLibrarySource *source);
static void rb_library_source_path_changed_cb (GtkComboBox *box,
						RBLibrarySource *source);
static void rb_library_source_filename_changed_cb (GtkComboBox *box,
						   RBLibrarySource *source);
static void rb_library_source_format_changed_cb (GtkWidget *widget,
						 RBLibrarySource *source);
static void layout_example_label_update (RBLibrarySource *source);
static RhythmDBImportJob *maybe_create_import_job (RBLibrarySource *source);

typedef struct {
	char *title;
	char *path;
} LibraryPathElement;

const LibraryPathElement library_layout_paths[] = {
	{N_("Artist/Artist - Album"), "%aa/%aa - %at"},
	{N_("Artist/Album"), "%aa/%at"},
	{N_("Artist - Album"), "%aa - %at"},
	{N_("Album"), "%at"},
	{N_("Artist"), "%aa"},
};
const int num_library_layout_paths = G_N_ELEMENTS (library_layout_paths);

const LibraryPathElement library_layout_filenames[] = {
	{N_("Number - Title"), "%tN - %tt"},
	{N_("Artist - Title"), "%ta - %tt"},
	{N_("Artist - Number - Title"), "%ta - %tN - %tt"},
	{N_("Artist (Album) - Number - Title"), "%ta (%at) - %tN - %tt"},
	{N_("Title"), "%tt"},
	{N_("Number. Artist - Title"), "%tN. %ta - %tt"},
};
const int num_library_layout_filenames = G_N_ELEMENTS (library_layout_filenames);

struct RBLibrarySourcePrivate
{
	RhythmDB *db;

	gboolean loading_prefs;
	RBShellPreferences *shell_prefs;

	GtkWidget *config_widget;

	GList *child_sources;

	GtkWidget *library_location_entry;
	GtkWidget *watch_library_check;
	GtkWidget *layout_path_menu;
	GtkWidget *layout_filename_menu;
	GtkWidget *preferred_format_menu;
	GtkWidget *layout_example_label;

	GList *import_jobs;
	guint start_import_job_id;

	GSettings *settings;
	GSettings *db_settings;
};

#define RB_LIBRARY_SOURCE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_LIBRARY_SOURCE, RBLibrarySourcePrivate))
G_DEFINE_TYPE (RBLibrarySource, rb_library_source, RB_TYPE_BROWSER_SOURCE)

static void
rb_library_source_class_init (RBLibrarySourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBDisplayPageClass *page_class = RB_DISPLAY_PAGE_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);
	RBBrowserSourceClass *browser_source_class = RB_BROWSER_SOURCE_CLASS (klass);

	object_class->dispose = rb_library_source_dispose;
	object_class->finalize = rb_library_source_finalize;
	object_class->constructed = rb_library_source_constructed;

	page_class->show_popup = impl_show_popup;
	page_class->get_config_widget = impl_get_config_widget;
	page_class->receive_drag = impl_receive_drag;
	page_class->get_status = impl_get_status;

	source_class->impl_can_copy = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_paste = (RBSourceFeatureFunc) impl_can_paste;
	source_class->impl_paste = impl_paste;
	source_class->impl_want_uri = impl_want_uri;
	source_class->impl_add_uri = impl_add_uri;

	browser_source_class->impl_has_drop_support = (RBBrowserSourceFeatureFunc) rb_true_function;

	g_type_class_add_private (klass, sizeof (RBLibrarySourcePrivate));

	gnome_media_profiles_init (gconf_client_get_default ());
}

static void
rb_library_source_init (RBLibrarySource *source)
{
	source->priv = RB_LIBRARY_SOURCE_GET_PRIVATE (source);
}

static void
rb_library_source_dispose (GObject *object)
{
	RBLibrarySource *source;
	source = RB_LIBRARY_SOURCE (object);

	if (source->priv->shell_prefs) {
		g_object_unref (source->priv->shell_prefs);
		source->priv->shell_prefs = NULL;
	}

	if (source->priv->db) {
		g_object_unref (source->priv->db);
		source->priv->db = NULL;
	}

	if (source->priv->settings) {
		g_object_unref (source->priv->settings);
		source->priv->settings = NULL;
	}

	if (source->priv->db_settings) {
		g_object_unref (source->priv->db_settings);
		source->priv->db_settings = NULL;
	}

	if (source->priv->import_jobs != NULL) {
		GList *t;
		if (source->priv->start_import_job_id != 0) {
			g_source_remove (source->priv->start_import_job_id);
			source->priv->start_import_job_id = 0;
		}
		for (t = source->priv->import_jobs; t != NULL; t = t->next) {
			RhythmDBImportJob *job = RHYTHMDB_IMPORT_JOB (t->data);
			rhythmdb_import_job_cancel (job);
			g_object_unref (job);
		}
		g_list_free (source->priv->import_jobs);
		source->priv->import_jobs = NULL;
	}

	G_OBJECT_CLASS (rb_library_source_parent_class)->dispose (object);
}

static void
rb_library_source_finalize (GObject *object)
{
	RBLibrarySource *source;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_LIBRARY_SOURCE (object));

	source = RB_LIBRARY_SOURCE (object);

	g_return_if_fail (source->priv != NULL);

	rb_debug ("finalizing library source");

	G_OBJECT_CLASS (rb_library_source_parent_class)->finalize (object);
}

static void
db_load_complete_cb (RhythmDB *db, RBLibrarySource *source)
{
	/* once the database is loaded, we can run the query to populate the library source */
	g_object_set (source, "populate", TRUE, NULL);
}

static void
rb_library_source_constructed (GObject *object)
{
	RBLibrarySource *source;
	RBShell *shell;
	RBEntryView *songs;
	char **locations;

	RB_CHAIN_GOBJECT_METHOD (rb_library_source_parent_class, constructed, object);
	source = RB_LIBRARY_SOURCE (object);

	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell, "db", &source->priv->db, NULL);

	source->priv->settings = g_settings_new ("org.gnome.rhythmbox.library");
	g_signal_connect_object (source->priv->settings, "changed", G_CALLBACK (library_settings_changed_cb), source, 0);

	source->priv->db_settings = g_settings_new ("org.gnome.rhythmbox.rhythmdb");
	g_signal_connect_object (source->priv->db_settings, "changed", G_CALLBACK (db_settings_changed_cb), source, 0);

	g_signal_connect_object (source->priv->db, "load-complete", G_CALLBACK (db_load_complete_cb), source, 0);

	/* Set up the default library location if there's no library location set */
	locations = g_settings_get_strv (source->priv->db_settings, "locations");
	if (g_strv_length (locations) == 0) {
		char *music_dir_uri;

		music_dir_uri = g_filename_to_uri (rb_music_dir (), NULL, NULL);
		if (music_dir_uri != NULL) {
			const char *set_locations[2];
			set_locations[0] = music_dir_uri;
			set_locations[1] = NULL;
			g_settings_set_strv (source->priv->db_settings, "locations", set_locations);

			g_free (music_dir_uri);
		}
	}
	g_strfreev (locations);

	songs = rb_source_get_entry_view (RB_SOURCE (source));

	rb_entry_view_append_column (songs, RB_ENTRY_VIEW_COL_RATING, FALSE);
	rb_entry_view_append_column (songs, RB_ENTRY_VIEW_COL_LAST_PLAYED, FALSE);
	rb_entry_view_append_column (songs, RB_ENTRY_VIEW_COL_FIRST_SEEN, FALSE);

	rb_library_source_sync_child_sources (source);

	g_object_unref (shell);
}

/**
 * rb_library_source_new:
 * @shell: the #RBShell
 *
 * Creates and returns the #RBLibrarySource instance
 *
 * Return value: the #RBLibrarySource
 */
RBSource *
rb_library_source_new (RBShell *shell)
{
	RBSource *source;
	GdkPixbuf *icon;
	GSettings *settings;
	gint size;

	gtk_icon_size_lookup (RB_SOURCE_ICON_SIZE, &size, NULL);
	icon = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
					 "audio-x-generic",
					 size,
					 0, NULL);
	settings = g_settings_new ("org.gnome.rhythmbox.library");
	source = RB_SOURCE (g_object_new (RB_TYPE_LIBRARY_SOURCE,
					  "name", _("Music"),
					  "entry-type", RHYTHMDB_ENTRY_TYPE_SONG,
					  "shell", shell,
					  "pixbuf", icon,
					  "populate", FALSE,		/* wait until the database is loaded */
					  "settings", g_settings_get_child (settings, "source"),
					  NULL));
	if (icon != NULL) {
		g_object_unref (icon);
	}
	g_object_unref (settings);

	rb_shell_register_entry_type_for_source (shell, source, RHYTHMDB_ENTRY_TYPE_SONG);

	return source;
}

static void
rb_library_source_edit_profile_clicked_cb (GtkButton *button, RBLibrarySource *source)
{
	GtkWidget *dialog;

	dialog = gm_audio_profiles_edit_new (gconf_client_get_default (),
					     GTK_WINDOW (source->priv->shell_prefs));
	gtk_widget_show_all (dialog);
	gtk_dialog_run (GTK_DIALOG (dialog));
}

static void
rb_library_source_location_button_clicked_cb (GtkButton *button, RBLibrarySource *source)
{
	GtkWidget *dialog;

	dialog = rb_file_chooser_new (_("Choose Library Location"), GTK_WINDOW (source->priv->shell_prefs),
				      GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, FALSE);
	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
		char *uri;
		char *path;

		uri = gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (dialog));
		if (uri == NULL) {
			uri = gtk_file_chooser_get_current_folder_uri (GTK_FILE_CHOOSER (dialog));
		}

		path = g_uri_unescape_string (uri, NULL);

		gtk_entry_set_text (GTK_ENTRY (source->priv->library_location_entry), path);
		rb_library_source_library_location_cb (GTK_ENTRY (source->priv->library_location_entry),
						       NULL, source);
		g_free (uri);
		g_free (path);
	}

	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
update_library_locations (RBLibrarySource *source)
{
	char **locations;

	if (source->priv->library_location_entry == NULL) {
		return;
	}

	locations = g_settings_get_strv (source->priv->db_settings, "locations");

	/* don't trigger the change notification */
	g_signal_handlers_block_by_func (G_OBJECT (source->priv->library_location_entry),
					 G_CALLBACK (rb_library_source_library_location_cb),
					 source);

	if (g_strv_length (locations) == 1) {
		char *path;

		gtk_widget_set_sensitive (source->priv->library_location_entry, TRUE);

		path = g_uri_unescape_string (locations[0], NULL);
		gtk_entry_set_text (GTK_ENTRY (source->priv->library_location_entry), path);
		g_free (path);
	} else if (g_strv_length (locations) == 0) {
		/* no library directories */
		gtk_widget_set_sensitive (source->priv->library_location_entry, TRUE);
		gtk_entry_set_text (GTK_ENTRY (source->priv->library_location_entry), "");
	} else {
		/* multiple library directories */
		gtk_widget_set_sensitive (source->priv->library_location_entry, FALSE);
		gtk_entry_set_text (GTK_ENTRY (source->priv->library_location_entry), _("Multiple locations set"));
	}

	g_signal_handlers_unblock_by_func (G_OBJECT (source->priv->library_location_entry),
					   G_CALLBACK (rb_library_source_library_location_cb),
					   source);

	g_strfreev (locations);
}

static void
update_layout_path (RBLibrarySource *source)
{
	char *value;
	int active;
	int i;

	value = g_settings_get_string (source->priv->settings, "layout-path");

	active = -1;
	for (i = 0; library_layout_paths[i].path != NULL; i++) {
		if (g_strcmp0 (library_layout_paths[i].path, value) == 0) {
			active = i;
			break;
		}
	}

	g_free (value);
	if (source->priv->layout_path_menu != NULL) {
		gtk_combo_box_set_active (GTK_COMBO_BOX (source->priv->layout_path_menu), active);
	}

	layout_example_label_update (source);
}

static void
update_layout_filename (RBLibrarySource *source)
{
	char *value;
	int active;
	int i;

	value = g_settings_get_string (source->priv->settings, "layout-filename");

	active = -1;
	for (i = 0; library_layout_filenames[i].path != NULL; i++) {
		if (strcmp (library_layout_filenames[i].path, value) == 0) {
			active = i;
			break;
		}
	}
	g_free (value);

	gtk_combo_box_set_active (GTK_COMBO_BOX (source->priv->layout_filename_menu), active);

	layout_example_label_update (source);
}

static void
update_preferred_format (RBLibrarySource *source)
{
	char *str = g_settings_get_string (source->priv->settings, "preferred-format");
	if (str) {
		gm_audio_profile_choose_set_active (source->priv->preferred_format_menu, str);
		g_free (str);
	}
}

static void
db_settings_changed_cb (GSettings *settings, const char *key, RBLibrarySource *source)
{
	if (g_strcmp0 (key, "locations") == 0) {
		update_library_locations (source);
		rb_library_source_sync_child_sources (source);
	}
}

static void
library_settings_changed_cb (GSettings *settings, const char *key, RBLibrarySource *source)
{
	if (g_strcmp0 (key, "layout-path") == 0) {
		rb_debug ("layout path changed");
		update_layout_path (source);
	} else if (g_strcmp0 (key, "layout-filename") == 0) {
		rb_debug ("layout filename changed");
		update_layout_filename (source);
	} else if (g_strcmp0 (key, "preferred-format") == 0) {
		rb_debug ("preferred format changed");
		update_preferred_format (source);
	}
}

static GtkWidget *
impl_get_config_widget (RBDisplayPage *asource, RBShellPreferences *prefs)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (asource);
	GtkBuilder *builder;
	GObject *tmp;
	GObject *label;
	int i;

	if (source->priv->config_widget)
		return source->priv->config_widget;

	g_object_ref (prefs);
	source->priv->shell_prefs = prefs;

	builder = rb_builder_load ("library-prefs.ui", source);
	source->priv->config_widget =
		GTK_WIDGET (gtk_builder_get_object (builder, "library_vbox"));

	rb_builder_boldify_label (builder, "library_location_label");

	source->priv->library_location_entry = GTK_WIDGET (gtk_builder_get_object (builder, "library_location_entry"));
	tmp = gtk_builder_get_object (builder, "library_location_button");
	g_signal_connect (tmp,
			  "clicked",
			  G_CALLBACK (rb_library_source_location_button_clicked_cb),
			  asource);
	g_signal_connect (G_OBJECT (source->priv->library_location_entry),
			  "focus-out-event",
			  G_CALLBACK (rb_library_source_library_location_cb),
			  asource);

	source->priv->watch_library_check = GTK_WIDGET (gtk_builder_get_object (builder, "watch_library_check"));
	g_settings_bind (source->priv->db_settings, "monitor-library",
			 source->priv->watch_library_check, "active",
			 G_SETTINGS_BIND_DEFAULT);

	rb_builder_boldify_label (builder, "library_structure_label");

	tmp = gtk_builder_get_object (builder, "layout_path_menu_box");
	label = gtk_builder_get_object (builder, "layout_path_menu_label");
	source->priv->layout_path_menu = gtk_combo_box_text_new ();
	gtk_box_pack_start (GTK_BOX (tmp), source->priv->layout_path_menu, TRUE, TRUE, 0);
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), source->priv->layout_path_menu);
	g_signal_connect (G_OBJECT (source->priv->layout_path_menu),
			  "changed",
			  G_CALLBACK (rb_library_source_path_changed_cb),
			  asource);
	for (i = 0; i < num_library_layout_paths; i++) {
		gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (source->priv->layout_path_menu),
						_(library_layout_paths[i].title));
	}

	tmp = gtk_builder_get_object (builder, "layout_filename_menu_box");
	label = gtk_builder_get_object (builder, "layout_filename_menu_label");
	source->priv->layout_filename_menu = gtk_combo_box_text_new ();
	gtk_box_pack_start (GTK_BOX (tmp), source->priv->layout_filename_menu, TRUE, TRUE, 0);
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), source->priv->layout_filename_menu);
	g_signal_connect (G_OBJECT (source->priv->layout_filename_menu),
			  "changed",
			  G_CALLBACK (rb_library_source_filename_changed_cb),
			  asource);
	for (i = 0; i < num_library_layout_filenames; i++) {
		gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (source->priv->layout_filename_menu),
						_(library_layout_filenames[i].title));
	}

	tmp = gtk_builder_get_object (builder, "edit_profile_button");
	g_signal_connect (tmp,
			  "clicked",
			  G_CALLBACK (rb_library_source_edit_profile_clicked_cb),
			  asource);

	tmp = gtk_builder_get_object (builder, "preferred_format_menu_box");
	label = gtk_builder_get_object (builder, "preferred_format_menu_label");
	source->priv->preferred_format_menu = gm_audio_profile_choose_new ();
	gtk_box_pack_start (GTK_BOX (tmp), source->priv->preferred_format_menu, TRUE, TRUE, 0);
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), source->priv->preferred_format_menu);
	g_signal_connect (G_OBJECT (source->priv->preferred_format_menu),
			  "changed",
			  G_CALLBACK (rb_library_source_format_changed_cb),
			  asource);

	source->priv->layout_example_label = GTK_WIDGET (gtk_builder_get_object (builder, "layout_example_label"));

	update_library_locations (source);
	update_preferred_format (source);

	update_layout_path (source);
	update_layout_filename (source);

	return source->priv->config_widget;
}

static gboolean
rb_library_source_library_location_cb (GtkEntry *entry,
				       GdkEventFocus *event,
				       RBLibrarySource *source)
{
	const char *path;
	const char *locations[2] = { NULL, NULL };
	GFile *file;
	char *uri;

	path = gtk_entry_get_text (entry);
	file = g_file_parse_name (path);
	uri = g_file_get_uri (file);
	g_object_unref (file);

	if (uri && uri[0]) {
		locations[0] = uri;
	}

	g_settings_set_strv (source->priv->db_settings, "locations", locations);

	g_free (uri);
	return FALSE;
}

static gboolean
impl_receive_drag (RBDisplayPage *asource, GtkSelectionData *data)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (asource);
	GList *list, *i;
	GList *entries = NULL;
	gboolean is_id;

	rb_debug ("parsing uri list");
	list = rb_uri_list_parse ((const char *) gtk_selection_data_get_data (data));
	is_id = (gtk_selection_data_get_data_type (data) == gdk_atom_intern ("application/x-rhythmbox-entry", TRUE));

	for (i = list; i != NULL; i = g_list_next (i)) {
		if (i->data != NULL) {
			char *uri = i->data;
			RhythmDBEntry *entry;

			entry = rhythmdb_entry_lookup_from_string (source->priv->db, uri, is_id);

			if (entry == NULL) {
				RhythmDBImportJob *job;
				/* add to the library */
				job = maybe_create_import_job (source);
				rhythmdb_import_job_add_uri (job, uri);
			} else {
				/* add to list of entries to copy */
				entries = g_list_prepend (entries, entry);
			}

			g_free (uri);
		}
	}

	if (entries) {
		entries = g_list_reverse (entries);
		if (rb_source_can_paste (RB_SOURCE (asource)))
			rb_source_paste (RB_SOURCE (asource), entries);
		g_list_free (entries);
	}

	g_list_free (list);
	return TRUE;
}

static gboolean
impl_show_popup (RBDisplayPage *source)
{
	_rb_display_page_show_popup (source, "/LibrarySourcePopup");
	return TRUE;
}

static void
rb_library_source_path_changed_cb (GtkComboBox *box, RBLibrarySource *source)
{
	const char *path;
	gint index;

	index = gtk_combo_box_get_active (box);
	if (index >= 0) {
		path = library_layout_paths[index].path;

		g_settings_set_string (source->priv->settings, "layout-path", path);
	}
}

static void
rb_library_source_filename_changed_cb (GtkComboBox *box, RBLibrarySource *source)
{
	const char *filename;
	gint index;

	index = gtk_combo_box_get_active (box);
	if (index >= 0) {
		filename = library_layout_filenames[index].path;
		g_settings_set_string (source->priv->settings, "layout-filename", filename);
	}
}

static void
rb_library_source_format_changed_cb (GtkWidget *widget, RBLibrarySource *source)
{
	GMAudioProfile *profile;

	profile = gm_audio_profile_choose_get_active (widget);
	g_settings_set_string (source->priv->settings, "preferred-format", gm_audio_profile_get_id (profile));
	
	layout_example_label_update (source);
}

/*
 * Perform magic on a path to make it safe.
 *
 * This will always replace '/' with '-', and optionally make the file name
 * shell-friendly. This involves removing replacing shell metacharacters and all
 * whitespace with '_'. Also any leading periods are removed so that the files
 * don't end up being hidden.
 */
static char *
sanitize_path (gboolean strip_chars, const char *str)
{
	gchar *s;

	/* Skip leading periods, otherwise files disappear... */
	while (*str == '.')
		str++;

	s = g_strdup(str);
	/* Replace path seperators with a hyphen */
	g_strdelimit (s, "/", '-');
	if (strip_chars) {
		/* Replace separators with a hyphen */
		g_strdelimit (s, "\\:|", '-');
		/* Replace all other weird characters to whitespace */
		g_strdelimit (s, "*?&!\'\"$()`>{}", ' ');
		/* Replace all whitespace with underscores */
		/* TODO: I'd like this to compress whitespace aswell */
		g_strdelimit (s, "\t ", '_');
	}

	return s;
}

static char *
sanitize_pattern (gboolean strip_chars, const char *pat)
{
	if (strip_chars) {
		gchar *s;

		s = g_strdup (pat);
		g_strdelimit (s, "\t ", '_');
		return s;
	} else {
		return g_strdup (pat);
	}
}

/*
 * Parse a filename pattern and replace markers with values from a RhythmDBEntry
 *
 * Valid markers so far are:
 * %at -- album title
 * %aa -- album artist
 * %aA -- album artist (lowercase)
 * %as -- album artist sortname
 * %aS -- album artist sortname (lowercase)
 * %ay -- album release year
 * %an -- album disc number
 * %aN -- album disc number, zero padded
 * %ag -- album genre
 * %aG -- album genre (lowercase)
 * %tn -- track number (i.e 8)
 * %tN -- track number, zero padded (i.e 08)
 * %tt -- track title
 * %ta -- track artist
 * %tA -- track artist (lowercase)
 * %ts -- track artist sortname
 * %tS -- track artist sortname (lowercase)
 */
static char *
filepath_parse_pattern (RBLibrarySource *source,
			const char *pattern,
			RhythmDBEntry *entry)
{
	/* p is the pattern iterator, i is a general purpose iterator */
	const char *p;
	char *temp;
	GString *s;
	RBRefString *albumartist;
	RBRefString *albumartist_sort;
	gboolean strip_chars;

	if (pattern == NULL || pattern[0] == 0)
		return g_strdup (" ");

	strip_chars = g_settings_get_boolean (source->priv->settings, "strip-chars");

	/* figure out album artist - use the plain artist field if not specified */
	albumartist = rhythmdb_entry_get_refstring (entry, RHYTHMDB_PROP_ALBUM_ARTIST);
	if (albumartist == NULL || g_strcmp0 (rb_refstring_get (albumartist), "") == 0) {
		albumartist = rhythmdb_entry_get_refstring (entry, RHYTHMDB_PROP_ARTIST);
	}
	albumartist_sort = rhythmdb_entry_get_refstring (entry, RHYTHMDB_PROP_ALBUM_ARTIST_SORTNAME);
	if (albumartist_sort == NULL || g_strcmp0 (rb_refstring_get (albumartist_sort), "") == 0) {
		albumartist_sort = rhythmdb_entry_get_refstring (entry, RHYTHMDB_PROP_ARTIST_SORTNAME);
	}

	s = g_string_new (NULL);

	p = pattern;
	while (*p) {
		char *string = NULL;

		/* If not a % marker, copy and continue */
		if (*p != '%') {
			g_string_append_c (s, *p++);
			/* Explicit increment as we continue past the increment */
			continue;
		}

		/* Is a % marker, go to next and see what to do */
		switch (*++p) {
		case '%':
			/*
			 * Literal %
			 */
			g_string_append_c (s, '%');
			break;
		case 'a':
			/*
			 * Album tag
			 */
			switch (*++p) {
			case 't':
				string = sanitize_path (strip_chars, rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM));
				break;
			case 'T':
				string = sanitize_path (strip_chars, rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM_FOLDED));
				break;
			case 'a':
				string = sanitize_path (strip_chars, rb_refstring_get (albumartist));
				break;
			case 'A':
				string = sanitize_path (strip_chars, rb_refstring_get_folded (albumartist));
				break;
			case 's':
				string = sanitize_path (strip_chars, rb_refstring_get (albumartist_sort));
				break;
			case 'S':
				string = sanitize_path (strip_chars, rb_refstring_get_folded (albumartist_sort));
				break;
			case 'y':
				string = g_strdup_printf ("%u", (guint)rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_YEAR));
				break;
			case 'n':
				string = g_strdup_printf ("%u", (guint)rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DISC_NUMBER));
				break;
			case 'N':
				string = g_strdup_printf ("%02u", (guint)rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DISC_NUMBER));
				break;
			case 'g':
				string = sanitize_path (strip_chars, rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_GENRE));
				break;
			case 'G':
				string = sanitize_path (strip_chars, rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_GENRE_FOLDED));
				break;
			default:
				string = g_strdup_printf ("%%a%c", *p);
			}

			break;

		case 't':
			/*
			 * Track tag
			 */
			switch (*++p) {
			case 't':
				string = sanitize_path (strip_chars, rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE));
				break;
			case 'T':
				string = sanitize_path (strip_chars, rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE_FOLDED));
				break;
			case 'a':
				string = sanitize_path (strip_chars, rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST));
				break;
			case 'A':
				string = sanitize_path (strip_chars, rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST_FOLDED));
				break;
			case 's':
				string = sanitize_path (strip_chars, rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST_SORTNAME));
				break;
			case 'S':
				string = sanitize_path (strip_chars, rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST_SORTNAME_FOLDED));
				break;
			case 'n':
				/* Track number */
				string = g_strdup_printf ("%u", (guint)rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_TRACK_NUMBER));
				break;
			case 'N':
				/* Track number, zero-padded */
				string = g_strdup_printf ("%02u", (guint)rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_TRACK_NUMBER));
				break;
			default:
				string = g_strdup_printf ("%%t%c", *p);
 			}

			break;

		default:
			string = g_strdup_printf ("%%%c", *p);
		}

		if (string)
			g_string_append (s, string);
		g_free (string);

		++p;
	}

	temp = s->str;
	g_string_free (s, FALSE);
	rb_refstring_unref (albumartist);
	return temp;
}

static void
layout_example_label_update (RBLibrarySource *source)
{
	char *file_pattern;
	char *path_pattern;
	char *file_value;
	char *path_value;
	char *example;
	char *format;
	char *tmp;
	gboolean strip_chars;
	GMAudioProfile *profile;
	RhythmDBEntryType *entry_type;
	RhythmDBEntry *sample_entry;

  	profile = gm_audio_profile_choose_get_active (source->priv->preferred_format_menu);

	file_pattern = g_settings_get_string (source->priv->settings, "layout-filename");
	if (file_pattern == NULL) {
		file_pattern = g_strdup (library_layout_filenames[0].path);
	}
	strip_chars = g_settings_get_boolean (source->priv->settings, "strip-chars");
	tmp = sanitize_pattern (strip_chars, file_pattern);
	g_free (file_pattern);
	file_pattern = tmp;

	path_pattern = g_settings_get_string (source->priv->settings, "layout-path");
	if (path_pattern == NULL) {
		path_pattern = g_strdup (library_layout_paths[0].path);
	}

	g_object_get (source, "entry-type", &entry_type, NULL);
	sample_entry = rhythmdb_entry_example_new (source->priv->db, entry_type, NULL);
	g_object_unref (entry_type);

	file_value = filepath_parse_pattern (source, file_pattern, sample_entry);
	path_value = filepath_parse_pattern (source, path_pattern, sample_entry);
	rhythmdb_entry_unref (sample_entry);

	example = g_build_filename (G_DIR_SEPARATOR_S, path_value, file_value, NULL);
	g_free (file_value);
	g_free (file_pattern);
	g_free (path_value);
	g_free (path_pattern);

	format = g_strconcat ("<small><i><b>",
			      _("Example Path:"),
			      "</b> ",
			      example,
			      ".",
			      profile ? gm_audio_profile_get_extension (profile) : "ogg",
			      "</i></small>", NULL);
	g_free (example);

	gtk_label_set_markup (GTK_LABEL (source->priv->layout_example_label), format);
	g_free (format);
}

/*
 * Build the absolute filename for the specified track.
 *
 * The base path is the extern variable 'base_path', the format to use
 * is the extern variable 'file_pattern'. Free the result when you
 * have finished with it.
 *
 * Stolen from Sound-Juicer
 */
static char*
build_filename (RBLibrarySource *source, RhythmDBEntry *entry, const char *extension)
{
	GFile *library_location;
	GFile *dir;
	GFile *dest;
	char *realfile;
	char *realpath;
	char *filename;
	char *string = NULL;
	char *tmp;
	char **locations;
	char *layout_path;
	char *layout_filename;
	gboolean strip_chars;

	locations = g_settings_get_strv (source->priv->db_settings, "locations");
	layout_path = g_settings_get_string (source->priv->settings, "layout-path");
	layout_filename = g_settings_get_string (source->priv->settings, "layout-filename");
	strip_chars = g_settings_get_boolean (source->priv->settings, "strip-chars");

	if (locations == NULL || layout_path == NULL || layout_filename == NULL) {
		/* emit warning */
		rb_debug ("Could not retrieve library layout settings");
		goto out;
	}

	tmp = sanitize_pattern (strip_chars, layout_filename);
	g_free (layout_filename);
	layout_filename = tmp;

	realpath = filepath_parse_pattern (source, layout_path, entry);

	library_location = g_file_new_for_uri ((const char *)locations[0]);
	dir = g_file_resolve_relative_path (library_location, realpath);
	g_object_unref (library_location);
	g_free (realpath);

	realfile = filepath_parse_pattern (source, layout_filename, entry);
	if (extension) {
		filename = g_strdup_printf ("%s.%s", realfile, extension);
		g_free (realfile);
	} else {
		filename = realfile;
	}

	dest = g_file_resolve_relative_path (dir, filename);
	g_object_unref (dir);
	g_free (filename);

	string = g_file_get_uri (dest);
	g_object_unref (dest);
 out:
	g_strfreev (locations);
	g_free (layout_path);
	g_free (layout_filename);

	return string;
}

static gboolean
impl_can_paste (RBSource *asource)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (asource);
	char **locations;
	gboolean can_paste = TRUE;
	char *str;

	locations = g_settings_get_strv (source->priv->db_settings, "locations");
	can_paste = (g_strv_length (locations) > 0);
	g_strfreev (locations);

	str = g_settings_get_string (source->priv->settings, "layout-path");
	can_paste &= (str != NULL);
	g_free (str);

	str = g_settings_get_string (source->priv->settings, "layout-filename");
	can_paste &= (str != NULL);
	g_free (str);

	str = g_settings_get_string (source->priv->settings, "preferred-format");
	can_paste &= (str != NULL);
	g_free (str);

	return can_paste;
}

static char *
get_dest_uri_cb (RBTrackTransferBatch *batch,
		 RhythmDBEntry *entry,
		 const char *mediatype,
		 const char *extension,
		 RBLibrarySource *source)
{
	char *dest;
	char *sane_dest;

	dest = build_filename (source, entry, extension);
	if (dest == NULL) {
		rb_debug ("could not create destination path for entry");
		return NULL;
	}

	sane_dest = rb_sanitize_uri_for_filesystem (dest);
	g_free (dest);
	rb_debug ("destination URI for %s is %s",
		  rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION),
		  sane_dest);
	return sane_dest;
}

static void
track_done_cb (RBTrackTransferBatch *batch,
	       RhythmDBEntry *entry,
	       const char *dest,
	       guint64 dest_size,
	       const char *dest_mediatype,
	       GError *error,
	       RBLibrarySource *source)
{
	if (error != NULL) {
		/* probably want to cancel the batch on some errors:
		 * - out of disk space / read only
		 * - source has vanished (hmm, how would we know?)
		 *
		 * and we probably want to do something intelligent about some other errors:
		 * - encoder pipeline errors?  hmm.
		 */
		if (g_error_matches (error, RB_ENCODER_ERROR, RB_ENCODER_ERROR_OUT_OF_SPACE) ||
		    g_error_matches (error, RB_ENCODER_ERROR, RB_ENCODER_ERROR_DEST_READ_ONLY)) {
			rb_debug ("fatal transfer error: %s", error->message);
			rb_track_transfer_batch_cancel (batch);
			rb_error_dialog (NULL, _("Error transferring track"), "%s", error->message);
		} else if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
			rb_debug ("not displaying 'file exists' error for %s", dest);
		} else {
			rb_error_dialog (NULL, _("Error transferring track"), "%s", error->message);
		}
	} else {
		/* could probably do something smarter here to avoid
		 * re-reading tags etc.
		 */
		rhythmdb_add_uri (source->priv->db, dest);
	}
}

static RBTrackTransferBatch *
impl_paste (RBSource *asource, GList *entries)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (asource);
	RBTrackTransferQueue *xferq;
	GList *l;
	RBShell *shell;
	RhythmDBEntryType *source_entry_type;
	RBTrackTransferBatch *batch;
	gboolean start_batch = FALSE;

	if (impl_can_paste (asource) == FALSE) {
		g_warning ("RBLibrarySource impl_paste called when layout settings unset");
		return NULL;
	}

	g_object_get (source,
		      "shell", &shell,
		      "entry-type", &source_entry_type,
		      NULL);
	g_object_get (shell, "track-transfer-queue", &xferq, NULL);
	g_object_unref (shell);

	batch = rb_track_transfer_batch_new (NULL, NULL, NULL, G_OBJECT (source));
	g_signal_connect_object (batch, "get-dest-uri", G_CALLBACK (get_dest_uri_cb), source, 0);
	g_signal_connect_object (batch, "track-done", G_CALLBACK (track_done_cb), source, 0);

	for (l = entries; l != NULL; l = g_list_next (l)) {
		RhythmDBEntry *entry = (RhythmDBEntry *)l->data;
		RhythmDBEntryType *entry_type;
		RBSource *source_source;

		rb_debug ("pasting entry %s", rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));

		entry_type = rhythmdb_entry_get_entry_type (entry);
		if (entry_type == source_entry_type) {
			rb_debug ("can't copy an entry from the library to itself");
			continue;
		}

		/* see if the responsible source lets us copy */
		source_source = rb_shell_get_source_by_entry_type (shell, entry_type);
		if ((source_source != NULL) && !rb_source_can_copy (source_source)) {
			rb_debug ("source for the entry doesn't want us to copy it");
			continue;
		}

		rb_track_transfer_batch_add (batch, entry);
		start_batch = TRUE;
	}
	g_object_unref (source_entry_type);

	if (start_batch) {
		rb_track_transfer_queue_start_batch (xferq, batch);
	} else {
		g_object_unref (batch);
		batch = NULL;
	}

	g_object_unref (xferq);
	return batch;
}

static guint
impl_want_uri (RBSource *source, const char *uri)
{
	/* assume anything local, on smb, or on sftp is a song */
	if (rb_uri_is_local (uri) ||
	    g_str_has_prefix (uri, "smb://") ||
	    g_str_has_prefix (uri, "sftp://") ||
	    g_str_has_prefix (uri, "ssh://"))
		return 50;

	return 0;
}

static void
import_job_status_changed_cb (RhythmDBImportJob *job, int total, int imported, RBLibrarySource *source)
{
	RhythmDBImportJob *head = RHYTHMDB_IMPORT_JOB (source->priv->import_jobs->data);
	if (job == head) {		/* it was inevitable */
		rb_display_page_notify_status_changed (RB_DISPLAY_PAGE (source));
	}
}

static void
import_job_complete_cb (RhythmDBImportJob *job, int total, RBLibrarySource *source)
{
	rb_debug ("import job complete");

	/* maybe show a notification here? */

	source->priv->import_jobs = g_list_remove (source->priv->import_jobs, job);
	g_object_unref (job);
}

static gboolean
start_import_job (RBLibrarySource *source)
{
	RhythmDBImportJob *job;
	source->priv->start_import_job_id = 0;

	rb_debug ("starting import job");
	job = RHYTHMDB_IMPORT_JOB (source->priv->import_jobs->data);

	rhythmdb_import_job_start (job);

	return FALSE;
}

static RhythmDBImportJob *
maybe_create_import_job (RBLibrarySource *source)
{
	RhythmDBImportJob *job;
	if (source->priv->import_jobs == NULL || source->priv->start_import_job_id == 0) {

		rb_debug ("creating new import job");
		job = rhythmdb_import_job_new (source->priv->db,
					       RHYTHMDB_ENTRY_TYPE_SONG,
					       RHYTHMDB_ENTRY_TYPE_IGNORE,
					       RHYTHMDB_ENTRY_TYPE_IMPORT_ERROR);

		g_signal_connect_object (job,
					 "status-changed",
					 G_CALLBACK (import_job_status_changed_cb),
					 source, 0);
		g_signal_connect_object (job,
					 "complete",
					 G_CALLBACK (import_job_complete_cb),
					 source, 0);
		source->priv->import_jobs = g_list_prepend (source->priv->import_jobs, job);
	} else {
		rb_debug ("using existing unstarted import job");
		job = RHYTHMDB_IMPORT_JOB (source->priv->import_jobs->data);
	}

	/* allow some time for more URIs to be added if we're importing a bunch of things */
	if (source->priv->start_import_job_id != 0) {
		g_source_remove (source->priv->start_import_job_id);
	}
	source->priv->start_import_job_id = g_timeout_add (250, (GSourceFunc) start_import_job, source);

	return job;
}

struct ImportJobCallbackData {
	char *uri;
	RBSource *source;
	RBSourceAddCallback callback;
	gpointer data;
	GDestroyNotify destroy_data;
};

static void
import_job_callback_destroy (struct ImportJobCallbackData *data)
{
	if (data->destroy_data != NULL) {
		data->destroy_data (data->data);
	}
	g_object_unref (data->source);
	g_free (data->uri);
	g_free (data);
}

static void
import_job_callback_cb (RhythmDBImportJob *job, int total, struct ImportJobCallbackData *data)
{
	data->callback (data->source, data->uri, data->data);
}

static void
impl_add_uri (RBSource *asource,
	      const char *uri,
	      const char *title,
	      const char *genre,
	      RBSourceAddCallback callback,
	      gpointer data,
	      GDestroyNotify destroy_data)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (asource);
	RhythmDBImportJob *job;

	job = maybe_create_import_job (source);

	rb_debug ("adding uri %s to library", uri);
	rhythmdb_import_job_add_uri (job, uri);

	if (callback != NULL) {
		struct ImportJobCallbackData *cbdata;

		cbdata = g_new0 (struct ImportJobCallbackData, 1);
		cbdata->uri = g_strdup (uri);
		cbdata->source = g_object_ref (source);
		cbdata->callback = callback;
		cbdata->data = data;
		cbdata->destroy_data = destroy_data;
		g_signal_connect_data (job, "complete", G_CALLBACK (import_job_callback_cb), cbdata, (GClosureNotify) import_job_callback_destroy, 0);
	}
}

static void
rb_library_source_add_child_source (const char *path, RBLibrarySource *library_source)
{
	RBSource *source;
	GPtrArray *query;
	RBShell *shell;
	char *name;
	GdkPixbuf *icon;
	RhythmDBEntryType *entry_type;
	char *sort_column;
	int sort_order;
	GFile *file;

	g_object_get (library_source,
		      "shell", &shell,
		      "entry-type", &entry_type,
		      NULL);

	file = g_file_new_for_uri (path);
	name = g_file_get_basename (file);
	g_object_unref (file);

	rb_entry_view_get_sorting_order (rb_source_get_entry_view (RB_SOURCE (library_source)),
					 &sort_column, &sort_order);

	source = rb_auto_playlist_source_new (shell, name, FALSE);
	query = rhythmdb_query_parse (library_source->priv->db,
				      RHYTHMDB_QUERY_PROP_EQUALS, RHYTHMDB_PROP_TYPE, entry_type,
				      RHYTHMDB_QUERY_PROP_PREFIX, RHYTHMDB_PROP_LOCATION, path,
				      RHYTHMDB_QUERY_END);
	rb_auto_playlist_source_set_query (RB_AUTO_PLAYLIST_SOURCE (source), query,
					   RHYTHMDB_QUERY_MODEL_LIMIT_NONE, NULL,
					   sort_column, sort_order);
	rhythmdb_query_free (query);
	g_free (sort_column);

	g_object_get (library_source, "pixbuf", &icon, NULL);
	g_object_set (source, "pixbuf", icon, NULL);
	if (icon != NULL) {
		g_object_unref (icon);
	}

	rb_shell_append_display_page (shell, RB_DISPLAY_PAGE (source), RB_DISPLAY_PAGE (library_source));
	library_source->priv->child_sources = g_list_prepend (library_source->priv->child_sources, source);

	g_object_unref (entry_type);
	g_object_unref (shell);
	g_free (name);
}

static void
rb_library_source_sync_child_sources (RBLibrarySource *source)
{
	char **locations;
	int num_locations;

	locations = g_settings_get_strv (source->priv->db_settings, "locations");

	/* FIXME: don't delete and re-create sources that are still there */
	g_list_foreach (source->priv->child_sources, (GFunc)rb_display_page_delete_thyself, NULL);
	g_list_free (source->priv->child_sources);
	source->priv->child_sources = NULL;

	num_locations = g_strv_length (locations);
	if (num_locations > 1) {
		int i;
		for (i = 0; i < num_locations; i++) {
			rb_library_source_add_child_source (locations[i], source);
		}
	}
	g_strfreev (locations);
}

static void
impl_get_status (RBDisplayPage *source, char **text, char **progress_text, float *progress)
{
	RB_DISPLAY_PAGE_CLASS (rb_library_source_parent_class)->get_status (source, text, progress_text, progress);
	RBLibrarySource *lsource = RB_LIBRARY_SOURCE (source);

	if (lsource->priv->import_jobs != NULL) {
		RhythmDBImportJob *job = RHYTHMDB_IMPORT_JOB (lsource->priv->import_jobs->data);
		_rb_source_set_import_status (RB_SOURCE (source), job, progress_text, progress);
	}
}
