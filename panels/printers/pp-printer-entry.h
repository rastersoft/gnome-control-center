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

#ifndef PP_PRINTER_ENTRY_H
#define PP_PRINTER_ENTRY_H

#include <gtk/gtk.h>
#include <cups/cups.h>

#define PP_PRINTER_ENTRY_TYPE (pp_printer_entry_get_type ())
#define PP_PRINTER_ENTRY(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), PP_PRINTER_ENTRY_TYPE, PpPrinterEntry))

typedef struct _PpPrinterEntry		PpPrinterEntry;
typedef struct _PpPrinterEntryClass  	PpPrinterEntryClass;

typedef struct
{
  gchar *marker_names;
  gchar *marker_levels;
  gchar *marker_colors;
  gchar *marker_types;
} InkLevelData;

GType		pp_printer_entry_get_type	(void);

PpPrinterEntry *pp_printer_entry_new		(cups_dest_t printer,
                                                 gboolean    is_authorized);

#endif /* PP_PRINTER_ENTRY_H */
