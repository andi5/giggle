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

#include "giggle-git-list-tree.h"
#include <string.h>

typedef struct GiggleGitListTreePriv GiggleGitListTreePriv;

struct GiggleGitListTreePriv {
	GHashTable     *files;
	GiggleRevision *revision;
	char           *path;
};

G_DEFINE_TYPE (GiggleGitListTree, giggle_git_list_tree, GIGGLE_TYPE_JOB)

#define GET_PRIV(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GIGGLE_TYPE_GIT_LIST_TREE, GiggleGitListTreePriv))

enum {
	PROP_0,
	PROP_REVISION,
	PROP_PATH,
};


static void
git_list_tree_finalize (GObject *object)
{
	GiggleGitListTreePriv *priv;

	priv = GET_PRIV (object);

	g_hash_table_destroy (priv->files);
	g_free (priv->path);

	G_OBJECT_CLASS (giggle_git_list_tree_parent_class)->finalize (object);
}

static void
git_list_tree_dispose (GObject *object)
{
	GiggleGitListTreePriv *priv;

	priv = GET_PRIV (object);

	if (priv->revision) {
		g_object_unref (priv->revision);
		priv->revision = NULL;
	}

	G_OBJECT_CLASS (giggle_git_list_tree_parent_class)->dispose (object);
}

static void
git_list_tree_get_property (GObject    *object,
			    guint       param_id,
			    GValue     *value,
			    GParamSpec *pspec)
{
	GiggleGitListTreePriv *priv;

	priv = GET_PRIV (object);
	
	switch (param_id) {
	case PROP_REVISION:
		g_value_set_object (value, priv->revision);
		break;

	case PROP_PATH:
		g_value_set_string (value, priv->path);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}

static void
git_list_tree_set_property (GObject      *object,
			    guint         param_id,
			    const GValue *value,
			    GParamSpec   *pspec)
{
	GiggleGitListTreePriv *priv;

	priv = GET_PRIV (object);

	switch (param_id) {
	case PROP_REVISION:
		g_assert (NULL == priv->revision);
		priv->revision = g_value_dup_object (value);
		break;

	case PROP_PATH:
		g_assert (NULL == priv->path);
		priv->path = g_value_dup_string (value);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}

static gboolean
git_list_tree_get_command_line (GiggleJob *job, gchar **command_line)
{
	GiggleGitListTreePriv *priv;
	const char *revision = NULL;
	char *path = NULL;

	priv = GET_PRIV (job);

	if (priv->revision)
		revision = giggle_revision_get_sha (priv->revision);
	if (priv->path)
		path = g_shell_quote (priv->path);

	*command_line = g_strdup_printf (GIT_COMMAND " ls-tree %s%s%s",
					 revision ? revision : "HEAD",
					 path ? " " : "", path ? path : "");

	g_free (path);

	return TRUE;
}

static void
git_list_tree_handle_output (GiggleJob   *job,
			     const gchar *output_str,
			     gsize        output_len)
{
	GiggleGitListTreePriv    *priv;
	const char		 *start, *end;
	char			 *file, *sha;

	priv = GET_PRIV (job);
	end = output_str;

	while (*end) {
		start = end;
		end = strchr (start, '\n');

		if (!end)
			break;

		start += 12;
		sha = g_strndup (start, 40);

		start += 41;
		file = g_strndup (start, end - start);

		g_hash_table_insert (priv->files, file, sha);
		end += 1;
	}
}

static void
giggle_git_list_tree_class_init (GiggleGitListTreeClass *class)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (class);
	GiggleJobClass *job_class    = GIGGLE_JOB_CLASS (class);

	object_class->get_property  = git_list_tree_get_property;
	object_class->set_property  = git_list_tree_set_property;
	object_class->finalize      = git_list_tree_finalize;
	object_class->dispose       = git_list_tree_dispose;

	job_class->get_command_line = git_list_tree_get_command_line;
	job_class->handle_output    = git_list_tree_handle_output;

	g_object_class_install_property (object_class,
					 PROP_REVISION,
					 g_param_spec_object ("revision",
							      "revision",
							      "revision the folder to list",
							      GIGGLE_TYPE_REVISION,
							      G_PARAM_READWRITE |
							      G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_PATH,
					 g_param_spec_string ("path",
							      "path",
							      "path of the folder to list",
							      NULL,
							      G_PARAM_READWRITE |
							      G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (object_class, sizeof (GiggleGitListTreePriv));
}

static void
giggle_git_list_tree_init (GiggleGitListTree *list_tree)
{
	GiggleGitListTreePriv *priv;

	priv = GET_PRIV (list_tree);

	priv->files = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

GiggleJob *
giggle_git_list_tree_new (GiggleRevision *revision,
			  const char     *path)
{
	return g_object_new (GIGGLE_TYPE_GIT_LIST_TREE,
			     "revision", revision,
			     "path", path, NULL);
}

const char *
giggle_git_list_tree_get_sha (GiggleGitListTree *list_tree,
			      const char        *file)
{
	GiggleGitListTreePriv *priv;

	g_return_val_if_fail (GIGGLE_IS_GIT_LIST_TREE (list_tree), NULL);
	g_return_val_if_fail (NULL != file, NULL);

	priv = GET_PRIV (list_tree);

	return g_hash_table_lookup (priv->files, file);
}

GList *
giggle_git_list_tree_get_files (GiggleGitListTree *list_tree)
{
	GiggleGitListTreePriv *priv;

	g_return_val_if_fail (GIGGLE_IS_GIT_LIST_TREE (list_tree), NULL);

	priv = GET_PRIV (list_tree);

	return g_hash_table_get_keys (priv->files);
}