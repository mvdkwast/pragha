/*************************************************************************/
/* Copyright (C) 2011-2013 matias <mati86dl@gmail.com>			 */
/* 									 */
/* This program is free software: you can redistribute it and/or modify	 */
/* it under the terms of the GNU General Public License as published by	 */
/* the Free Software Foundation, either version 3 of the License, or	 */
/* (at your option) any later version.					 */
/* 									 */
/* This program is distributed in the hope that it will be useful,	 */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of	 */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the	 */
/* GNU General Public License for more details.				 */
/* 									 */
/* You should have received a copy of the GNU General Public License	 */
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
#include <glib/gstdio.h>

#ifdef HAVE_LIBGLYR
#include <glyr/glyr.h>
#include <glyr/cache.h>
#endif
#include "pragha-simple-async.h"
#include "pragha-musicobject.h"
#include "pragha-utils.h"
#include "pragha-simple-widgets.h"
#include "pragha.h"

#ifdef HAVE_LIBGLYR

struct _PraghaGlyr {
	PraghaApplication *pragha;
	GlyrDatabase *cache_db;
	GtkActionGroup *action_group_main_menu;
	guint merge_id_main_menu;
	GtkActionGroup *action_group_playlist;
	guint merge_id_playlist;
};

typedef struct
{
	PraghaApplication	*pragha;
	GlyrQuery	query;
	GlyrMemCache	*head;
}
glyr_struct;

static void get_lyric_action (GtkAction *action, PraghaGlyr *glyr);
static void get_artist_info_action (GtkAction *action, PraghaGlyr *glyr);

static const GtkActionEntry main_menu_actions [] = {
	{"Search lyric", GTK_STOCK_JUSTIFY_FILL, N_("Search _lyric"),
	 "<Control>Y", "Search lyric", G_CALLBACK(get_lyric_action)},
	{"Search artist info", GTK_STOCK_INFO, N_("Search _artist info"),
	 "", "Search artist info", G_CALLBACK(get_artist_info_action)},
};

static const gchar *main_menu_xml = "<ui>					\
	<menubar name=\"Menubar\">						\
		<menu action=\"ToolsMenu\">					\
			<placeholder name=\"pragha-plugins-placeholder\">		\
				<menuitem action=\"Search lyric\"/>		\
				<menuitem action=\"Search artist info\"/>	\
				<separator/>						\
			</placeholder>						\
		</menu>								\
	</menubar>								\
</ui>";

static void get_lyric_current_playlist_action (GtkAction *action, PraghaGlyr *glyr);
static void get_artist_info_current_playlist_action (GtkAction *action, PraghaGlyr *glyr);

static const GtkActionEntry playlist_actions [] = {
	{"Search lyric", GTK_STOCK_JUSTIFY_FILL, N_("Search _lyric"),
	 "", "Search lyric", G_CALLBACK(get_lyric_current_playlist_action)},
	{"Search artist info", GTK_STOCK_INFO, N_("Search _artist info"),
	 "", "Search artist info", G_CALLBACK(get_artist_info_current_playlist_action)},
};

static const gchar *playlist_xml = "<ui>					\
	<popup name=\"SelectionPopup\">		   				\
	<menu action=\"ToolsMenu\">						\
		<placeholder name=\"pragha-glyr-placeholder\">			\
			<menuitem action=\"Search lyric\"/>			\
			<menuitem action=\"Search artist info\"/>		\
			<separator/>						\
		</placeholder>							\
	</menu>									\
	</popup>				    				\
</ui>";

/* Use the download info on glyr thread and show a dialog. */

static void
pragha_text_info_dialog_response(GtkDialog *dialog,
				gint response,
				PraghaApplication *pragha)
{
	gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void
pragha_show_related_text_info_dialog (glyr_struct *glyr_info, gchar *title_header, gchar *subtitle_header)
{
	GtkWidget *dialog, *header, *view, *scrolled;
	GtkTextBuffer *buffer;

	PraghaApplication *pragha = glyr_info->pragha;

	view = gtk_text_view_new ();
	gtk_text_view_set_editable (GTK_TEXT_VIEW (view), FALSE);
	gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (view), FALSE);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW (view), GTK_WRAP_WORD);
	gtk_text_view_set_accepts_tab (GTK_TEXT_VIEW (view), FALSE);

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));
	gtk_text_buffer_set_text (buffer, glyr_info->head->data, -1);

	scrolled = gtk_scrolled_window_new (NULL, NULL);

	gtk_container_add (GTK_CONTAINER (scrolled), view);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
					GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);

	gtk_container_set_border_width (GTK_CONTAINER (scrolled), 8);

	dialog = gtk_dialog_new_with_buttons(title_header,
					     GTK_WINDOW(pragha_application_get_window(pragha)),
					     GTK_DIALOG_DESTROY_WITH_PARENT,
					     GTK_STOCK_OK,
					     GTK_RESPONSE_OK,
					     NULL);

	gtk_window_set_default_size(GTK_WINDOW (dialog), 450, 350);

	header = sokoke_xfce_header_new (subtitle_header, NULL);

	gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), header, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), scrolled, TRUE, TRUE, 0);

	g_signal_connect(G_OBJECT(dialog), "response",
			G_CALLBACK(pragha_text_info_dialog_response), pragha);

	gtk_widget_show_all(dialog);
}

/* Save the downloaded album art in cache, and updates the gui.*/

static void
pragha_update_downloaded_album_art (glyr_struct *glyr_info)
{
	PraghaToolbar *toolbar;
	PraghaArtCache *art_cache;
	const gchar *artist = NULL, *album = NULL;
	gchar *album_art_path = NULL;
	PraghaBackend *backend;
	PraghaMpris2 *mpris2;

	if(glyr_info->head == NULL)
		return;

	PraghaApplication *pragha = glyr_info->pragha;

	artist = glyr_info->query.artist;
	album = glyr_info->query.album;

	art_cache = pragha_application_get_art_cache (pragha);

	if (glyr_info->head->data)
		pragha_art_cache_put (art_cache, artist, album, glyr_info->head->data, glyr_info->head->size);

	album_art_path = pragha_art_cache_get (art_cache, artist, album);

	if (album_art_path) {
		backend = pragha_application_get_backend (pragha);
		if (pragha_backend_get_state (backend) != ST_STOPPED) {
			PraghaMusicobject *mobj = pragha_backend_get_musicobject (backend);
			const gchar *lartist = pragha_musicobject_get_artist (mobj);
			const gchar *lalbum = pragha_musicobject_get_album (mobj);

			if ((0 == g_strcmp0 (artist, lartist)) &&
			    (0 == g_strcmp0 (album, lalbum))) {
				/* TODO: Emit a signal to update the album art and mpris. */
				toolbar = pragha_application_get_toolbar (pragha);
				pragha_toolbar_set_image_album_art (toolbar, album_art_path);

				mpris2 = pragha_application_get_mpris2 (pragha);
				pragha_mpris_update_metadata_changed (mpris2);
			}
		}
		g_free (album_art_path);
	}
}

/* Manages the results of glyr threads. */

static void
glyr_finished_successfully(glyr_struct *glyr_info)
{
	gchar *title_header = NULL, *subtitle_header = NULL;

	switch (glyr_info->head->type) {
	case GLYR_TYPE_COVERART:
		pragha_update_downloaded_album_art(glyr_info);
		break;
	case GLYR_TYPE_LYRICS:
		title_header =  g_strdup_printf(_("Lyrics thanks to %s"), glyr_info->head->prov);
		subtitle_header = g_markup_printf_escaped (_("%s <small><span weight=\"light\">by</span></small> %s"), glyr_info->query.title, glyr_info->query.artist);
		pragha_show_related_text_info_dialog(glyr_info, title_header, subtitle_header);
		break;
	case GLYR_TYPE_ARTIST_BIO:
		title_header =  g_strdup_printf(_("Artist info"));
		subtitle_header = g_strdup_printf(_("%s <small><span weight=\"light\">thanks to</span></small> %s"), glyr_info->query.artist, glyr_info->head->prov);
		pragha_show_related_text_info_dialog(glyr_info, title_header, subtitle_header);
		break;
	default:
		break;
	}

	g_free(title_header);
	g_free(subtitle_header);

	glyr_free_list(glyr_info->head);
}

static void
glyr_finished_incorrectly(glyr_struct *glyr_info)
{
	PraghaStatusbar *statusbar = pragha_statusbar_get ();

	switch (glyr_info->query.type) {
		case GLYR_GET_LYRICS:
			pragha_statusbar_set_misc_text (statusbar, _("Lyrics not found."));
			break;
		case GLYR_GET_ARTIST_BIO:
			pragha_statusbar_set_misc_text (statusbar, _("Artist information not found."));
			break;
		case GLYR_GET_COVERART:
		default:
			break;
	}
	g_object_unref (statusbar);
}

static gboolean
glyr_finished_thread_update (gpointer data)
{
	glyr_struct *glyr_info = data;

	remove_watch_cursor (pragha_application_get_window(glyr_info->pragha));
	if(glyr_info->head != NULL)
		glyr_finished_successfully(glyr_info);
	else
		glyr_finished_incorrectly(glyr_info);

	glyr_query_destroy(&glyr_info->query);
	g_slice_free(glyr_struct, glyr_info);

	return FALSE;
}

/* Get artist bio or lyric on a thread. */

static gpointer
get_related_info_idle_func (gpointer data)
{
	GlyrMemCache *head;
	GLYR_ERROR error;

	glyr_struct *glyr_info = data;

	head = glyr_get(&glyr_info->query, &error, NULL);

	glyr_info->head = head;

	return glyr_info;
}

/* Configure the thrad to get the artist bio or lyric. */

static void
configure_and_launch_get_text_info_dialog(GLYR_GET_TYPE type, const gchar *artist, const gchar *title, PraghaApplication *pragha)
{
	PraghaGlyr *glyr;
	glyr_struct *glyr_info;
	glyr_info = g_slice_new0 (glyr_struct);

	glyr_query_init(&glyr_info->query);
	glyr_opt_type(&glyr_info->query, type);

	switch (type) {
	case GLYR_GET_ARTIST_BIO:
		glyr_opt_artist(&glyr_info->query, artist);

		glyr_opt_lang (&glyr_info->query, "auto");
		glyr_opt_lang_aware_only (&glyr_info->query, TRUE);
		break;
	case GLYR_GET_LYRICS:
		glyr_opt_artist(&glyr_info->query, artist);
		glyr_opt_title(&glyr_info->query, title);
		break;
	default:
		break;
	}

	glyr = pragha_application_get_glyr (pragha);

	glyr_opt_lookup_db (&glyr_info->query, glyr->cache_db);
	glyr_opt_db_autowrite(&glyr_info->query, TRUE);

	glyr_info->pragha = pragha;

	set_watch_cursor (pragha_application_get_window(pragha));
	pragha_async_launch(get_related_info_idle_func, glyr_finished_thread_update, glyr_info);
}

static void
get_artist_info_action (GtkAction *action, PraghaGlyr *glyr)
{
	PraghaBackend *backend;
	PraghaApplication *pragha = glyr->pragha;

	backend = pragha_application_get_backend (pragha);

	if(pragha_backend_get_state (backend) == ST_STOPPED)
		return;

	CDEBUG(DBG_INFO, "Get Artist info Action");

	PraghaMusicobject *mobj = pragha_backend_get_musicobject (backend);
	const gchar *artist = pragha_musicobject_get_artist (mobj);

	if (string_is_empty(artist))
		return;

	configure_and_launch_get_text_info_dialog(GLYR_GET_ARTISTBIO, artist, NULL, pragha);
}

static void
get_lyric_action (GtkAction *action, PraghaGlyr *glyr)
{
	PraghaBackend *backend;
	PraghaApplication *pragha = glyr->pragha;

	backend = pragha_application_get_backend (pragha);

	if(pragha_backend_get_state (backend) == ST_STOPPED)
		return;

	CDEBUG(DBG_INFO, "Get lyrics Action");

	PraghaMusicobject *mobj = pragha_backend_get_musicobject (backend);
	const gchar *artist = pragha_musicobject_get_artist (mobj);
	const gchar *title = pragha_musicobject_get_title (mobj);

	if (string_is_empty(artist) || string_is_empty(title))
		return;

	configure_and_launch_get_text_info_dialog(GLYR_GET_LYRICS, artist, title, pragha);
}

static void
get_artist_info_current_playlist_action (GtkAction *action, PraghaGlyr *glyr)
{
	PraghaPlaylist *playlist;
	PraghaMusicobject *mobj;

	PraghaApplication *pragha = glyr->pragha;

	playlist = pragha_application_get_playlist (pragha);

	mobj = pragha_playlist_get_selected_musicobject(playlist);

	const gchar *artist = pragha_musicobject_get_artist(mobj);

	CDEBUG(DBG_INFO, "Get Artist info Action of current playlist selection");

	if (string_is_empty(artist))
		return;

	configure_and_launch_get_text_info_dialog(GLYR_GET_ARTISTBIO, artist, NULL, pragha);
}

static void
get_lyric_current_playlist_action (GtkAction *action, PraghaGlyr *glyr)
{
	PraghaPlaylist *playlist;
	PraghaMusicobject *mobj;
	PraghaApplication *pragha = glyr->pragha;

	playlist = pragha_application_get_playlist (pragha);
	mobj = pragha_playlist_get_selected_musicobject (playlist);

	const gchar *artist = pragha_musicobject_get_artist(mobj);
	const gchar *title = pragha_musicobject_get_title(mobj);

	CDEBUG(DBG_INFO, "Get lyrics Action of current playlist selection.");

	if (string_is_empty(artist) || string_is_empty(title))
		return;

	configure_and_launch_get_text_info_dialog(GLYR_GET_LYRICS, artist, title, pragha);
}

static void
related_get_album_art_handler (PraghaApplication *pragha)
{
	PraghaBackend *backend;
	PraghaArtCache *art_cache;
	glyr_struct *glyr_info;
	gchar *album_art_path;

	CDEBUG(DBG_INFO, "Get album art handler");

	backend = pragha_application_get_backend (pragha);

	if (pragha_backend_get_state (backend) == ST_STOPPED)
		return;

	PraghaMusicobject *mobj = pragha_backend_get_musicobject (backend);
	const gchar *artist = pragha_musicobject_get_artist (mobj);
	const gchar *album = pragha_musicobject_get_album (mobj);

	if (string_is_empty(artist) || string_is_empty(album))
		return;

	art_cache = pragha_application_get_art_cache (pragha);
	album_art_path = pragha_art_cache_get (art_cache, artist, album);

	if (album_art_path)
		goto exists;

	glyr_info = g_slice_new0 (glyr_struct);

	glyr_query_init(&glyr_info->query);

	glyr_opt_type(&glyr_info->query, GLYR_GET_COVERART);
	glyr_opt_from(&glyr_info->query, "lastfm;musicbrainz");

	glyr_opt_artist(&glyr_info->query, artist);
	glyr_opt_album(&glyr_info->query, album);

	glyr_info->pragha = pragha;

	pragha_async_launch(get_related_info_idle_func, glyr_finished_thread_update, glyr_info);

exists:
	g_free(album_art_path);
}

static void
backend_changed_state_cb (PraghaBackend *backend, GParamSpec *pspec, gpointer user_data)
{
	PraghaPreferences *preferences;
	PraghaMusicType file_type = FILE_NONE;
	PraghaBackendState state = 0;
	GtkAction *action;

	PraghaGlyr *glyr = user_data;
	PraghaApplication *pragha = glyr->pragha;

	state = pragha_backend_get_state (backend);
	gboolean playing = (state != ST_STOPPED);

	glyr = pragha_application_get_glyr (pragha);

	action = gtk_action_group_get_action (glyr->action_group_main_menu, "Search lyric");
	gtk_action_set_sensitive (action, playing);

	action = gtk_action_group_get_action (glyr->action_group_main_menu, "Search artist info");
	gtk_action_set_sensitive (action, playing);

	CDEBUG(DBG_INFO, "Configuring thread to get the cover art");

	if(state != ST_PLAYING) {
		return;
	}

	file_type = pragha_musicobject_get_file_type (pragha_backend_get_musicobject (backend));

	if(file_type == FILE_NONE || file_type == FILE_HTTP)
		return;

	preferences = pragha_application_get_preferences (pragha);
	if (pragha_preferences_get_download_album_art (preferences))
		related_get_album_art_handler (pragha);
}

static void
setup_main_menu (PraghaGlyr *glyr)
{
	GtkAction *action;

	glyr->action_group_main_menu = gtk_action_group_new ("PraghaGlyrMainMenuActions");
	gtk_action_group_set_translation_domain (glyr->action_group_main_menu, GETTEXT_PACKAGE);
	gtk_action_group_add_actions (glyr->action_group_main_menu,
	                              main_menu_actions,
	                              G_N_ELEMENTS (main_menu_actions),
	                              glyr);

	glyr->merge_id_main_menu = pragha_menubar_append_plugin_action (glyr->pragha,
	                                                                glyr->action_group_main_menu,
	                                                                main_menu_xml);

	action = gtk_action_group_get_action (glyr->action_group_main_menu, "Search lyric");
	gtk_action_set_sensitive (action, FALSE);

	action = gtk_action_group_get_action (glyr->action_group_main_menu, "Search artist info");
	gtk_action_set_sensitive (action, FALSE);
}

static void
setup_playlist (PraghaGlyr *glyr)
{
	PraghaPlaylist *playlist;
	PraghaApplication *pragha = glyr->pragha;

	glyr->action_group_playlist = gtk_action_group_new ("PraghaGlyrPlaylistActions");
	gtk_action_group_set_translation_domain (glyr->action_group_playlist, GETTEXT_PACKAGE);
	gtk_action_group_add_actions (glyr->action_group_playlist,
	                              playlist_actions,
	                              G_N_ELEMENTS (playlist_actions),
	                              glyr);

	playlist = pragha_application_get_playlist (pragha);
	glyr->merge_id_playlist = pragha_playlist_append_plugin_action (playlist,
	                                                                glyr->action_group_playlist,
	                                                                playlist_xml);
}

void
pragha_glyr_free (PraghaGlyr *glyr)
{
	PraghaPlaylist *playlist;
	PraghaApplication *pragha = glyr->pragha;

	g_signal_handlers_disconnect_by_func (pragha_application_get_backend (pragha),
	                                      backend_changed_state_cb, glyr);
	glyr_db_destroy (glyr->cache_db);

	pragha_menubar_remove_plugin_action (pragha,
	                                     glyr->action_group_main_menu,
	                                     glyr->merge_id_main_menu);
	glyr->merge_id_main_menu = 0;

	playlist = pragha_application_get_playlist (pragha);
	pragha_playlist_remove_plugin_action (playlist,
	                                      glyr->action_group_playlist,
	                                      glyr->merge_id_playlist);

	glyr->merge_id_playlist = 0;

	g_slice_free (PraghaGlyr, glyr);

	glyr_cleanup ();
}

PraghaGlyr *
pragha_glyr_new (PraghaApplication *pragha)
{
	gchar *cache_folder = g_build_path (G_DIR_SEPARATOR_S, g_get_user_cache_dir (), "pragha", NULL);
	g_mkdir_with_parents (cache_folder, S_IRWXU);

	glyr_init ();

	PraghaGlyr *glyr = g_slice_new (PraghaGlyr);

	glyr->pragha = pragha;
	glyr->cache_db = glyr_db_init (cache_folder);

	g_free (cache_folder);

	setup_main_menu (glyr);
	setup_playlist (glyr);

	g_signal_connect (pragha_application_get_backend (pragha), "notify::state",
	                  G_CALLBACK (backend_changed_state_cb), glyr);

	return glyr;
}
#endif
