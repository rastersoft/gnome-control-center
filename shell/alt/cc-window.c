/*
 * Copyright (c) 2009, 2010 Intel, Inc.
 * Copyright (c) 2010 Red Hat, Inc.
 * Copyright (c) 2016 Endless, Inc.
 *
 * The Control Center is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * The Control Center is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with the Control Center; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Author: Thomas Wood <thos@gnome.org>
 */

#include <config.h>

#include "cc-window.h"

#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <string.h>
#include <libgd/gd.h>

#include "cc-panel.h"
#include "cc-shell.h"
#include "cc-shell-category-view.h"
#include "cc-shell-model.h"
#include "cc-panel-loader.h"
#include "cc-util.h"

#define MOUSE_BACK_BUTTON 8

#define DEFAULT_WINDOW_ICON_NAME "preferences-system"

#define SEARCH_PAGE "_search"
#define OVERVIEW_PAGE "_overview"

typedef struct
{
  GtkWidget  *row;
  GIcon      *icon;
  gchar      *id;
  gchar      *name;
  gchar      *description;
  GtkWidget  *description_label;
} RowData;

struct _CcWindow
{
  GtkApplicationWindow parent;

  GtkWidget  *stack;
  GtkWidget  *header;
  GtkWidget  *header_box;
  GtkWidget  *listbox;
  GtkWidget  *list_scrolled;
  GtkWidget  *panel_headerbar;
  GtkWidget  *search_scrolled;
  GtkWidget  *previous_button;
  GtkWidget  *top_right_box;
  GtkWidget  *search_button;
  GtkWidget  *search_bar;
  GtkWidget  *search_entry;
  GtkWidget  *lock_button;
  GtkWidget  *current_panel_box;
  GtkWidget  *current_panel;
  char       *current_panel_id;
  GQueue     *previous_panels;

  GtkSizeGroup *header_sizegroup;

  GPtrArray  *custom_widgets;

  GtkListStore *store;

  CcPanel *active_panel;
};

static void     cc_shell_iface_init         (CcShellInterface      *iface);

G_DEFINE_TYPE_WITH_CODE (CcWindow, cc_window, GTK_TYPE_APPLICATION_WINDOW,
                         G_IMPLEMENT_INTERFACE (CC_TYPE_SHELL, cc_shell_iface_init))

enum
{
  PROP_0,
  PROP_ACTIVE_PANEL
};

static gboolean cc_window_set_active_panel_from_id (CcShell      *shell,
                                                    const gchar  *start_id,
                                                    GVariant     *parameters,
                                                    GError      **err);

static const gchar *
get_icon_name_from_g_icon (GIcon *gicon)
{
  const gchar * const *names;
  GtkIconTheme *icon_theme;
  int i;

  if (!G_IS_THEMED_ICON (gicon))
    return NULL;

  names = g_themed_icon_get_names (G_THEMED_ICON (gicon));
  icon_theme = gtk_icon_theme_get_default ();

  for (i = 0; names[i] != NULL; i++)
    {
      if (gtk_icon_theme_has_icon (icon_theme, names[i]))
        return names[i];
    }

  return NULL;
}

/*
 * RowData functions
 */
static void
row_data_free (RowData *data)
{
  g_object_unref (data->icon);
  g_free (data->description);
  g_free (data->name);
  g_free (data);
}

static RowData*
row_data_new (const gchar *id,
              const gchar *name,
              const gchar *description,
              GIcon       *icon)
{
  GtkWidget *label, *grid;
  RowData *data;

  data = g_new0 (RowData, 1);
  data->row = gtk_list_box_row_new ();
  data->id = g_strdup (id);
  data->name = g_strdup (name);
  data->description = g_strdup (description);
  data->icon = g_object_ref (icon);

  /* Setup the row */
  grid = g_object_new (GTK_TYPE_GRID,
                       "visible", TRUE,
                       "hexpand", TRUE,
                       "border-width", 12,
                       "column-spacing", 6,
                       NULL);

  /* Name label */
  label = g_object_new (GTK_TYPE_LABEL,
                        "label", name,
                        "visible", TRUE,
                        "xalign", 0,
                        "hexpand", TRUE,
                        NULL);
  gtk_grid_attach (GTK_GRID (grid), label, 1, 0, 1, 1);

  /* Description label */
  label = g_object_new (GTK_TYPE_LABEL,
                        "label", description,
                        "visible", FALSE,
                        "xalign", 0,
                        "hexpand", TRUE,
                        NULL);
  gtk_label_set_max_width_chars (GTK_LABEL (label), 25);
  gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);

  gtk_style_context_add_class (gtk_widget_get_style_context (label), "dim-label");
  gtk_grid_attach (GTK_GRID (grid), label, 1, 1, 1, 1);

  data->description_label = label;

  gtk_container_add (GTK_CONTAINER (data->row), grid);
  gtk_widget_show (data->row);

  g_object_set_data_full (G_OBJECT (data->row), "data", data, (GDestroyNotify) row_data_free);

  return data;
}

static gboolean
activate_panel (CcWindow           *self,
                const gchar        *id,
                GVariant           *parameters,
                const gchar        *name,
                GIcon              *gicon)
{
  GtkWidget *box;
  const gchar *icon_name;

  if (!id)
    return FALSE;

  self->current_panel = GTK_WIDGET (cc_panel_loader_load_by_name (CC_SHELL (self), id, parameters));
  cc_shell_set_active_panel (CC_SHELL (self), CC_PANEL (self->current_panel));
  gtk_widget_show (self->current_panel);

  gtk_lock_button_set_permission (GTK_LOCK_BUTTON (self->lock_button),
                                  cc_panel_get_permission (CC_PANEL (self->current_panel)));

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  gtk_box_pack_start (GTK_BOX (box), self->current_panel,
                      TRUE, TRUE, 0);

  gtk_stack_add_named (GTK_STACK (self->stack), box, id);

  /* switch to the new panel */
  gtk_widget_show (box);
  gtk_stack_set_visible_child_name (GTK_STACK (self->stack), id);

  /* set the title of the window */
  icon_name = get_icon_name_from_g_icon (gicon);

  gtk_window_set_role (GTK_WINDOW (self), id);
  gtk_header_bar_set_title (GTK_HEADER_BAR (self->panel_headerbar), name);
  gtk_window_set_default_icon_name (icon_name);
  gtk_window_set_icon_name (GTK_WINDOW (self), icon_name);

  self->current_panel_box = box;

  return TRUE;
}

static void
_shell_remove_all_custom_widgets (CcWindow *self)
{
  GtkWidget *widget;
  guint i;

  /* remove from the header */
  for (i = 0; i < self->custom_widgets->len; i++)
    {
        widget = g_ptr_array_index (self->custom_widgets, i);
        gtk_container_remove (GTK_CONTAINER (self->top_right_box), widget);
    }
  g_ptr_array_set_size (self->custom_widgets, 0);
}

static void
add_current_panel_to_history (CcShell    *shell,
                              const char *start_id)
{
  CcWindow *self;

  g_return_if_fail (start_id != NULL);

  self = CC_WINDOW (shell);

  if (!self->current_panel_id ||
      g_strcmp0 (self->current_panel_id, start_id) == 0)
    return;

  g_queue_push_head (self->previous_panels, g_strdup (self->current_panel_id));
  g_debug ("Added '%s' to the previous panels", self->current_panel_id);
}

static void
shell_show_overview_page (CcWindow *self)
{
  gtk_stack_set_visible_child_name (GTK_STACK (self->stack), OVERVIEW_PAGE);

  if (self->current_panel_box)
    gtk_container_remove (GTK_CONTAINER (self->stack), self->current_panel_box);
  self->current_panel = NULL;
  self->current_panel_box = NULL;
  g_clear_pointer (&self->current_panel_id, g_free);

  /* Clear the panel history */
  g_queue_free_full (self->previous_panels, g_free);
  self->previous_panels = g_queue_new ();

  /* clear the search text */
  gtk_entry_set_text (GTK_ENTRY (self->search_entry), "");
  if (gtk_search_bar_get_search_mode (GTK_SEARCH_BAR (self->search_bar)))
    gtk_widget_grab_focus (self->search_entry);

  gtk_lock_button_set_permission (GTK_LOCK_BUTTON (self->lock_button), NULL);

  /* reset window title and icon */
  gtk_window_set_role (GTK_WINDOW (self), NULL);
  gtk_window_set_default_icon_name (DEFAULT_WINDOW_ICON_NAME);
  gtk_window_set_icon_name (GTK_WINDOW (self), DEFAULT_WINDOW_ICON_NAME);

  cc_shell_set_active_panel (CC_SHELL (self), NULL);

  /* clear any custom widgets */
  _shell_remove_all_custom_widgets (self);
}

void
cc_window_set_overview_page (CcWindow *center)
{
  shell_show_overview_page (center);
}

void
cc_window_set_search_item (CcWindow   *center,
                           const char *search)
{
  shell_show_overview_page (center);
  gtk_search_bar_set_search_mode (GTK_SEARCH_BAR (center->search_bar), TRUE);
  gtk_entry_set_text (GTK_ENTRY (center->search_entry), search);
  gtk_editable_set_position (GTK_EDITABLE (center->search_entry), -1);
}

static void
row_selected_cb (GtkListBox    *listbox,
                 GtkListBoxRow *row,
                 CcWindow      *self)
{
  /*
   * When the widget is in the destruction process, it emits
   * a ::row-selected signal with NULL row. Trying to do anything
   * in this case will result in a segfault, so we have to
   * check it here.
   */
  if (gtk_widget_in_destruction (GTK_WIDGET (self)))
    return;

  if (row)
    {
      RowData *data = g_object_get_data (G_OBJECT (row), "data");

      cc_window_set_active_panel_from_id (CC_SHELL (self), data->id, NULL, NULL);
    }
  else
    {
      shell_show_overview_page (self);
    }
}

/*
 * GtkListBox functions
 */
static gboolean
filter_func (GtkListBoxRow *row,
             gpointer       user_data)
{
  CcWindow *self;
  RowData *data;
  gchar *search_text, *panel_text, *panel_description;
  const gchar *entry_text;
  gboolean retval;

  self = CC_WINDOW (user_data);
  data = g_object_get_data (G_OBJECT (row), "data");
  entry_text = gtk_entry_get_text (GTK_ENTRY (self->search_entry));

  panel_text = cc_util_normalize_casefold_and_unaccent (data->name);
  search_text = cc_util_normalize_casefold_and_unaccent (entry_text);
  panel_description = cc_util_normalize_casefold_and_unaccent (data->description);

  g_strstrip (panel_text);
  g_strstrip (search_text);
  g_strstrip (panel_description);

  /*
   * The description label is only visible when the search is
   * happening.
   */
  gtk_widget_set_visible (data->description_label, g_utf8_strlen (search_text, -1) > 0);

  retval = g_strstr_len (panel_text, -1, search_text) != NULL ||
           g_strstr_len (panel_description, -1, search_text) != NULL;

  g_free (panel_text);
  g_free (search_text);
  g_free (panel_description);

  return retval;
}

static void
search_entry_changed_cb (GtkEntry *entry,
                         CcWindow *self)
{
  gtk_list_box_invalidate_filter (GTK_LIST_BOX (self->listbox));
}

static void
search_entry_activate_cb (GtkEntry *entry,
                          CcWindow *self)
{
  GtkListBoxRow *row;

  row = gtk_list_box_get_row_at_y (GTK_LIST_BOX (self->listbox), 0);

  if (row)
    {
      gtk_search_bar_set_search_mode (GTK_SEARCH_BAR (self->search_bar), FALSE);
      gtk_list_box_select_row (GTK_LIST_BOX (self->listbox), row);
      gtk_widget_grab_focus (GTK_WIDGET (row));
    }
}


static void
setup_model (CcWindow *shell)
{
  GtkTreeModel *model;
  GtkTreeIter iter;
  gboolean valid;

  shell->store = (GtkListStore *) cc_shell_model_new ();
  model = GTK_TREE_MODEL (shell->store);

  cc_panel_loader_fill_model (CC_SHELL_MODEL (shell->store));

  /* Create a row for each panel */
  valid = gtk_tree_model_get_iter_first (model, &iter);

  while (valid)
    {
      RowData *data;
      GIcon *icon;
      gchar *name, *description, *id;

      gtk_tree_model_get (model, &iter,
                          COL_DESCRIPTION, &description,
                          COL_GICON, &icon,
                          COL_ID, &id,
                          COL_NAME, &name,
                          -1);

      data = row_data_new (id, name, description, icon);

      gtk_container_add (GTK_CONTAINER (shell->listbox), data->row);

      valid = gtk_tree_model_iter_next (model, &iter);

      g_clear_pointer (&description, g_free);
      g_clear_pointer (&name, g_free);
      g_clear_pointer (&id, g_free);
      g_clear_object (&icon);
    }
}

static void
previous_button_clicked_cb (GtkButton *button,
                            CcWindow  *shell)
{
  g_debug ("Num previous panels? %d", g_queue_get_length (shell->previous_panels));
  if (g_queue_is_empty (shell->previous_panels)) {
    shell_show_overview_page (shell);
  } else {
    char *panel_name;

    panel_name = g_queue_pop_head (shell->previous_panels);
    g_debug ("About to go to previous panel '%s'", panel_name);
    cc_window_set_active_panel_from_id (CC_SHELL (shell), panel_name, NULL, NULL);
    g_free (panel_name);
  }
}

static void
stack_page_notify_cb (GtkStack     *stack,
                      GParamSpec  *spec,
                      CcWindow    *self)
{
  const char *id;

  id = gtk_stack_get_visible_child_name (stack);

  /* make sure the home button is shown on all pages except the overview page */

  if (g_strcmp0 (id, OVERVIEW_PAGE) == 0 || g_strcmp0 (id, SEARCH_PAGE) == 0)
    {
      gtk_widget_hide (self->previous_button);
      gtk_widget_show (self->search_button);
      gtk_widget_show (self->search_bar);
      gtk_widget_hide (self->lock_button);
    }
  else
    {
      gtk_widget_show (self->previous_button);
      gtk_widget_hide (self->search_button);
      gtk_widget_hide (self->search_bar);
    }
}

static void
sidelist_size_allocate_cb (GtkWidget    *box,
                           GdkRectangle *allocation,
                           CcWindow     *self)
{
  /* Keep the sidelist and the first headerbar synchronized */
  gtk_widget_set_size_request (self->header, allocation->width, -1);
}

/* CcShell implementation */
static void
_shell_embed_widget_in_header (CcShell      *shell,
                               GtkWidget    *widget)
{
  CcWindow *self = CC_WINDOW (shell);

  /* add to header */
  gtk_box_pack_end (GTK_BOX (self->top_right_box), widget, FALSE, FALSE, 0);
  g_ptr_array_add (self->custom_widgets, g_object_ref (widget));

  gtk_size_group_add_widget (self->header_sizegroup, widget);
}

/* CcShell implementation */
static gboolean
cc_window_set_active_panel_from_id (CcShell      *shell,
                                    const gchar  *start_id,
                                    GVariant     *parameters,
                                    GError      **err)
{
  GtkTreeIter iter;
  gboolean iter_valid;
  gchar *name = NULL;
  GIcon *gicon = NULL;
  CcWindow *self = CC_WINDOW (shell);
  GtkWidget *old_panel;

  /* When loading the same panel again, just set its parameters */
  if (g_strcmp0 (self->current_panel_id, start_id) == 0)
    {
      g_object_set (G_OBJECT (self->current_panel), "parameters", parameters, NULL);
      return TRUE;
    }

  /* clear any custom widgets */
  _shell_remove_all_custom_widgets (self);

  iter_valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (self->store),
                                              &iter);

  /* find the details for this item */
  while (iter_valid)
    {
      gchar *id;

      gtk_tree_model_get (GTK_TREE_MODEL (self->store), &iter,
                          COL_NAME, &name,
                          COL_GICON, &gicon,
                          COL_ID, &id,
                          -1);

      if (id && !strcmp (id, start_id))
        {
          g_free (id);
          break;
        }
      else
        {
          g_free (id);
          g_free (name);
          if (gicon)
            g_object_unref (gicon);

          name = NULL;
          id = NULL;
          gicon = NULL;
        }

      iter_valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (self->store),
                                             &iter);
    }

  old_panel = self->current_panel_box;

  if (!name)
    {
      g_warning ("Could not find settings panel \"%s\"", start_id);
    }
  else if (activate_panel (CC_WINDOW (shell), start_id, parameters,
                           name, gicon) == FALSE)
    {
      /* Failed to activate the panel for some reason,
       * let's keep the old panel around instead */
    }
  else
    {
      /* Successful activation */
      g_free (self->current_panel_id);
      self->current_panel_id = g_strdup (start_id);

      if (old_panel)
        gtk_container_remove (GTK_CONTAINER (self->stack), old_panel);
    }

  g_free (name);
  if (gicon)
    g_object_unref (gicon);

  return TRUE;
}

static gboolean
_shell_set_active_panel_from_id (CcShell      *shell,
                                 const gchar  *start_id,
                                 GVariant     *parameters,
                                 GError      **err)
{
  add_current_panel_to_history (shell, start_id);
  return cc_window_set_active_panel_from_id (shell, start_id, parameters, err);
}

static GtkWidget *
_shell_get_toplevel (CcShell *shell)
{
  return GTK_WIDGET (shell);
}

static void
gdk_window_set_cb (GObject    *object,
                   GParamSpec *pspec,
                   CcWindow   *self)
{
  GdkWindow *window;
  gchar *str;

  if (!GDK_IS_X11_DISPLAY (gdk_display_get_default ()))
    return;

  window = gtk_widget_get_window (GTK_WIDGET (self));

  if (!window)
    return;

  str = g_strdup_printf ("%u", (guint) GDK_WINDOW_XID (window));
  g_setenv ("GNOME_CONTROL_CENTER_XID", str, TRUE);
  g_free (str);
}

static gboolean
window_map_event_cb (GtkWidget *widget,
                     GdkEvent  *event,
                     CcWindow  *self)
{
  /* If focus ends up in a category icon view one of the items is
   * immediately selected which looks odd when we are starting up, so
   * we explicitly unset the focus here. */
  gtk_window_set_focus (GTK_WINDOW (self), NULL);
  return GDK_EVENT_PROPAGATE;
}

/* GObject Implementation */
static void
cc_window_get_property (GObject    *object,
                        guint       property_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
  CcWindow *self = CC_WINDOW (object);

  switch (property_id)
    {
    case PROP_ACTIVE_PANEL:
      g_value_set_object (value, self->active_panel);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
set_active_panel (CcWindow *shell,
                  CcPanel *panel)
{
  g_return_if_fail (CC_IS_SHELL (shell));
  g_return_if_fail (panel == NULL || CC_IS_PANEL (panel));

  if (panel != shell->active_panel)
    {
      /* remove the old panel */
      g_clear_object (&shell->active_panel);

      /* set the new panel */
      if (panel)
        {
          shell->active_panel = g_object_ref (panel);
        }
      else
        {
          shell_show_overview_page (shell);
        }
      g_object_notify (G_OBJECT (shell), "active-panel");
    }
}

static void
cc_window_set_property (GObject      *object,
                        guint         property_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
  CcWindow *shell = CC_WINDOW (object);

  switch (property_id)
    {
    case PROP_ACTIVE_PANEL:
      set_active_panel (shell, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
cc_window_dispose (GObject *object)
{
  CcWindow *self = CC_WINDOW (object);

  /* Avoid receiving notifications about the pages changing
   * when destroying the children one-by-one */
  if (self->stack)
    {
      g_signal_handlers_disconnect_by_func (self->stack, stack_page_notify_cb, object);
      self->stack = NULL;
    }

  g_free (self->current_panel_id);
  self->current_panel_id = NULL;

  if (self->custom_widgets)
    {
      g_ptr_array_unref (self->custom_widgets);
      self->custom_widgets = NULL;
    }

  g_clear_object (&self->store);
  g_clear_object (&self->active_panel);

  G_OBJECT_CLASS (cc_window_parent_class)->dispose (object);
}

static void
cc_window_finalize (GObject *object)
{
  CcWindow *self = CC_WINDOW (object);

  if (self->previous_panels)
    {
      g_queue_free_full (self->previous_panels, g_free);
      self->previous_panels = NULL;
    }

  G_OBJECT_CLASS (cc_window_parent_class)->finalize (object);
}

static void
cc_shell_iface_init (CcShellInterface *iface)
{
  iface->set_active_panel_from_id = _shell_set_active_panel_from_id;
  iface->embed_widget_in_header = _shell_embed_widget_in_header;
  iface->get_toplevel = _shell_get_toplevel;
}

static void
cc_window_class_init (CcWindowClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = cc_window_get_property;
  object_class->set_property = cc_window_set_property;
  object_class->dispose = cc_window_dispose;
  object_class->finalize = cc_window_finalize;

  g_object_class_override_property (object_class, PROP_ACTIVE_PANEL, "active-panel");

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/ControlCenter/gtk/window.ui");

  gtk_widget_class_bind_template_child (widget_class, CcWindow, header);
  gtk_widget_class_bind_template_child (widget_class, CcWindow, header_box);
  gtk_widget_class_bind_template_child (widget_class, CcWindow, header_sizegroup);
  gtk_widget_class_bind_template_child (widget_class, CcWindow, list_scrolled);
  gtk_widget_class_bind_template_child (widget_class, CcWindow, lock_button);
  gtk_widget_class_bind_template_child (widget_class, CcWindow, panel_headerbar);
  gtk_widget_class_bind_template_child (widget_class, CcWindow, previous_button);
  gtk_widget_class_bind_template_child (widget_class, CcWindow, search_bar);
  gtk_widget_class_bind_template_child (widget_class, CcWindow, search_button);
  gtk_widget_class_bind_template_child (widget_class, CcWindow, search_entry);
  gtk_widget_class_bind_template_child (widget_class, CcWindow, stack);
  gtk_widget_class_bind_template_child (widget_class, CcWindow, top_right_box);

  gtk_widget_class_bind_template_callback (widget_class, previous_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, gdk_window_set_cb);
  gtk_widget_class_bind_template_callback (widget_class, row_selected_cb);
  gtk_widget_class_bind_template_callback (widget_class, search_entry_activate_cb);
  gtk_widget_class_bind_template_callback (widget_class, search_entry_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, sidelist_size_allocate_cb);
  gtk_widget_class_bind_template_callback (widget_class, stack_page_notify_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_map_event_cb);
}

static gboolean
window_button_release_event (GtkWidget          *win,
			     GdkEventButton     *event,
			     CcWindow           *self)
{
  /* back button */
  if (event->button == MOUSE_BACK_BUTTON)
    shell_show_overview_page (self);
  return FALSE;
}

static gboolean
window_key_press_event (GtkWidget   *win,
                        GdkEventKey *event,
                        CcWindow    *self)
{
  GdkKeymap *keymap;
  gboolean retval;
  GdkModifierType state;
  gboolean is_rtl;

  retval = GDK_EVENT_PROPAGATE;
  state = event->state;
  keymap = gdk_keymap_get_default ();
  gdk_keymap_add_virtual_modifiers (keymap, &state);
  state = state & gtk_accelerator_get_default_mod_mask ();
  is_rtl = gtk_widget_get_direction (win) == GTK_TEXT_DIR_RTL;

  if (gtk_search_bar_handle_event (GTK_SEARCH_BAR (self->search_bar), (GdkEvent*) event) == GDK_EVENT_STOP)
    return GDK_EVENT_STOP;

  if (state == GDK_CONTROL_MASK)
    {
      switch (event->keyval)
        {
          case GDK_KEY_s:
          case GDK_KEY_S:
          case GDK_KEY_f:
          case GDK_KEY_F:
            retval = !gtk_search_bar_get_search_mode (GTK_SEARCH_BAR (self->search_bar));
            gtk_search_bar_set_search_mode (GTK_SEARCH_BAR (self->search_bar), retval);
            if (retval)
              gtk_widget_grab_focus (self->search_entry);
            retval = GDK_EVENT_STOP;
            break;
          case GDK_KEY_Q:
          case GDK_KEY_q:
            gtk_widget_destroy (GTK_WIDGET (self));
            retval = GDK_EVENT_STOP;
            break;
          case GDK_KEY_W:
          case GDK_KEY_w:
            retval = GDK_EVENT_STOP;
            break;
        }
    }
  else if ((!is_rtl && state == GDK_MOD1_MASK && event->keyval == GDK_KEY_Left) ||
           (is_rtl && state == GDK_MOD1_MASK && event->keyval == GDK_KEY_Right) ||
           event->keyval == GDK_KEY_Back)
    {
      previous_button_clicked_cb (NULL, self);
      retval = GDK_EVENT_STOP;
    }

  return retval;
}

static void
create_window (CcWindow *self)
{
  AtkObject *accessible;

  /* previous button */
  accessible = gtk_widget_get_accessible (self->previous_button);
  atk_object_set_name (accessible, _("All Settings"));

  gtk_window_set_titlebar (GTK_WINDOW (self), self->header_box);
  gtk_widget_show_all (self->header_box);

  /*
   * We have to create the listbox here because declaring it in window.ui
   * and letting GtkBuilder handle it would hit the bug where the focus is
   * not tracked.
   */
  self->listbox = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (self->listbox), GTK_SELECTION_BROWSE);
  gtk_list_box_set_filter_func (GTK_LIST_BOX (self->listbox), filter_func, self, NULL);

  g_signal_connect (self->listbox, "row-selected", G_CALLBACK (row_selected_cb), self);

  gtk_container_add (GTK_CONTAINER (self->list_scrolled), self->listbox);
  gtk_widget_show (self->listbox);

  setup_model (self);

  /* connect various signals */
  g_signal_connect_after (self, "key_press_event",
                          G_CALLBACK (window_key_press_event), self);
  gtk_widget_add_events (GTK_WIDGET (self), GDK_BUTTON_RELEASE_MASK);
  g_signal_connect (self, "button-release-event",
                    G_CALLBACK (window_button_release_event), self);
}

static void
cc_window_init (CcWindow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  create_window (self);

  self->previous_panels = g_queue_new ();

  /* keep a list of custom widgets to unload on panel change */
  self->custom_widgets = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);

  stack_page_notify_cb (GTK_STACK (self->stack), NULL, self);
}

CcWindow *
cc_window_new (GtkApplication *application)
{
  g_return_val_if_fail (GTK_IS_APPLICATION (application), NULL);

  return g_object_new (CC_TYPE_WINDOW,
                       "application", application,
                       "resizable", TRUE,
                       "title", _("Settings"),
                       "icon-name", DEFAULT_WINDOW_ICON_NAME,
                       "window-position", GTK_WIN_POS_CENTER,
                       NULL);
}

void
cc_window_present (CcWindow *center)
{
  gtk_window_present (GTK_WINDOW (center));
}

void
cc_window_show (CcWindow *center)
{
  gtk_window_present (GTK_WINDOW (center));
}