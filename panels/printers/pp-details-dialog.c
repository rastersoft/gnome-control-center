/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright 2016  Red Hat, Inc,
 *
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Felipe Borges <feborges@redhat.com>
 */

#include "config.h"

#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <cups/cups.h>
#include <cups/ppd.h>

#include "cc-editable-entry.h"
#include "pp-details-dialog.h"
#include "pp-ppd-selection-dialog.h"
#include "pp-utils.h"

struct _PpDetailsDialog {
  GtkBuilder *builder;
  GtkWidget  *parent;

  GtkWidget  *dialog;

  UserResponseCallback user_callback;
  gpointer             user_data;

  gchar        *printer_name;
  gchar        *ppd_file_name;
  PPDList      *all_ppds_list;
  GCancellable *get_all_ppds_cancellable;

  /* Dialogs */
  PpPPDSelectionDialog *pp_ppd_selection_dialog;
};

static gboolean
printer_name_edit_cb (GtkWidget *entry,
                      GdkEventFocus *event,
                      PpDetailsDialog *dialog)
{
  const gchar *new_name;

  // FIXME: update the header bar title

  new_name = gtk_entry_get_text (GTK_ENTRY (entry));

  printer_rename (dialog->printer_name, new_name); // FIXME: it has to be async

  dialog->printer_name = g_strdup (new_name);

  return FALSE;
}

static gboolean
printer_location_edit_cb (GtkWidget *entry,
                          GdkEventFocus *event,
                          PpDetailsDialog *dialog)
{
  const gchar             *location;

  location = gtk_entry_get_text (GTK_ENTRY (entry));

  // FIXME: do this async
  // FIXME: actualize printers list
  printer_set_location (dialog->printer_name, location);

  return FALSE;
}

static void
search_for_drivers (GtkButton       *button,
                    PpDetailsDialog *dialog)
{
  g_print ("search_for_drivers\n");
}

static void
set_ppd_cb (gchar    *printer_name,
            gboolean  success,
            gpointer  user_data)
{
  PpDetailsDialog *dialog = (PpDetailsDialog*) user_data;
  GtkWidget *widget;

  widget = (GtkWidget *) gtk_builder_get_object (dialog->builder, "printer-model-label");
  gtk_label_set_text (GTK_LABEL (widget), dialog->ppd_file_name);
}

static void
ppd_selection_dialog_response_cb (GtkDialog *dialog,
                                  gint       response_id,
                                  gpointer   user_data)
{
  PpDetailsDialog *self = (PpDetailsDialog*) user_data;

  if (response_id == GTK_RESPONSE_OK)
    {
      gchar *ppd_name;

      ppd_name = pp_ppd_selection_dialog_get_ppd_name (self->pp_ppd_selection_dialog);

      if (self->printer_name && ppd_name)
        {
          GCancellable *cancellable;

          cancellable = g_cancellable_new ();

          printer_set_ppd_async (self->printer_name,
                                 ppd_name,
                                 cancellable,
                                 set_ppd_cb,
                                 self);

          self->ppd_file_name = g_strdup (ppd_name);
        }

      g_free (ppd_name);
    }

  pp_ppd_selection_dialog_free (self->pp_ppd_selection_dialog);
  self->pp_ppd_selection_dialog = NULL;
}

static void
select_ppd_in_dialog (GtkButton       *button,
                      PpDetailsDialog *self)
{
  gchar                  *device_id = NULL;
  gchar                  *manufacturer = NULL;

  self->ppd_file_name = g_strdup (cupsGetPPD (self->printer_name));

  if (!self->pp_ppd_selection_dialog)
    {
      device_id =
        get_ppd_attribute (self->ppd_file_name,
                           "1284DeviceID");

      if (device_id)
        {
          manufacturer = get_tag_value (device_id, "mfg");
          if (!manufacturer)
            manufacturer = get_tag_value (device_id, "manufacturer");
          }

        if (manufacturer == NULL)
          {
            manufacturer =
              get_ppd_attribute (self->ppd_file_name,
                                 "Manufacturer");
          }

        if (manufacturer == NULL)
          {
            manufacturer = g_strdup ("Raw");
          }

      self->pp_ppd_selection_dialog = pp_ppd_selection_dialog_new (
        GTK_WINDOW (gtk_widget_get_toplevel (self->dialog)),
        NULL,
        manufacturer,
        ppd_selection_dialog_response_cb,
        self);

      g_free (manufacturer);
      g_free (device_id);
    }
}

static void
select_ppd_manually (GtkButton       *button,
                     PpDetailsDialog *self)
{
  GtkFileFilter          *filter;
  GtkWidget              *dialog;

  dialog = gtk_file_chooser_dialog_new (_("Select PPD File"),
                                        GTK_WINDOW (self->dialog),
                                        GTK_FILE_CHOOSER_ACTION_OPEN,
                                        _("_Cancel"), GTK_RESPONSE_CANCEL,
                                        _("_Open"), GTK_RESPONSE_ACCEPT,
                                        NULL);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter,
    _("PostScript Printer Description files (*.ppd, *.PPD, *.ppd.gz, *.PPD.gz, *.PPD.GZ)"));
  gtk_file_filter_add_pattern (filter, "*.ppd");
  gtk_file_filter_add_pattern (filter, "*.PPD");
  gtk_file_filter_add_pattern (filter, "*.ppd.gz");
  gtk_file_filter_add_pattern (filter, "*.PPD.gz");
  gtk_file_filter_add_pattern (filter, "*.PPD.GZ");

  gtk_file_chooser_set_filter (GTK_FILE_CHOOSER (dialog), filter);

  if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT)
    {
      gchar *ppd_filename;

      ppd_filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));

      if (self->printer_name && ppd_filename)
        {
          GCancellable *cancellable;

          cancellable = g_cancellable_new ();

          printer_set_ppd_file_async (self->printer_name,
                                      ppd_filename,
                                      cancellable,
                                      set_ppd_cb,
                                      self);
        }

      g_free (ppd_filename);
    }

  gtk_widget_destroy (dialog);
}

static void
get_all_ppds_async_cb (PPDList  *ppds,
                       gpointer  user_data)
{
  PpDetailsDialog *self = user_data;

  self->all_ppds_list = ppds;

  if (self->pp_ppd_selection_dialog)
    pp_ppd_selection_dialog_set_ppd_list (self->pp_ppd_selection_dialog,
                                          self->all_ppds_list);

  g_object_unref (self->get_all_ppds_cancellable);
  self->get_all_ppds_cancellable = NULL;
}

PpDetailsDialog *
pp_details_dialog_new (GtkWindow            *parent,
                       UserResponseCallback  user_callback,
                       gpointer              user_data,
                       gchar                *printer_name,
                       gchar                *printer_location,
                       gchar                *printer_address,
                       gchar                *printer_make_and_model,
                       gboolean              sensitive)
{
  PpDetailsDialog *dialog;
  GtkWidget       *widget;
  GError          *error = NULL;
  gchar           *objects[] = { "details-dialog", NULL };
  gchar           *title;
  gchar           *printer_url;
  guint            builder_result;

  dialog = g_new0 (PpDetailsDialog, 1);

  dialog->builder = gtk_builder_new ();
  dialog->parent = GTK_WIDGET (parent);

  builder_result = gtk_builder_add_objects_from_resource (dialog->builder,
                                                          "/org/gnome/control-center/printers/details-dialog.ui",
                                                          objects, &error);
  if (builder_result == 0)
    {
      g_warning ("Could not load ui: %s", error->message);
      g_error_free (error);
      return NULL;
    }

  dialog->dialog = (GtkWidget *) gtk_builder_get_object (dialog->builder, "details-dialog");
  dialog->user_callback = user_callback;
  dialog->user_data = user_data;
  dialog->printer_name = g_strdup (printer_name);
  dialog->ppd_file_name = NULL;

  title = g_strdup_printf (C_("Printer Details dialog title", "%s Details"), printer_name);
  gtk_window_set_title (GTK_WINDOW (dialog->dialog), title);

  widget = (GtkWidget *) gtk_builder_get_object (dialog->builder, "printer-address-label");
  printer_url = g_strdup_printf ("<a href=\"http://%s\">%s</a>", printer_address, printer_address);
  gtk_label_set_markup (GTK_LABEL (widget), printer_url);
  g_free (printer_url);

  /* connect signals */
  widget = (GtkWidget *) gtk_builder_get_object (dialog->builder, "printer-name-entry");
  gtk_entry_set_text (GTK_ENTRY (widget), printer_name);
  g_signal_connect (widget, "focus-out-event", G_CALLBACK (printer_name_edit_cb), dialog);

  widget = (GtkWidget *) gtk_builder_get_object (dialog->builder, "printer-location-entry");
  gtk_entry_set_text (GTK_ENTRY (widget), printer_location);
  g_signal_connect (widget, "focus-out-event", G_CALLBACK (printer_location_edit_cb), dialog);

  widget = (GtkWidget *) gtk_builder_get_object (dialog->builder, "printer-model-label");
  gtk_label_set_text (GTK_LABEL (widget), printer_make_and_model);

  widget = (GtkWidget *) gtk_builder_get_object (dialog->builder, "search-for-drivers-button");
  g_signal_connect (widget, "clicked", G_CALLBACK (search_for_drivers), dialog);

  widget = (GtkWidget *) gtk_builder_get_object (dialog->builder, "select-from-database-button");
  g_signal_connect (widget, "clicked", G_CALLBACK (select_ppd_in_dialog), dialog);

  widget = (GtkWidget *) gtk_builder_get_object (dialog->builder, "install-ppd-button");
  g_signal_connect (widget, "clicked", G_CALLBACK (select_ppd_manually), dialog);

  gtk_window_set_transient_for (GTK_WINDOW (dialog->dialog), GTK_WINDOW (parent));
  gtk_widget_show_all (GTK_WIDGET (dialog->dialog));

  if (dialog->all_ppds_list == NULL)
    {
      dialog->get_all_ppds_cancellable = g_cancellable_new ();
      get_all_ppds_async (dialog->get_all_ppds_cancellable, get_all_ppds_async_cb, dialog);
    }

  return dialog;
}

void
pp_details_dialog_free (PpDetailsDialog *dialog)
{
  gtk_widget_destroy (GTK_WIDGET (dialog->dialog));
  dialog->dialog = NULL;

  g_object_unref (dialog->builder);
  dialog->builder = NULL;

  g_free (dialog->printer_name);
  dialog->printer_name = NULL;

  if (dialog->all_ppds_list)
    {
      ppd_list_free (dialog->all_ppds_list);
      dialog->all_ppds_list = NULL;
    }

  if (dialog->get_all_ppds_cancellable)
    {
      g_cancellable_cancel (dialog->get_all_ppds_cancellable);
      g_object_unref (dialog->get_all_ppds_cancellable);
      dialog->get_all_ppds_cancellable = NULL;
    }

  g_free (dialog);
}
