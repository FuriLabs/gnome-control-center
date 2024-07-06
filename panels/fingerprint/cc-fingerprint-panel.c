/*
 * Copyright (C) 2024 Bardia Moshiri <fakeshell@bardia.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cc-fingerprint-panel.h"
#include "cc-fingerprint-resources.h"
#include "cc-util.h"

#include <adwaita.h>

#define FPD_DBUS_NAME         "org.droidian.fingerprint"
#define FPD_DBUS_PATH         "/org/droidian/fingerprint"
#define FPD_DBUS_INTERFACE    "org.droidian.fingerprint"

struct _CcFingerprintPanel {
  CcPanel            parent;
  AdwToastOverlay   *toast_overlay;
  GtkImage          *fingerprint_image;
  GtkProgressBar    *enroll_progress;
  AdwComboRow       *finger_select_row;
  GtkButton         *remove_finger_button;
  GtkButton         *enroll_finger_button;
  GtkButton         *identify_finger_button;
  GtkToggleButton   *show_enrolled_list;
  GtkToggleButton   *show_unenrolled_list;
  gboolean           enrollment_done;
  gboolean           identification_done;
  gboolean           finger_canceled;
  gboolean           sensitive;
};

G_DEFINE_TYPE (CcFingerprintPanel, cc_fingerprint_panel, CC_TYPE_PANEL)

static void
cc_fingerprint_panel_finalize (GObject *object)
{
  G_OBJECT_CLASS (cc_fingerprint_panel_parent_class)->finalize (object);
}

static void
show_toast (CcFingerprintPanel *self, const char *format, ...)
{
  va_list args;
  char *message;
  AdwToast *toast;

  va_start (args, format);
  message = g_strdup_vprintf (format, args);
  va_end (args);

  toast = adw_toast_new (message);
  adw_toast_set_timeout (toast, 3);

  adw_toast_overlay_add_toast (self->toast_overlay, toast);

  g_free (message);
}

// TODO: maybe do this a bit better?
static gboolean
set_ui_sensitivity (gpointer user_data)
{
  CcFingerprintPanel *self = CC_FINGERPRINT_PANEL (user_data);
  gtk_widget_set_sensitive (GTK_WIDGET (self->enroll_finger_button), self->sensitive);
  gtk_widget_set_sensitive (GTK_WIDGET (self->finger_select_row), self->sensitive);
  gtk_widget_set_sensitive (GTK_WIDGET (self->remove_finger_button), self->sensitive);
  gtk_widget_set_sensitive (GTK_WIDGET (self->identify_finger_button), self->sensitive);
  gtk_widget_set_sensitive (GTK_WIDGET (self->show_enrolled_list), self->sensitive);
  gtk_widget_set_sensitive (GTK_WIDGET (self->show_unenrolled_list), self->sensitive);

  return G_SOURCE_REMOVE;
}

static gchar **
get_enrolled_fingers (void)
{
  GDBusProxy *fpd_proxy;
  GError *error = NULL;
  GVariant *result;
  gchar **fpd_fingers = NULL;

  fpd_proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SYSTEM,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    FPD_DBUS_NAME,
    FPD_DBUS_PATH,
    FPD_DBUS_INTERFACE,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error creating proxy: %s\n", error->message);
    g_clear_error (&error);
    return NULL;
  }

  result = g_dbus_proxy_call_sync(
    fpd_proxy,
    "GetAll",
    NULL,
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error calling GetAll: %s\n", error->message);
    g_clear_error (&error);
    g_object_unref (fpd_proxy);
    return NULL;
  } else {
    GVariant *fpd_list;
    fpd_list = g_variant_get_child_value (result, 0);
    fpd_fingers = g_variant_dup_strv (fpd_list, NULL);
    g_variant_unref (fpd_list);
    g_variant_unref (result);
  }

  g_object_unref (fpd_proxy);
  return fpd_fingers;
}

static gboolean
remove_fingerprint (const gchar *finger)
{
  GDBusProxy *fpd_proxy;
  GError *error = NULL;
  GVariant *result;

  fpd_proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SYSTEM,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    FPD_DBUS_NAME,
    FPD_DBUS_PATH,
    FPD_DBUS_INTERFACE,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error creating proxy: %s\n", error->message);
    g_clear_error (&error);
    return FALSE;
  }

  result = g_dbus_proxy_call_sync(
    fpd_proxy,
    "Remove",
    g_variant_new ("(s)", finger),
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error calling Remove: %s\n", error->message);
    g_clear_error (&error);
    g_object_unref (fpd_proxy);
    return FALSE;
  }

  g_variant_unref (result);
  g_object_unref (fpd_proxy);
  return TRUE;
}

static void
refresh_fingerprint_list (CcFingerprintPanel *self)
{
  gchar *all_fingers[] = {
    "right-index-finger",
    "left-index-finger",
    "right-thumb",
    "right-middle-finger",
    "right-ring-finger",
    "right-little-finger",
    "left-thumb",
    "left-middle-finger",
    "left-ring-finger",
    "left-little-finger",
    NULL
  };

  gchar **enrolled_fingers = get_enrolled_fingers ();
  GtkStringList *string_list = gtk_string_list_new (NULL);

  gboolean show_enrolled = gtk_toggle_button_get_active (self->show_enrolled_list);

  gboolean has_items = FALSE;
  for (int i = 0; all_fingers[i] != NULL; i++) {
    gboolean is_enrolled = g_strv_contains ((const gchar *const *) enrolled_fingers, all_fingers[i]);
    if ((show_enrolled && is_enrolled) || (!show_enrolled && !is_enrolled)) {
      gtk_string_list_append (string_list, all_fingers[i]);
      has_items = TRUE;
    }
  }

  adw_combo_row_set_model (self->finger_select_row, NULL);

  if (has_items) {
    adw_combo_row_set_model (self->finger_select_row, G_LIST_MODEL (string_list));
    adw_combo_row_set_selected (self->finger_select_row, 0);
  } else {
    adw_combo_row_set_model (self->finger_select_row, G_LIST_MODEL (string_list));
    adw_combo_row_set_selected (self->finger_select_row, GTK_INVALID_LIST_POSITION);
  }

  gboolean has_enrolled_fingers = (enrolled_fingers != NULL && enrolled_fingers[0] != NULL);
  gtk_widget_set_sensitive (GTK_WIDGET (self->remove_finger_button), show_enrolled && has_items);
  gtk_widget_set_sensitive (GTK_WIDGET (self->enroll_finger_button), !show_enrolled && has_items);
  gtk_widget_set_sensitive (GTK_WIDGET (self->identify_finger_button), has_enrolled_fingers);

  g_strfreev (enrolled_fingers);
}

static void
on_show_enrolled_list_toggled (GtkToggleButton *togglebutton, CcFingerprintPanel *self)
{
  gtk_toggle_button_set_active (self->show_unenrolled_list, !gtk_toggle_button_get_active (togglebutton));
  refresh_fingerprint_list (self);
}

static void
on_show_unenrolled_list_toggled (GtkToggleButton *togglebutton, CcFingerprintPanel *self)
{
  gtk_toggle_button_set_active (self->show_enrolled_list, !gtk_toggle_button_get_active (togglebutton));
  refresh_fingerprint_list (self);
}

static gchar *
get_finger_at_index (CcFingerprintPanel *self, guint index)
{
  GListModel *model = adw_combo_row_get_model (self->finger_select_row);
  if (!model) {
    g_warning ("No model found for finger_select_row");
    return NULL;
  }

  GtkStringObject *string_object = g_list_model_get_item (model, index);
  if (!string_object) {
    g_warning ("No item found at index %u", index);
    return NULL;
  }

  const gchar *str = gtk_string_object_get_string (string_object);
  if (!str) {
    g_warning ("Empty string found at index %u", index);
    g_object_unref (string_object);
    return NULL;
  }

  gchar *result = g_strdup (str);
  g_object_unref (string_object);
  return result;
}

static void
on_finger_select_changed (AdwComboRow *combo_row, GParamSpec *pspec, CcFingerprintPanel *self)
{
  guint selected_index = adw_combo_row_get_selected (self->finger_select_row);

  if (selected_index == GTK_INVALID_LIST_POSITION) {
    gtk_widget_set_sensitive (GTK_WIDGET (self->remove_finger_button), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->enroll_finger_button), FALSE);
    return;
  }

  gchar *selected_finger = get_finger_at_index (self, selected_index);
  if (selected_finger == NULL) {
    gtk_widget_set_sensitive (GTK_WIDGET (self->remove_finger_button), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->enroll_finger_button), FALSE);
    g_free(selected_finger);
    return;
  }

  gchar **enrolled_fingers = get_enrolled_fingers ();
  gboolean is_enrolled = g_strv_contains ((const gchar *const *) enrolled_fingers, selected_finger);

  gtk_widget_set_sensitive (GTK_WIDGET (self->remove_finger_button), is_enrolled);
  gtk_widget_set_sensitive (GTK_WIDGET (self->enroll_finger_button), !is_enrolled);

  g_free (selected_finger);
  g_strfreev (enrolled_fingers);
}

static void
cc_fingerprint_panel_remove_finger (GtkButton *button, CcFingerprintPanel *self)
{
  guint selected_index = adw_combo_row_get_selected (self->finger_select_row);
  gchar *selected_finger = get_finger_at_index (self, selected_index);
  if (selected_finger) {
    if (remove_fingerprint (selected_finger)) {
      g_debug ("Successfully removed fingerprint: %s", selected_finger);
      show_toast (self, "Successfully removed fingerprint");
      refresh_fingerprint_list (self);
    } else {
      g_warning ("Failed to remove fingerprint: %s", selected_finger);
      show_toast (self, "Failed to remove fingerprint");
    }

    g_free (selected_finger);
  }
}

static void
handle_signal (GDBusProxy *proxy, gchar *sender_name, gchar *signal_name, GVariant *parameters, gpointer user_data)
{
  CcFingerprintPanel *self = (CcFingerprintPanel *) user_data;
  gint progress;
  gchar *info;

  if (g_strcmp0 (signal_name, "EnrollProgressChanged") == 0) {
    g_variant_get (parameters, "(i)", &progress);

    gtk_widget_set_visible (GTK_WIDGET (self->enroll_progress), TRUE);
    gtk_progress_bar_set_fraction (self->enroll_progress, progress / 100.0);

    g_debug ("Enrollment percentage: %d", progress);
    if (progress == 100) {
      g_usleep (500 * 1000);
      self->enrollment_done = TRUE;
      gtk_widget_set_visible (GTK_WIDGET (self->enroll_progress), FALSE);
    }
  } else if (g_strcmp0 (signal_name, "Identified") == 0) {
    g_variant_get (parameters, "(s)", &info);
    g_debug ("%s received: %s", signal_name, info);
    self->identification_done = TRUE;
    show_toast (self, "Identified finger: %s", info);
    g_free (info);
  } else if (g_strcmp0 (signal_name, "StateChanged") == 0) {
    if (g_strcmp0 (info, "FPSTATE_IDLE") == 0)
      g_usleep (500 * 1000); // wait for fpd to update
  } else if (g_strcmp0 (signal_name, "ErrorInfo") == 0) {
    g_variant_get (parameters, "(s)", &info);
    g_debug ("%s received: %s", signal_name, info);

    if (g_strcmp0 (info, "ERROR_NO_SPACE") == 0)
      show_toast (self, "No space available for new fingerprints");
    else if (g_strcmp0 (info, "ERROR_HW_UNAVAILABLE") == 0)
      show_toast (self, "Fingerprint hardware is unavailable");
    else if (g_strcmp0 (info, "ERROR_UNABLE_TO_PROCESS") == 0)
      show_toast (self, "Unable to process fingerprint");
    else if (g_strcmp0 (info, "ERROR_TIMEOUT") == 0)
      show_toast (self, "Fingerprint operation timed out");
    else if (g_strcmp0 (info, "ERROR_CANCELED") == 0)
      show_toast (self, "Fingerprint operation was canceled");
    else if (g_strcmp0 (info, "ERROR_UNABLE_TO_REMOVE") == 0)
      show_toast (self, "Unable to remove the fingerprint");
    else if (g_strcmp0 (info, "FINGER_NOT_RECOGNIZED") == 0)
      show_toast (self, "Fingerprint is not recognized");
    else
      show_toast (self, "An error occurred with the fingerprint reader");

    self->finger_canceled = TRUE;
    self->enrollment_done = TRUE;
    g_free (info);
  } else if (g_strcmp0 (signal_name, "AcquisitionInfo") == 0) {
    g_variant_get (parameters, "(s)", &info);
    g_debug ("%s received: %s", signal_name, info);
    if (g_strcmp0 (info, "FPACQUIRED_PARTIAL") == 0)
      show_toast (self, "Partial fingerprint detected. Please try again");
    else if (g_strcmp0 (info, "FPACQUIRED_IMAGER_DIRTY") == 0)
      show_toast (self, "The sensor is dirty. Please clean and try again");
    else if (g_strcmp0 (info, " FPACQUIRED_TOO_FAST") == 0)
      show_toast (self, "Finger moved too fast. Please try again");
    else if (g_strcmp0 (info, " FPACQUIRED_TOO_SLOW") == 0)
      show_toast (self, "Finger moved too slow. Please try again");
    else if (g_strcmp0 (info, " FPACQUIRED_INSUFFICIENT") == 0)
      show_toast (self, "Couldn't process fingerprint. Please try again");

    g_free (info);
  }
}

static gpointer
enroll_finger_thread (gpointer user_data)
{
  CcFingerprintPanel *self = (CcFingerprintPanel *) user_data;
  GDBusProxy *proxy;
  GError *error = NULL;
  GVariant *result;
  guint selected_index = adw_combo_row_get_selected (self->finger_select_row);
  gchar *finger = get_finger_at_index (self, selected_index);

  proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SYSTEM,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    FPD_DBUS_NAME,
    FPD_DBUS_PATH,
    FPD_DBUS_INTERFACE,
    NULL,
    &error
  );

  if (error) {
    g_warning ("Error creating proxy: %s\n", error->message);
    g_clear_error (&error);
    return NULL;
  }

  g_debug ("Enrolling %s", finger);
  g_signal_connect (proxy, "g-signal", G_CALLBACK (handle_signal), self);

  if (finger != NULL) {
    result = g_dbus_proxy_call_sync(
      proxy,
      "Enroll",
      g_variant_new ("(s)", finger),
      G_DBUS_CALL_FLAGS_NONE,
      -1,
      NULL,
      &error
    );

    if (error) {
      g_warning ("Error calling Enroll: %s\n", error->message);
      g_clear_error (&error);
      g_object_unref (proxy);
      g_free (finger);
      return NULL;
    }

    g_variant_unref (result);
    g_free (finger);
  }

  self->sensitive = FALSE;
  g_idle_add (set_ui_sensitivity, self);

  while (!self->enrollment_done)
    g_usleep (500 * 100);

  self->sensitive = TRUE;
  g_idle_add (set_ui_sensitivity, self);

  if (self->finger_canceled) {
    g_usleep (500 * 100);
    gtk_widget_set_visible (GTK_WIDGET (self->enroll_progress), FALSE);
  }

  // this won't update if we refresh too early as fpd database has not been updated yet
  g_usleep (500 * 100);
  refresh_fingerprint_list (self);

  g_object_unref (proxy);

  return NULL;
}

static void
cc_fingerprint_panel_enroll_finger (GtkButton *button, CcFingerprintPanel *self)
{
  self->enrollment_done = FALSE;
  self->finger_canceled = FALSE;
  g_thread_new (NULL, enroll_finger_thread, self);
}

static gpointer
identify_finger_thread (gpointer user_data)
{
  CcFingerprintPanel *self = (CcFingerprintPanel *) user_data;
  GError *error = NULL;
  GVariant *result;
  GDBusProxy *proxy;

  proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SYSTEM,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    FPD_DBUS_NAME,
    FPD_DBUS_PATH,
    FPD_DBUS_INTERFACE,
    NULL,
    &error
  );

  if (error) {
    g_warning ("Error creating proxy: %s\n", error->message);
    g_clear_error (&error);
    return NULL;
  }

  g_signal_connect (proxy, "g-signal", G_CALLBACK (handle_signal), self);

  result = g_dbus_proxy_call_sync(
    proxy,
    "Identify",
    NULL,
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    &error
  );

  if (error) {
    g_warning ("Error calling Identify: %s\n", error->message);
    g_clear_error (&error);
    g_object_unref (proxy);
    return NULL;
  }

  g_variant_unref(result);


  self->sensitive = FALSE;
  g_idle_add (set_ui_sensitivity, self);

  while (!self->identification_done)
    g_usleep (500 * 100);

  self->sensitive = TRUE;
  g_idle_add (set_ui_sensitivity, self);

  g_object_unref (proxy);

  return NULL;
}

static void
cc_fingerprint_panel_identify_finger (GtkButton *button, CcFingerprintPanel *self)
{
  self->identification_done = FALSE;
  g_thread_new (NULL, identify_finger_thread, self);
}

static gboolean
ping_fpd (void)
{
  GDBusProxy *proxy;
  GError *error = NULL;
  GVariant *result;

  proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SYSTEM,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    FPD_DBUS_NAME,
    FPD_DBUS_PATH,
    "org.freedesktop.DBus.Peer",
    NULL,
    &error
  );

  if (error) {
    g_warning ("Error creating proxy: %s\n", error->message);
    g_clear_error (&error);
    return FALSE;
  }

  result = g_dbus_proxy_call_sync(
    proxy,
    "Ping",
    NULL,
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    &error
  );

  g_object_unref (proxy);

  if (error) {
    g_warning ("Error calling Ping: %s\n", error->message);
    g_clear_error (&error);
    return FALSE;
  }

  g_variant_unref (result);
  return TRUE;
}

// TODO: get rid of these
static gboolean
refresh_fingerprint_list_thread (gpointer data)
{
  CcFingerprintPanel *self = CC_FINGERPRINT_PANEL (data);
  refresh_fingerprint_list (self);
  return G_SOURCE_REMOVE;
}

static gpointer
queue_refresh_fingerprint_list_task (gpointer data)
{
  CcFingerprintPanel *self = CC_FINGERPRINT_PANEL (data);
  g_usleep (100000);
  g_idle_add (refresh_fingerprint_list_thread, self);
  return NULL;
}

static void
cc_fingerprint_panel_class_init (CcFingerprintPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = cc_fingerprint_panel_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/control-center/fingerprint/cc-fingerprint-panel.ui");

  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        toast_overlay);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        enroll_progress);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        finger_select_row);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        remove_finger_button);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        enroll_finger_button);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        identify_finger_button);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        show_enrolled_list);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        show_unenrolled_list);
}

static void
cc_fingerprint_panel_init (CcFingerprintPanel *self)
{
  g_resources_register (cc_fingerprint_get_resource ());
  gtk_widget_init_template (GTK_WIDGET (self));

  if (ping_fpd ()) {
    g_signal_connect (G_OBJECT (self->remove_finger_button), "clicked", G_CALLBACK (cc_fingerprint_panel_remove_finger), self);
    g_signal_connect (G_OBJECT (self->enroll_finger_button), "clicked", G_CALLBACK (cc_fingerprint_panel_enroll_finger), self);
    g_signal_connect (G_OBJECT (self->identify_finger_button), "clicked", G_CALLBACK (cc_fingerprint_panel_identify_finger), self);
    g_signal_connect (self->finger_select_row, "notify::selected", G_CALLBACK (on_finger_select_changed), self);
    g_signal_connect (self->show_enrolled_list, "toggled", G_CALLBACK (on_show_enrolled_list_toggled), self);
    g_signal_connect (self->show_unenrolled_list, "toggled", G_CALLBACK (on_show_unenrolled_list_toggled), self);

    gtk_toggle_button_set_active (self->show_enrolled_list, TRUE);

    GThread *thread = g_thread_new ("refresh-fingerprint-list", queue_refresh_fingerprint_list_task, self);
    g_thread_unref (thread);
  } else {
    gtk_widget_set_sensitive (GTK_WIDGET (self->finger_select_row), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->remove_finger_button), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->enroll_finger_button), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->identify_finger_button), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->show_enrolled_list), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->show_unenrolled_list), FALSE);
  }
}

CcFingerprintPanel *
cc_fingerprint_panel_new (void)
{
  return CC_FINGERPRINT_PANEL (g_object_new (CC_TYPE_FINGERPRINT_PANEL, NULL));
}
