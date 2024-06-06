/*
 * Copyright (C) 2024 Bardia Moshiri <fakeshell@bardia.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cc-waydroid-panel.h"
#include "cc-waydroid-resources.h"
#include "cc-util.h"

#include <adwaita.h>
#include <gio/gdesktopappinfo.h>
#include <glib/gi18n.h>
#include <unistd.h>

#define WAYDROID_CONTAINER_DBUS_NAME          "id.waydro.Container"
#define WAYDROID_CONTAINER_DBUS_PATH          "/ContainerManager"
#define WAYDROID_CONTAINER_DBUS_INTERFACE     "id.waydro.ContainerManager"

#define WAYDROID_SESSION_DBUS_NAME          "id.waydro.Session"
#define WAYDROID_SESSION_DBUS_PATH          "/SessionManager"
#define WAYDROID_SESSION_DBUS_INTERFACE     "id.waydro.SessionManager"

#define LINE_SIZE 1024

struct _CcWaydroidPanel {
  CcPanel            parent;
  GtkWidget        *waydroid_enabled_switch;
  GtkWidget        *waydroid_autostart_switch;
  GtkWidget        *waydroid_shared_folder_switch;
  GtkWidget        *waydroid_ip_label;
  GtkWidget        *waydroid_vendor_label;
  GtkWidget        *waydroid_version_label;
  GtkWidget        *app_selector;
  GListStore       *app_list_store;
  GtkWidget        *launch_app_button;
  GtkWidget        *remove_app_button;
  GtkWidget        *install_app_button;
  GtkWidget        *store_button;
  GtkWidget        *refresh_app_list_button;
  GtkWidget        *waydroid_factory_reset;
};

typedef struct {
    CcWaydroidPanel *self;
    gchar **apps;
    gchar *pkgname;
    gchar *waydroid_ip_output;
    gchar *waydroid_vendor_output;
    gchar *waydroid_version_output;
} ThreadData;

G_DEFINE_TYPE (CcWaydroidPanel, cc_waydroid_panel, CC_TYPE_PANEL)

static void
cc_waydroid_panel_finalize (GObject *object)
{
  G_OBJECT_CLASS (cc_waydroid_panel_parent_class)->finalize (object);
}

gchar *
waydroid_get_state (void)
{
  GDBusProxy *waydroid_proxy;
  GError *error = NULL;
  GVariant *result;
  gchar *state = NULL;

  waydroid_proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SYSTEM,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    WAYDROID_CONTAINER_DBUS_NAME,
    WAYDROID_CONTAINER_DBUS_PATH,
    WAYDROID_CONTAINER_DBUS_INTERFACE,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error creating proxy: %s\n", error->message);
    g_clear_error (&error);
    return NULL;
  }

  result = g_dbus_proxy_call_sync(
    waydroid_proxy,
    "GetSession",
    NULL,
    G_DBUS_CALL_FLAGS_NONE,
    G_MAXINT,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error calling GetSession: %s\n", error->message);
    g_clear_error (&error);
  } else {
    GVariant *inner_dict;
    GVariantIter iter;
    gchar *key, *value;

    inner_dict = g_variant_get_child_value (result, 0);

    g_variant_iter_init (&iter, inner_dict);

    while (g_variant_iter_next (&iter, "{ss}", &key, &value)) {
      if (g_strcmp0 (key, "state") == 0) {
        state = g_strdup (value);
        g_free (key);
        g_free (value);
        break;
      }
      g_free (key);
      g_free (value);
    }

    g_variant_unref (inner_dict);
    g_variant_unref (result);
  }

  g_object_unref (waydroid_proxy);

  return state;
}

gboolean
waydroid_get_nfc_status (void)
{
  GDBusProxy *waydroid_proxy;
  GError *error = NULL;
  GVariant *result;
  gboolean nfc_status = FALSE;

  waydroid_proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SYSTEM,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    WAYDROID_CONTAINER_DBUS_NAME,
    WAYDROID_CONTAINER_DBUS_PATH,
    WAYDROID_CONTAINER_DBUS_INTERFACE,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error creating proxy: %s\n", error->message);
    g_clear_error (&error);
    return FALSE;
  }

  result = g_dbus_proxy_call_sync(
    waydroid_proxy,
    "GetNfcStatus",
    NULL,
    G_DBUS_CALL_FLAGS_NONE,
    G_MAXINT,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error calling GetNfcStatus: %s\n", error->message);
    g_clear_error (&error);
  } else {
    g_variant_get(result, "(b)", &nfc_status);
    g_variant_unref (result);
  }

  g_object_unref (waydroid_proxy);

  return nfc_status;
}

void
waydroid_toggle_nfc (void)
{
  GDBusProxy *waydroid_proxy;
  GError *error = NULL;

  waydroid_proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SYSTEM,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    WAYDROID_CONTAINER_DBUS_NAME,
    WAYDROID_CONTAINER_DBUS_PATH,
    WAYDROID_CONTAINER_DBUS_INTERFACE,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error creating proxy: %s\n", error->message);
    g_clear_error (&error);
    return;
  }

  g_dbus_proxy_call_sync(
    waydroid_proxy,
    "NfcToggle",
    NULL,
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error calling NfcToggle: %s\n", error->message);
    g_clear_error (&error);
  }

  g_object_unref (waydroid_proxy);
}

gchar *
waydroid_get_vendor (void)
{
  GDBusProxy *waydroid_proxy;
  GError *error = NULL;
  GVariant *result;
  gchar *vendor = NULL;

  waydroid_proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SESSION,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    WAYDROID_SESSION_DBUS_NAME,
    WAYDROID_SESSION_DBUS_PATH,
    WAYDROID_SESSION_DBUS_INTERFACE,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error creating proxy: %s\n", error->message);
    g_clear_error (&error);
    return NULL;
  }

  result = g_dbus_proxy_call_sync(
    waydroid_proxy,
    "VendorType",
    NULL,
    G_DBUS_CALL_FLAGS_NONE,
    G_MAXINT,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error calling VendorType: %s\n", error->message);
    g_clear_error (&error);
  } else {
    g_variant_get (result, "(s)", &vendor);
    g_variant_unref (result);
  }

  g_object_unref (waydroid_proxy);

  return vendor;
}

gchar *
waydroid_get_ip (void)
{
  GDBusProxy *waydroid_proxy;
  GError *error = NULL;
  GVariant *result;
  gchar *ip = NULL;

  waydroid_proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SESSION,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    WAYDROID_SESSION_DBUS_NAME,
    WAYDROID_SESSION_DBUS_PATH,
    WAYDROID_SESSION_DBUS_INTERFACE,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error creating proxy: %s\n", error->message);
    g_clear_error (&error);
    return NULL;
  }

  result = g_dbus_proxy_call_sync(
    waydroid_proxy,
    "IpAddress",
    NULL,
    G_DBUS_CALL_FLAGS_NONE,
    G_MAXINT,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error calling IpAddress: %s\n", error->message);
    g_clear_error (&error);
  } else {
    g_variant_get (result, "(s)", &ip);
    g_variant_unref (result);
  }

  g_object_unref (waydroid_proxy);

  return ip;
}

gchar *
waydroid_get_version (void)
{
  GDBusProxy *waydroid_proxy;
  GError *error = NULL;
  GVariant *result;
  gchar *version = NULL;

  waydroid_proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SESSION,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    WAYDROID_SESSION_DBUS_NAME,
    WAYDROID_SESSION_DBUS_PATH,
    WAYDROID_SESSION_DBUS_INTERFACE,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error creating proxy: %s\n", error->message);
    g_clear_error (&error);
    return NULL;
  }

  result = g_dbus_proxy_call_sync(
    waydroid_proxy,
    "LineageVersion",
    NULL,
    G_DBUS_CALL_FLAGS_NONE,
    G_MAXINT,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error calling LineageVersion: %s\n", error->message);
    g_clear_error (&error);
  } else {
    g_variant_get (result, "(s)", &version);
    g_variant_unref (result);
  }

  g_object_unref (waydroid_proxy);

  return version;
}

void
waydroid_mount_shared (void)
{
  GDBusProxy *waydroid_proxy;
  GError *error = NULL;

  waydroid_proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SYSTEM,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    WAYDROID_CONTAINER_DBUS_NAME,
    WAYDROID_CONTAINER_DBUS_PATH,
    WAYDROID_CONTAINER_DBUS_INTERFACE,
    NULL,
    &error
  );

  if (error) {
    g_print ("Error creating proxy: %s\n", error->message);
    g_clear_error (&error);
    return;
  }

  g_dbus_proxy_call_sync(
    waydroid_proxy,
    "MountSharedFolder",
    NULL,
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error calling MountSharedFolder: %s\n", error->message);
    g_clear_error (&error);
  }

  g_object_unref (waydroid_proxy);
}

void
waydroid_umount_shared (void)
{
  GDBusProxy *waydroid_proxy;
  GError *error = NULL;

  waydroid_proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SYSTEM,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    WAYDROID_CONTAINER_DBUS_NAME,
    WAYDROID_CONTAINER_DBUS_PATH,
    WAYDROID_CONTAINER_DBUS_INTERFACE,
    NULL,
    &error
  );

  if (error) {
    g_print ("Error creating proxy: %s\n", error->message);
    g_clear_error (&error);
    return;
  }

  g_dbus_proxy_call_sync(
    waydroid_proxy,
    "UnmountSharedFolder",
    NULL,
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error calling UnmountSharedFolder: %s\n", error->message);
    g_clear_error (&error);
  }

  g_object_unref (waydroid_proxy);
}

void
waydroid_remove_app (const gchar *package_name)
{
  GDBusProxy *waydroid_proxy;
  GError *error = NULL;

  waydroid_proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SESSION,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    WAYDROID_SESSION_DBUS_NAME,
    WAYDROID_SESSION_DBUS_PATH,
    WAYDROID_SESSION_DBUS_INTERFACE,
    NULL,
    &error
  );

  if (error) {
    g_print ("Error creating proxy: %s\n", error->message);
    g_clear_error (&error);
    return;
  }

  g_dbus_proxy_call_sync(
    waydroid_proxy,
    "RemoveApp",
    g_variant_new("(s)", package_name),
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error calling RemoveApp: %s\n", error->message);
    g_clear_error (&error);
  }

  g_object_unref (waydroid_proxy);
}

void
waydroid_install_app (const gchar *package_path)
{
  GDBusProxy *waydroid_proxy;
  GError *error = NULL;

  waydroid_proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SESSION,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    WAYDROID_SESSION_DBUS_NAME,
    WAYDROID_SESSION_DBUS_PATH,
    WAYDROID_SESSION_DBUS_INTERFACE,
    NULL,
    &error
  );

  if (error) {
    g_print ("Error creating proxy: %s\n", error->message);
    g_clear_error (&error);
    return;
  }

  g_dbus_proxy_call_sync(
    waydroid_proxy,
    "InstallApp",
    g_variant_new("(s)", package_path),
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error calling InstallApp: %s\n", error->message);
    g_clear_error (&error);
  }

  g_object_unref (waydroid_proxy);
}

int
is_mounted (const char *path)
{
  FILE *fp;
  char *line = NULL;
  size_t len = 0;
  int found = 0;

  fp = fopen ("/proc/mounts", "r");
  if (fp == NULL) {
    perror ("Failed to open /proc/mounts");
    return -1;
  }

  while (getline (&line, &len, fp) != -1) {
    char *mount_point;

    char *line_copy = strdup (line);
    if (line_copy == NULL) {
      perror ("Failed to allocate memory");
      free (line);
      fclose (fp);
      return -1;
    }

    strtok (line_copy, " ");
    mount_point = strtok (NULL, " ");

    if (mount_point != NULL && strcmp (mount_point, path) == 0) {
      found = 1;
      free (line_copy);
      break;
    }

    free (line_copy);
  }

  free (line);
  fclose (fp);

  return found;
}

static gboolean
child_stdout_callback (GIOChannel *channel, GIOCondition condition, gpointer data)
{
  gchar *string;
  gsize size;

  if (g_io_channel_read_line(channel, &string, &size, NULL, NULL) == G_IO_STATUS_NORMAL) {
    if (strstr (string, "Android with user 0 is ready") != NULL)
        g_io_channel_shutdown (channel, FALSE, NULL);
  }

  g_free (string);
  return TRUE;
}

static void
child_exited_callback (GPid pid, gint status, gpointer data)
{
  g_spawn_close_pid (pid);
}

static gboolean
update_ip_idle (gpointer user_data)
{
  ThreadData *data = (ThreadData *) user_data;

  gtk_label_set_text (GTK_LABEL (data->self->waydroid_ip_label), data->waydroid_ip_output);

  g_free (data->waydroid_ip_output);
  g_free (data);

  return G_SOURCE_REMOVE;
}

static gpointer
update_waydroid_ip (gpointer user_data)
{
  CcWaydroidPanel *self = (CcWaydroidPanel *) user_data;
  gchar *ip_address = NULL;

  ip_address = waydroid_get_ip ();

  ThreadData *data = g_new (ThreadData, 1);
  data->self = self;
  data->waydroid_ip_output = g_strdup (ip_address);

  g_idle_add (update_ip_idle, data);

  if (ip_address)
    g_free(ip_address);

  return NULL;
}

static void
update_waydroid_ip_threaded (CcWaydroidPanel *self)
{
  g_thread_new ("update_waydroid_ip", update_waydroid_ip, self);
}

static gboolean
update_app_list_idle (gpointer user_data)
{
  ThreadData *data = user_data;
  CcWaydroidPanel *self = data->self;
  gchar **apps = data->apps;

  GtkDropDown *drop_down = GTK_DROP_DOWN (self->app_selector);
  const char *initial_strings[] = { NULL };
  GtkStringList *list = gtk_string_list_new (initial_strings);

  for (gchar **app = apps; *app; app++) {
    if (*app[0] != '\0') {
      gtk_string_list_append (list, *app);
    }
  }

  gtk_drop_down_set_model (drop_down, G_LIST_MODEL (list));
  gtk_widget_set_sensitive (GTK_WIDGET (drop_down), TRUE);

  g_strfreev (apps);
  g_free (data);

  return G_SOURCE_REMOVE;
}

static gpointer
update_app_list (gpointer user_data)
{
  CcWaydroidPanel *self = (CcWaydroidPanel *) user_data;
  gchar *output, *error, **apps;
  gint exit_status;

  g_spawn_command_line_sync ("sh -c \"waydroid app list | awk -F': ' '/^Name:/ {print $2}'\"", &output, &error, &exit_status, NULL);

  if (exit_status != 0 || output == NULL || output[0] == '\0') {
    g_free (output);
    g_free (error);
    return NULL;
  }

  apps = g_strsplit_set (output, "\n", -1);

  ThreadData *data = g_new (ThreadData, 1);
  data->self = self;
  data->apps = apps;

  g_idle_add (update_app_list_idle, data);

  g_free (output);
  g_free (error);

  return NULL;
}

static void
update_app_list_threaded (CcWaydroidPanel *self)
{
  g_thread_new ("update_app_list", update_app_list, self);
}

static gchar*
get_selected_app_pkgname (CcWaydroidPanel *self)
{
  GtkStringObject *selected_obj = GTK_STRING_OBJECT (gtk_drop_down_get_selected_item (GTK_DROP_DOWN (self->app_selector)));
  if (selected_obj) {
    const gchar *selected_app = gtk_string_object_get_string (selected_obj);

    gchar *command, *output, *error;
    gint exit_status;

    command = g_strdup_printf("sh -c \"waydroid app list | awk -v app=\\\"%s\\\" '/Name:/ { name = substr(\\$0, index(\\$0, \\$2)); getline; if (name == app) print \\$2 }'\"", selected_app);

    g_spawn_command_line_sync (command, &output, &error, &exit_status, NULL);
    g_free (command);

    if (exit_status == 0 && output != NULL) {
      g_free (error);
      return output;
    }

    g_free (output);
    g_free (error);
  }

  return NULL;
}

static void
cc_waydroid_panel_uninstall_app (GtkWidget *widget, CcWaydroidPanel *self)
{
  gchar *pkgname = get_selected_app_pkgname (self);
  if (pkgname != NULL) {
    gchar *stripped_pkgname = g_strstrip (pkgname);
    waydroid_remove_app (stripped_pkgname);

    gtk_widget_set_sensitive (GTK_WIDGET (self->app_selector), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->remove_app_button), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->install_app_button), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->refresh_app_list_button), FALSE);

    g_timeout_add_seconds (5, (GSourceFunc) gtk_widget_set_sensitive, GTK_WIDGET (self->app_selector));
    g_timeout_add_seconds (5, (GSourceFunc) gtk_widget_set_sensitive, GTK_WIDGET (self->remove_app_button));
    g_timeout_add_seconds (5, (GSourceFunc) gtk_widget_set_sensitive, GTK_WIDGET (self->install_app_button));
    g_timeout_add_seconds (5, (GSourceFunc) gtk_widget_set_sensitive, GTK_WIDGET (self->refresh_app_list_button));

    update_app_list_threaded (self);
    g_free (pkgname);
  }
}

static gpointer
cc_waydroid_panel_launch_app (gpointer user_data)
{
  ThreadData *data = user_data;
  gchar *pkgname = data->pkgname;

  if (pkgname != NULL && g_strstrip (pkgname)[0] != '\0') {
    g_debug ("Launching Waydroid application: %s", pkgname);

    const gchar *home_dir = g_get_home_dir ();
    gchar *desktop_file_path = g_strdup_printf ("%s/.local/share/applications/waydroid.%s.desktop", home_dir, g_strstrip (pkgname));
    gchar *launch_command = g_strdup_printf ("dex \"%s\"", desktop_file_path);

    g_spawn_command_line_async (launch_command, NULL);

    g_free (launch_command);
    g_free (desktop_file_path);
  }

  g_free (pkgname);
  g_free (data);

  return NULL;
}

static void
cc_waydroid_panel_launch_app_threaded (GtkWidget *widget, CcWaydroidPanel *self)
{
  gchar *pkgname = get_selected_app_pkgname (self);
  ThreadData *data = g_new (ThreadData, 1);
  data->pkgname = pkgname;

  g_thread_new ("cc_waydroid_panel_launch_app", cc_waydroid_panel_launch_app, data);
}

static gboolean
update_vendor_idle (gpointer user_data)
{
  ThreadData *data = (ThreadData *) user_data;

  gtk_label_set_text (GTK_LABEL (data->self->waydroid_vendor_label), data->waydroid_vendor_output);

  g_free (data->waydroid_vendor_output);
  g_free (data);

  return G_SOURCE_REMOVE;
}

static gpointer
update_waydroid_vendor (gpointer user_data)
{
  CcWaydroidPanel *self = (CcWaydroidPanel *) user_data;
  gchar *vendor_info = NULL;

  vendor_info = waydroid_get_vendor ();

  ThreadData *data = g_new (ThreadData, 1);
  data->self = self;
  data->waydroid_vendor_output = g_strdup (vendor_info);

  g_idle_add (update_vendor_idle, data);

  if (vendor_info)
    g_free (vendor_info);

  return NULL;
}

static void
update_waydroid_vendor_threaded (CcWaydroidPanel *self)
{
  g_thread_new ("update_waydroid_vendor", update_waydroid_vendor, self);
}

static gboolean
update_version_idle (gpointer user_data)
{
  ThreadData *data = (ThreadData *) user_data;

  gtk_label_set_text (GTK_LABEL (data->self->waydroid_version_label), data->waydroid_version_output);

  g_free (data->waydroid_version_output);
  g_free (data);

  return G_SOURCE_REMOVE;
}

static gpointer
update_waydroid_version (gpointer user_data)
{
  CcWaydroidPanel *self = (CcWaydroidPanel *) user_data;
  gchar *version = NULL;

  version = waydroid_get_version ();

  ThreadData *data = g_new (ThreadData, 1);
  data->self = self;
  data->waydroid_version_output = g_strdup (version);

  g_idle_add (update_version_idle, data);

  if (version)
    g_free (version);

  return NULL;
}

static void
update_waydroid_version_threaded (CcWaydroidPanel *self)
{
  g_thread_new ("update_waydroid_version", update_waydroid_version, self);
}

static void
cc_waydroid_refresh_button (GtkButton *button, gpointer user_data)
{
  CcWaydroidPanel *self = CC_WAYDROID_PANEL (user_data);

  update_waydroid_ip_threaded (self);
  update_waydroid_vendor_threaded (self);
  update_waydroid_version_threaded (self);
  update_app_list_threaded (self);
}

static void
install_app (CcWaydroidPanel *self, GFile *file)
{
  gchar *file_path = g_file_get_path (file);
  waydroid_install_app (file_path);

  g_free (file_path);

  update_app_list_threaded (self);
}

static void
on_file_chosen (GtkFileChooserNative *native, gint response_id, CcWaydroidPanel *self)
{
  if (response_id == GTK_RESPONSE_ACCEPT) {
    GFile *file = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (native));
    if (file) {
      install_app (self, file);
      g_object_unref (file);
    }
  }

  gtk_native_dialog_destroy (GTK_NATIVE_DIALOG (native));
}

static void
cc_waydroid_panel_install_app (GtkWidget *widget, CcWaydroidPanel *self)
{
  GtkFileChooserNative *native = gtk_file_chooser_native_new ("Choose an APK",
                                                              GTK_WINDOW (gtk_widget_get_root (widget)),
                                                              GTK_FILE_CHOOSER_ACTION_OPEN,
                                                              "Open",
                                                              "Cancel");

  GtkFileFilter *filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, "APK files");
  gtk_file_filter_add_pattern (filter, "*.apk");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (native), filter);

  g_signal_connect (native, "response", G_CALLBACK (on_file_chosen), self);

  gtk_native_dialog_show (GTK_NATIVE_DIALOG (native));
}

static void
cc_waydroid_panel_open_store (GtkButton *button, gpointer user_data)
{
  const gchar *home_dir = g_get_home_dir ();
  gchar *desktop_file_path = g_strdup_printf ("%s/.local/share/applications/waydroid.org.fdroid.fdroid.desktop", home_dir);
  gchar *launch_command = g_strdup_printf ("dex \"%s\"", desktop_file_path);

  g_spawn_command_line_async (launch_command, NULL);

  g_free (launch_command);
  g_free (desktop_file_path);
}

static void
cc_waydroid_factory_reset_threaded (GtkWidget *widget, CcWaydroidPanel *self)
{
  GError *error = NULL;
  gchar *command = "rm -rf $HOME/.local/share/waydroid";
  gchar *home_env = g_strdup_printf ("HOME=%s", g_get_home_dir ());
  gchar *argv[] = {"pkexec", "env", home_env, "/bin/sh", "-c", command, NULL};

  if (!g_spawn_async (NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, NULL, &error)) {
    g_warning ("Error running command: %s", error->message);
    g_clear_error (&error);
  } else {
    gtk_widget_set_sensitive (GTK_WIDGET (self->waydroid_factory_reset), FALSE);
    g_timeout_add_seconds (10, (GSourceFunc) gtk_widget_set_sensitive, self->waydroid_factory_reset);
  }

  g_free (home_env);
}

static gboolean
reenable_switch_and_update_info (gpointer data)
{
  CcWaydroidPanel *self = (CcWaydroidPanel *) data;
  gtk_widget_set_sensitive (GTK_WIDGET (self->waydroid_enabled_switch), TRUE);
  update_waydroid_ip_threaded (self);
  update_waydroid_vendor_threaded (self);
  update_waydroid_version_threaded (self);

  gtk_widget_set_sensitive (GTK_WIDGET (self->launch_app_button), TRUE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->remove_app_button), TRUE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->install_app_button), TRUE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->app_selector), TRUE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->store_button), TRUE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->refresh_app_list_button), TRUE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->waydroid_factory_reset), FALSE);

  g_signal_connect (G_OBJECT (self->launch_app_button), "clicked", G_CALLBACK (cc_waydroid_panel_launch_app_threaded), self);
  g_signal_connect (G_OBJECT (self->remove_app_button), "clicked", G_CALLBACK (cc_waydroid_panel_uninstall_app), self);
  g_signal_connect (G_OBJECT (self->install_app_button), "clicked", G_CALLBACK (cc_waydroid_panel_install_app), self);
  g_signal_connect (G_OBJECT (self->store_button), "clicked", G_CALLBACK (cc_waydroid_panel_open_store), self);
  g_signal_connect (self->refresh_app_list_button, "clicked", G_CALLBACK (cc_waydroid_refresh_button), self);

  g_usleep (5000000);
  update_app_list_threaded (self);

  return G_SOURCE_REMOVE;
}

static gboolean
cc_waydroid_panel_enable_waydroid (GtkSwitch *widget, gboolean state, CcWaydroidPanel *self)
{
  GError *error = NULL;

  if (state) {
    gchar *argv[] = { "waydroid", "session", "start", NULL };
    GPid child_pid;
    gint stdout_fd;

    if (!g_spawn_async_with_pipes (NULL, argv, NULL,
                                   G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                                   NULL, NULL, &child_pid,
                                   NULL, &stdout_fd, NULL, &error)) {
      g_printerr ("Error: %s\n", error->message);
      g_error_free (error);
      return FALSE;
    }

    GIOChannel *stdout_channel = g_io_channel_unix_new (stdout_fd);
    g_io_add_watch (stdout_channel, G_IO_IN, (GIOFunc) child_stdout_callback, self);

    g_child_watch_add (child_pid, (GChildWatchFunc) child_exited_callback, NULL);

    gtk_widget_set_sensitive (GTK_WIDGET (self->waydroid_enabled_switch), FALSE);

    // we should find a way to query the container instead of waiting aimlessly, waydroid status isn't good enough either
    g_timeout_add_seconds (10, reenable_switch_and_update_info, self);
  } else {
    gchar *argv[] = { "waydroid", "session", "stop", NULL };
    gint exit_status = 0;

    if (!g_spawn_sync (NULL, argv, NULL,
                       G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
                       NULL, NULL, NULL, NULL, &exit_status, &error)) {
      g_printerr ("Error: %s\n", error->message);
      g_error_free (error);
    }

    update_waydroid_ip_threaded (self);
    update_waydroid_vendor_threaded (self);
    update_waydroid_version_threaded (self);

    gtk_label_set_text (GTK_LABEL (self->waydroid_vendor_label), "");
    gtk_label_set_text (GTK_LABEL (self->waydroid_version_label), "");

    GtkDropDown *drop_down = GTK_DROP_DOWN (self->app_selector);
    const char *empty_strings[] = { NULL };
    GtkStringList *empty_list = gtk_string_list_new (empty_strings);
    gtk_drop_down_set_model (drop_down, G_LIST_MODEL (empty_list));

    gtk_widget_set_sensitive (GTK_WIDGET (self->launch_app_button), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->remove_app_button), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->install_app_button), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->app_selector), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->store_button), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->refresh_app_list_button), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->waydroid_factory_reset), TRUE);
  }

  return FALSE;
}

static void
cc_waydroid_panel_autostart (GtkSwitch *widget, gboolean state, CcWaydroidPanel *self)
{
  if (state) {
    system ("touch ~/.android_enable");
    gtk_switch_set_state (GTK_SWITCH (self->waydroid_autostart_switch), TRUE);
    gtk_switch_set_active (GTK_SWITCH (self->waydroid_autostart_switch), TRUE);
  } else {
    system ("rm -f ~/.android_enable");
    gtk_switch_set_state (GTK_SWITCH (self->waydroid_autostart_switch), FALSE);
    gtk_switch_set_active (GTK_SWITCH (self->waydroid_autostart_switch), FALSE);
  }
}

static void
cc_waydroid_panel_shared_folder (GtkSwitch *widget, gboolean state, CcWaydroidPanel *self)
{
  if (state) {
    waydroid_mount_shared ();
    gtk_switch_set_state (GTK_SWITCH (self->waydroid_shared_folder_switch), TRUE);
    gtk_switch_set_active (GTK_SWITCH (self->waydroid_shared_folder_switch), TRUE);
  } else {
    waydroid_umount_shared ();
    gtk_switch_set_state (GTK_SWITCH (self->waydroid_shared_folder_switch), FALSE);
    gtk_switch_set_active (GTK_SWITCH (self->waydroid_shared_folder_switch), FALSE);
  }
}

static void
cc_waydroid_panel_class_init (CcWaydroidPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = cc_waydroid_panel_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/control-center/waydroid/cc-waydroid-panel.ui");

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        waydroid_enabled_switch);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        waydroid_autostart_switch);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        waydroid_shared_folder_switch);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        waydroid_ip_label);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        waydroid_vendor_label);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        waydroid_version_label);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        app_selector);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        launch_app_button);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        remove_app_button);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        install_app_button);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        store_button);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        refresh_app_list_button);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        waydroid_factory_reset);
}

static void
cc_waydroid_panel_init (CcWaydroidPanel *self)
{
  g_resources_register (cc_waydroid_get_resource ());
  gtk_widget_init_template (GTK_WIDGET (self));

  self->app_list_store = g_list_store_new (G_TYPE_APP_INFO);

  if (g_file_test ("/usr/bin/waydroid", G_FILE_TEST_EXISTS)) {
    g_signal_connect (G_OBJECT (self->waydroid_enabled_switch), "state-set", G_CALLBACK (cc_waydroid_panel_enable_waydroid), self);
    g_signal_connect (G_OBJECT (self->waydroid_autostart_switch), "state-set", G_CALLBACK (cc_waydroid_panel_autostart), self);
    g_signal_connect (G_OBJECT (self->waydroid_shared_folder_switch), "state-set", G_CALLBACK (cc_waydroid_panel_shared_folder), self);
    g_signal_connect (G_OBJECT (self->waydroid_factory_reset), "clicked", G_CALLBACK (cc_waydroid_factory_reset_threaded), self);

    gchar *file_path = g_build_filename (g_get_home_dir (), ".android_enable", NULL);
    if (g_file_test (file_path, G_FILE_TEST_EXISTS)) {
      g_signal_handlers_block_by_func (self->waydroid_autostart_switch, cc_waydroid_panel_autostart, self);
      gtk_switch_set_state (GTK_SWITCH (self->waydroid_autostart_switch), TRUE);
      gtk_switch_set_active (GTK_SWITCH (self->waydroid_autostart_switch), TRUE);
      g_signal_handlers_unblock_by_func (self->waydroid_autostart_switch, cc_waydroid_panel_autostart, self);
    } else {
      g_signal_handlers_block_by_func (self->waydroid_autostart_switch, cc_waydroid_panel_autostart, self);
      gtk_switch_set_state (GTK_SWITCH (self->waydroid_autostart_switch), FALSE);
      gtk_switch_set_active (GTK_SWITCH (self->waydroid_autostart_switch), FALSE);
      g_signal_handlers_unblock_by_func (self->waydroid_autostart_switch, cc_waydroid_panel_autostart, self);
    }

    g_free (file_path);

    gchar *current_state = waydroid_get_state ();

    if (current_state != NULL && g_strcmp0(current_state, "RUNNING") == 0) {
      g_signal_handlers_block_by_func (self->waydroid_enabled_switch, cc_waydroid_panel_enable_waydroid, self);
      gtk_switch_set_state (GTK_SWITCH (self->waydroid_enabled_switch), TRUE);
      gtk_switch_set_active (GTK_SWITCH (self->waydroid_enabled_switch), TRUE);
      g_signal_handlers_unblock_by_func (self->waydroid_enabled_switch, cc_waydroid_panel_enable_waydroid, self);

      gtk_widget_set_sensitive (GTK_WIDGET (self->waydroid_factory_reset), FALSE);

      g_signal_connect (G_OBJECT (self->launch_app_button), "clicked", G_CALLBACK (cc_waydroid_panel_launch_app_threaded), self);
      g_signal_connect (G_OBJECT (self->remove_app_button), "clicked", G_CALLBACK (cc_waydroid_panel_uninstall_app), self);
      g_signal_connect (G_OBJECT (self->install_app_button), "clicked", G_CALLBACK (cc_waydroid_panel_install_app), self);
      g_signal_connect (G_OBJECT (self->store_button), "clicked", G_CALLBACK (cc_waydroid_panel_open_store), self);
      g_signal_connect (self->refresh_app_list_button, "clicked", G_CALLBACK (cc_waydroid_refresh_button), self);

      const gchar *home_dir = g_get_home_dir();
      gchar *android_dir_path = g_strdup_printf("%s/Android", home_dir);
      if (is_mounted (android_dir_path)) {
        g_signal_handlers_block_by_func (self->waydroid_shared_folder_switch, cc_waydroid_panel_shared_folder, self);
        gtk_switch_set_state (GTK_SWITCH (self->waydroid_shared_folder_switch), TRUE);
        gtk_switch_set_active (GTK_SWITCH (self->waydroid_shared_folder_switch), TRUE);
        g_signal_handlers_unblock_by_func (self->waydroid_shared_folder_switch, cc_waydroid_panel_shared_folder, self);
      } else {
        g_signal_handlers_block_by_func (self->waydroid_shared_folder_switch, cc_waydroid_panel_shared_folder, self);
        gtk_switch_set_state (GTK_SWITCH (self->waydroid_shared_folder_switch), FALSE);
        gtk_switch_set_active (GTK_SWITCH (self->waydroid_shared_folder_switch), FALSE);
        g_signal_handlers_unblock_by_func (self->waydroid_shared_folder_switch, cc_waydroid_panel_shared_folder, self);
      }

      g_free (android_dir_path);

      update_waydroid_ip_threaded (self);
      update_waydroid_vendor_threaded (self);
      update_app_list_threaded (self);
      update_waydroid_version_threaded (self);
    } else {
      g_signal_handlers_block_by_func (self->waydroid_enabled_switch, cc_waydroid_panel_enable_waydroid, self);
      gtk_switch_set_state (GTK_SWITCH (self->waydroid_enabled_switch), FALSE);
      gtk_switch_set_active (GTK_SWITCH (self->waydroid_enabled_switch), FALSE);
      g_signal_handlers_unblock_by_func (self->waydroid_enabled_switch, cc_waydroid_panel_enable_waydroid, self);
      gtk_label_set_text (GTK_LABEL (self->waydroid_vendor_label), "");
      gtk_label_set_text (GTK_LABEL (self->waydroid_version_label), "");
      gtk_widget_set_sensitive (GTK_WIDGET (self->launch_app_button), FALSE);
      gtk_widget_set_sensitive (GTK_WIDGET (self->remove_app_button), FALSE);
      gtk_widget_set_sensitive (GTK_WIDGET (self->install_app_button), FALSE);
      gtk_widget_set_sensitive (GTK_WIDGET (self->app_selector), FALSE);
      gtk_widget_set_sensitive (GTK_WIDGET (self->store_button), FALSE);
      gtk_widget_set_sensitive (GTK_WIDGET (self->refresh_app_list_button), FALSE);
    }

    g_free (current_state);
  } else {
    gtk_switch_set_state (GTK_SWITCH (self->waydroid_enabled_switch), FALSE);
    gtk_switch_set_active (GTK_SWITCH (self->waydroid_enabled_switch), FALSE);
    gtk_widget_set_sensitive (self->waydroid_enabled_switch, FALSE);
    gtk_widget_set_sensitive (self->waydroid_autostart_switch, FALSE);
    gtk_widget_set_sensitive (self->waydroid_shared_folder_switch, FALSE);
    gtk_label_set_text (GTK_LABEL (self->waydroid_vendor_label), "");
    gtk_label_set_text (GTK_LABEL (self->waydroid_version_label), "");
    gtk_widget_set_sensitive (GTK_WIDGET (self->launch_app_button), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->remove_app_button), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->install_app_button), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->app_selector), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->store_button), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->refresh_app_list_button), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->waydroid_factory_reset), FALSE);
  }
}

CcWaydroidPanel *
cc_waydroid_panel_new (void)
{
  return CC_WAYDROID_PANEL (g_object_new (CC_TYPE_WAYDROID_PANEL, NULL));
}
