/*
 * Copyright 2016  Red Hat, Inc
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

#include <config.h>

#include "pp-printer-entry.h"
#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#include "pp-details-dialog.h"
#include "pp-options-dialog.h"
#include "pp-jobs-dialog.h"
#include "pp-utils.h"

#define SUPPLY_BAR_HEIGHT 12

struct _PpPrinterEntry
{
  GtkBox parent;

  gchar    *printer_uri;
  gchar    *printer_name;
  gchar    *ppd_file_name;
  int       num_jobs;
  gboolean  is_accepting_jobs;
  gchar    *printer_make_and_model;
  gchar    *printer_location;
  gchar    *printer_hostname;
  gboolean  is_authorized;

  /* Widgets */
  GtkImage       *printer_icon;
  GtkLabel       *printer_status;
  GtkLabel       *printer_name_label;
  GtkLabel       *printer_model_label;
  GtkLabel       *printer_model;
  GtkLabel       *printer_location_label;
  GtkLabel       *printer_location_address_label;
  GtkDrawingArea *supply_drawing_area;
  GtkWidget      *show_jobs_dialog_button;
  GtkCheckButton *printer_default_checkbutton;
  GtkModelButton *set_default_printer_menuitem;
  GtkModelButton *remove_printer_menuitem;

  /* Dialogs */
  PpDetailsDialog *pp_details_dialog;
  PpOptionsDialog *pp_options_dialog;
  PpJobsDialog    *pp_jobs_dialog;
};

struct _PpPrinterEntryClass
{
  GtkBoxClass parent_class;

  void (*printer_changed) (PpPrinterEntry *printer_entry);
};

G_DEFINE_TYPE (PpPrinterEntry, pp_printer_entry, GTK_TYPE_BOX)

enum {
  IS_DEFAULT_PRINTER,
  LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL] = { 0 };

static void
pp_printer_entry_init (PpPrinterEntry *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

typedef struct {
  gchar *color;
  gchar *type;
  gchar *name;
  gint   level;
} MarkerItem;

static gint
markers_cmp (gconstpointer a,
             gconstpointer b)
{
  MarkerItem *x = (MarkerItem*) a;
  MarkerItem *y = (MarkerItem*) b;

  if (x->level < y->level)
    return 1;
  else if (x->level == y->level)
    return 0;
  else
    return -1;
}

static gchar *
sanitize_printer_model (gchar *printer_make_and_model)
{
  gchar  *breakpoint = NULL, *tmp = NULL, *tmp2 = NULL;
  gchar  *printer_model = NULL;
  gchar   backup;
  size_t  length = 0;
  gchar  *forbiden[] = {
    "foomatic",
    ",",
    "hpijs",
    "hpcups",
    "(recommended)",
    "postscript (recommended)",
    NULL };
  int     i;

  tmp = g_ascii_strdown (printer_make_and_model, -1);

  for (i = 0; i < g_strv_length (forbiden); i++)
    {
      tmp2 = g_strrstr (tmp, forbiden[i]);
      if (breakpoint == NULL ||
         (tmp2 != NULL && tmp2 < breakpoint))
           breakpoint = tmp2;
    }

    if (breakpoint)
      {
        backup = *breakpoint;
        *breakpoint = '\0';
        length = strlen (tmp);
        *breakpoint = backup;
        g_free (tmp);

        if (length > 0)
          printer_model = g_strndup (printer_make_and_model, length);
      }
    else
      printer_model = g_strdup (printer_make_and_model);

  return printer_model;
}

static gboolean
supply_levels_draw_cb (GtkWidget   *widget,
                       cairo_t     *cr,
                       InkLevelData *inklevel)
{
  GtkStyleContext        *context;
  gchar                  *tooltip_text = NULL;
  gint                    width;
  gint                    height;
  int                     i;

  context = gtk_widget_get_style_context (widget);

  width = gtk_widget_get_allocated_width (widget);
  height = gtk_widget_get_allocated_height (widget);

  gtk_render_background (context, cr, 0, 0, width, height);

  if (inklevel->marker_levels && inklevel->marker_colors && inklevel->marker_names && inklevel->marker_types)
    {
      GSList   *markers = NULL;
      GSList   *tmp_list = NULL;
      gchar   **marker_levelsv = NULL;
      gchar   **marker_colorsv = NULL;
      gchar   **marker_namesv = NULL;
      gchar   **marker_typesv = NULL;
      gchar    *tmp = NULL;

      gtk_style_context_save (context);

      marker_levelsv = g_strsplit (inklevel->marker_levels, ",", -1);
      marker_colorsv = g_strsplit (inklevel->marker_colors, ",", -1);
      marker_namesv = g_strsplit (inklevel->marker_names, ",", -1);
      marker_typesv = g_strsplit (inklevel->marker_types, ",", -1);

      if (g_strv_length (marker_levelsv) == g_strv_length (marker_colorsv) &&
          g_strv_length (marker_colorsv) == g_strv_length (marker_namesv) &&
          g_strv_length (marker_namesv) == g_strv_length (marker_typesv))
        {
          for (i = 0; i < g_strv_length (marker_levelsv); i++)
            {
              MarkerItem *marker;

              if (g_strcmp0 (marker_typesv[i], "ink") == 0 ||
                  g_strcmp0 (marker_typesv[i], "toner") == 0 ||
                  g_strcmp0 (marker_typesv[i], "inkCartridge") == 0 ||
                  g_strcmp0 (marker_typesv[i], "tonerCartridge") == 0)
                {
                  marker = g_new0 (MarkerItem, 1);
                  marker->type = g_strdup (marker_typesv[i]);
                  marker->name = g_strdup (marker_namesv[i]);
                  marker->color = g_strdup (marker_colorsv[i]);
                  marker->level = atoi (marker_levelsv[i]);

                  markers = g_slist_prepend (markers, marker);
                }
            }

            markers = g_slist_sort (markers, markers_cmp);

            for (tmp_list = markers; tmp_list; tmp_list = tmp_list->next)
              {
                GdkRGBA color = {0.0, 0.0, 0.0, 1.0};
                double  display_value;
                int     value;

                value = ((MarkerItem*) tmp_list->data)->level;

                gdk_rgba_parse (&color, ((MarkerItem*) tmp_list->data)->color);

                if (value > 0)
                  {
                    display_value = value / 100.0 * (width - 3.0);
                    gdk_cairo_set_source_rgba (cr, &color);
		    cairo_rectangle (cr, 3.5, 3.5, display_value, SUPPLY_BAR_HEIGHT - 3.0);
                    cairo_fill (cr);
                  }

                if (tooltip_text)
                  {
                    tmp = g_strdup_printf ("%s\n%s",
                                           tooltip_text,
                                           ((MarkerItem*) tmp_list->data)->name);
                    g_free (tooltip_text);
                    tooltip_text = tmp;
                    tmp = NULL;
                  }
                else
                  tooltip_text = g_strdup_printf ("%s",
                                                  ((MarkerItem*) tmp_list->data)->name);
              }

            gtk_render_frame (context, cr, 1, 1, width - 1, SUPPLY_BAR_HEIGHT + 3);

            for (tmp_list = markers; tmp_list; tmp_list = tmp_list->next)
              {
                g_free (((MarkerItem*) tmp_list->data)->name);
                g_free (((MarkerItem*) tmp_list->data)->type);
                g_free (((MarkerItem*) tmp_list->data)->color);
              }
            g_slist_free_full (markers, g_free);
          }

        gtk_style_context_restore (context);

      // free inkLevelData

    if (tooltip_text)
      {
        gtk_widget_set_tooltip_text (widget, tooltip_text);
        g_free (tooltip_text);
      }
    else
      {
        gtk_widget_set_tooltip_text (widget, NULL);
        gtk_widget_set_has_tooltip (widget, FALSE);
      }
    }

  return TRUE;
}

static void
details_dialog_cb (GtkDialog *dialog,
                   gint       response_id,
                   gpointer   user_data)
{
  PpPrinterEntry *self = PP_PRINTER_ENTRY (user_data);

  pp_details_dialog_free (self->pp_details_dialog);
  self->pp_details_dialog = NULL;

  g_signal_emit_by_name (self, "printer-changed");
}

static void
on_show_printer_details_dialog (GtkButton      *button,
                                PpPrinterEntry *self)
{
  self->pp_details_dialog = pp_details_dialog_new (
    GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (self))),
    self->printer_name,
    self->printer_location,
    self->printer_hostname,
    self->printer_make_and_model,
    self->is_authorized);

  g_signal_connect (self->pp_details_dialog, "response", G_CALLBACK (details_dialog_cb), self);
  gtk_widget_show_all (GTK_WIDGET (self->pp_details_dialog));
}

static void
printer_options_dialog_cb (GtkDialog *dialog,
                           gint       response_id,
                           gpointer   user_data)
{
  PpPrinterEntry *self = PP_PRINTER_ENTRY (user_data);

  pp_options_dialog_free (self->pp_options_dialog);
  self->pp_options_dialog = NULL;
}

static void
on_show_printer_options_dialog (GtkButton      *button,
                                PpPrinterEntry *self)
{
  self->pp_options_dialog = pp_options_dialog_new (
    GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (self))),
    printer_options_dialog_cb,
    self,
    self->printer_name,
    self->is_authorized);
}

static void
set_as_default_printer (GtkToggleButton *button,
                        PpPrinterEntry  *self)
{
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (self->printer_default_checkbutton), TRUE);
  printer_set_default (self->printer_name);

  g_signal_emit_by_name (self, "printer-changed");
}

static void
remove_printer (GtkButton *button,
                PpPrinterEntry *self)
{
  printer_delete (self->printer_name);
}

static void
update_jobs_count (PpPrinterEntry *self)
{
  cups_job_t *jobs;
  gchar      *button_label;
  gint       *num_paused_jobs = 0;
  gint        num_jobs;
  int         i;

  // FIXME do this async
  self->num_jobs = cupsGetJobs (&jobs, self->printer_name, 1, CUPS_WHICHJOBS_ACTIVE);
  if (self->num_jobs > 0)
    cupsFreeJobs (self->num_jobs, jobs);

  num_jobs = self->num_jobs < 0 ? 0 : (guint) self->num_jobs;

  for (i = 0; i < num_jobs; i++)
    if (jobs[i].state == IPP_JOB_HELD)
      num_paused_jobs++;

  if (self->num_jobs <= 0)
    button_label = g_strdup ("No Active Jobs");
  else
    if (num_paused_jobs > 0)
      button_label = g_strdup_printf ("%u Paused Jobs", num_jobs);
    else
      button_label = g_strdup_printf ("%u Jobs", num_jobs);

  gtk_button_set_label (GTK_BUTTON (self->show_jobs_dialog_button), button_label);
  gtk_widget_set_sensitive (self->show_jobs_dialog_button, self->is_accepting_jobs);

  g_free (button_label);
}

static void
jobs_dialog_response_cb (GtkDialog  *dialog,
                         gint        response_id,
                         gpointer    user_data)
{
  PpPrinterEntry *self = PP_PRINTER_ENTRY (user_data);

  pp_jobs_dialog_free (self->pp_jobs_dialog);
  self->pp_jobs_dialog = NULL;
}

static void
show_jobs_dialog (GtkButton      *button,
                  PpPrinterEntry *self)
{
  self->pp_jobs_dialog = pp_jobs_dialog_new (
    GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (self))),
    jobs_dialog_response_cb,
    self,
    self->printer_name);
}

PpPrinterEntry *
pp_printer_entry_new (cups_dest_t  printer,
                      gboolean     is_authorized)
{
  PpPrinterEntry *self;
  InkLevelData   *inklevel;
  cups_ptype_t    printer_type = 0;
  gboolean        is_accepting_jobs;
  gchar          *instance;
  gchar          *printer_uri = NULL;
  gchar          *location = NULL;
  gchar          *printer_icon_name = NULL;
  gchar          *default_icon_name = NULL;
  gchar          *printer_make_and_model = NULL;
  gchar          *printer_status = NULL;
  int             printer_state = 3;
  int             i;

  self = g_object_new (PP_PRINTER_ENTRY_TYPE, NULL);

  inklevel = g_slice_new0 (InkLevelData);

  if (printer.instance)
    {
      instance = g_strdup_printf ("%s / %s", printer.name, printer.instance);
    }
  else
    {
      instance = g_strdup (printer.name);
    }

  for (i = 0; i < printer.num_options; i++)
    {
      if (g_strcmp0 (printer.options[i].name, "device-uri") == 0)
        self->printer_uri = printer.options[i].value;
      else if (g_strcmp0 (printer.options[i].name, "printer-uri-supported") == 0)
        printer_uri = printer.options[i].value;
      else if (g_strcmp0 (printer.options[i].name, "printer-type") == 0)
        printer_type = atoi (printer.options[i].value);
      else if (g_strcmp0 (printer.options[i].name, "printer-location") == 0)
        location = printer.options[i].value;
      else if (g_strcmp0 (printer.options[i].name, "marker-names") == 0)
        inklevel->marker_names = g_strcompress (printer.options[i].value);
      else if (g_strcmp0 (printer.options[i].name, "marker-levels") == 0)
        inklevel->marker_levels = printer.options[i].value;
      else if (g_strcmp0 (printer.options[i].name, "marker-colors") == 0)
        inklevel->marker_colors = printer.options[i].value;
      else if (g_strcmp0 (printer.options[i].name, "marker-types") == 0)
        inklevel->marker_types = printer.options[i].value;
      else if (g_strcmp0 (printer.options[i].name, "printer-make-and-model") == 0)
        printer_make_and_model = printer.options[i].value;
      else if (g_strcmp0 (printer.options[i].name, "printer-state") == 0)
        printer_state = atoi (printer.options[i].value);
      else if (g_strcmp0 (printer.options[i].name, "printer-is-accepting-jobs") == 0)
        {
          if (g_strcmp0 (printer.options[i].value, "true") == 0)
            is_accepting_jobs = TRUE;
          else
            is_accepting_jobs = FALSE;
        }
    }

  switch (printer_state)
    {
      case 3:
        if (is_accepting_jobs)
          {
            /* Translators: Printer's state (can start new job without waiting) */
            printer_status = g_strdup ( C_("printer state", "Ready"));
          }
        else
          {
            /* Translators: Printer's state (printer is ready but doesn't accept new jobs) */
            printer_status = g_strdup ( C_("printer state", "Does not accept jobs"));
          }
        break;
      case 4:
        /* Translators: Printer's state (jobs are processing) */
        printer_status = g_strdup ( C_("printer state", "Processing"));
        break;
      case 5:
        /* Translators: Printer's state (no jobs can be processed) */
        printer_status = g_strdup ( C_("printer state", "Stopped"));
        break;
    }

  if (printer_is_local (printer_type, self->printer_uri))
    printer_icon_name = g_strdup ("printer");
  else
    printer_icon_name = g_strdup ("printer-network");

  self->printer_name = printer.name;
  self->is_accepting_jobs = is_accepting_jobs;
  self->printer_make_and_model = printer_make_and_model;
  self->printer_location = location;
  self->is_authorized = is_authorized;

  self->printer_hostname = printer_get_hostname (printer_type, self->printer_uri, printer_uri);

  gtk_image_set_from_icon_name (self->printer_icon, printer_icon_name, GTK_ICON_SIZE_DIALOG);
  gtk_label_set_text (self->printer_status, printer_status);
  gtk_label_set_text (self->printer_name_label, instance);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (self->printer_default_checkbutton), printer.is_default);

  self->printer_make_and_model = sanitize_printer_model (printer_make_and_model);

  if (self->printer_make_and_model == NULL)
    {
      gtk_widget_hide (GTK_WIDGET (self->printer_model_label));
      gtk_widget_hide (GTK_WIDGET (self->printer_model));
    }

  gtk_label_set_text (self->printer_model, self->printer_make_and_model);

  if (location[0] == '\0')
    {
      gtk_widget_hide (GTK_WIDGET (self->printer_location_label));
      gtk_widget_hide (GTK_WIDGET (self->printer_location_address_label));
    }

  gtk_label_set_text (self->printer_location_address_label, location);

  g_signal_connect (self->supply_drawing_area, "draw", G_CALLBACK (supply_levels_draw_cb), inklevel);

  update_jobs_count (self);

  gtk_widget_set_sensitive (GTK_WIDGET (self->set_default_printer_menuitem), self->is_authorized);
  gtk_widget_set_sensitive (GTK_WIDGET (self->printer_default_checkbutton), self->is_authorized);
  gtk_widget_set_sensitive (GTK_WIDGET (self->remove_printer_menuitem), self->is_authorized);

  self->pp_details_dialog = NULL;
  self->pp_options_dialog = NULL;
  self->pp_jobs_dialog = NULL;

  g_free (printer_icon_name);
  g_free (default_icon_name);

  return self;
}

static void
pp_printer_entry_class_init (PpPrinterEntryClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/control-center/printers/printer-entry.ui");

  gtk_widget_class_bind_template_child (widget_class, PpPrinterEntry, printer_icon);
  gtk_widget_class_bind_template_child (widget_class, PpPrinterEntry, printer_name_label);
  gtk_widget_class_bind_template_child (widget_class, PpPrinterEntry, printer_status);
  gtk_widget_class_bind_template_child (widget_class, PpPrinterEntry, printer_model_label);
  gtk_widget_class_bind_template_child (widget_class, PpPrinterEntry, printer_model);
  gtk_widget_class_bind_template_child (widget_class, PpPrinterEntry, printer_location_label);
  gtk_widget_class_bind_template_child (widget_class, PpPrinterEntry, printer_location_address_label);
  gtk_widget_class_bind_template_child (widget_class, PpPrinterEntry, supply_drawing_area);
  gtk_widget_class_bind_template_child (widget_class, PpPrinterEntry, set_default_printer_menuitem);
  gtk_widget_class_bind_template_child (widget_class, PpPrinterEntry, printer_default_checkbutton);
  gtk_widget_class_bind_template_child (widget_class, PpPrinterEntry, show_jobs_dialog_button);
  gtk_widget_class_bind_template_child (widget_class, PpPrinterEntry, remove_printer_menuitem);

  gtk_widget_class_bind_template_callback (widget_class, on_show_printer_details_dialog);
  gtk_widget_class_bind_template_callback (widget_class, on_show_printer_options_dialog);
  gtk_widget_class_bind_template_callback (widget_class, set_as_default_printer);
  gtk_widget_class_bind_template_callback (widget_class, remove_printer);
  gtk_widget_class_bind_template_callback (widget_class, show_jobs_dialog);

  signals[IS_DEFAULT_PRINTER] =
    g_signal_new ("printer-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (PpPrinterEntryClass, printer_changed),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}
