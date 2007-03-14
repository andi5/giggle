/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2007 Imendio AB
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <config.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <string.h>

#include "giggle-git.h"
#include "giggle-file-list.h"
#include "giggle-diff-window.h"
#include "giggle-git-ignore.h"
#include "giggle-git-list-files.h"
#include "giggle-git-add.h"
#include "giggle-revision.h"

typedef struct GiggleFileListPriv GiggleFileListPriv;

struct GiggleFileListPriv {
	GiggleGit    *git;
	GtkIconTheme *icon_theme;

	GtkTreeStore *store;
	GtkTreeModel *filter_model;

	GtkWidget    *popup;
	GtkUIManager *ui_manager;

	GiggleJob    *job;

	GtkWidget    *diff_window;

	gboolean      show_all : 1;
	gboolean      compact_mode : 1;
};

static void       file_list_finalize           (GObject        *object);
static void       file_list_get_property       (GObject        *object,
						guint           param_id,
						GValue         *value,
						GParamSpec     *pspec);
static void       file_list_set_property       (GObject        *object,
						guint           param_id,
						const GValue   *value,
						GParamSpec     *pspec);
static gboolean   file_list_button_press       (GtkWidget      *widget,
						GdkEventButton *event);

static void       file_list_directory_changed  (GObject        *object,
						GParamSpec     *pspec,
						gpointer        user_data);
static void       file_list_managed_files_changed (GiggleFileList *list);

static void       file_list_populate           (GiggleFileList *list);
static void       file_list_populate_dir       (GiggleFileList *list,
						const gchar    *directory,
						const gchar    *rel_path,
						GtkTreeIter    *parent_iter);
static gboolean   file_list_filter_func        (GtkTreeModel   *model,
						GtkTreeIter    *iter,
						gpointer        user_data);

static gboolean   file_list_search_equal_func   (GtkTreeModel   *model,
						 gint            column,
						 const gchar    *key,
						 GtkTreeIter    *iter,
						 gpointer        search_data);
static gint       file_list_compare_func        (GtkTreeModel   *model,
						 GtkTreeIter    *a,
						 GtkTreeIter    *b,
						 gpointer        user_data);

static gboolean   file_list_get_name_and_ignore_for_iter (GiggleFileList   *list,
							  GtkTreeIter      *iter,
							  gchar           **name,
							  GiggleGitIgnore **git_ignore);

static void       file_list_diff_file           (GtkWidget        *widget,
						 GiggleFileList   *list);
static void       file_list_add_file            (GtkWidget        *widget,
						 GiggleFileList   *list);
static void       file_list_ignore_file         (GtkWidget        *widget,
						 GiggleFileList   *list);
static void       file_list_unignore_file       (GtkWidget        *widget,
						 GiggleFileList   *list);
static void       file_list_toggle_show_all     (GtkWidget        *widget,
						 GiggleFileList   *list);

static void       file_list_cell_data_sensitive_func  (GtkCellLayout   *cell_layout,
						       GtkCellRenderer *renderer,
						       GtkTreeModel    *tree_model,
						       GtkTreeIter     *iter,
						       gpointer         data);


G_DEFINE_TYPE (GiggleFileList, giggle_file_list, GTK_TYPE_TREE_VIEW)

#define GET_PRIV(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GIGGLE_TYPE_FILE_LIST, GiggleFileListPriv))

enum {
	COL_NAME,
	COL_REL_PATH,
	COL_PIXBUF,
	COL_GIT_IGNORE,
	COL_MANAGED, /* File managed by git */
	LAST_COL
};

enum {
	PROP_0,
	PROP_SHOW_ALL,
	PROP_COMPACT_MODE,
};

GtkActionEntry menu_items [] = {
	{ "Diff",     NULL,             N_("_Diff"),                   NULL, NULL, G_CALLBACK (file_list_diff_file) },
	{ "AddFile",  NULL,             N_("A_dd file to repository"), NULL, NULL, G_CALLBACK (file_list_add_file) },
	{ "Ignore",   GTK_STOCK_ADD,    N_("_Add to .gitignore"),      NULL, NULL, G_CALLBACK (file_list_ignore_file) },
	{ "Unignore", GTK_STOCK_REMOVE, N_("_Remove from .gitignore"), NULL, NULL, G_CALLBACK (file_list_unignore_file) },
};

GtkToggleActionEntry toggle_menu_items [] = {
	{ "ShowAll", NULL, N_("_Show all files"), NULL, NULL, G_CALLBACK (file_list_toggle_show_all), FALSE },
};

const gchar *ui_description =
	"<ui>"
	"  <popup name='PopupMenu'>"
	"    <menuitem action='Diff'/>"
	"    <separator/>"
	"    <menuitem action='AddFile'/>"
	"    <separator/>"
	"    <menuitem action='Ignore'/>"
	"    <menuitem action='Unignore'/>"
	"    <separator/>"
	"    <menuitem action='ShowAll'/>"
	"  </popup>"
	"</ui>";


static void
giggle_file_list_class_init (GiggleFileListClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (class);

	object_class->finalize = file_list_finalize;
	object_class->get_property = file_list_get_property;
	object_class->set_property = file_list_set_property;

	widget_class->button_press_event = file_list_button_press;

	g_object_class_install_property (object_class,
					 PROP_SHOW_ALL,
					 g_param_spec_boolean ("show-all",
							       "Show all",
							       "Whether to show all elements",
							       FALSE,
							       G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_COMPACT_MODE,
					 g_param_spec_boolean ("compact-mode",
							       "Compact mode",
							       "Whether to show the list in compact mode or not",
							       FALSE,
							       G_PARAM_READWRITE));

	g_type_class_add_private (object_class, sizeof (GiggleFileListPriv));
}

static void
giggle_file_list_init (GiggleFileList *list)
{
	GiggleFileListPriv *priv;
	GtkCellRenderer    *renderer;
	GtkTreeViewColumn  *column;
	GtkActionGroup     *action_group;
	GtkTreeSelection   *selection;

	priv = GET_PRIV (list);

	priv->git = giggle_git_get ();
	g_signal_connect (priv->git, "notify::project-dir",
			  G_CALLBACK (file_list_directory_changed), list);
	g_signal_connect_swapped (priv->git, "notify::git-dir",
				  G_CALLBACK (file_list_managed_files_changed), list);

	priv->icon_theme = gtk_icon_theme_get_default ();

	priv->store = gtk_tree_store_new (LAST_COL, G_TYPE_STRING, G_TYPE_STRING, GDK_TYPE_PIXBUF,
					  GIGGLE_TYPE_GIT_IGNORE, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN);
	priv->filter_model = gtk_tree_model_filter_new (GTK_TREE_MODEL (priv->store), NULL);

	gtk_tree_model_filter_set_visible_func (GTK_TREE_MODEL_FILTER (priv->filter_model),
						file_list_filter_func, list, NULL);

	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (priv->store),
					 COL_NAME,
					 file_list_compare_func,
					 list, NULL);

	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (priv->store),
					      COL_NAME, GTK_SORT_ASCENDING);

	gtk_tree_view_set_model (GTK_TREE_VIEW (list), priv->filter_model);

	gtk_tree_view_set_search_equal_func (GTK_TREE_VIEW (list),
					     file_list_search_equal_func,
					     NULL, NULL);
	
	column = gtk_tree_view_column_new ();
	gtk_tree_view_column_set_title (column, _("Project"));

	renderer = gtk_cell_renderer_pixbuf_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (column), renderer, FALSE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (column), renderer,
					"pixbuf", COL_PIXBUF,
					NULL);
	gtk_cell_layout_set_cell_data_func (GTK_CELL_LAYOUT (column), renderer,
					    file_list_cell_data_sensitive_func,
					    list, NULL);

	renderer = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (column), renderer, FALSE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (column), renderer,
					"text", COL_NAME,
					NULL);
	gtk_cell_layout_set_cell_data_func (GTK_CELL_LAYOUT (column), renderer,
					    file_list_cell_data_sensitive_func,
					    list, NULL);

	gtk_tree_view_append_column (GTK_TREE_VIEW (list), column);
	file_list_populate (list);

	/* create the popup menu */
	action_group = gtk_action_group_new ("PopupActions");
	gtk_action_group_set_translation_domain (action_group, NULL);
	gtk_action_group_add_actions (action_group, menu_items,
				      G_N_ELEMENTS (menu_items), list);
	gtk_action_group_add_toggle_actions (action_group, toggle_menu_items,
					     G_N_ELEMENTS (toggle_menu_items), list);
	
	priv->ui_manager = gtk_ui_manager_new ();
	gtk_ui_manager_insert_action_group (priv->ui_manager, action_group, 0);

	if (gtk_ui_manager_add_ui_from_string (priv->ui_manager, ui_description, -1, NULL)) {
		priv->popup = gtk_ui_manager_get_widget (priv->ui_manager, "/ui/PopupMenu");
	}

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (list));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);

	gtk_rc_parse_string ("style \"file-list-compact-style\""
			     "{"
			     "  GtkTreeView::vertical-separator = 0"
			     "}"
			     "widget \"*.file-list\" style \"file-list-compact-style\"");

	/* create diff window */
	priv->diff_window = giggle_diff_window_new ();

	g_signal_connect_after (priv->diff_window, "response",
				G_CALLBACK (gtk_widget_hide), NULL);
}

static void
file_list_finalize (GObject *object)
{
	GiggleFileListPriv *priv;

	priv = GET_PRIV (object);

	if (priv->job) {
		giggle_git_cancel_job (priv->git, priv->job);
		g_object_unref (priv->job);
		priv->job = NULL;
	}

	g_object_unref (priv->git);
	g_object_unref (priv->store);
	g_object_unref (priv->filter_model);

	g_object_unref (priv->ui_manager);

	G_OBJECT_CLASS (giggle_file_list_parent_class)->finalize (object);
}

static void
file_list_get_property (GObject    *object,
			guint       param_id,
			GValue     *value,
			GParamSpec *pspec)
{
	GiggleFileListPriv *priv;

	priv = GET_PRIV (object);

	switch (param_id) {
	case PROP_SHOW_ALL:
		g_value_set_boolean (value, priv->show_all);
		break;
	case PROP_COMPACT_MODE:
		g_value_set_boolean (value, priv->compact_mode);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}

static void
file_list_set_property (GObject      *object,
			guint         param_id,
			const GValue *value,
			GParamSpec   *pspec)
{
	GiggleFileListPriv *priv;

	priv = GET_PRIV (object);

	switch (param_id) {
	case PROP_SHOW_ALL:
		giggle_file_list_set_show_all (GIGGLE_FILE_LIST (object),
					       g_value_get_boolean (value));
		break;
	case PROP_COMPACT_MODE:
		giggle_file_list_set_compact_mode (GIGGLE_FILE_LIST (object),
						   g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}

static gboolean
file_list_button_press (GtkWidget      *widget,
			GdkEventButton *event)
{
	GiggleFileList     *list;
	GiggleFileListPriv *priv;
	gboolean            add, ignore, unignore;
	gchar              *name;
	GiggleGitIgnore    *git_ignore;
	GtkAction          *action;
	GtkTreeSelection   *selection;
	GtkTreeModel       *model;
	GList              *rows, *l;
	GtkTreeIter         iter;

	list = GIGGLE_FILE_LIST (widget);
	priv = GET_PRIV (list);
	ignore = unignore = add = FALSE;

	GTK_WIDGET_CLASS (giggle_file_list_parent_class)->button_press_event (widget, event);

	if (event->button == 3) {
		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (list));
		rows = gtk_tree_selection_get_selected_rows (selection, &model);

		for (l = rows; l; l = l->next) {
			gtk_tree_model_get_iter (model, &iter, l->data);

			gtk_tree_model_get (model, &iter,
					    COL_MANAGED, &add,
					    -1);

			if (file_list_get_name_and_ignore_for_iter (list, &iter, &name, &git_ignore)) {
				if (giggle_git_ignore_name_matches (git_ignore, name)) {
					unignore = TRUE;
				} else {
					ignore = TRUE;
				}

				g_object_unref (git_ignore);
				g_free (name);
			}
		}

		action = gtk_ui_manager_get_action (priv->ui_manager, "/ui/PopupMenu/AddFile");
		gtk_action_set_sensitive (action, add);
		action = gtk_ui_manager_get_action (priv->ui_manager, "/ui/PopupMenu/Ignore");
		gtk_action_set_sensitive (action, ignore);
		action = gtk_ui_manager_get_action (priv->ui_manager, "/ui/PopupMenu/Unignore");
		gtk_action_set_sensitive (action, unignore);
		
		gtk_menu_popup (GTK_MENU (priv->popup), NULL, NULL,
				NULL, NULL, event->button, event->time);

		g_list_foreach (rows, (GFunc) gtk_tree_path_free, NULL);
		g_list_free (rows);
	}

	return TRUE;
}

static void
file_list_directory_changed (GObject    *object,
			     GParamSpec *pspec,
			     gpointer    user_data)
{
	GiggleFileListPriv *priv;
	GiggleFileList     *list;

	list = GIGGLE_FILE_LIST (user_data);
	priv = GET_PRIV (list);

	gtk_tree_store_clear (priv->store);
	file_list_populate (list);
}

static void
file_list_update_managed (GiggleFileList *file_list,
			  GtkTreeIter    *parent,
			  const gchar    *parent_path,
			  GList          *files)
{
	GiggleFileListPriv *priv;
	GtkTreeIter         iter;
	gboolean            valid;
	GiggleGitIgnore    *git_ignore;
	gchar              *name, *path;
	gboolean            managed;

	priv = GET_PRIV (file_list);

	if (parent) {
		valid = gtk_tree_model_iter_children (GTK_TREE_MODEL (priv->store),
						      &iter, parent);
	} else {
		valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (priv->store), &iter);
	}

	while (valid) {
		gtk_tree_model_get (GTK_TREE_MODEL (priv->store), &iter,
				    COL_NAME, &name,
				    COL_GIT_IGNORE, &git_ignore,
				    -1);

		if (parent_path) {
			path = g_build_filename (parent_path, name, NULL);
			managed = (g_list_find_custom (files, path, (GCompareFunc) strcmp) != NULL);
		} else {
			/* we don't want the project basename included */
			path = g_strdup ("");
			managed = FALSE;
		}

		gtk_tree_store_set (priv->store, &iter,
				    COL_MANAGED, managed,
				    -1);

		if (git_ignore) {
			/* it's a directory */
			file_list_update_managed (file_list, &iter, path, files);
			g_object_unref (git_ignore);
		}

		g_free (path);
		g_free (name);
		valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (priv->store), &iter);
	}
}

static void
file_list_files_callback (GiggleGit *git,
			  GiggleJob *job,
			  GError    *error,
			  gpointer   user_data)
{
	GiggleFileList     *list;
	GiggleFileListPriv *priv;
	GList              *files;

	list = GIGGLE_FILE_LIST (user_data);
	priv = GET_PRIV (list);

	if (error) {
		GtkWidget *dialog;

		dialog = gtk_message_dialog_new (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (list))),
						 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
						 GTK_MESSAGE_ERROR,
						 GTK_BUTTONS_OK,
						 _("An error ocurred when retrieving the file list:\n%s"),
						 error->message);

		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
	} else {
		files = giggle_git_list_files_get_files (GIGGLE_GIT_LIST_FILES (priv->job));
		file_list_update_managed (list, NULL, NULL, files);
	}

	g_object_unref (priv->job);
	priv->job = NULL;
}

static void
file_list_managed_files_changed (GiggleFileList *list)
{
	GiggleFileListPriv *priv;

	priv = GET_PRIV (list);

	file_list_update_managed (list, NULL, NULL, NULL);

	if (priv->job) {
		giggle_git_cancel_job (priv->git, priv->job);
		g_object_unref (priv->job);
		priv->job = NULL;
	}

	priv->job = giggle_git_list_files_new ();

	giggle_git_run_job (priv->git,
			    priv->job,
			    file_list_files_callback,
			    list);
}

static void
file_list_diff_file (GtkWidget      *widget,
		     GiggleFileList *list)
{
	GiggleFileListPriv *priv;
	GtkWidget          *toplevel;
	GList              *files;

	priv = GET_PRIV (list);

	files = giggle_file_list_get_selection (list);
	giggle_diff_window_set_files (GIGGLE_DIFF_WINDOW (priv->diff_window), files);

	toplevel = gtk_widget_get_toplevel (GTK_WIDGET (list));
	gtk_window_set_transient_for (GTK_WINDOW (priv->diff_window),
				      GTK_WINDOW (toplevel));
	gtk_widget_show (priv->diff_window);
}

static void
file_list_add_element (GiggleFileList *list,
		       const gchar    *directory,
		       const gchar    *rel_path,
		       const gchar    *name,
		       GtkTreeIter    *parent_iter)
{
	GiggleFileListPriv *priv;
	GdkPixbuf          *pixbuf;
	GtkTreeIter         iter;
	gboolean            is_dir;
	GiggleGitIgnore    *git_ignore = NULL;
	gchar              *full_path;

	priv = GET_PRIV (list);

	full_path = g_build_filename (directory, rel_path, NULL);
	is_dir = g_file_test (full_path, G_FILE_TEST_IS_DIR);

	gtk_tree_store_append (priv->store,
			       &iter,
			       parent_iter);

	if (is_dir) {
		file_list_populate_dir (list, directory, rel_path, &iter);
		pixbuf = gtk_icon_theme_load_icon (priv->icon_theme,
						   "folder", 16, 0, NULL);;
		git_ignore = giggle_git_ignore_new (full_path);
	} else {
		pixbuf = gtk_icon_theme_load_icon (priv->icon_theme,
						   "text-x-generic", 16, 0, NULL);;
	}

	gtk_tree_store_set (priv->store, &iter,
			    COL_PIXBUF, pixbuf,
			    COL_NAME, (name) ? name : full_path,
			    COL_REL_PATH, rel_path,
			    COL_GIT_IGNORE, git_ignore,
			    -1);
	if (pixbuf) {
		g_object_unref (pixbuf);
	}

	if (git_ignore) {
		g_object_unref (git_ignore);
	}

	g_free (full_path);
}

static void
file_list_populate_dir (GiggleFileList *list,
			const gchar    *directory,
			const gchar    *rel_path,
			GtkTreeIter    *parent_iter)
{
	GiggleFileListPriv *priv;
	GDir               *dir;
	const gchar        *name;
	gchar              *full_path, *path;

	priv = GET_PRIV (list);
	full_path = g_build_filename (directory, rel_path, NULL);
	dir = g_dir_open (full_path, 0, NULL);

	g_return_if_fail (dir != NULL);

	while ((name = g_dir_read_name (dir))) {
		path = g_build_filename (rel_path, name, NULL);
		file_list_add_element (list, directory, path, name, parent_iter);
		g_free (path);
	}

	g_free (full_path);
	g_dir_close (dir);
}

static void
file_list_populate (GiggleFileList *list)
{
	GiggleFileListPriv *priv;
	const gchar        *directory;
	GtkTreePath        *path;

	priv = GET_PRIV (list);
	directory = giggle_git_get_project_dir (priv->git);

	file_list_add_element (list, directory, "", NULL, NULL);

	/* expand the project folder */
	path = gtk_tree_path_new_first ();
	gtk_tree_view_expand_row (GTK_TREE_VIEW (list), path, FALSE);
	gtk_tree_path_free (path);
}

static void
file_list_add_file_callback (GiggleGit *git,
			     GiggleJob *job,
			     GError    *error,
			     gpointer   user_data)
{
	GiggleFileList     *list;
	GiggleFileListPriv *priv;

	list = GIGGLE_FILE_LIST (user_data);
	priv = GET_PRIV (list);

	if (error) {
		GtkWidget *dialog;

		dialog = gtk_message_dialog_new (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (list))),
						 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
						 GTK_MESSAGE_ERROR,
						 GTK_BUTTONS_OK,
						 _("An error ocurred when adding a file to git:\n%s"),
						 error->message);

		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);

		g_object_unref (priv->job);
		priv->job = NULL;
	} else {
		g_object_unref (priv->job);
		priv->job = NULL;

		file_list_managed_files_changed (list);
	}
}

static void
file_list_add_file (GtkWidget        *widget,
		    GiggleFileList   *list)
{
	GiggleFileListPriv *priv;

	priv = GET_PRIV (list);

	if (priv->job) {
		giggle_git_cancel_job (priv->git, priv->job);
		g_object_unref (priv->job);
		priv->job = NULL;
	}

	priv->job = giggle_git_add_new ();
	giggle_git_add_set_files (GIGGLE_GIT_ADD (priv->job),
				  giggle_file_list_get_selection (list));

	giggle_git_run_job (priv->git,
			    priv->job,
			    file_list_add_file_callback,
			    list);
}

static gboolean
file_list_get_ignore_file (GtkTreeModel *model,
			   GtkTreeIter  *file_iter,
			   const gchar  *name)
{
	GiggleGitIgnore *git_ignore;
	GtkTreeIter      iter, parent;
	gboolean         matches = FALSE;

	iter = *file_iter;

	while (!matches && gtk_tree_model_iter_parent (model, &parent, &iter)) {
		gtk_tree_model_get (model, &parent,
				    COL_GIT_IGNORE, &git_ignore,
				    -1);

		if (git_ignore) {
			matches = giggle_git_ignore_name_matches (git_ignore, name);
			g_object_unref (git_ignore);
		}

		/* scale up through the hierarchy */
		iter = parent;
	}

	return matches;
}

static gboolean
file_list_filter_func (GtkTreeModel   *model,
		       GtkTreeIter    *iter,
		       gpointer        user_data)
{
	GiggleFileList     *list;
	GiggleFileListPriv *priv;
	gchar              *name;
	gboolean            retval = TRUE;

	list = GIGGLE_FILE_LIST (user_data);
	priv = GET_PRIV (list);

	gtk_tree_model_get (model, iter,
			    COL_NAME, &name,
			    -1);
	if (!name) {
		return FALSE;
	}

	/* we never want to show these files */
	if (strcmp (name, ".git") == 0 ||
	    strcmp (name, ".gitignore") == 0 ||
	    (!priv->show_all && file_list_get_ignore_file (model, iter, name))) {
		retval = FALSE;
	}

	g_free (name);

	return retval;
}

static gboolean
file_list_get_name_and_ignore_for_iter (GiggleFileList   *list,
					GtkTreeIter      *iter,
					gchar           **name,
					GiggleGitIgnore **git_ignore)
{
	GiggleFileListPriv *priv;
	GtkTreeIter         parent;

	priv = GET_PRIV (list);

	if (!gtk_tree_model_iter_parent (priv->filter_model, &parent, iter)) {
		*name = NULL;
		*git_ignore = NULL;

		return FALSE;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (priv->filter_model), iter,
			    COL_NAME, name,
			    -1);
	gtk_tree_model_get (GTK_TREE_MODEL (priv->filter_model), &parent,
			    COL_GIT_IGNORE, git_ignore,
			    -1);
	return TRUE;
}

static gboolean
file_list_search_equal_func (GtkTreeModel *model,
			     gint          column,
			     const gchar  *key,
			     GtkTreeIter  *iter,
			     gpointer      search_data)
{
	gchar    *str;
	gchar    *normalized_key, *normalized_str;
	gchar    *casefold_key, *casefold_str;
	gboolean  ret;
	
	gtk_tree_model_get (model, iter, column, &str, -1);

	normalized_key = g_utf8_normalize (key, -1, G_NORMALIZE_ALL);
	normalized_str = g_utf8_normalize (str, -1, G_NORMALIZE_ALL);

	ret = TRUE;
	if (normalized_str && normalized_key) {
		casefold_key = g_utf8_casefold (normalized_key, -1);
		casefold_str = g_utf8_casefold (normalized_str, -1);

		if (strcmp (casefold_key, normalized_key) != 0) {
			/* Use case sensitive match when the search key has a
			 * mix of upper and lower case, mimicking vim smartcase
			 * search.
			 */
			if (strstr (normalized_str, normalized_key)) {
				ret = FALSE;
			}
		} else {
			if (strstr (casefold_str, casefold_key)) {
				ret = FALSE;
			}
		}

		g_free (casefold_key);
		g_free (casefold_str);
	}

	g_free (str);
	g_free (normalized_key);
	g_free (normalized_str);

	return ret;
}

static gint
file_list_compare_func (GtkTreeModel *model,
			GtkTreeIter  *iter1,
			GtkTreeIter  *iter2,
			gpointer      user_data)
{
	GiggleGitIgnore *git_ignore1, *git_ignore2;
	gchar           *name1, *name2;
	gint             retval = 0;

	gtk_tree_model_get (model, iter1,
			    COL_GIT_IGNORE, &git_ignore1,
			    COL_NAME, &name1,
			    -1);
	gtk_tree_model_get (model, iter2,
			    COL_GIT_IGNORE, &git_ignore2,
			    COL_NAME, &name2,
			    -1);

	if (git_ignore1 && !git_ignore2) {
		retval = -1;
	} else if (git_ignore2 && !git_ignore1) {
		retval = 1;
	} else {
		retval = strcmp (name1, name2);
	}

	/* free stuff */
	if (git_ignore1) {
		g_object_unref (git_ignore1);
	}

	if (git_ignore2) {
		g_object_unref (git_ignore2);
	}

	g_free (name1);
	g_free (name2);

	return retval;
}

static void
file_list_ignore_file_foreach (GtkTreeModel *model,
			       GtkTreePath  *path,
			       GtkTreeIter  *iter,
			       gpointer      data)
{
	GiggleGitIgnore *git_ignore;
	gchar           *name;

	if (!file_list_get_name_and_ignore_for_iter (GIGGLE_FILE_LIST (data),
						     iter, &name, &git_ignore)) {
		return;
	}

	if (git_ignore) {
		giggle_git_ignore_add_glob (git_ignore, name);
		g_object_unref (git_ignore);
	}

	g_free (name);
}

static void
file_list_ignore_file (GtkWidget      *widget,
		       GiggleFileList *list)
{
	GiggleFileListPriv *priv;
	GtkTreeSelection   *selection;

	priv = GET_PRIV (list);

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (list));
	gtk_tree_selection_selected_foreach (selection, file_list_ignore_file_foreach, list);
	gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (priv->filter_model));
}

static void
file_list_unignore_file_foreach (GtkTreeModel *model,
				 GtkTreePath  *path,
				 GtkTreeIter  *iter,
				 gpointer      data)
{
	GiggleFileList  *list;
	GiggleGitIgnore *git_ignore;
	gchar           *name;

	list = GIGGLE_FILE_LIST (data);

	if (!file_list_get_name_and_ignore_for_iter (list, iter, &name, &git_ignore)) {
		return;
	}

	if (git_ignore) {
		if (!giggle_git_ignore_remove_glob_for_name (git_ignore, name, TRUE)) {
			GtkWidget *dialog, *toplevel;

			toplevel = gtk_widget_get_toplevel (GTK_WIDGET (list));
			dialog = gtk_message_dialog_new (GTK_WINDOW (toplevel),
							 GTK_DIALOG_MODAL,
							 GTK_MESSAGE_INFO,
							 GTK_BUTTONS_YES_NO,
							 _("Delete glob pattern?"));

			gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
								  _("The selected file was shadowed by a glob pattern "
								    "that may be hiding other files, delete it?"));

			if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_YES) {
				giggle_git_ignore_remove_glob_for_name (git_ignore, name, FALSE);
			}

			gtk_widget_destroy (dialog);
		}

		g_object_unref (git_ignore);
	}

	g_free (name);
}

static void
file_list_unignore_file (GtkWidget      *widget,
			 GiggleFileList *list)
{
	GiggleFileListPriv *priv;
	GtkTreeSelection   *selection;

	priv = GET_PRIV (list);

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (list));
	gtk_tree_selection_selected_foreach (selection, file_list_unignore_file_foreach, list);
	gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (priv->filter_model));
}

static void
file_list_toggle_show_all (GtkWidget      *widget,
			   GiggleFileList *list)
{
	GiggleFileListPriv *priv;
	gboolean active;

	priv = GET_PRIV (list);
	active = gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (widget));
	giggle_file_list_set_show_all (list, active);
}

static void
file_list_cell_data_sensitive_func (GtkCellLayout   *layout,
				    GtkCellRenderer *renderer,
				    GtkTreeModel    *tree_model,
				    GtkTreeIter     *iter,
				    gpointer         data)
{
	GiggleFileList     *list;
	GiggleFileListPriv *priv;
	GiggleGitIgnore    *git_ignore = NULL;
	gchar              *name = NULL;
	gboolean            value = TRUE;
	GtkTreeIter         parent;
	GtkStateType        state;
	GdkColor            color;

	list = GIGGLE_FILE_LIST (data);
	priv = GET_PRIV (list);

	if (priv->show_all) {
		if (file_list_get_name_and_ignore_for_iter (list, iter, &name, &git_ignore)) {
			value = ! giggle_git_ignore_name_matches (git_ignore, name);
		} else {
			/* we don't want the project root being set insensitive */
			value = ! gtk_tree_model_iter_parent (tree_model, &parent, iter);
		}
	}

	if (value) {
		/* check whether the file is managed by git */
		gtk_tree_model_get (tree_model, iter,
				    COL_MANAGED, &value,
				    -1);
		value ^= 1;
	}

	if (GTK_IS_CELL_RENDERER_TEXT (renderer)) {
		state = (value) ? GTK_STATE_NORMAL : GTK_STATE_INSENSITIVE;
		color = GTK_WIDGET (list)->style->text [state];
		g_object_set (renderer, "foreground-gdk", &color, NULL);
	} else {
		g_object_set (renderer, "sensitive", value, NULL);
	}

	if (git_ignore) {
		g_object_unref (git_ignore);
	}
	g_free (name);
}

GtkWidget *
giggle_file_list_new (void)
{
	return g_object_new (GIGGLE_TYPE_FILE_LIST, NULL);
}

gboolean
giggle_file_list_get_show_all (GiggleFileList *list)
{
	GiggleFileListPriv *priv;

	g_return_val_if_fail (GIGGLE_IS_FILE_LIST (list), FALSE);

	priv = GET_PRIV (list);
	return priv->show_all;
}

void
giggle_file_list_set_show_all (GiggleFileList *list,
			       gboolean        show_all)
{
	GiggleFileListPriv *priv;

	g_return_if_fail (GIGGLE_IS_FILE_LIST (list));

	priv = GET_PRIV (list);

	priv->show_all = (show_all == TRUE);
	gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (priv->filter_model));
	g_object_notify (G_OBJECT (list), "show-all");
}

GList *
giggle_file_list_get_selection (GiggleFileList *list)
{
	GiggleFileListPriv *priv;
	GtkTreeSelection   *selection;
	GtkTreeModel       *model;
	GtkTreeIter         iter;
	GList              *rows, *l, *files;
	gchar              *path;

	g_return_val_if_fail (GIGGLE_IS_FILE_LIST (list), NULL);

	priv = GET_PRIV (list);
	files = NULL;

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (list));
	rows = gtk_tree_selection_get_selected_rows (selection, &model);

	for (l = rows; l; l = l->next) {
		gtk_tree_model_get_iter (model, &iter, l->data);

		gtk_tree_model_get (model, &iter,
				    COL_REL_PATH, &path,
				    -1);

		files = g_list_prepend (files, path);
	}

	g_list_foreach (rows, (GFunc) gtk_tree_path_free, NULL);
	g_list_free (rows);

	return g_list_reverse (files);
}

gboolean
giggle_file_list_get_compact_mode (GiggleFileList *list)
{
	GiggleFileListPriv *priv;

	g_return_val_if_fail (GIGGLE_IS_FILE_LIST (list), FALSE);

	priv = GET_PRIV (list);
	return priv->compact_mode;
}

void
giggle_file_list_set_compact_mode (GiggleFileList *list,
				   gboolean        compact_mode)
{
	GiggleFileListPriv *priv;
	GtkRcStyle         *rc_style;
	gint                size;

	g_return_if_fail (GIGGLE_IS_FILE_LIST (list));

	priv = GET_PRIV (list);

	if (compact_mode != priv->compact_mode) {
		priv->compact_mode = (compact_mode == TRUE);
		rc_style = gtk_widget_get_modifier_style (GTK_WIDGET (list));

		if (rc_style->font_desc) {
			/* free old font desc */
			pango_font_description_free (rc_style->font_desc);
			rc_style->font_desc = NULL;
		}

		if (priv->compact_mode) {
			rc_style->font_desc = pango_font_description_copy (GTK_WIDGET (list)->style->font_desc);
			size = pango_font_description_get_size (rc_style->font_desc);
			pango_font_description_set_size (rc_style->font_desc,
							 size * PANGO_SCALE_SMALL);
		}

		gtk_widget_modify_style (GTK_WIDGET (list), rc_style);
		gtk_widget_set_name (GTK_WIDGET (list),
				     (priv->compact_mode) ? "file-list" : NULL);
		g_object_notify (G_OBJECT (list), "compact-mode");
	}
}
