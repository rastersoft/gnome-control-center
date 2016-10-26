/* dndlistbox.c
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

#include "dndlistbox.h"

struct _DndListBox {
  GtkListBox parent;

  GtkTargetList *source_targets;
  GtkWidget *dnd_window;
  GtkWidget *row_placeholder;
  gint row_placeholder_index;
  gint row_destination_index;
  GtkWidget *drag_row;
  gint row_source_row_offset;
  gint drag_row_height;
  gint drag_row_x;
  gint drag_row_y;
  gint drag_root_x;
  gint drag_root_y;
  gboolean is_on_drag;

  DndListBoxCreatePlaceholderFunc create_placeholder_func;
  gpointer create_placeholder_func_data;
  GDestroyNotify create_placeholder_func_data_destroy;
};

G_DEFINE_TYPE (DndListBox, dnd_list_box, GTK_TYPE_LIST_BOX)

enum {
  ROW_MOVED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

#define ROW_OUTSIDE_LISTBOX -1

GtkWidget *
dnd_list_box_new (void)
{
  return g_object_new (DND_TYPE_LIST_BOX, NULL);
}

void
dnd_list_box_set_create_placeholder_func (DndListBox                      *self,
                                          DndListBoxCreatePlaceholderFunc  func,
                                          gpointer                         user_data,
                                          GDestroyNotify                   destroy)
{
  g_return_if_fail (DND_IS_LIST_BOX (self));

  if (self->create_placeholder_func_data_destroy)
    self->create_placeholder_func_data_destroy (self->create_placeholder_func_data);

  self->create_placeholder_func = func;
  self->create_placeholder_func_data = user_data;
  self->create_placeholder_func_data_destroy = destroy;
}

void
dnd_list_box_set_drag_row (DndListBox     *self,
                           GtkWidget      *row,
                           GdkEventButton *event)
{
  g_return_if_fail (DND_IS_LIST_BOX (self));

  self->drag_row = row;

  if (event != NULL)
    {
      self->drag_row_x = (gint)event->x;
      self->drag_row_y = (gint)event->y;
      self->drag_root_x = event->x_root;
      self->drag_root_y = event->y_root;
    }
}

static void
dnd_list_box_drag_data_received (GtkWidget        *widget,
                                 GdkDragContext   *context,
                                 gint              x,
                                 gint              y,
                                 GtkSelectionData *data,
                                 guint             info,
                                 guint             time)
{
  DndListBox *self = DND_LIST_BOX (widget);
  GtkWidget *source = NULL;
  GtkWidget **source_row;

  source = gtk_drag_get_source_widget (context);
  source_row = (void*)gtk_selection_data_get_data (data);
  if (source && gtk_selection_data_get_target (data) == gdk_atom_intern_static_string ("GTK_LIST_BOX_ROW"))
    {
      gint source_index;

      source_index = gtk_list_box_row_get_index (GTK_LIST_BOX_ROW (*source_row));

      /* move the row */
      if (DND_LIST_BOX (source) != self ||
          (self->row_destination_index != source_index &&
           self->row_destination_index != source_index + 1))
        {
          GtkWidget *row;
          gint position;

          position = gtk_list_box_row_get_index (GTK_LIST_BOX_ROW (*source_row));

          row = g_object_ref (gtk_list_box_get_row_at_index (GTK_LIST_BOX (self), position));
          gtk_container_remove (GTK_CONTAINER (self), row);

          if (self->row_destination_index > source_index)
            gtk_list_box_insert (GTK_LIST_BOX (self), row, self->row_destination_index - 1);
          else
            gtk_list_box_insert (GTK_LIST_BOX (self), row, self->row_destination_index);

          g_signal_emit (self, signals[ROW_MOVED], 0, row);
        }
    }

  gtk_drag_finish (context, FALSE, FALSE, time);

  if (self->row_placeholder)
    {
      gtk_widget_destroy (self->row_placeholder);
      self->row_placeholder = NULL;
    }
}

static void
dnd_list_box_drag_data_get (GtkWidget        *widget,
                            GdkDragContext   *context,
                            GtkSelectionData *data,
                            guint             info,
                            guint             time)
{
  DndListBox *self = DND_LIST_BOX (widget);
  GdkAtom target;

  target = gtk_selection_data_get_target (data);
  if (target == gdk_atom_intern_static_string ("GTK_LIST_BOX_ROW"))
    {
      gtk_selection_data_set (data, target, 8, (void*)&self->drag_row, sizeof (gpointer));

      return;
    }

  gtk_widget_show (self->drag_row);
}

static gboolean
dnd_list_box_drag_drop (GtkWidget      *widget,
                        GdkDragContext *context,
                        gint            x,
                        gint            y,
                        guint           time)
{
  DndListBox *self = DND_LIST_BOX (widget);
  GtkWidget *source_widget;
  GdkAtom target;

  target = gtk_drag_dest_find_target (widget, context, NULL);
  source_widget = gtk_drag_get_source_widget (context);

  if (DND_IS_LIST_BOX (source_widget))
    gtk_widget_show (self->drag_row);

  if (target == gdk_atom_intern_static_string ("GTK_LIST_BOX_ROW"))
    {
      gtk_drag_get_data (widget, context, target, time);
      return TRUE;
    }

  self->row_placeholder_index = ROW_OUTSIDE_LISTBOX;

  return FALSE;
}

static void
dnd_list_box_drag_leave (GtkWidget      *widget,
                         GdkDragContext *context,
                         guint           time)
{
  DndListBox *self = DND_LIST_BOX (widget);

  if (self->row_placeholder_index != ROW_OUTSIDE_LISTBOX)
    {
      gtk_container_remove (GTK_CONTAINER (self), self->row_placeholder);
      self->row_placeholder_index = ROW_OUTSIDE_LISTBOX;
    }

  gtk_list_box_drag_unhighlight_row (GTK_LIST_BOX (widget));
}

static GtkWidget *
create_placeholder_row (gint height)
{
  GtkWidget *row;

  row = gtk_list_box_row_new ();
  gtk_style_context_add_class (gtk_widget_get_style_context (row), "background");
  gtk_widget_set_opacity (row, 0.6);
  gtk_widget_set_size_request (row, -1, height);

  return row;
}

static gboolean
dnd_list_box_drag_motion (GtkWidget      *widget,
                          GdkDragContext *context,
                          gint            x,
                          gint            y,
                          gint            time)
{
  DndListBox *self = DND_LIST_BOX (widget);
  GtkListBoxRow *generic_row;
  GtkWidget *source;
  GdkAtom target;
  gint dest_x, dest_y;
  gint row_placeholder_index;
  gint row_index;

  target = gtk_drag_dest_find_target (widget, context, NULL);
  if (target != gdk_atom_intern_static_string ("GTK_LIST_BOX_ROW"))
    {
      gdk_drag_status (context, 0, time);
      return FALSE;
    }

  gtk_widget_translate_coordinates (widget, GTK_WIDGET (self), x, y, &dest_x, &dest_y);

  generic_row = (GtkListBoxRow *)gtk_list_box_get_row_at_y (GTK_LIST_BOX (self), dest_y);
  source = gtk_drag_get_source_widget (context);

  if (!self->row_placeholder)
    {
      if (!generic_row)
        {
          self->drag_row_height = DND_LIST_BOX (source)->drag_row_height;
        }
      else
        {
          GtkAllocation allocation;

          gtk_widget_get_allocation (GTK_WIDGET (generic_row), &allocation);
          self->drag_row_height = allocation.height;
        }

      self->row_placeholder = create_placeholder_row (self->drag_row_height);

      gtk_widget_show_all (self->row_placeholder);
      g_object_ref_sink (self->row_placeholder);
    }
  else if (GTK_WIDGET (generic_row) == self->row_placeholder)
    {
      /* cursor on placeholder */
      gdk_drag_status (context, GDK_ACTION_MOVE, time);

      return TRUE;
    }

  if (!generic_row)
    {
      GList *children;

      children = gtk_container_get_children (GTK_CONTAINER (self));
      row_placeholder_index = g_list_length (children);

      g_list_free (children);
    }
  else
    {
      row_index = gtk_list_box_row_get_index (GTK_LIST_BOX_ROW (generic_row));

      gtk_widget_translate_coordinates (widget, GTK_WIDGET (generic_row), x, y, &dest_x, &dest_y);

      if (dest_y <= self->drag_row_height / 2 && row_index >= 0)
        {
          row_placeholder_index = row_index;
        }
      else
        {
          row_placeholder_index = row_index + 1;
        }
    }

  if (source == widget)
    {
      gint source_row_index;

      source_row_index = gtk_list_box_row_get_index (GTK_LIST_BOX_ROW (self->drag_row));
      self->row_source_row_offset = source_row_index < row_placeholder_index ? -1 : 0;
    }

  if (self->row_placeholder_index != row_placeholder_index)
    {
      if (self->row_placeholder_index != ROW_OUTSIDE_LISTBOX)
        {
          gtk_container_remove (GTK_CONTAINER (self), self->row_placeholder);

          if (self->row_placeholder_index < row_placeholder_index)
            row_placeholder_index -= 1;
	}

      self->row_destination_index = self->row_placeholder_index = row_placeholder_index;

      gtk_list_box_insert (GTK_LIST_BOX (self), self->row_placeholder, self->row_placeholder_index);
    }

  gdk_drag_status (context, GDK_ACTION_MOVE, time);

  return TRUE;
}

static gboolean
dnd_list_box_drag_failed (GtkWidget      *widget,
                          GdkDragContext *context,
                          GtkDragResult   result)
{
  GtkWidget *source;

  source = gtk_drag_get_source_widget (context);
  if (DND_IS_LIST_BOX (source))
    gtk_widget_show (DND_LIST_BOX (widget)->drag_row);

  return FALSE;
}

static void
dnd_list_box_drag_end (GtkWidget      *widget,
                       GdkDragContext *context)
{
  DndListBox *self = DND_LIST_BOX (widget);

  self->drag_row = NULL;
  self->is_on_drag = FALSE;

  gtk_widget_destroy (self->dnd_window);
  self->dnd_window = NULL;
}

static void
dnd_list_box_drag_begin (GtkWidget      *widget,
                         GdkDragContext *context)
{
  DndListBox *self = DND_LIST_BOX (widget);
  GtkWidget *drag_row, *row;
  GtkAllocation allocation;

  drag_row = self->drag_row;
  gtk_widget_get_allocation (drag_row, &allocation);
  gtk_widget_hide (drag_row);

  self->drag_row_height = allocation.height;

  if (self->create_placeholder_func)
    row = self->create_placeholder_func (self->drag_row, GINT_TO_POINTER (self->drag_row_height));
  else
    row = create_placeholder_row (self->drag_row_height);

  self->dnd_window = gtk_window_new (GTK_WINDOW_POPUP);
  gtk_widget_set_size_request (self->dnd_window, allocation.width, allocation.height);
  gtk_window_set_screen (GTK_WINDOW (self->dnd_window), gtk_widget_get_screen (drag_row));

  gtk_container_add (GTK_CONTAINER (self->dnd_window), row);
  gtk_widget_show_all (self->dnd_window);
  gtk_widget_set_opacity (self->dnd_window, 0.8);

  gtk_drag_set_icon_widget (context, self->dnd_window, self->drag_row_x, self->drag_row_y);
}

static gboolean
dnd_list_box_motion_notify_event (GtkWidget      *widget,
                                  GdkEventMotion *event)
{
  DndListBox *self = DND_LIST_BOX (widget);

  if (self->drag_row == NULL || self->is_on_drag)
    return FALSE;

  if (!(event->state & GDK_BUTTON1_MASK))
    {
      self->drag_row = NULL;

      return FALSE;
    }

  if (gtk_drag_check_threshold (widget, self->drag_root_x, self->drag_root_y, event->x_root, event->y_root))
    {
      self->is_on_drag = TRUE;

      gtk_drag_begin_with_coordinates (widget, self->source_targets, GDK_ACTION_MOVE, GDK_BUTTON_PRIMARY, (GdkEvent*)event, -1, -1);
    }

  return FALSE;
}

static void
dnd_list_box_finalize (GObject *object)
{
  DndListBox *self = DND_LIST_BOX (object);

  g_clear_object (&self->dnd_window);
  g_clear_object (&self->drag_row);

  G_OBJECT_CLASS (dnd_list_box_parent_class)->finalize (object);
}

static void
dnd_list_box_class_init (DndListBoxClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = dnd_list_box_finalize;

  widget_class->motion_notify_event = dnd_list_box_motion_notify_event;
  widget_class->drag_begin = dnd_list_box_drag_begin;
  widget_class->drag_end = dnd_list_box_drag_end;
  widget_class->drag_failed = dnd_list_box_drag_failed;
  widget_class->drag_motion = dnd_list_box_drag_motion;
  widget_class->drag_leave = dnd_list_box_drag_leave;
  widget_class->drag_drop = dnd_list_box_drag_drop;
  widget_class->drag_data_get = dnd_list_box_drag_data_get;
  widget_class->drag_data_received = dnd_list_box_drag_data_received;

  signals[ROW_MOVED] =
    g_signal_new ("row-moved",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1,
                  GTK_TYPE_LIST_BOX_ROW);
}

static const GtkTargetEntry targets [] = {
  { "GTK_LIST_BOX_ROW", GTK_TARGET_SAME_WIDGET, 0 },
};

static void
dnd_list_box_init (DndListBox *self)
{
  self->source_targets = gtk_target_list_new (targets, G_N_ELEMENTS (targets));
  gtk_target_list_add_text_targets (self->source_targets, 0);

  gtk_drag_dest_set (GTK_WIDGET (self), 0, targets, G_N_ELEMENTS (targets), GDK_ACTION_MOVE);

  gtk_drag_dest_set_track_motion (GTK_WIDGET (self), TRUE);

  self->drag_row = NULL;
  self->row_placeholder = NULL;
  self->row_placeholder_index = ROW_OUTSIDE_LISTBOX;
  self->row_destination_index = ROW_OUTSIDE_LISTBOX;
  self->row_source_row_offset = 0;
  self->is_on_drag = FALSE;
}
