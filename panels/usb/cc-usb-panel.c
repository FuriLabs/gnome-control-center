/*
 * Copyright (C) 2023 Bardia Moshiri <fakeshell@bardia.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cc-usb-panel.h"
#include "cc-usb-resources.h"
#include "cc-util.h"

#include <adwaita.h>
#include <gtk/gtk.h>
#include <gio/gdesktopappinfo.h>
#include <glib/gi18n.h>

struct _CcUsbPanel {
  CcPanel            parent;
  GtkWidget        *mtp_enabled_switch;
  GtkWidget        *cdrom_enabled_switch;
  GtkWidget        *iso_selection_switch;
  GtkWidget        *iso_label;
  GtkComboBoxText  *usb_state_dropdown;
  GtkWidget        *help_button;
  char             *path;
};

G_DEFINE_TYPE (CcUsbPanel, cc_usb_panel, CC_TYPE_PANEL)

static void
cc_usb_panel_finalize (GObject *object)
{
  G_OBJECT_CLASS (cc_usb_panel_parent_class)->finalize (object);
}

static void
cc_usb_panel_enable_mtp (GtkSwitch *widget, gboolean state, CcUsbPanel *self)
{
  if (state) {
    system ("rm -f ~/.mtp_disable");
    system ("systemctl --user start mtp-server");
  } else {
    system ("touch ~/.mtp_disable");
    system ("systemctl --user stop mtp-server");
  }

  gtk_switch_set_state (GTK_SWITCH (self->mtp_enabled_switch), state);
  gtk_switch_set_active (GTK_SWITCH (self->mtp_enabled_switch), state);
}

static gchar *
get_config_file_path ()
{
  if (g_file_test ("/usr/lib/droidian/device/mtp-configfs.conf", G_FILE_TEST_EXISTS))
    return "/usr/lib/droidian/device/mtp-configfs.conf";
  else if (g_file_test ("/etc/mtp-configfs.conf", G_FILE_TEST_EXISTS))
    return "/etc/mtp-configfs.conf";

  return NULL;
}

static void
cc_usb_panel_help_button_clicked (GtkButton *button, CcUsbPanel *self)
{
  const gchar *selected_mode = gtk_combo_box_get_active_id (GTK_COMBO_BOX (self->usb_state_dropdown));
  gchar *message;

  if (g_strcmp0 (selected_mode, "mtp") == 0)
    message = "MTP: Media Transfer Protocol, allows you to transfer files via a USB connection";
  else if (g_strcmp0 (selected_mode, "rndis") == 0)
    message = "RNDIS: Remote Network Driver Interface Specification, allows for USB networking and SSH over a USB connection";
  else
    message = "None: Disables all special USB functionalities.";

  GtkWidget *dialog = gtk_message_dialog_new (GTK_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (self), GTK_TYPE_WINDOW)), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", message);

  g_signal_connect (dialog, "response", G_CALLBACK (gtk_window_destroy), NULL);

  gtk_window_present (GTK_WINDOW (dialog));
}

static void
cc_usb_panel_usb_state_changed (GtkComboBox *widget, CcUsbPanel *self)
{
  const gchar *new_mode = gtk_combo_box_get_active_id (widget);
  GError *error = NULL;
  gchar *config_file_path = get_config_file_path ();

  if (new_mode && config_file_path) {
    gchar *contents;
    if (g_file_get_contents (config_file_path, &contents, NULL, &error)) {
      gchar **lines = g_strsplit (contents, "\n", -1);
      GString *new_contents = g_string_new ("");
      gboolean line_replaced = FALSE;

      for (gint i = 0; lines[i] != NULL; i++) {
        if (g_str_has_prefix (lines[i], "USBMODE=")) {
          g_string_append_printf (new_contents, "USBMODE=%s\n", new_mode);
          line_replaced = TRUE;
        } else {
          g_string_append_printf (new_contents, "%s\n", lines[i]);
        }
      }

      if (!line_replaced)
        g_string_append_printf (new_contents, "USBMODE=%s\n", new_mode);

      FILE *fp = fopen (config_file_path, "w");
      if (fp) {
        if (fwrite (new_contents->str, sizeof (char), strlen (new_contents->str), fp) < strlen (new_contents->str))
          g_printerr ("Failed to write to the file.");
        fclose (fp);
      } else {
        g_printerr ("Failed to open the file for writing.");
      }

      g_string_free (new_contents, TRUE);
      g_strfreev (lines);
      g_free (contents);
    } else {
      g_printerr ("Failed to read the file: %s", error->message);
      g_error_free (error);
    }
  } else {
    g_printerr ("Either no new mode specified or config file does not exist.");
  }
}

static void
cc_usb_panel_enable_cdrom (GtkSwitch *widget, gboolean state, CcUsbPanel *self)
{
  if (state) {
    g_debug ("Mounting cdrom: %s", self->path);
    gchar *argv[] = {"pkexec", "isodrive", self->path, "-cdrom", NULL};
    g_spawn_async (NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, NULL, NULL);
  } else {
    g_debug ("Unmounting cdrom");
    gchar *argv[] = {"pkexec", "isodrive", "umount", NULL};
    g_spawn_async (NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, NULL, NULL);
  }

  gtk_switch_set_state (GTK_SWITCH (self->cdrom_enabled_switch), state);
  gtk_switch_set_active (GTK_SWITCH (self->cdrom_enabled_switch), state);
}

static void
on_file_chosen (GtkFileChooserNative *native, gint response_id, CcUsbPanel *self)
{
  if (response_id == GTK_RESPONSE_ACCEPT) {
    GFile *file = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (native));
    char *path = g_file_get_path (file);
    if (file) {
      g_free (self->path);
      g_strchomp (path);
      char *basename = g_path_get_basename (path);
      self->path = g_strdup (path);
      gtk_widget_set_sensitive (GTK_WIDGET (self->cdrom_enabled_switch), TRUE);
      gtk_label_set_text (GTK_LABEL (self->iso_label), basename);
      g_free (path);
      g_free (basename);
    }
    g_object_unref (file);
  }

  gtk_native_dialog_destroy (GTK_NATIVE_DIALOG (native));
}

static void
cc_usb_panel_select_iso (GtkWidget *widget, CcUsbPanel *self)
{
  GtkFileChooserNative *native = gtk_file_chooser_native_new ("Choose an ISO",
                                                              GTK_WINDOW (gtk_widget_get_root (widget)),
                                                              GTK_FILE_CHOOSER_ACTION_OPEN,
                                                              "Open",
                                                              "Cancel");

  GtkFileFilter *filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, "ISO files");
  gtk_file_filter_add_mime_type (filter, "application/vnd.efi.iso");
  gtk_file_filter_add_mime_type (filter, "application/vnd.efi.img");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (native), filter);

  g_signal_connect (native, "response", G_CALLBACK (on_file_chosen), self);

  gtk_native_dialog_show (GTK_NATIVE_DIALOG (native));
}

static void
cc_usb_panel_class_init (CcUsbPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = cc_usb_panel_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/control-center/usb/cc-usb-panel.ui");

  gtk_widget_class_bind_template_child (widget_class,
                                        CcUsbPanel,
                                        mtp_enabled_switch);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcUsbPanel,
                                        cdrom_enabled_switch);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcUsbPanel,
                                        iso_selection_switch);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcUsbPanel,
                                        iso_label);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcUsbPanel,
                                        usb_state_dropdown);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcUsbPanel,
                                        help_button);
}

static void
cc_usb_panel_init (CcUsbPanel *self)
{
  g_resources_register (cc_usb_get_resource ());
  gtk_widget_init_template (GTK_WIDGET (self));

  gchar *config_file_path = get_config_file_path ();
  gboolean mtp_supported = g_file_test ("/usr/lib/droidian/device/mtp-supported", G_FILE_TEST_EXISTS);

  if (!mtp_supported || !config_file_path) {
    gtk_widget_set_sensitive (GTK_WIDGET (self->mtp_enabled_switch), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->usb_state_dropdown), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->help_button), FALSE);
  } else {
    if (g_file_test ("/usr/bin/mtp-server", G_FILE_TEST_EXISTS)) {
      g_signal_connect (G_OBJECT (self->mtp_enabled_switch), "state-set", G_CALLBACK (cc_usb_panel_enable_mtp), self);
      g_signal_connect (G_OBJECT (self->usb_state_dropdown), "changed", G_CALLBACK (cc_usb_panel_usb_state_changed), self);
      g_signal_connect (G_OBJECT (self->help_button), "clicked", G_CALLBACK (cc_usb_panel_help_button_clicked), self);

      gchar *mtp_output;
      gchar *mtp_error;
      gint mtp_exit_status;
      g_spawn_command_line_sync("systemctl --user is-active mtp-server", &mtp_output, &mtp_error, &mtp_exit_status, NULL);

      if (g_str_has_prefix (mtp_output, "active")) {
        g_signal_handlers_block_by_func (self->mtp_enabled_switch, cc_usb_panel_enable_mtp, self);
        gtk_switch_set_state (GTK_SWITCH (self->mtp_enabled_switch), TRUE);
        gtk_switch_set_active (GTK_SWITCH (self->mtp_enabled_switch), TRUE);
        g_signal_handlers_unblock_by_func (self->mtp_enabled_switch, cc_usb_panel_enable_mtp, self);
      } else {
        g_signal_handlers_block_by_func (self->mtp_enabled_switch, cc_usb_panel_enable_mtp, self);
        gtk_switch_set_state (GTK_SWITCH (self->mtp_enabled_switch), FALSE);
        gtk_switch_set_active (GTK_SWITCH (self->mtp_enabled_switch), FALSE);
        g_signal_handlers_unblock_by_func (self->mtp_enabled_switch, cc_usb_panel_enable_mtp, self);
      }

      g_free (mtp_output);
      g_free (mtp_error);
    } else {
      gtk_widget_set_sensitive (GTK_WIDGET (self->mtp_enabled_switch), FALSE);
    }

    gchar *config_contents;
    if (g_file_get_contents (config_file_path, &config_contents, NULL, NULL)) {
      gchar **lines = g_strsplit(config_contents, "\n", -1);
      for (gint i = 0; lines[i] != NULL; i++) {
        if (g_str_has_prefix (lines[i], "USBMODE=")) {
          gchar **tokens = g_strsplit (lines[i], "=", 2);
          if (tokens[1] != NULL)
            gtk_combo_box_set_active_id (GTK_COMBO_BOX (self->usb_state_dropdown), tokens[1]);

          g_strfreev (tokens);
          break;
        }
      }

      g_strfreev (lines);
      g_free (config_contents);
    }
  }

  gtk_widget_set_sensitive (GTK_WIDGET (self->cdrom_enabled_switch), FALSE);
  if (g_file_test ("/usr/bin/isodrive", G_FILE_TEST_EXISTS)) {
    g_signal_connect (G_OBJECT (self->cdrom_enabled_switch), "state-set", G_CALLBACK (cc_usb_panel_enable_cdrom), self);
    g_signal_connect (G_OBJECT (self->iso_selection_switch), "clicked", G_CALLBACK (cc_usb_panel_select_iso), self);
    if (g_file_test ("/sys/kernel/config/usb_gadget/g1/functions/mass_storage.0/lun.0/cdrom", G_FILE_TEST_EXISTS)) {
      gchar *content = NULL;
      if (g_file_get_contents ("/sys/kernel/config/usb_gadget/g1/functions/mass_storage.0/lun.0/cdrom", &content, NULL, NULL)) {
        int cdrom_status = atoi (content);
        g_free (content);
        if (cdrom_status == 1) {
          g_signal_handlers_block_by_func (self->cdrom_enabled_switch, cc_usb_panel_enable_cdrom, self);
          gtk_switch_set_state (GTK_SWITCH (self->cdrom_enabled_switch), TRUE);
          gtk_switch_set_active (GTK_SWITCH (self->cdrom_enabled_switch), TRUE);
          gtk_widget_set_sensitive (GTK_WIDGET (self->cdrom_enabled_switch), TRUE);
          g_signal_handlers_unblock_by_func (self->cdrom_enabled_switch, cc_usb_panel_enable_cdrom, self);
          if (g_file_test ("/sys/kernel/config/usb_gadget/g1/functions/mass_storage.0/lun.0/file", G_FILE_TEST_EXISTS)) {
            gchar *content = NULL;
            if (g_file_get_contents ("/sys/kernel/config/usb_gadget/g1/functions/mass_storage.0/lun.0/file", &content, NULL, NULL)) {
              g_strchomp (content);
              char *basename = g_path_get_basename (content);
              gtk_label_set_text (GTK_LABEL (self->iso_label), basename);
              self->path = g_strdup (basename);
              g_free (content);
              g_free (basename);
            }
          }
        }
      }
    }
  } else {
    gtk_widget_set_sensitive (GTK_WIDGET (self->iso_selection_switch), FALSE);
  }
}

CcUsbPanel *
cc_usb_panel_new (void)
{
  return CC_USB_PANEL (g_object_new (CC_TYPE_USB_PANEL, NULL));
}
