/*
 * Copyright (C) 2007 Jan Arne Petersen <jap@gnome.org>
 * Copyright (C) 2012 Pavel Vasin
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>. 
 */

#ifndef PRAGHA_GNOME_MEDIA_KEYS_H
#define PRAGHA_GNOME_MEDIA_KEYS_H

/* pragha.h */
typedef struct _PraghaApplication PraghaApplication;

typedef struct _con_gnome_media_keys con_gnome_media_keys;

gboolean gnome_media_keys_will_be_useful();
con_gnome_media_keys *init_gnome_media_keys(PraghaApplication *pragha);
void gnome_media_keys_free(con_gnome_media_keys *gmk);

#endif /* PRAGHAGNOME_MEDIA_KEYS_H */