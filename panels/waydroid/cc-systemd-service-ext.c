/*
 * Copyright (C) 2024 FuriLabs

 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Authors: Bardia Moshiri <bardia@furilabs.com>
 */

#include "cc-systemd-service-ext.h"

/* this is an extension of systemd-service from panels/nfc/ */

gboolean
cc_mask_user_service (const char *service)
{
  const gchar *home_dir = g_get_home_dir ();
  gchar *config_dir = g_build_filename (home_dir, ".config", "systemd", "user", NULL);
  gchar *service_path = g_build_filename (config_dir, service, NULL);

  g_mkdir_with_parents (config_dir, 0755);
  chmod (config_dir, 0755);

  GFile *null_file = g_file_new_for_path ("/dev/null");
  GFile *service_file = g_file_new_for_path (service_path);

  GError *error = NULL;
  gboolean success = g_file_make_symbolic_link (service_file, "/dev/null", NULL, &error);
  if (!success) {
    g_warning ("Failed to create symlink: %s", error->message);
    g_error_free (error);
  }

  g_object_unref (null_file);
  g_object_unref (service_file);
  g_free (config_dir);
  g_free (service_path);

  return success;
}

gboolean
cc_unmask_user_service (const char *service)
{
  const gchar *home_dir = g_get_home_dir ();
  gchar *service_path = g_build_filename (home_dir, ".config", "systemd", "user", service, NULL);

  GFile *service_file = g_file_new_for_path (service_path);

  GError *error = NULL;
  gboolean success = g_file_delete (service_file, NULL, &error);

  if (!success) {
    if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
      success = TRUE;
    else
      g_warning ("Failed to delete symlink: %s", error->message);
    g_error_free (error);
  }

  g_object_unref (service_file);
  g_free (service_path);

  return success;
}
