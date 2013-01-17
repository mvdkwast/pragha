/*************************************************************************/
/* Copyright (C) 2013 matias <mati86dl@gmail.com>			 */
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

#ifndef PRAGHA_DATABASE_H
#define PRAGHA_DATABASE_H

#include <glib-object.h>

G_BEGIN_DECLS

#define PRAGHA_TYPE_DATABASE (pragha_database_get_type())
#define PRAGHA_DATABASE(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), PRAGHA_TYPE_DATABASE, PraghaDatabase))
#define PRAGHA_DATABASE_CONST(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), PRAGHA_TYPE_DATABASE, PraghaDatabase const))
#define PRAGHA_DATABASE_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), PRAGHA_TYPE_DATABASE, PraghaDatabaseClass))
#define PRAGHA_IS_DATABASE(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PRAGHA_TYPE_DATABASE))
#define PRAGHA_IS_DATABASE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), PRAGHA_TYPE_DATABASE))
#define PRAGHA_DATABASE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), PRAGHA_TYPE_DATABASE, PraghaDatabaseClass))

typedef struct _PraghaDatabase PraghaDatabase;
typedef struct _PraghaDatabaseClass PraghaDatabaseClass;
typedef struct _PraghaDatabasePrivate PraghaDatabasePrivate;

typedef struct {
	gchar **resultp;
	gint no_rows;
	gint no_columns;
} PraghaDbResponse;

struct _PraghaDatabase
{
   GObject parent;

   /*< private >*/
   PraghaDatabasePrivate *priv;
};

struct _PraghaDatabaseClass
{
   GObjectClass parent_class;
};

gboolean
pragha_database_exec_query (PraghaDatabase *database,
                            const gchar *query);

gboolean
pragha_database_exec_sqlite_query(PraghaDatabase *database,
                                  gchar *query,
                                  PraghaDbResponse *result);

gboolean
pragha_database_start_successfully (PraghaDatabase *database);

PraghaDatabase* pragha_database_get (void);
GType pragha_database_get_type (void) G_GNUC_CONST;

G_END_DECLS

#endif /* PRAGHA_DATABASE_H */