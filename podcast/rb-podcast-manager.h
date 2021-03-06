/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2005 Renato Araujo Oliveira Filho - INdT <renato.filho@indt.org.br>
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

#include <glib.h>
#include <rhythmdb/rhythmdb.h>

#ifndef RB_PODCAST_MANAGER_H
#define RB_PODCAST_MANAGER_H

G_BEGIN_DECLS

#define RB_TYPE_PODCAST_MANAGER            (rb_podcast_manager_get_type ())
#define RB_PODCAST_MANAGER(o)              (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_PODCAST_MANAGER, RBPodcastManager))
#define RB_PODCAST_MANAGER_CLASS(k)        (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_PODCAST_MANAGER, RBPodcastManagerClass))
#define RB_IS_PODCAST_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_PODCAST_MANAGER))
#define RB_IS_PODCAST_MANAGER_CLASS(k)     (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_PODCAST_MANAGER))
#define RB_PODCAST_MANAGER_GET_CLASS(o)    (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_PODCAST_MANAGER, RBPodcastManagerClass))

typedef struct RBPodcastManagerPrivate RBPodcastManagerPrivate;

typedef struct
{
        GObject parent;

        RBPodcastManagerPrivate *priv;
} RBPodcastManager;

typedef struct
{
	GObjectClass parent_class;

    /* signals */
    void        (*status_changed)    		(RBPodcastManager* pd, RhythmDBEntry *entry, glong value);
    void        (*start_download)    		(RBPodcastManager* pd, RhythmDBEntry *entry);
    void        (*finish_download)   		(RBPodcastManager* pd, RhythmDBEntry *entry);
    void        (*feed_updates_available)   	(RBPodcastManager* pd, RhythmDBEntry *entry);
    gboolean    (*process_error)	   	(RBPodcastManager* pd, const char *error, gboolean existing);

} RBPodcastManagerClass;

GType                   rb_podcast_manager_get_type    		(void);
RBPodcastManager*      	rb_podcast_manager_new         		(RhythmDB *db);
void                    rb_podcast_manager_download_entry  	(RBPodcastManager *pd, RhythmDBEntry *entry);
void		        rb_podcast_manager_cancel_download	(RBPodcastManager *pd, RhythmDBEntry *entry);
void 			rb_podcast_manager_update_feeds 	(RBPodcastManager *pd);
void                    rb_podcast_manager_start_sync  		(RBPodcastManager *pd);
void			rb_podcast_manager_delete_download	(RBPodcastManager *pd, RhythmDBEntry *entry);
gboolean                rb_podcast_manager_remove_feed 		(RBPodcastManager *pd,
								 const gchar* url,
								 gboolean remove_files);
gchar *                 rb_podcast_manager_get_podcast_dir	(RBPodcastManager *pd);

gboolean                rb_podcast_manager_subscribe_feed    	(RBPodcastManager *pd, const gchar* url, gboolean automatic);
void            	rb_podcast_manager_unsubscribe_feed    	(RhythmDB *db, const gchar* url);
void			rb_podcast_manager_shutdown 		(RBPodcastManager *pd);
RhythmDBEntry *         rb_podcast_manager_add_post  	  	(RhythmDB *db,
                               			         	 const char *name,
	                                                 	 const char *title,
	                                                 	 const char *subtitle,
	                                                 	 const char *generator,
	                                                 	 const char *uri,
		        	                               	 const char *description,
	        	                                       	 gulong date,
								 gulong duration,
								 guint64 filesize);

gboolean		rb_podcast_manager_entry_downloaded	(RhythmDBEntry *entry);
gboolean		rb_podcast_manager_entry_in_download_queue (RBPodcastManager *pd, RhythmDBEntry *entry);


G_END_DECLS

#endif /* RB_PODCAST_MANAGER_H */
