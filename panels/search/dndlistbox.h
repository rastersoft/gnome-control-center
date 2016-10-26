/* dndlistbox.h
 *
 * Copyright (C) 2016 Felipe Borges <felipeborges@gnome.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#ifndef DND_LIST_BOX_H
#define DND_LIST_BOX_H

#include <gdk/gdk.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define DND_TYPE_LIST_BOX (dnd_list_box_get_type())

G_DECLARE_FINAL_TYPE (DndListBox, dnd_list_box, DND, LIST_BOX, GtkListBox)

struct _DndListBoxClass
{
  GtkListBoxClass parent_class;

  void (*row_moved) (DndListBox    *box,
                     GtkListBoxRow *row);
};

typedef GtkWidget * (*DndListBoxCreatePlaceholderFunc) (gpointer item,
                                                        gpointer user_data);

GtkWidget *dnd_list_box_new          (void);

void       dnd_list_box_set_drag_row (DndListBox     *self,
                                      GtkWidget      *row,
                                      GdkEventButton *event);

void       dnd_list_box_set_create_placeholder_func (DndListBox                      *self,
                                                     DndListBoxCreatePlaceholderFunc  func,
                                                     gpointer                         user_data,
                                                     GDestroyNotify                   destroy);

G_END_DECLS

#endif /* DND_LIST_BOX_H */

