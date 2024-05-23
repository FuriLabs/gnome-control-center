/*
 * Copyright (C) 2024 Bardia Moshiri <fakeshell@bardia.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cc-fingerprint-panel.h"
#include "cc-fingerprint-resources.h"
#include "cc-util.h"

#include <adwaita.h>
#include <gio/gdesktopappinfo.h>
#include <glib/gi18n.h>
#include <gdk/gdk.h>

struct _CcFingerprintPanel {
  CcPanel            parent;
  GtkComboBoxText  *finger_select_combo;
  GtkWidget        *remove_finger_button;
  GtkWidget        *enroll_finger_button;
  GtkWidget        *enroll_status_label;
  GtkWidget        *identify_finger_button;
  GtkWidget        *identify_status_label;
  GtkBox           *show_list_box;
  GtkToggleButton  *show_enrolled_list;
  GtkToggleButton  *show_unenrolled_list;
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

gchar **
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
    "org.droidian.fingerprint",
    "/org/droidian/fingerprint",
    "org.droidian.fingerprint",
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

gboolean
remove_fingerprint (const gchar *finger)
{
  GDBusProxy *fpd_proxy;
  GError *error = NULL;
  GVariant *result;

  fpd_proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SYSTEM,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    "org.droidian.fingerprint",
    "/org/droidian/fingerprint",
    "org.droidian.fingerprint",
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
    if (remove_fingerprint (selected_finger))
      g_debug ("Successfully removed fingerprint: %s", selected_finger);
    else
      g_warning ("Failed to remove fingerprint: %s", selected_finger);

    g_free (selected_finger);
    refresh_enrolled_list (self);
  }
}

static gboolean
set_label_text (gpointer data)
{
  LabelTextData *label_text_data = (LabelTextData *) data;
  gchar *text_chomped = g_strchomp (g_strdup (label_text_data->text));
  gchar *text_with_percent = g_strconcat (text_chomped, "%", NULL);

  gtk_widget_set_visible (GTK_WIDGET (label_text_data->label), TRUE);
  gtk_label_set_text (GTK_LABEL (label_text_data->label), text_with_percent);

  g_free (text_with_percent);
  g_free (text_chomped);
  g_free (label_text_data->text);
  g_free (label_text_data);
  return G_SOURCE_REMOVE;
}

static gpointer
update_enroll_status_label (gpointer data)
{
  LabelTextData *label_text_data = (LabelTextData *) data;
  GtkWidget *label = label_text_data->label;
  FILE *file;
  char *line = NULL;
  size_t len = 0;
  ssize_t read;

  while (!label_text_data->stop) {
    while ((file = fopen ("/tmp/.enrollment_status", "r")) == NULL) {
      if (label_text_data->stop) return NULL;
      g_usleep (500 * 1000);
    }

    while ((read = getline (&line, &len, file)) != -1) {
      if (label_text_data->stop) {
        fclose (file);
        return NULL;
      }
    }

    if (line != NULL) {
      LabelTextData *new_label_text_data = g_new (LabelTextData, 1);
      new_label_text_data->label = label;
      new_label_text_data->text = g_strdup (line);
      g_idle_add ((GSourceFunc) set_label_text, new_label_text_data);
      free (line);
      line = NULL;
    }

    fclose (file);

    g_usleep (500 * 1000);
  }

  return NULL;
}

static gboolean
set_identify_label_text (gpointer data)
{
  LabelTextData *label_text_data = (LabelTextData *) data;
  gchar *text_chomped = g_strchomp(g_strdup (label_text_data->text));
  gchar *text_with_percent = g_strconcat (text_chomped, NULL);

  gtk_widget_set_visible (GTK_WIDGET (label_text_data->label), TRUE);
  gtk_label_set_text (GTK_LABEL (label_text_data->label), text_with_percent);

  g_free (text_with_percent);
  g_free (text_chomped);
  g_free (label_text_data->text);
  g_free (label_text_data);
  return G_SOURCE_REMOVE;
}

static gpointer
enroll_finger_thread (gpointer user_data)
{
  CcFingerprintPanel *self = (CcFingerprintPanel *) user_data;
  gchar *finger = gtk_combo_box_text_get_active_text (self->finger_select_combo);

  gtk_widget_set_sensitive (GTK_WIDGET (self->enroll_finger_button), FALSE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->finger_select_combo), FALSE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->show_unenrolled_list), FALSE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->show_enrolled_list), FALSE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->identify_finger_button), FALSE);

  LabelTextData *label_text_data = g_new0 (LabelTextData, 1);
  label_text_data->label = self->enroll_status_label;
  label_text_data->stop = FALSE;
  GThread *thread = g_thread_new ("update_label_thread", update_enroll_status_label, label_text_data);

  if (finger != NULL) {
    gchar *command = g_strdup_printf ("(rm -f /tmp/.enrollment_status; droidian-fpd-client enroll %s > /tmp/.enrollment_status 2>&1; echo 100 >> /tmp/.enrollment_status; systemctl restart --user fpdlistener)", finger);
    system (command);
    g_free (command);
    g_free (finger);
  }

  label_text_data->stop = TRUE;
  g_thread_join (thread);
  g_free (label_text_data);

  gtk_label_set_text (GTK_LABEL (self->enroll_status_label), "100%");

  refresh_unenrolled_list (self);

  gtk_widget_set_sensitive (GTK_WIDGET (self->enroll_finger_button), TRUE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->finger_select_combo), TRUE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->show_unenrolled_list), TRUE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->show_enrolled_list), TRUE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->identify_finger_button), TRUE);

  return NULL;
}

static void
cc_fingerprint_panel_enroll_finger (GtkButton *button, CcFingerprintPanel *self)
{
  g_thread_new (NULL, enroll_finger_thread, self);
}

static gboolean
enable_identify_button (gpointer user_data)
{
  CcFingerprintPanel *self = (CcFingerprintPanel *) user_data;
  gtk_widget_set_sensitive (GTK_WIDGET (self->identify_finger_button), TRUE);
  return FALSE;
}

static gpointer
identify_finger_thread (gpointer user_data)
{
  CcFingerprintPanel *self = (CcFingerprintPanel *) user_data;

  gchar *command = g_strdup_printf ("droidian-fpd-client identify");
  gchar *output = NULL;
  gchar *error_output = NULL;
  GError *error = NULL;

  if (!g_spawn_command_line_sync (command, &error_output, &output, NULL, &error)) {
    g_printerr ("Failed to execute command: %s", error->message);
    g_error_free (error);
  } else {
    LabelTextData *label_text_data = g_new (LabelTextData, 1);
    label_text_data->label = self->identify_status_label;
    label_text_data->text = g_strdup (output);
    g_idle_add ((GSourceFunc) set_identify_label_text, label_text_data);
  }

  if (output)
    g_free (output);

  if (error_output)
    g_free (error_output);

  g_free (command);

  g_idle_add ((GSourceFunc) enable_identify_button, self);

  return NULL;
}

static void
cc_fingerprint_panel_identify_finger (GtkButton *button, CcFingerprintPanel *self)
{
  gtk_widget_set_sensitive (GTK_WIDGET (self->identify_finger_button), FALSE);
  g_thread_new (NULL, identify_finger_thread, self);
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

  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        identify_status_label);
}

static void
cc_fingerprint_panel_init (CcFingerprintPanel *self)
{
  g_resources_register (cc_fingerprint_get_resource ());
  gtk_widget_init_template (GTK_WIDGET (self));

  system ("rm -f /tmp/.enrollment_status");

  if (g_file_test ("/usr/bin/droidian-fpd-client", G_FILE_TEST_EXISTS)) {
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
    gtk_label_set_text (GTK_LABEL (self->enroll_status_label), "");
  }
}

CcFingerprintPanel *
cc_fingerprint_panel_new (void)
{
  return CC_FINGERPRINT_PANEL (g_object_new (CC_TYPE_FINGERPRINT_PANEL, NULL));
}
