/*************************************************************************/
/* Copyright (C) 2011-2013 matias <mati86dl@gmail.com>                   */
/*                                                                       */
/* This program is free software: you can redistribute it and/or modify  */
/* it under the terms of the GNU General Public License as published by  */
/* the Free Software Foundation, either version 3 of the License, or     */
/* (at your option) any later version.                                   */
/*                                                                       */
/* This program is distributed in the hope that it will be useful,       */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of        */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         */
/* GNU General Public License for more details.                          */
/*                                                                       */
/* You should have received a copy of the GNU General Public License     */
/* along with this program.  If not, see <http://www.gnu.org/licenses/>. */
/*************************************************************************/

#if HAVE_CONFIG_H
#include <config.h>
#endif

#if defined(GETTEXT_PACKAGE)
#include <glib/gi18n-lib.h>
#else
#include <glib/gi18n.h>
#endif

#include "pragha-musicobject.h"
#include "pragha-lastfm.h"
#include "pragha-simple-async.h"
#include "pragha-utils.h"
#include "pragha-tagger.h"
#include "pragha-tags-dialog.h"
#include "pragha-tags-mgmt.h"
#include "pragha-musicobject-mgmt.h"
#include "xml_helper.h"
#include "pragha.h"

#ifdef HAVE_LIBCLASTFM

#include <clastfm.h>

struct _PraghaLastfm {
	/* Last session status. */
	LASTFM_SESSION           *session_id;
	enum LASTFM_STATUS_CODES  status;
	gboolean                  has_user;
	gboolean                  has_pass;

	/* Elapsed Time*/
	time_t playback_started;

	GtkWidget         *ntag_lastfm_button;
	PraghaMusicobject *nmobj;
	PRAGHA_MUTEX      (nmobj_mutex);

	/* Menu options */
	GtkActionGroup *action_group_main_menu;
	guint           merge_id_main_menu;

	GtkActionGroup *action_group_playlist;
	guint merge_id_playlist;

	guint           timeout_id;

	/* Future PraghaAplication */
	PraghaApplication *pragha;
};

#define LASTFM_API_KEY "ecdc2d21dbfe1139b1f0da35daca9309"
#define LASTFM_SECRET  "f3498ce387f30eeae8ea1b1023afb32b"

typedef enum {
	LASTFM_NONE = 0,
	LASTFM_GET_SIMILAR,
	LASTFM_GET_LOVED
} LastfmQueryType;

/*
 * Prototypes
 */

static void lastfm_get_similar_current_playlist_action  (GtkAction *action, PraghaLastfm *clastfm);
static void lastfm_track_current_playlist_love_action   (GtkAction *action, PraghaLastfm *clastfm);
static void lastfm_track_current_playlist_unlove_action (GtkAction *action, PraghaLastfm *clastfm);
static void lastfm_add_favorites_action                 (GtkAction *action, PraghaLastfm *clastfm);
static void lastfm_get_similar_action                   (GtkAction *action, PraghaLastfm *clastfm);
static void lastfm_import_xspf_action                   (GtkAction *action, PraghaLastfm *clastfm);
static void lastfm_track_love_action                    (GtkAction *action, PraghaLastfm *clastfm);
static void lastfm_track_unlove_action                  (GtkAction *action, PraghaLastfm *clastfm);

static const GtkActionEntry main_menu_actions [] = {
	{"Lastfm", NULL, N_("_Lastfm")},
	{"Love track", NULL, N_("Love track"),
	 "", "Love track", G_CALLBACK(lastfm_track_love_action)},
	{"Unlove track", NULL, N_("Unlove track"),
	 "", "Unlove track", G_CALLBACK(lastfm_track_unlove_action)},
	{"Import a XSPF playlist", NULL, N_("Import a XSPF playlist"),
	 "", "Import a XSPF playlist", G_CALLBACK(lastfm_import_xspf_action)},
	{"Add favorites", NULL, N_("Add favorites"),
	 "", "Add favorites", G_CALLBACK(lastfm_add_favorites_action)},
	{"Add similar", NULL, N_("Add similar"),
	 "", "Add similar", G_CALLBACK(lastfm_get_similar_action)},
};

static const gchar *main_menu_xml = "<ui>					\
	<menubar name=\"Menubar\">						\
		<menu action=\"ToolsMenu\">					\
			<placeholder name=\"pragha-plugins-placeholder\">		\
				<menu action=\"Lastfm\">				\
					<menuitem action=\"Love track\"/>		\
					<menuitem action=\"Unlove track\"/>		\
					<separator/>					\
					<menuitem action=\"Import a XSPF playlist\"/>	\
					<menuitem action=\"Add favorites\"/>		\
					<menuitem action=\"Add similar\"/>		\
				</menu>							\
				<separator/>						\
			</placeholder>						\
		</menu>								\
	</menubar>								\
</ui>";

static const GtkActionEntry playlist_actions [] = {
	{"Love track", NULL, N_("Love track"),
	 "", "Love track", G_CALLBACK(lastfm_track_current_playlist_love_action)},
	{"Unlove track", NULL, N_("Unlove track"),
	 "", "Unlove track", G_CALLBACK(lastfm_track_current_playlist_unlove_action)},
	{"Add similar", NULL, N_("Add similar"),
	 "", "Add similar", G_CALLBACK(lastfm_get_similar_current_playlist_action)},
};

static const gchar *playlist_xml = "<ui>					\
	<popup name=\"SelectionPopup\">		   				\
	<menu action=\"ToolsMenu\">						\
		<placeholder name=\"pragha-plugins-placeholder\">			\
			<menuitem action=\"Love track\"/>				\
			<menuitem action=\"Unlove track\"/>				\
			<separator/>							\
			<menuitem action=\"Add similar\"/>				\
			<separator/>						\
		</placeholder>							\
	</menu>									\
	</popup>				    				\
</ui>";

/*
 * Some structs to handle threads.
 */

typedef struct {
	GList *list;
	LastfmQueryType query_type;
	guint query_count;
	PraghaLastfm *clastfm;
} AddMusicObjectListData;

typedef struct {
	PraghaLastfm *clastfm;
	PraghaMusicobject *mobj;
} PraghaLastfmAsyncData;

static PraghaLastfmAsyncData *
pragha_lastfm_async_data_new (PraghaLastfm *clastfm)
{
	PraghaBackend *backend;
	PraghaLastfmAsyncData *data;

	backend = pragha_application_get_backend (clastfm->pragha);

	data = g_slice_new (PraghaLastfmAsyncData);
	data->clastfm = clastfm;
	data->mobj = pragha_musicobject_dup (pragha_backend_get_musicobject (backend));

	return data;
}

static void
pragha_lastfm_async_data_free (PraghaLastfmAsyncData *data)
{
	g_object_unref (data->mobj);
	g_slice_free (PraghaLastfmAsyncData, data);
}


/* Save a get the lastfm password.
 * TODO: Implement any basic crypto.
 */

void
pragha_lastfm_set_password (PraghaPreferences *preferences, const gchar *pass)
{
	if (string_is_not_empty(pass))
		pragha_preferences_set_string (preferences,
		                               GROUP_SERVICES,
		                               KEY_LASTFM_PASS,
		                               pass);
	else
 		pragha_preferences_remove_key (preferences,
		                               GROUP_SERVICES,
		                               KEY_LASTFM_PASS);
}

const gchar *
pragha_lastfm_get_password (PraghaPreferences *preferences)
{
	return pragha_preferences_get_string (preferences,
	                                      GROUP_SERVICES,
	                                      KEY_LASTFM_PASS);
}

/* Upadate lastfm menubar acording lastfm state */

static void
pragha_action_group_set_sensitive (GtkActionGroup *group, const gchar *name, gboolean sensitive)
{
	GtkAction *action;
	action = gtk_action_group_get_action (group, name);
	gtk_action_set_sensitive (action, sensitive);
}

void
pragha_lastfm_update_menu_actions (PraghaLastfm *clastfm)
{
	PraghaBackend *backend;
	PraghaBackendState state = 0;

	PraghaApplication *pragha = clastfm->pragha;
	
	backend = pragha_application_get_backend (pragha);
	state = pragha_backend_get_state (backend);

	gboolean playing    = (state != ST_STOPPED);
	gboolean logged     = (clastfm->status == LASTFM_STATUS_OK);
	gboolean lfm_inited = (clastfm->session_id != NULL);
	gboolean has_user   = (lfm_inited && clastfm->has_user);

	pragha_action_group_set_sensitive (clastfm->action_group_main_menu, "Love track", playing && logged);
	pragha_action_group_set_sensitive (clastfm->action_group_main_menu, "Unlove track", playing && logged);
	pragha_action_group_set_sensitive (clastfm->action_group_main_menu, "Add favorites", has_user);
	pragha_action_group_set_sensitive (clastfm->action_group_main_menu, "Add similar", playing && lfm_inited);

	pragha_action_group_set_sensitive (clastfm->action_group_playlist, "Love track", logged);
	pragha_action_group_set_sensitive (clastfm->action_group_playlist, "Unlove track", logged);
	pragha_action_group_set_sensitive (clastfm->action_group_playlist, "Add similar", lfm_inited);
}

/*
 * Advise not connect with lastfm.
 */
static void pragha_lastfm_no_connection_advice (void)
{
	PraghaStatusbar *statusbar = pragha_statusbar_get ();

	pragha_statusbar_set_misc_text (statusbar, _("No connection Last.fm has been established."));
	g_object_unref (statusbar);
}

/* Find a song with the artist and title independently of the album and adds it to the playlist */

static GList *
prepend_song_with_artist_and_title_to_mobj_list (PraghaLastfm *clastfm,
                                                 const gchar *artist,
                                                 const gchar *title,
                                                 GList *list)
{
	PraghaPlaylist *playlist;
	PraghaDatabase *cdbase;
	PraghaMusicobject *mobj = NULL;
	gint location_id = 0;

	PraghaApplication *pragha = clastfm->pragha;

	playlist = pragha_application_get_playlist (pragha);

	if(pragha_mobj_list_already_has_title_of_artist (list, title, artist) ||
	   pragha_playlist_already_has_title_of_artist (playlist, title, artist))
		return list;

	cdbase = pragha_application_get_database (pragha);

	const gchar *sql = "SELECT TRACK.title, ARTIST.name, LOCATION.id "
				"FROM TRACK, ARTIST, LOCATION "
				"WHERE ARTIST.id = TRACK.artist AND LOCATION.id = TRACK.location "
				"AND TRACK.title = ? COLLATE NOCASE "
				"AND ARTIST.name = ? COLLATE NOCASE "
				"ORDER BY RANDOM() LIMIT 1;";

	PraghaPreparedStatement *statement = pragha_database_create_statement (cdbase, sql);
	pragha_prepared_statement_bind_string (statement, 1, title);
	pragha_prepared_statement_bind_string (statement, 2, artist);

	if (pragha_prepared_statement_step (statement)) {
		location_id = pragha_prepared_statement_get_int (statement, 2);
		mobj = new_musicobject_from_db (cdbase, location_id);
		list = g_list_prepend (list, mobj);
	}

	pragha_prepared_statement_free (statement);

	return list;
}

/* Set correction basedm on lastfm now playing segestion.. */

static void
pragha_corrected_by_lastfm_dialog_response (GtkWidget    *dialog,
                                            gint          response_id,
                                            PraghaLastfm *clastfm)
{
	PraghaBackend *backend;
	PraghaPlaylist *playlist;
	PraghaToolbar *toolbar;
	PraghaMpris2 *mpris2;
	PraghaMusicobject *nmobj, *current_mobj;
	PraghaTagger *tagger;
	gint changed = 0;

	PraghaApplication *pragha = clastfm->pragha;

	if (response_id == GTK_RESPONSE_HELP) {
		nmobj = pragha_tags_dialog_get_musicobject(PRAGHA_TAGS_DIALOG(dialog));
		pragha_track_properties_dialog(nmobj, pragha_application_get_window(pragha));
		return;
	}

	if (response_id == GTK_RESPONSE_OK) {
		changed = pragha_tags_dialog_get_changed(PRAGHA_TAGS_DIALOG(dialog));
		if(changed) {
			backend = pragha_application_get_backend (pragha);

			nmobj = pragha_tags_dialog_get_musicobject(PRAGHA_TAGS_DIALOG(dialog));

			if(pragha_backend_get_state (backend) != ST_STOPPED) {
				current_mobj = pragha_backend_get_musicobject (backend);
				if (pragha_musicobject_compare(nmobj, current_mobj) == 0) {
					toolbar = pragha_application_get_toolbar (pragha);

					/* Update public current song */
					pragha_update_musicobject_change_tag(current_mobj, changed, nmobj);

					/* Update current song on playlist */
					playlist = pragha_application_get_playlist (pragha);
					pragha_playlist_update_current_track (playlist, changed, nmobj);

					pragha_toolbar_set_title(toolbar, current_mobj);

					mpris2 = pragha_application_get_mpris2 (pragha);
					pragha_mpris_update_metadata_changed (mpris2);
				}
			}

			if(G_LIKELY(pragha_musicobject_is_local_file (nmobj))) {
				tagger = pragha_tagger_new();
				pragha_tagger_add_file (tagger, pragha_musicobject_get_file(nmobj));
				pragha_tagger_set_changes(tagger, nmobj, changed);
				pragha_tagger_apply_changes (tagger);
				g_object_unref(tagger);
			}
		}
	}

	gtk_widget_hide (clastfm->ntag_lastfm_button);
	gtk_widget_destroy (dialog);
}

static void
edit_tags_corrected_by_lastfm (GtkButton *button, PraghaLastfm *clastfm)
{
	PraghaBackend *backend;
	PraghaMusicobject *tmobj, *nmobj;
	gchar *otitle = NULL, *oartist = NULL, *oalbum = NULL;
	gchar *ntitle = NULL, *nartist = NULL, *nalbum = NULL;
	gint prechanged = 0;
	GtkWidget *dialog;

	PraghaApplication *pragha = clastfm->pragha;

	backend = pragha_application_get_backend (pragha);

	if(pragha_backend_get_state (backend) == ST_STOPPED)
		return;

	/* Get all info of current track */

	tmobj = pragha_musicobject_dup (pragha_backend_get_musicobject (backend));

	g_object_get(tmobj,
	             "title", &otitle,
	             "artist", &oartist,
	             "album", &oalbum,
	             NULL);

	/* Get all info of suggestions
	 * Temp Musicobject to not block tag edit dialog */
	pragha_mutex_lock (clastfm->nmobj_mutex);
	nmobj = pragha_musicobject_dup(clastfm->nmobj);
	pragha_mutex_unlock (clastfm->nmobj_mutex);

	g_object_get(nmobj,
	             "title", &ntitle,
	             "artist", &nartist,
	             "album", &nalbum,
	             NULL);

	/* Compare original mobj and suggested from lastfm */
	if(g_ascii_strcasecmp(otitle, ntitle)) {
		pragha_musicobject_set_title(tmobj, ntitle);
		prechanged |= TAG_TITLE_CHANGED;
	}
	if(g_ascii_strcasecmp(oartist, nartist)) {
		pragha_musicobject_set_artist(tmobj, nartist);
		prechanged |= TAG_ARTIST_CHANGED;
	}
	if(g_ascii_strcasecmp(oalbum, nalbum)) {
		pragha_musicobject_set_album(tmobj, nalbum);
		prechanged |= TAG_ALBUM_CHANGED;
	}

	dialog = pragha_tags_dialog_new();

	g_signal_connect (G_OBJECT (dialog), "response",
	                  G_CALLBACK (pragha_corrected_by_lastfm_dialog_response), clastfm);

	pragha_tags_dialog_set_musicobject(PRAGHA_TAGS_DIALOG(dialog), tmobj);
	pragha_tags_dialog_set_changed(PRAGHA_TAGS_DIALOG(dialog), prechanged);

	gtk_widget_show (dialog);
}

static GtkWidget*
pragha_lastfm_tag_suggestion_button_new (PraghaLastfm *clastfm)
{
	GtkWidget* ntag_lastfm_button;

	ntag_lastfm_button = gtk_button_new();
	gtk_button_set_relief(GTK_BUTTON(ntag_lastfm_button), GTK_RELIEF_NONE);
	gtk_button_set_image(GTK_BUTTON(ntag_lastfm_button),
                         gtk_image_new_from_stock(GTK_STOCK_SPELL_CHECK, GTK_ICON_SIZE_MENU));
	gtk_widget_set_tooltip_text(GTK_WIDGET(ntag_lastfm_button),
	                            _("Last.fm suggested a tag correction"));

	g_signal_connect(G_OBJECT(ntag_lastfm_button), "clicked",
	                 G_CALLBACK(edit_tags_corrected_by_lastfm), clastfm);

	return ntag_lastfm_button;
}

/* Love and unlove music object */

gpointer
do_lastfm_love_mobj (PraghaLastfm *clastfm, const gchar *title, const gchar *artist)
{
	gint rv;

	CDEBUG(DBG_LASTFM, "Love mobj on thread");

	rv = LASTFM_track_love (clastfm->session_id,
	                        title,
	                        artist);

	if (rv != LASTFM_STATUS_OK)
		return _("Love song on Last.fm failed.");
	else
		return NULL;
}

gpointer
do_lastfm_unlove_mobj (PraghaLastfm *clastfm, const gchar *title, const gchar *artist)
{
	gint rv;

	CDEBUG(DBG_LASTFM, "Unlove mobj on thread");

	rv = LASTFM_track_unlove (clastfm->session_id,
	                          title,
	                          artist);

	if (rv != LASTFM_STATUS_OK)
		return _("Unlove song on Last.fm failed.");
	else
		return NULL;
}


/* Functions related to current playlist. */

gpointer
do_lastfm_current_playlist_love (gpointer data)
{
	PraghaPlaylist *playlist;
	PraghaMusicobject *mobj = NULL;
	const gchar *title, *artist;

	PraghaLastfm *clastfm = data;
	PraghaApplication *pragha = clastfm->pragha;

	playlist = pragha_application_get_playlist (pragha);
	mobj = pragha_playlist_get_selected_musicobject (playlist);

	title = pragha_musicobject_get_title(mobj);
	artist = pragha_musicobject_get_artist(mobj);

	return do_lastfm_love_mobj (clastfm, title, artist);
}

static void
lastfm_track_current_playlist_love_action (GtkAction *action, PraghaLastfm *clastfm)
{
	CDEBUG(DBG_LASTFM, "Love handler to current playlist");

	if (clastfm->status != LASTFM_STATUS_OK) {
		pragha_lastfm_no_connection_advice ();
		return;
	}

	pragha_async_launch (do_lastfm_current_playlist_love,
	                     pragha_async_set_idle_message,
	                     clastfm);
}

gpointer
do_lastfm_current_playlist_unlove (gpointer data)
{
	PraghaPlaylist *playlist;
	PraghaMusicobject *mobj = NULL;
	const gchar *title, *artist;

	PraghaLastfm *clastfm = data;
	PraghaApplication *pragha = clastfm->pragha;

	playlist = pragha_application_get_playlist (pragha);
	mobj = pragha_playlist_get_selected_musicobject (playlist);

	title = pragha_musicobject_get_title(mobj);
	artist = pragha_musicobject_get_artist(mobj);

	return do_lastfm_unlove_mobj (clastfm, title, artist);
}

static void
lastfm_track_current_playlist_unlove_action (GtkAction *action, PraghaLastfm *clastfm)
{
	CDEBUG(DBG_LASTFM, "Unlove Handler to current playlist");

	if (clastfm->status != LASTFM_STATUS_OK) {
		pragha_lastfm_no_connection_advice ();
		return;
	}

	pragha_async_launch (do_lastfm_current_playlist_unlove,
	                     pragha_async_set_idle_message,
	                     clastfm);
}

static gboolean
append_mobj_list_current_playlist_idle(gpointer user_data)
{
	PraghaPlaylist *playlist;
	PraghaStatusbar *statusbar;
	gchar *summary = NULL;
	guint songs_added = 0;

	AddMusicObjectListData *data = user_data;

	GList *list = data->list;
	PraghaLastfm *clastfm = data->clastfm;
	PraghaApplication *pragha = clastfm->pragha;

	if(list != NULL) {
		playlist = pragha_application_get_playlist (pragha);
		pragha_playlist_append_mobj_list (playlist, list);

		songs_added = g_list_length(list);
		g_list_free(list);
	}
	else {
		remove_watch_cursor (pragha_application_get_window(pragha));
	}

	switch(data->query_type) {
		case LASTFM_GET_SIMILAR:
			if(data->query_count > 0)
				summary = g_strdup_printf(_("Added %d songs of %d sugested from Last.fm."),
							  songs_added, data->query_count);
			else
				summary = g_strdup_printf(_("Last.fm not suggest any similar song."));
			break;
		case LASTFM_GET_LOVED:
			if(data->query_count > 0)
				summary = g_strdup_printf(_("Added %d songs of the last %d loved on Last.fm."),
							  songs_added, data->query_count);
			else
				summary = g_strdup_printf(_("You had no favorite songs on Last.fm."));
			break;
		case LASTFM_NONE:
		default:
			break;
	}

	if (summary != NULL) {
		statusbar = pragha_statusbar_get ();
		pragha_statusbar_set_misc_text (statusbar, summary);
		g_object_unref (statusbar);
		g_free(summary);
	}

	g_slice_free (AddMusicObjectListData, data);

	return FALSE;
}

gpointer
do_lastfm_get_similar (PraghaLastfm *clastfm, const gchar *title, const gchar *artist)
{
	LFMList *results = NULL, *li;
	LASTFM_TRACK_INFO *track = NULL;
	guint query_count = 0;
	GList *list = NULL;
	gint rv;

	AddMusicObjectListData *data;

	if(string_is_not_empty(title) && string_is_not_empty(artist)) {
		rv = LASTFM_track_get_similar (clastfm->session_id,
		                               title,
		                               artist,
		                               50, &results);

		for (li=results; li && rv == LASTFM_STATUS_OK; li=li->next) {
			track = li->data;
			list = prepend_song_with_artist_and_title_to_mobj_list (clastfm, track->artist, track->name, list);
			query_count += 1;
		}
	}

	data = g_slice_new (AddMusicObjectListData);
	data->list = list;
	data->query_type = LASTFM_GET_SIMILAR;
	data->query_count = query_count;
	data->clastfm = clastfm;

	LASTFM_free_track_info_list (results);

	return data;
}

gpointer
do_lastfm_get_similar_current_playlist_action (gpointer user_data)
{
	PraghaPlaylist *playlist;
	PraghaMusicobject *mobj = NULL;
	const gchar *title, *artist;

	AddMusicObjectListData *data;

	PraghaLastfm *clastfm = user_data;
	PraghaApplication *pragha = clastfm->pragha;

	playlist = pragha_application_get_playlist (pragha);
	mobj = pragha_playlist_get_selected_musicobject (playlist);

	title = pragha_musicobject_get_title(mobj);
	artist = pragha_musicobject_get_artist(mobj);

	data = do_lastfm_get_similar (clastfm, title, artist);

	return data;
}

static void
lastfm_get_similar_current_playlist_action (GtkAction *action, PraghaLastfm *clastfm)
{
	PraghaApplication *pragha = clastfm->pragha;

	CDEBUG(DBG_LASTFM, "Get similar action to current playlist");

	if (clastfm->session_id == NULL) {
		pragha_lastfm_no_connection_advice ();
		return;
	}

	set_watch_cursor (pragha_application_get_window(pragha));
	pragha_async_launch (do_lastfm_get_similar_current_playlist_action,
	                     append_mobj_list_current_playlist_idle,
	                     clastfm);
}

/* Functions that respond to menu options. */

static void
lastfm_import_xspf_response (GtkDialog *dialog,
                             gint response,
                             PraghaLastfm *clastfm)
{
	PraghaPlaylist *playlist;
	PraghaStatusbar *statusbar;
	XMLNode *xml = NULL, *xi, *xc, *xt;
	gchar *contents, *summary;
	gint try = 0, added = 0;
	GList *list = NULL;

	GFile *file;
	gsize size;

	PraghaApplication *pragha = clastfm->pragha;

	if(response != GTK_RESPONSE_ACCEPT)
		goto cancel;

	file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));

	if (!g_file_load_contents (file, NULL, &contents, &size, NULL, NULL)) {
		goto out;
    	}

	if (g_utf8_validate (contents, -1, NULL) == FALSE) {
		gchar *fixed;
		fixed = g_convert (contents, -1, "UTF-8", "ISO8859-1", NULL, NULL, NULL);
		if (fixed != NULL) {
			g_free (contents);
			contents = fixed;
		}
	}

	xml = tinycxml_parse(contents);

	xi = xmlnode_get(xml,CCA { "playlist","trackList","track",NULL},NULL,NULL);
	for(;xi;xi= xi->next) {
		try++;
		xt = xmlnode_get(xi,CCA {"track","title",NULL},NULL,NULL);
		xc = xmlnode_get(xi,CCA {"track","creator",NULL},NULL,NULL);

		if (xt && xc)
			list = prepend_song_with_artist_and_title_to_mobj_list (clastfm, xc->content, xt->content, list);
	}

	if(list) {
		playlist = pragha_application_get_playlist (pragha);
		pragha_playlist_append_mobj_list (playlist, list);
		g_list_free (list);
	}

	summary = g_strdup_printf(_("Added %d songs from %d of the imported playlist."), added, try);

	statusbar = pragha_statusbar_get ();
	pragha_statusbar_set_misc_text (statusbar, summary);
	g_object_unref (statusbar);

	g_free(summary);

	xmlnode_free(xml);
	g_free (contents);
out:
	g_object_unref (file);
cancel:
	gtk_widget_destroy (GTK_WIDGET(dialog));
}

static void
lastfm_import_xspf_action (GtkAction *action, PraghaLastfm *clastfm)
{
	GtkWidget *dialog;
	GtkFileFilter *media_filter;

	PraghaApplication *pragha = clastfm->pragha;

	dialog = gtk_file_chooser_dialog_new (_("Import a XSPF playlist"),
				      GTK_WINDOW(pragha_application_get_window(pragha)),
				      GTK_FILE_CHOOSER_ACTION_OPEN,
				      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				      GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
				      NULL);

	media_filter = gtk_file_filter_new();
	gtk_file_filter_set_name(GTK_FILE_FILTER(media_filter), _("Supported media"));
	gtk_file_filter_add_mime_type(GTK_FILE_FILTER(media_filter), "application/xspf+xml");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), GTK_FILE_FILTER(media_filter));

	g_signal_connect (G_OBJECT(dialog), "response",
	                  G_CALLBACK(lastfm_import_xspf_response), clastfm);

	gtk_widget_show_all (dialog);
}

gpointer
do_lastfm_add_favorites_action (gpointer user_data)
{
	PraghaPreferences *preferences;
	LFMList *results = NULL, *li;
	LASTFM_TRACK_INFO *track;
	gint rpages = 0, cpage = 0;
	AddMusicObjectListData *data;
	guint query_count = 0;
	GList *list = NULL;

	PraghaLastfm *clastfm = user_data;
	PraghaApplication *pragha = clastfm->pragha;

	do {
		preferences = pragha_application_get_preferences (pragha);
		rpages = LASTFM_user_get_loved_tracks(clastfm->session_id,
		                                      pragha_preferences_get_lastfm_user (preferences),
		                                      cpage,
		                                      &results);

		for(li=results; li; li=li->next) {
			track = li->data;
			list = prepend_song_with_artist_and_title_to_mobj_list (clastfm, track->artist, track->name, list);
			query_count += 1;
		}
		LASTFM_free_track_info_list (results);
		cpage++;
	} while(rpages != 0);

	data = g_slice_new (AddMusicObjectListData);
	data->list = list;
	data->query_type = LASTFM_GET_LOVED;
	data->query_count = query_count;
	data->clastfm = clastfm;

	return data;
}

static void
lastfm_add_favorites_action (GtkAction *action, PraghaLastfm *clastfm)
{
	PraghaApplication *pragha = clastfm->pragha;

	CDEBUG(DBG_LASTFM, "Add Favorites action");

	if ((clastfm->session_id == NULL) || !clastfm->has_user) {
		pragha_lastfm_no_connection_advice ();
		return;
	}

	set_watch_cursor (pragha_application_get_window(pragha));
	pragha_async_launch (do_lastfm_add_favorites_action,
	                     append_mobj_list_current_playlist_idle,
	                     clastfm);
}

static gpointer
do_lastfm_get_similar_action (gpointer user_data)
{
	AddMusicObjectListData *data;

	PraghaLastfmAsyncData *lastfm_async_data = user_data;

	PraghaLastfm *clastfm   = lastfm_async_data->clastfm;
	PraghaMusicobject *mobj = lastfm_async_data->mobj;

	const gchar *title = pragha_musicobject_get_title (mobj);
	const gchar *artist = pragha_musicobject_get_artist (mobj);

	data = do_lastfm_get_similar (clastfm, title, artist);

	pragha_lastfm_async_data_free(lastfm_async_data);

	return data;
}

static void
lastfm_get_similar_action (GtkAction *action, PraghaLastfm *clastfm)
{
	PraghaBackend *backend;

	PraghaApplication *pragha = clastfm->pragha;

	CDEBUG(DBG_LASTFM, "Get similar action");

	backend = pragha_application_get_backend (pragha);
	if(pragha_backend_get_state (backend) == ST_STOPPED)
		return;

	if (clastfm->session_id == NULL) {
		pragha_lastfm_no_connection_advice ();
		return;
	}

	set_watch_cursor (pragha_application_get_window(pragha));
	pragha_async_launch (do_lastfm_get_similar_action,
	                     append_mobj_list_current_playlist_idle,
	                     pragha_lastfm_async_data_new (clastfm));
}

static gpointer
do_lastfm_current_song_love (gpointer data)
{
	gpointer msg_data = NULL;

	PraghaLastfmAsyncData *lastfm_async_data = data;

	PraghaLastfm *clastfm   = lastfm_async_data->clastfm;
	PraghaMusicobject *mobj = lastfm_async_data->mobj;

	const gchar *title = pragha_musicobject_get_title (mobj);
	const gchar *artist = pragha_musicobject_get_artist (mobj);

	msg_data = do_lastfm_love_mobj (clastfm, title, artist);

	pragha_lastfm_async_data_free(lastfm_async_data);

	return msg_data;
}

static void
lastfm_track_love_action (GtkAction *action, PraghaLastfm *clastfm)
{
	PraghaBackend *backend;

	PraghaApplication *pragha = clastfm->pragha;

	CDEBUG(DBG_LASTFM, "Love Handler");

	backend = pragha_application_get_backend (pragha);
	if(pragha_backend_get_state (backend) == ST_STOPPED)
		return;

	if (clastfm->status != LASTFM_STATUS_OK) {
		pragha_lastfm_no_connection_advice ();
		return;
	}

	pragha_async_launch (do_lastfm_current_song_love,
	                     pragha_async_set_idle_message,
	                     pragha_lastfm_async_data_new (clastfm));
}

static gpointer
do_lastfm_current_song_unlove (gpointer data)
{
	gpointer msg_data = NULL;

	PraghaLastfmAsyncData *lastfm_async_data = data;

	PraghaLastfm *clastfm   = lastfm_async_data->clastfm;
	PraghaMusicobject *mobj = lastfm_async_data->mobj;

	const gchar *title = pragha_musicobject_get_title (mobj);
	const gchar *artist = pragha_musicobject_get_artist (mobj);

	msg_data = do_lastfm_unlove_mobj (clastfm, title, artist);

	pragha_lastfm_async_data_free(lastfm_async_data);

	return msg_data;
}

static void
lastfm_track_unlove_action (GtkAction *action, PraghaLastfm *clastfm)
{
	PraghaBackend *backend;

	PraghaApplication *pragha = clastfm->pragha;

	CDEBUG(DBG_LASTFM, "Unlove Handler");

	backend = pragha_application_get_backend (pragha);
	if(pragha_backend_get_state (backend) == ST_STOPPED)
		return;

	if (clastfm->status != LASTFM_STATUS_OK) {
		pragha_lastfm_no_connection_advice ();
		return;
	}

	pragha_async_launch (do_lastfm_current_song_unlove,
	                     pragha_async_set_idle_message,
	                     pragha_lastfm_async_data_new (clastfm));
}

static gpointer
do_lastfm_scrob (gpointer data)
{
	gint rv;
	gchar *title = NULL, *artist = NULL, *album = NULL;
	gint track_no, length;

	PraghaLastfmAsyncData *lastfm_async_data = data;

	PraghaLastfm *clastfm   = lastfm_async_data->clastfm;
	PraghaMusicobject *mobj = lastfm_async_data->mobj;

	CDEBUG(DBG_LASTFM, "Scrobbler thread");

	g_object_get(mobj,
	             "title", &title,
	             "artist", &artist,
	             "album", &album,
	             "track-no", &track_no,
	             "length", &length,
	             NULL);

	rv = LASTFM_track_scrobble (clastfm->session_id,
	                            title,
	                            album,
	                            artist,
	                            clastfm->playback_started,
	                            length,
	                            track_no,
	                            0, NULL);

	g_free(title);
	g_free(artist);
	g_free(album);
	pragha_lastfm_async_data_free(lastfm_async_data);

	if (rv != LASTFM_STATUS_OK)
		return _("Last.fm submission failed");
	else
		return _("Track scrobbled on Last.fm");
}

gboolean
lastfm_scrob_handler(gpointer data)
{
	PraghaBackend *backend;
	PraghaLastfm *clastfm = data;

	CDEBUG(DBG_LASTFM, "Scrobbler Handler");

	backend = pragha_application_get_backend (clastfm->pragha);
	if(pragha_backend_get_state (backend) == ST_STOPPED)
		return FALSE;

	if (clastfm->status != LASTFM_STATUS_OK) {
		pragha_lastfm_no_connection_advice ();
		return FALSE;
	}

	pragha_async_launch (do_lastfm_scrob,
	                     pragha_async_set_idle_message,
	                     pragha_lastfm_async_data_new (clastfm));

	return FALSE;
}

static gboolean
show_lastfm_sugest_corrrection_button (gpointer user_data)
{
	PraghaToolbar *toolbar;
	PraghaBackend *backend;
	gchar *cfile = NULL, *nfile = NULL;

	PraghaLastfm *clastfm = user_data;
	PraghaApplication *pragha = clastfm->pragha;

	/* Hack to safe!.*/
	if(!clastfm->ntag_lastfm_button) {
		toolbar = pragha_application_get_toolbar (pragha);

		clastfm->ntag_lastfm_button =
			pragha_lastfm_tag_suggestion_button_new (clastfm);

		pragha_toolbar_add_extention_widget (toolbar, clastfm->ntag_lastfm_button);
	}

	backend = pragha_application_get_backend (pragha);
	g_object_get(pragha_backend_get_musicobject (backend),
	             "file", &cfile,
	             NULL);

	pragha_mutex_lock (clastfm->nmobj_mutex);
	g_object_get (clastfm->nmobj,
	              "file", &nfile,
	              NULL);
	pragha_mutex_unlock (clastfm->nmobj_mutex);

	if(g_ascii_strcasecmp(cfile, nfile) == 0)
		gtk_widget_show (clastfm->ntag_lastfm_button);

	g_free(cfile);
	g_free(nfile);

	return FALSE;
}

static gpointer
do_lastfm_now_playing (gpointer data)
{
	PraghaMusicobject *tmobj;
	gchar *title = NULL, *artist = NULL, *album = NULL;
	gint track_no, length;
	LFMList *list = NULL;
	LASTFM_TRACK_INFO *ntrack = NULL;
	gint changed = 0, rv;

	PraghaLastfmAsyncData *lastfm_async_data = data;

	PraghaLastfm *clastfm   = lastfm_async_data->clastfm;
	PraghaMusicobject *mobj = lastfm_async_data->mobj;

	CDEBUG(DBG_LASTFM, "Update now playing thread");

	g_object_get(mobj,
	             "title", &title,
	             "artist", &artist,
	             "album", &album,
	             "track-no", &track_no,
	             "length", &length,
	             NULL);

	rv = LASTFM_track_update_now_playing (clastfm->session_id,
	                                      title,
	                                      album,
	                                      artist,
	                                      length,
	                                      track_no,
	                                      0,
	                                      &list);

	if (rv == LASTFM_STATUS_OK) {
		/* Fist check lastfm response, and compare tags. */
		if(list != NULL) {
			ntrack = list->data;
			if(ntrack->name && g_ascii_strcasecmp(title, ntrack->name))
				changed |= TAG_TITLE_CHANGED;
			if(ntrack->artist && g_ascii_strcasecmp(artist, ntrack->artist))
				changed |= TAG_ARTIST_CHANGED;
			if(ntrack->album && g_ascii_strcasecmp(album, ntrack->album))
				changed |= TAG_ALBUM_CHANGED;
		}

		if (changed) {
			tmobj = g_object_ref(mobj);

			if(changed & TAG_TITLE_CHANGED)
				pragha_musicobject_set_title(tmobj, ntrack->name);
			if(changed & TAG_ARTIST_CHANGED)
				pragha_musicobject_set_artist(tmobj, ntrack->artist);
			if(changed & TAG_ALBUM_CHANGED)
				pragha_musicobject_set_album(tmobj, ntrack->album);

			pragha_mutex_lock (clastfm->nmobj_mutex);
			g_object_unref (clastfm->nmobj);
			clastfm->nmobj = tmobj;
			pragha_mutex_unlock (clastfm->nmobj_mutex);

			g_idle_add (show_lastfm_sugest_corrrection_button, clastfm);
		}
	}

	LASTFM_free_track_info_list(list);

	g_free(title);
	g_free(artist);
	g_free(album);
	pragha_lastfm_async_data_free(lastfm_async_data);

	if (rv != LASTFM_STATUS_OK)
		return _("Update current song on Last.fm failed.");
	else
		return NULL;
}

void
lastfm_now_playing_handler (PraghaLastfm *clastfm)
{
	PraghaBackend *backend;
	gchar *title = NULL, *artist = NULL;
	gint length;

	CDEBUG(DBG_LASTFM, "Update now playing Handler");

	backend = pragha_application_get_backend (clastfm->pragha);

	if(pragha_backend_get_state (backend) == ST_STOPPED)
		return;

	if (!clastfm->has_user || !clastfm->has_pass)
		return;

	if (clastfm->status != LASTFM_STATUS_OK) {
		pragha_lastfm_no_connection_advice ();
		return;
	}

	g_object_get(pragha_backend_get_musicobject (backend),
	             "title", &title,
	             "artist", &artist,
	             "length", &length,
	             NULL);

	if (string_is_empty(title) || string_is_empty(artist) || (length < 30))
		goto exit;

	pragha_async_launch (do_lastfm_now_playing,
	                     pragha_async_set_idle_message,
	                     pragha_lastfm_async_data_new (clastfm));

	/* Kick the lastfm scrobbler on
	 * Note: Only scrob if tracks is more than 30s.
	 * and scrob when track is at 50% or 4mins, whichever comes
	 * first */

	if((length / 2) > (240 - WAIT_UPDATE)) {
		length = 240 - WAIT_UPDATE;
	}
	else {
		length = (length / 2) - WAIT_UPDATE;
	}

	clastfm->timeout_id = g_timeout_add_seconds_full (G_PRIORITY_DEFAULT_IDLE, length,
	                                                  lastfm_scrob_handler, clastfm, NULL);
exit:
	g_free(title);
	g_free(artist);

	return;
}

static gboolean
update_related_handler (gpointer data)
{
	PraghaPreferences *preferences;

	PraghaLastfm *clastfm = data;

	CDEBUG(DBG_INFO, "Updating Lastm depending preferences");

	preferences = pragha_application_get_preferences (clastfm->pragha);
	if (pragha_preferences_get_lastfm_support (preferences))
		lastfm_now_playing_handler (clastfm);

	return FALSE;
}

static void
backend_changed_state_cb (PraghaBackend *backend, GParamSpec *pspec, gpointer user_data)
{
	PraghaMusicType file_type = FILE_NONE;
	PraghaBackendState state = 0;

	PraghaLastfm *clastfm = user_data;

	state = pragha_backend_get_state (backend);

	CDEBUG(DBG_INFO, "Configuring thread to update Lastfm");

	/* Update gui. */

	pragha_lastfm_update_menu_actions (clastfm);

	/* Update thread. */

	if (clastfm->timeout_id) {
		g_source_remove (clastfm->timeout_id);
		clastfm->timeout_id = 0;
	}

	if (state != ST_PLAYING) {
		if (clastfm->ntag_lastfm_button)
			gtk_widget_hide (clastfm->ntag_lastfm_button);
		return;
	}

	file_type = pragha_musicobject_get_file_type (pragha_backend_get_musicobject (backend));

	if (file_type == FILE_NONE || file_type == FILE_HTTP)
		return;

	if (clastfm->status == LASTFM_STATUS_OK)
		time(&clastfm->playback_started);

	clastfm->timeout_id = g_timeout_add_seconds_full (G_PRIORITY_DEFAULT_IDLE, WAIT_UPDATE,
	                                                  update_related_handler, clastfm, NULL);
}

static void
pragha_menubar_append_lastfm (PraghaLastfm *clastfm)
{
	PraghaPlaylist *playlist;
	PraghaApplication *pragha = clastfm->pragha;

	clastfm->action_group_main_menu = gtk_action_group_new ("PraghaLastfmMainMenuActions");
	gtk_action_group_set_translation_domain (clastfm->action_group_main_menu, GETTEXT_PACKAGE);
	gtk_action_group_add_actions (clastfm->action_group_main_menu,
	                              main_menu_actions,
	                              G_N_ELEMENTS (main_menu_actions),
	                              clastfm);

	clastfm->merge_id_main_menu = pragha_menubar_append_plugin_action (pragha,
	                                                                   clastfm->action_group_main_menu,
	                                                                   main_menu_xml);

	pragha_action_group_set_sensitive (clastfm->action_group_main_menu, "Love track", FALSE);
	pragha_action_group_set_sensitive (clastfm->action_group_main_menu, "Unlove track", FALSE);
	pragha_action_group_set_sensitive (clastfm->action_group_main_menu, "Add favorites", FALSE);
	pragha_action_group_set_sensitive (clastfm->action_group_main_menu, "Add similar", FALSE);

	clastfm->action_group_playlist = gtk_action_group_new ("PraghaLastfmPlaylistActions");
	gtk_action_group_set_translation_domain (clastfm->action_group_playlist, GETTEXT_PACKAGE);
	gtk_action_group_add_actions (clastfm->action_group_playlist,
	                              playlist_actions,
	                              G_N_ELEMENTS (playlist_actions),
	                              clastfm);

	playlist = pragha_application_get_playlist (pragha);
	clastfm->merge_id_playlist = pragha_playlist_append_plugin_action (playlist,
	                                                                   clastfm->action_group_playlist,
	                                                                   playlist_xml);
}

static void
pragha_menubar_remove_lastfm (PraghaLastfm *clastfm)
{
	PraghaPlaylist *playlist;
	PraghaApplication *pragha = clastfm->pragha;

	if(!clastfm->merge_id_main_menu)
		return;

	pragha_menubar_remove_plugin_action (pragha,
	                                     clastfm->action_group_main_menu,
	                                     clastfm->merge_id_main_menu);

	clastfm->merge_id_main_menu = 0;

	if (!clastfm->merge_id_playlist)
		return;

	playlist = pragha_application_get_playlist (pragha);
	pragha_playlist_remove_plugin_action (playlist,
	                                      clastfm->action_group_playlist,
	                                      clastfm->merge_id_playlist);

	clastfm->merge_id_playlist = 0;
}

static gboolean
pragha_lastfm_connect_idle(gpointer data)
{
	PraghaPreferences *preferences;
	const gchar *user;
	const gchar *pass;

	PraghaLastfm *clastfm = data;

	PraghaApplication *pragha = clastfm->pragha;

	clastfm->has_user = FALSE;
	clastfm->has_pass = FALSE;

	clastfm->session_id = LASTFM_init(LASTFM_API_KEY, LASTFM_SECRET);

	if (clastfm->session_id != NULL) {
		preferences = pragha_application_get_preferences (pragha);
		user = pragha_preferences_get_lastfm_user (preferences);
		pass = pragha_lastfm_get_password (preferences);

		clastfm->has_user = string_is_not_empty(user);
		clastfm->has_pass = string_is_not_empty(pass);

		if(clastfm->has_user && clastfm->has_pass) {
			clastfm->status = LASTFM_login (clastfm->session_id,
			                                user, pass);

			if (clastfm->status == LASTFM_STATUS_OK) {
				g_signal_connect (pragha_application_get_backend (pragha), "notify::state",
				                  G_CALLBACK (backend_changed_state_cb), clastfm);
			}
			else {
				pragha_lastfm_no_connection_advice ();
				CDEBUG(DBG_INFO, "Failure to login on lastfm");
			}
		}
	}
	else {
		pragha_lastfm_no_connection_advice ();
		CDEBUG(DBG_INFO, "Failure to init libclastfm");
	}

	pragha_menubar_append_lastfm (clastfm);
	pragha_lastfm_update_menu_actions (clastfm);

	return FALSE;
}

/* Init lastfm with a simple thread when change preferences and show error messages. */

gint
pragha_lastfm_connect (PraghaLastfm *clastfm)
{
	CDEBUG(DBG_INFO, "Connecting LASTFM");

	g_idle_add (pragha_lastfm_connect_idle, clastfm);

	return 0;
}

void
pragha_lastfm_disconnect (PraghaLastfm *clastfm)
{
	if (clastfm->session_id != NULL) {
		CDEBUG(DBG_INFO, "Disconnecting LASTFM");

		if (clastfm->status == LASTFM_STATUS_OK)
			g_signal_handlers_disconnect_by_func (pragha_application_get_backend (clastfm->pragha),
			                                      backend_changed_state_cb, clastfm->pragha);

		pragha_menubar_remove_lastfm (clastfm);

		LASTFM_dinit(clastfm->session_id);

		clastfm->session_id = NULL;
		clastfm->status = LASTFM_STATUS_INVALID;
		clastfm->has_user = FALSE;
		clastfm->has_pass = FALSE;
	}
}

/* When just launch pragha init lastfm immediately if has internet or otherwise waiting 30 seconds.
 * And no show any error. */

PraghaLastfm *
pragha_lastfm_new (PraghaApplication *pragha)
{
	PraghaLastfm *clastfm;
	PraghaPreferences *preferences;

	clastfm = g_slice_new0(PraghaLastfm);

	/* Init struct flags */

	clastfm->session_id = NULL;
	clastfm->status = LASTFM_STATUS_INVALID;
	clastfm->nmobj = pragha_musicobject_new();
	pragha_mutex_create (clastfm->nmobj_mutex);
	clastfm->ntag_lastfm_button = NULL;

	clastfm->has_user = FALSE;
	clastfm->has_pass = FALSE;

	clastfm->timeout_id = 0;

	clastfm->pragha = pragha;

	/* Test internet and launch threads.*/

	preferences = pragha_application_get_preferences (pragha);
	if (pragha_preferences_get_lastfm_support (preferences)) {
		CDEBUG(DBG_INFO, "Initializing LASTFM");

#if GLIB_CHECK_VERSION(2,32,0)
		if (g_network_monitor_get_network_available (g_network_monitor_get_default ()))
#else
		if (nm_is_online () == TRUE)
#endif
			g_idle_add (pragha_lastfm_connect_idle, clastfm);
		else
			g_timeout_add_seconds_full (G_PRIORITY_DEFAULT_IDLE, 30,
			                            pragha_lastfm_connect_idle, clastfm, NULL);
	}

	return clastfm;
}

void
pragha_lastfm_free (PraghaLastfm *clastfm)
{
	pragha_lastfm_disconnect (clastfm);

	g_object_unref(clastfm->nmobj);
	pragha_mutex_free(clastfm->nmobj_mutex);

	g_slice_free(PraghaLastfm, clastfm);
}
#endif
