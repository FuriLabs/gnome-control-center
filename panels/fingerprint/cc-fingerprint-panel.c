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
  AdwToastOverlay  *toast_overlay;
  GtkComboBoxText  *finger_select_combo;
  GtkWidget        *remove_finger_button;
  GtkWidget        *enroll_finger_button;
  GtkWidget        *enroll_status_label;
  GtkWidget        *identify_finger_button;
  GtkWidget        *identify_status_label;
  GtkBox           *show_list_box;
  GtkToggleButton  *show_enrolled_list;
  GtkToggleButton  *show_unenrolled_list;
  gboolean         enrollment_done;
  gboolean         identification_done;
  gboolean         finger_canceled;
};

typedef struct {
    GtkWidget *label;
    gchar *text;
    gboolean stop;
} LabelTextData;

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

void
refresh_enrolled_list (CcFingerprintPanel *self)
{
  gchar **fpd_fingers = get_enrolled_fingers();
  if (!fpd_fingers)
    return;

  g_return_if_fail(self->finger_select_combo != NULL);

  gtk_combo_box_text_remove_all (self->finger_select_combo);

  for (int i = 0; fpd_fingers[i] != NULL; i++) {
    gchar *trimmed_finger = g_strstrip (fpd_fingers[i]);
    if (trimmed_finger && *trimmed_finger)
      gtk_combo_box_text_append_text (self->finger_select_combo, trimmed_finger);
  }

  if (gtk_combo_box_get_active (GTK_COMBO_BOX (self->finger_select_combo)) == -1)
    gtk_combo_box_set_active (GTK_COMBO_BOX (self->finger_select_combo), 0);

  gtk_widget_set_sensitive (GTK_WIDGET (self->remove_finger_button), gtk_combo_box_get_active (GTK_COMBO_BOX (self->finger_select_combo)) != -1);

  g_strfreev (fpd_fingers);
}

void
refresh_unenrolled_list (CcFingerprintPanel *self)
{
  gchar *fingers[] = {
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

  gchar **fpd_fingers = get_enrolled_fingers ();
  if (!fpd_fingers)
    return;

  gtk_combo_box_text_remove_all (self->finger_select_combo);

  for (int i = 0; fingers[i] != NULL; i++) {
    if (!g_strv_contains ((const gchar *const *) fpd_fingers, fingers[i]))
      gtk_combo_box_text_append_text (self->finger_select_combo, fingers[i]);
  }

  if (gtk_combo_box_get_active (GTK_COMBO_BOX (self->finger_select_combo)) == -1)
    gtk_combo_box_set_active (GTK_COMBO_BOX (self->finger_select_combo), 0);

  gtk_widget_set_sensitive (GTK_WIDGET (self->enroll_finger_button), gtk_combo_box_get_active (GTK_COMBO_BOX (self->finger_select_combo)) != -1);

  g_strfreev (fpd_fingers);
}

gboolean
on_show_enrolled_list_toggled (GtkToggleButton *togglebutton, gpointer user_data)
{
  CcFingerprintPanel *self = (CcFingerprintPanel *) user_data;

  refresh_enrolled_list (self);
  gtk_toggle_button_set_active (self->show_unenrolled_list, FALSE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->enroll_finger_button), FALSE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->remove_finger_button), TRUE);

  return TRUE;
}

gboolean
on_show_unenrolled_list_toggled (GtkToggleButton *togglebutton, gpointer user_data)
{
  CcFingerprintPanel *self = (CcFingerprintPanel *) user_data;

  refresh_unenrolled_list (self);
  gtk_toggle_button_set_active (self->show_enrolled_list, FALSE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->remove_finger_button), FALSE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->enroll_finger_button), TRUE);

  return TRUE;
}

static void
on_finger_select_changed (GtkComboBox *widget, CcFingerprintPanel *self)
{
  gboolean has_selection = gtk_combo_box_get_active (widget) != -1;
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->show_enrolled_list))) {
    gtk_widget_set_sensitive (GTK_WIDGET (self->remove_finger_button), has_selection);
    gtk_widget_set_sensitive (GTK_WIDGET (self->enroll_finger_button), FALSE);
  } else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->show_unenrolled_list))) {
    gtk_widget_set_sensitive (GTK_WIDGET (self->enroll_finger_button), has_selection);
    gtk_widget_set_sensitive (GTK_WIDGET (self->remove_finger_button), FALSE);
  }
}

static void
cc_fingerprint_panel_remove_finger (GtkButton *button, CcFingerprintPanel *self)
{
  gchar *selected_finger = gtk_combo_box_text_get_active_text (self->finger_select_combo);
  if (selected_finger) {
    if (remove_fingerprint (selected_finger)) {
      g_debug ("Successfully removed fingerprint: %s", selected_finger);
      show_toast (self, "Successfully removed fingerprint: %s", selected_finger);
      refresh_enrolled_list (self);
    } else {
      g_warning ("Failed to remove fingerprint: %s", selected_finger);
      show_toast (self, "Failed to remove fingerprint: %s", selected_finger);
    }

    g_free (selected_finger);
  }
}

static gboolean
set_label_text (gpointer data)
{
  LabelTextData *label_text_data = (LabelTextData *) data;
  gchar *text_chomped = g_strchomp (g_strdup (label_text_data->text));
  gchar *text_with_percent = g_strconcat (text_chomped, "%", NULL);

  if (text_with_percent) {
    gtk_widget_set_visible (GTK_WIDGET (label_text_data->label), TRUE);
    gtk_label_set_text (GTK_LABEL (label_text_data->label), text_with_percent);
    g_free (text_with_percent);
  }

  g_free (text_chomped);
  g_free (label_text_data->text);
  g_free (label_text_data);
  return G_SOURCE_REMOVE;
}

static void
handle_signal (GDBusProxy *proxy, gchar *sender_name, gchar *signal_name, GVariant *parameters, gpointer user_data)
{
  CcFingerprintPanel *self = (CcFingerprintPanel *) user_data;
  gint progress;
  gchar *info;

  if (g_strcmp0 (signal_name, "EnrollProgressChanged") == 0) {
    g_variant_get (parameters, "(i)", &progress);

    LabelTextData *label_text_data = g_new0 (LabelTextData, 1);
    label_text_data->label = self->enroll_status_label;
    label_text_data->text = g_strdup_printf ("%d", progress);
    g_idle_add (set_label_text, label_text_data);

    if (progress == 100) {
      g_usleep (500 * 1000); // without this it segfaults. why? i don't know!
      self->enrollment_done = TRUE;
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
      show_toast (self, "No space available for new fingerprint");
    else if (g_strcmp0 (info, "ERROR_HW_UNAVAILABLE") == 0)
      show_toast (self, "Fingerprint hardware is unavailable");
    else if (g_strcmp0 (info, "ERROR_UNABLE_TO_PROCESS") == 0)
      show_toast (self, "Unable to process fingerprint");
    else if (g_strcmp0 (info, "ERROR_TIMEOUT") == 0)
      show_toast (self, "Fingerprint operation timed out");
    else if (g_strcmp0 (info, "ERROR_CANCELED") == 0)
      show_toast (self, "Fingerprint operation was canceled");
    else if (g_strcmp0 (info, "FINGER_NOT_RECOGNIZED") == 0)
      show_toast (self, "Fingerprint is not recognized");
    else
      show_toast (self, "An error occurred with the fingerprint reader");

    self->finger_canceled = TRUE;
    self->enrollment_done = TRUE;
    g_free (info);
  }
}

static gpointer
enroll_finger_thread (gpointer user_data)
{
  CcFingerprintPanel *self = (CcFingerprintPanel *) user_data;
  gchar *finger = gtk_combo_box_text_get_active_text (self->finger_select_combo);
  GDBusProxy *proxy;
  GError *error = NULL;
  GVariant *result;

  gtk_widget_set_sensitive (GTK_WIDGET (self->enroll_finger_button), FALSE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->finger_select_combo), FALSE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->show_unenrolled_list), FALSE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->show_enrolled_list), FALSE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->identify_finger_button), FALSE);

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

  while (!self->enrollment_done)
    g_usleep (500 * 100);

  gtk_widget_set_sensitive (GTK_WIDGET (self->enroll_finger_button), TRUE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->finger_select_combo), TRUE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->show_unenrolled_list), TRUE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->show_enrolled_list), TRUE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->identify_finger_button), TRUE);

  if (self->finger_canceled) {
    g_usleep (500 * 100);
    gtk_label_set_text (GTK_LABEL (self->enroll_status_label), "");
  }

  // this won't update if we refresh too early as fpd database has not been updated yet
  g_usleep (500 * 100);
  refresh_unenrolled_list (self);

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

  gtk_widget_set_sensitive (GTK_WIDGET (self->identify_finger_button), FALSE);

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

  while (!self->identification_done)
    g_usleep (500 * 100);

  gtk_widget_set_sensitive (GTK_WIDGET (self->identify_finger_button), TRUE);

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
                                        show_list_box);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        show_unenrolled_list);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        show_enrolled_list);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        remove_finger_button);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        enroll_finger_button);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        enroll_status_label);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        finger_select_combo);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        identify_finger_button);
}

static void
cc_fingerprint_panel_init (CcFingerprintPanel *self)
{
  g_resources_register (cc_fingerprint_get_resource ());
  gtk_widget_init_template (GTK_WIDGET (self));

  if (ping_fpd ()) {
    g_signal_connect (G_OBJECT (self->remove_finger_button), "clicked", G_CALLBACK (cc_fingerprint_panel_remove_finger), self);
    g_signal_connect (G_OBJECT (self->enroll_finger_button), "clicked", G_CALLBACK (cc_fingerprint_panel_enroll_finger), self);

    gtk_widget_set_direction (GTK_WIDGET (self->show_list_box), GTK_TEXT_DIR_LTR);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (self->show_enrolled_list), TRUE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->enroll_finger_button), FALSE);

    g_signal_connect (GTK_TOGGLE_BUTTON (self->show_enrolled_list), "toggled", G_CALLBACK (on_show_enrolled_list_toggled), self);
    g_signal_connect (GTK_TOGGLE_BUTTON (self->show_unenrolled_list), "toggled", G_CALLBACK (on_show_unenrolled_list_toggled), self);
    g_signal_connect (G_OBJECT (self->identify_finger_button), "clicked", G_CALLBACK (cc_fingerprint_panel_identify_finger), self);
    g_signal_connect (GTK_COMBO_BOX (self->finger_select_combo), "changed", G_CALLBACK (on_finger_select_changed), self);

    refresh_enrolled_list (self);
  } else {
    gtk_widget_set_sensitive (GTK_WIDGET (self->remove_finger_button), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->finger_select_combo), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->enroll_finger_button), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->identify_finger_button), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->show_enrolled_list), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->show_unenrolled_list), FALSE);
    gtk_label_set_text (GTK_LABEL (self->enroll_status_label), "");
  }
}

CcFingerprintPanel *
cc_fingerprint_panel_new (void)
{
  return CC_FINGERPRINT_PANEL (g_object_new (CC_TYPE_FINGERPRINT_PANEL, NULL));
}
