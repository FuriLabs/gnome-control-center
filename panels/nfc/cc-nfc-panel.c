/*
 * Copyright (C) 2024 Bardia Moshiri <fakeshell@bardia.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cc-nfc-panel.h"
#include "cc-nfc-resources.h"
#include "cc-util.h"

#include <adwaita.h>
#include <gio/gdesktopappinfo.h>
#include <glib/gi18n.h>

struct _CcNfcPanel {
  CcPanel            parent;
  GtkWidget        *nfc_enabled_switch;
};

G_DEFINE_TYPE (CcNfcPanel, cc_nfc_panel, CC_TYPE_PANEL)

static void
cc_nfc_panel_finalize (GObject *object)
{
  G_OBJECT_CLASS (cc_nfc_panel_parent_class)->finalize (object);
}

static gboolean
cc_nfc_panel_enable_nfc (GtkSwitch *widget, gboolean state, CcNfcPanel *self)
{
  GError *error = NULL;

  const gchar *home_dir = g_get_home_dir ();
  gchar *filepath = g_strdup_printf ("%s/.nfc_disable", home_dir);

  g_signal_handlers_block_by_func (self->nfc_enabled_switch, cc_nfc_panel_enable_nfc, self);

  if (state) {
    if (unlink (filepath) != 0)
        g_printerr ("Error deleting ~/.nfc_disable");

    g_spawn_command_line_sync ("systemctl start nfcd", NULL, NULL, NULL, &error);
  } else {
    FILE *file = fopen (filepath, "w");
    if (file != NULL)
        fclose (file);
    else
        g_printerr ("Error creating ~/.nfc_disable");

    g_spawn_command_line_sync ("systemctl stop nfcd", NULL, NULL, NULL, &error);
  }

  gtk_switch_set_state (GTK_SWITCH (self->nfc_enabled_switch), state);
  gtk_switch_set_active (GTK_SWITCH (self->nfc_enabled_switch), state);

  g_signal_handlers_unblock_by_func (self->nfc_enabled_switch, cc_nfc_panel_enable_nfc, self);

  g_free (filepath);

  if (error != NULL) {
    g_printerr ("Failed to toggle NFC service: %s\n", error->message);
    g_error_free (error);

    return FALSE;
  }

  return TRUE;
}

static void
cc_nfc_panel_class_init (CcNfcPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = cc_nfc_panel_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/control-center/nfc/cc-nfc-panel.ui");
  gtk_widget_class_bind_template_child (widget_class,
                                        CcNfcPanel,
                                        nfc_enabled_switch);
}

static void
cc_nfc_panel_init (CcNfcPanel *self)
{
  g_resources_register (cc_nfc_get_resource ());
  gtk_widget_init_template (GTK_WIDGET (self));

  gint exit_status;
  g_spawn_command_line_sync ("systemctl --no-pager --quiet is-failed nfcd", NULL, NULL, &exit_status, NULL);

  if (g_file_test ("/usr/sbin/nfcd", G_FILE_TEST_EXISTS)) {
    if (exit_status == 0) {
      gtk_widget_set_sensitive (GTK_WIDGET (self->nfc_enabled_switch), FALSE);
    } else {
      g_signal_connect (G_OBJECT (self->nfc_enabled_switch), "state-set", G_CALLBACK (cc_nfc_panel_enable_nfc), self);

      // Check if nfcd is running
      gint nfc_exit_status;
      g_spawn_command_line_sync ("systemctl is-active -q nfcd", NULL, NULL, &nfc_exit_status, NULL);

      // If the nfcd is active, set the switch to ON
      if (nfc_exit_status == 0) {
        g_signal_handlers_block_by_func (self->nfc_enabled_switch, cc_nfc_panel_enable_nfc, self);
        gtk_switch_set_state (GTK_SWITCH (self->nfc_enabled_switch), TRUE);
        gtk_switch_set_active (GTK_SWITCH (self->nfc_enabled_switch), TRUE);
        g_signal_handlers_unblock_by_func (self->nfc_enabled_switch, cc_nfc_panel_enable_nfc, self);
      } else {
        g_signal_handlers_block_by_func (self->nfc_enabled_switch, cc_nfc_panel_enable_nfc, self);
        gtk_switch_set_state (GTK_SWITCH (self->nfc_enabled_switch), FALSE);
        gtk_switch_set_active (GTK_SWITCH (self->nfc_enabled_switch), FALSE);
        g_signal_handlers_unblock_by_func (self->nfc_enabled_switch, cc_nfc_panel_enable_nfc, self);
      }
    }
  } else
    gtk_widget_set_sensitive (GTK_WIDGET (self->nfc_enabled_switch), FALSE);
}

CcNfcPanel *
cc_nfc_panel_new (void)
{
  return CC_NFC_PANEL (g_object_new (CC_TYPE_NFC_PANEL, NULL));
}
