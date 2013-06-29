/* See LICENSE file for license and copyright information */

#include <girara/session.h>
#include <girara/settings.h>
#include <girara/datastructures.h>
#include <girara/shortcuts.h>
#include <girara/utils.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "callbacks.h"
#include "shortcuts.h"
#include "document.h"
#include "zathura.h"
#include "render.h"
#include "utils.h"
#include "page.h"
#include "print.h"
#include "page-widget.h"
#include "adjustment.h"

/* Helper function; see sc_display_link and sc_follow. */
static bool
draw_links(zathura_t* zathura)
{
  /* set pages to draw links */
  bool show_links = false;
  unsigned int page_offset = 0;
  unsigned int number_of_pages = zathura_document_get_number_of_pages(zathura->document);
  for (unsigned int page_id = 0; page_id < number_of_pages; page_id++) {
    zathura_page_t* page = zathura_document_get_page(zathura->document, page_id);
    if (page == NULL) {
      continue;
    }

    GtkWidget* page_widget = zathura_page_get_widget(zathura, page);
    g_object_set(page_widget, "search-results", NULL, NULL);
    if (zathura_page_get_visibility(page) == true) {
      g_object_set(page_widget, "draw-links", TRUE, NULL);

      int number_of_links = 0;
      g_object_get(page_widget, "number-of-links", &number_of_links, NULL);
      if (number_of_links != 0) {
        show_links = true;
      }
      g_object_set(page_widget, "offset-links", page_offset, NULL);
      page_offset += number_of_links;
    } else {
      g_object_set(page_widget, "draw-links", FALSE, NULL);
    }
  }
  return show_links;
}

bool
sc_abort(girara_session_t* session, girara_argument_t* UNUSED(argument),
         girara_event_t* UNUSED(event), unsigned int UNUSED(t))
{
  g_return_val_if_fail(session != NULL, false);
  g_return_val_if_fail(session->global.data != NULL, false);
  zathura_t* zathura = session->global.data;

  bool clear_search = true;
  girara_setting_get(session, "abort-clear-search", &clear_search);

  if (zathura->document != NULL) {
    unsigned int number_of_pages = zathura_document_get_number_of_pages(zathura->document);
    for (unsigned int page_id = 0; page_id < number_of_pages; ++page_id) {
      zathura_page_t* page = zathura_document_get_page(zathura->document, page_id);
      if (page == NULL) {
        continue;
      }

      g_object_set(zathura_page_get_widget(zathura, page), "draw-links", FALSE, NULL);
      if (clear_search == true) {
        g_object_set(zathura_page_get_widget(zathura, page), "search-results", NULL, NULL);
      }
    }
  }

  girara_mode_set(session, session->modes.normal);
  girara_sc_abort(session, NULL, NULL, 0);

  return false;
}

bool
sc_adjust_window(girara_session_t* session, girara_argument_t* argument,
                 girara_event_t* UNUSED(event), unsigned int UNUSED(t))
{
  g_return_val_if_fail(session != NULL, false);
  g_return_val_if_fail(session->global.data != NULL, false);
  zathura_t* zathura = session->global.data;
  g_return_val_if_fail(argument != NULL, false);

  unsigned int pages_per_row = 1;
  girara_setting_get(session, "pages-per-row", &pages_per_row);

  unsigned int first_page_column = 1;
  girara_setting_get(session, "first-page-column", &first_page_column);

  int padding = 1;
  girara_setting_get(zathura->ui.session, "page-padding", &padding);

  if (zathura->ui.page_widget == NULL || zathura->document == NULL) {
    goto error_ret;
  }

  zathura_document_set_adjust_mode(zathura->document, argument->n);
  if (argument->n == ZATHURA_ADJUST_NONE) {
    /* there is nothing todo */
    goto error_ret;
  }

  /* get window size */
  GtkAllocation allocation;
  gtk_widget_get_allocation(session->gtk.view, &allocation);
  unsigned int width  = allocation.width;
  unsigned int height = allocation.height;

  /* scrollbar spacing */
  gint spacing;
  gtk_widget_style_get(session->gtk.view, "scrollbar_spacing", &spacing, NULL);
  width -= spacing;

  /* correct view size */
  if (gtk_widget_get_visible(GTK_WIDGET(session->gtk.inputbar)) == true) {
    gtk_widget_get_allocation(session->gtk.inputbar, &allocation);
    height += allocation.height;
  }

  double scale = 1.0;
  unsigned int cell_height = 0, cell_width = 0;
  unsigned int document_height = 0, document_width = 0;

  zathura_document_set_scale(zathura->document, scale);
  zathura_document_get_cell_size(zathura->document, &cell_height, &cell_width);
  zathura_get_document_size(zathura, cell_height, cell_width,
                            &document_height, &document_width);

  double page_ratio   = (double)cell_height / (double)document_width;
  double window_ratio = (double)height / (double)width;

  if (argument->n == ZATHURA_ADJUST_WIDTH ||
      (argument->n == ZATHURA_ADJUST_BESTFIT && page_ratio < window_ratio)) {
    scale = (double)(width - (pages_per_row - 1) * padding) /
            (double)(pages_per_row * cell_width);
    zathura_document_set_scale(zathura->document, scale);

    bool show_scrollbars = false;
    girara_setting_get(session, "show-scrollbars", &show_scrollbars);

    if (show_scrollbars) {
      /* If the document is taller than the view, there's a vertical
       * scrollbar; we need to substract its width from the view's width. */
      zathura_get_document_size(zathura, cell_height, cell_width,
                                &document_height, &document_width);
      if (height < document_height) {
        GtkWidget* vscrollbar = gtk_scrolled_window_get_vscrollbar(
            GTK_SCROLLED_WINDOW(session->gtk.view));

        if (vscrollbar != NULL) {
          GtkRequisition requisition;
          gtk_widget_get_requisition(vscrollbar, &requisition);
          if (0 < requisition.width && (unsigned)requisition.width < width) {
            width -= requisition.width;
            scale = (double)(width - (pages_per_row - 1) * padding) /
                    (double)(pages_per_row * cell_width);
            zathura_document_set_scale(zathura->document, scale);
          }
        }
      }
    }
  }
  else if (argument->n == ZATHURA_ADJUST_BESTFIT) {
    scale = (double)height / (double)cell_height;
    zathura_document_set_scale(zathura->document, scale);
  }
  else {
    goto error_ret;
  }

  /* re-render all pages */
  render_all(zathura);

error_ret:

  return false;
}

bool
sc_change_mode(girara_session_t* session, girara_argument_t* argument,
               girara_event_t* UNUSED(event), unsigned int UNUSED(t))
{
  g_return_val_if_fail(session != NULL, false);

  girara_mode_set(session, argument->n);

  return false;
}

bool
sc_display_link(girara_session_t* session, girara_argument_t* UNUSED(argument),
                girara_event_t* UNUSED(event), unsigned int UNUSED(t))
{
  g_return_val_if_fail(session != NULL, false);
  g_return_val_if_fail(session->global.data != NULL, false);
  zathura_t* zathura = session->global.data;

  if (zathura->document == NULL || zathura->ui.session == NULL) {
    return false;
  }

  bool show_links = draw_links(zathura);

  /* ask for input */
  if (show_links) {
    zathura_document_set_adjust_mode(zathura->document, ZATHURA_ADJUST_INPUTBAR);
    girara_dialog(zathura->ui.session, "Display link:", FALSE, NULL,
        (girara_callback_inputbar_activate_t) cb_sc_display_link,
        zathura->ui.session);
  }

  return false;
}

bool
sc_focus_inputbar(girara_session_t* session, girara_argument_t* argument, girara_event_t* UNUSED(event), unsigned int UNUSED(t))
{
  g_return_val_if_fail(session != NULL, false);
  g_return_val_if_fail(session->gtk.inputbar_entry != NULL, false);
  g_return_val_if_fail(session->global.data != NULL, false);
  zathura_t* zathura = session->global.data;
  g_return_val_if_fail(argument != NULL, false);

  zathura_document_set_adjust_mode(zathura->document, ZATHURA_ADJUST_INPUTBAR);

  if (gtk_widget_get_visible(GTK_WIDGET(session->gtk.inputbar)) == false) {
    gtk_widget_show(GTK_WIDGET(session->gtk.inputbar));
  }

  if (gtk_widget_get_visible(GTK_WIDGET(session->gtk.notification_area)) == true) {
    gtk_widget_hide(GTK_WIDGET(session->gtk.notification_area));
  }

  gtk_widget_grab_focus(GTK_WIDGET(session->gtk.inputbar_entry));

  if (argument->data != NULL) {
    gtk_entry_set_text(session->gtk.inputbar_entry, (char*) argument->data);

    /* append filepath */
    if (argument->n == APPEND_FILEPATH && zathura->document != NULL) {
      const char* file_path = zathura_document_get_path(zathura->document);
      if (file_path == NULL) {
        return false;
      }

      char* path = g_path_get_dirname(file_path);
      char* escaped = girara_escape_string(path);
      char* tmp  = g_strdup_printf("%s%s/", (char*) argument->data, (g_strcmp0(path, "/") == 0) ? "" : escaped);
      g_free(path);
      g_free(escaped);

      gtk_entry_set_text(session->gtk.inputbar_entry, tmp);
      g_free(tmp);
    }

    /* we save the X clipboard that will be clear by "grab_focus" */
    gchar* x_clipboard_text = gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_PRIMARY));

    gtk_editable_set_position(GTK_EDITABLE(session->gtk.inputbar_entry), -1);

    if (x_clipboard_text != NULL) {
      /* we reset the X clipboard with saved text */
      gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_PRIMARY), x_clipboard_text, -1);
      g_free(x_clipboard_text);
    }
  }

  return true;
}

bool
sc_follow(girara_session_t* session, girara_argument_t* UNUSED(argument),
          girara_event_t* UNUSED(event), unsigned int UNUSED(t))
{
  g_return_val_if_fail(session != NULL, false);
  g_return_val_if_fail(session->global.data != NULL, false);
  zathura_t* zathura = session->global.data;

  if (zathura->document == NULL || zathura->ui.session == NULL) {
    return false;
  }

  bool show_links = draw_links(zathura);

  /* ask for input */
  if (show_links == true) {
    zathura_document_set_adjust_mode(zathura->document, ZATHURA_ADJUST_INPUTBAR);
    girara_dialog(zathura->ui.session, "Follow link:", FALSE, NULL, (girara_callback_inputbar_activate_t) cb_sc_follow, zathura->ui.session);
  }

  return false;
}

bool
sc_goto(girara_session_t* session, girara_argument_t* argument, girara_event_t* UNUSED(event), unsigned int t)
{
  g_return_val_if_fail(session != NULL, false);
  g_return_val_if_fail(session->global.data != NULL, false);
  zathura_t* zathura = session->global.data;
  g_return_val_if_fail(argument != NULL, false);
  g_return_val_if_fail(zathura->document != NULL, false);

  zathura_jumplist_save(zathura);
  if (t != 0) {
    /* add offset */
    t += zathura_document_get_page_offset(zathura->document);

    page_set(zathura, t - 1);
  } else if (argument->n == TOP) {
    page_set(zathura, 0);
  } else if (argument->n == BOTTOM) {
    page_set(zathura, zathura_document_get_number_of_pages(zathura->document) - 1);
  }

  /* adjust horizontal position */
  GtkAdjustment* hadjustment = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(session->gtk.view));
  cb_view_hadjustment_changed(hadjustment, zathura);

  zathura_jumplist_add(zathura);

  return false;
}

bool
sc_mouse_scroll(girara_session_t* session, girara_argument_t* argument, girara_event_t* event, unsigned int t)
{
  g_return_val_if_fail(session != NULL, false);
  g_return_val_if_fail(session->global.data != NULL, false);
  zathura_t* zathura = session->global.data;
  g_return_val_if_fail(argument != NULL, false);
  g_return_val_if_fail(event != NULL, false);

  if (zathura->document == NULL) {
    return false;
  }

  static int x = 0;
  static int y = 0;

  GtkAdjustment* x_adj = NULL;
  GtkAdjustment* y_adj = NULL;

  switch (event->type) {
      /* scroll */
    case GIRARA_EVENT_SCROLL_UP:
    case GIRARA_EVENT_SCROLL_DOWN:
    case GIRARA_EVENT_SCROLL_LEFT:
    case GIRARA_EVENT_SCROLL_RIGHT:
      return sc_scroll(session, argument, NULL, t);

      /* drag */
    case GIRARA_EVENT_BUTTON_PRESS:
      x = event->x;
      y = event->y;
      break;
    case GIRARA_EVENT_BUTTON_RELEASE:
      x = 0;
      y = 0;
      break;
    case GIRARA_EVENT_MOTION_NOTIFY:
      x_adj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(session->gtk.view));
      y_adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(session->gtk.view));

      if (x_adj == NULL || y_adj == NULL) {
        return false;
      }

      zathura_adjustment_set_value(x_adj,
          gtk_adjustment_get_value(x_adj) - (event->x - x));
      zathura_adjustment_set_value(y_adj,
          gtk_adjustment_get_value(y_adj) - (event->y - y));
      break;

      /* unhandled events */
    default:
      break;
  }

  return false;
}

bool
sc_mouse_zoom(girara_session_t* session, girara_argument_t* argument, girara_event_t* event, unsigned int t)
{
  g_return_val_if_fail(session != NULL, false);
  g_return_val_if_fail(session->global.data != NULL, false);
  zathura_t* zathura = session->global.data;
  g_return_val_if_fail(argument != NULL, false);
  g_return_val_if_fail(event != NULL, false);

  if (zathura->document == NULL) {
    return false;
  }

  /* scroll event */
  switch (event->type) {
    case GIRARA_EVENT_SCROLL_UP:
      argument->n = ZOOM_IN;
      break;
    case GIRARA_EVENT_SCROLL_DOWN:
      argument->n = ZOOM_OUT;
      break;
    default:
      return false;
  }

  return sc_zoom(session, argument, NULL, t);
}

bool
sc_navigate(girara_session_t* session, girara_argument_t* argument,
            girara_event_t* UNUSED(event), unsigned int t)
{
  g_return_val_if_fail(session != NULL, false);
  g_return_val_if_fail(session->global.data != NULL, false);
  zathura_t* zathura = session->global.data;
  g_return_val_if_fail(argument != NULL, false);
  g_return_val_if_fail(zathura->document != NULL, false);

  int number_of_pages = zathura_document_get_number_of_pages(zathura->document);
  int new_page        = zathura_document_get_current_page_number(zathura->document);

  bool scroll_wrap = false;
  girara_setting_get(session, "scroll-wrap", &scroll_wrap);

  bool columns_per_row_offset = false;
  girara_setting_get(session, "advance-pages-per-row", &columns_per_row_offset);

  int offset = 1;
  if (columns_per_row_offset == true) {
    girara_setting_get(session, "pages-per-row", &offset);
  }

  t = (t == 0) ? (unsigned int) offset : t;
  if (argument->n == NEXT) {
    if (scroll_wrap == false) {
      new_page = new_page + t;
    } else {
      new_page = (new_page + t) % number_of_pages;
    }
  } else if (argument->n == PREVIOUS) {
    if (scroll_wrap == false) {
      new_page = new_page - t;
    } else {
      new_page = (new_page + number_of_pages - t) % number_of_pages;
    }
  }

  if ((new_page < 0 || new_page >= number_of_pages) && !scroll_wrap) {
    return false;
  }

  page_set(zathura, new_page);

  /* adjust horizontal position */
  GtkAdjustment* hadjustment = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(session->gtk.view));
  cb_view_hadjustment_changed(hadjustment, zathura);

  return false;
}

bool
sc_print(girara_session_t* session, girara_argument_t* UNUSED(argument),
         girara_event_t* UNUSED(event), unsigned int UNUSED(t))
{
  g_return_val_if_fail(session != NULL, false);
  g_return_val_if_fail(session->global.data != NULL, false);
  zathura_t* zathura = session->global.data;

  if (zathura->document == NULL) {
    girara_notify(session, GIRARA_ERROR, _("No document opened."));
    return false;
  }

  print(zathura);

  return true;
}

bool
sc_recolor(girara_session_t* session, girara_argument_t* UNUSED(argument),
           girara_event_t* UNUSED(event), unsigned int UNUSED(t))
{
  g_return_val_if_fail(session != NULL, false);

  bool value = false;
  girara_setting_get(session, "recolor", &value);
  value = !value;
  girara_setting_set(session, "recolor", &value);

  return false;
}

bool
sc_reload(girara_session_t* session, girara_argument_t* UNUSED(argument),
          girara_event_t* UNUSED(event), unsigned int UNUSED(t))
{
  g_return_val_if_fail(session != NULL, false);
  g_return_val_if_fail(session->global.data != NULL, false);
  zathura_t* zathura = session->global.data;

  if (zathura->file_monitor.file_path == NULL) {
    return false;
  }

  /* close current document */
  document_close(zathura, true);

  /* reopen document */
  document_open(zathura, zathura->file_monitor.file_path,
                zathura->file_monitor.password,
                ZATHURA_PAGE_NUMBER_UNSPECIFIED);

  return false;
}

bool
sc_rotate(girara_session_t* session, girara_argument_t* argument,
          girara_event_t* UNUSED(event), unsigned int t)
{
  g_return_val_if_fail(session != NULL, false);
  g_return_val_if_fail(session->global.data != NULL, false);
  zathura_t* zathura = session->global.data;
  g_return_val_if_fail(zathura->document != NULL, false);

  unsigned int page_number = zathura_document_get_current_page_number(zathura->document);

  int angle = 90;
  if (argument != NULL && argument->n == ROTATE_CCW) {
    angle = 270;
  }

  /* update rotate value */
  t = (t == 0) ? 1 : t;
  unsigned int rotation = zathura_document_get_rotation(zathura->document);
  zathura_document_set_rotation(zathura->document, (rotation + angle * t) % 360);

  /* update scale */
  girara_argument_t new_argument = { zathura_document_get_adjust_mode(zathura->document), NULL };
  sc_adjust_window(zathura->ui.session, &new_argument, NULL, 0);

  /* render all pages again */
  render_all(zathura);

  page_set_delayed(zathura, page_number);

  return false;
}

bool
sc_scroll(girara_session_t* session, girara_argument_t* argument,
          girara_event_t* UNUSED(event), unsigned int t)
{
  g_return_val_if_fail(session != NULL, false);
  g_return_val_if_fail(session->global.data != NULL, false);
  zathura_t* zathura = session->global.data;
  g_return_val_if_fail(argument != NULL, false);
  if (zathura->document == NULL) {
    return false;
  }

  if (t == 0) {
    t = 1;
  }

  GtkAdjustment* adjustment = NULL;
  if ( (argument->n == LEFT) || (argument->n == FULL_LEFT) || (argument->n == HALF_LEFT) ||
       (argument->n == RIGHT) || (argument->n == FULL_RIGHT) || (argument->n == HALF_RIGHT)) {
    adjustment = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(session->gtk.view));
  } else {
    adjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(session->gtk.view));
  }

  gdouble view_size                  = gtk_adjustment_get_page_size(adjustment);
  gdouble value                      = gtk_adjustment_get_value(adjustment);
  gdouble max                        = gtk_adjustment_get_upper(adjustment) - view_size;
  zathura->global.update_page_number = true;

  float scroll_step = 40;
  girara_setting_get(session, "scroll-step", &scroll_step);
  float scroll_hstep = -1;
  girara_setting_get(session, "scroll-hstep", &scroll_hstep);
  if (scroll_hstep < 0) {
    scroll_hstep = scroll_step;
  }
  float scroll_full_overlap = 0.0;
  girara_setting_get(session, "scroll-full-overlap", &scroll_full_overlap);
  bool scroll_page_aware = false;
  girara_setting_get(session, "scroll-page-aware", &scroll_page_aware);

  bool scroll_wrap = false;
  girara_setting_get(session, "scroll-wrap", &scroll_wrap);

  int padding = 1;
  girara_setting_get(session, "page-padding", &padding);

  gdouble new_value;

  switch(argument->n) {
    case FULL_UP:
    case FULL_LEFT:
      new_value = value - (1.0 - scroll_full_overlap) * view_size - padding;
      break;
    case FULL_DOWN:
    case FULL_RIGHT:
      new_value = value + (1.0 - scroll_full_overlap) * view_size + padding;
      break;
    case HALF_UP:
    case HALF_LEFT:
      new_value = value - ((view_size + padding) / 2);
      break;
    case HALF_DOWN:
    case HALF_RIGHT:
      new_value = value + ((view_size + padding) / 2);
      break;
    case LEFT:
      new_value = value - scroll_hstep * t;
      break;
    case UP:
      new_value = value - scroll_step * t;
      break;
    case RIGHT:
      new_value = value + scroll_hstep * t;
      break;
    case DOWN:
      new_value = value + scroll_step * t;
      break;
    case TOP:
      new_value = 0;
      break;
    case BOTTOM:
      new_value = max;
      break;
    default:
      new_value = value;
  }

  if (scroll_wrap == true) {
    if (new_value < 0)
      new_value = max;
    else if (new_value > max)
      new_value = 0;
  }

  if (scroll_page_aware == true) {
    int page_offset;
    double page_size;

    {
      unsigned int page_id = zathura_document_get_current_page_number(zathura->document);
      zathura_page_t* page = zathura_document_get_page(zathura->document, page_id);
      page_offset_t offset;
      page_calculate_offset(zathura, page, &offset);

      double scale = zathura_document_get_scale(zathura->document);

      if ((argument->n == LEFT) || (argument->n == FULL_LEFT) || (argument->n == HALF_LEFT) ||
          (argument->n == RIGHT) || (argument->n == FULL_RIGHT) || (argument->n == HALF_RIGHT)) {
        page_offset = offset.x;
        page_size = zathura_page_get_width(page) * scale;
      } else {
        page_offset = offset.y;
        page_size = zathura_page_get_height(page) * scale;
      }

      page_offset -= padding / 2;
      page_size   += padding;
    }

    if ((argument->n == FULL_DOWN) || (argument->n == HALF_DOWN) ||
        (argument->n == FULL_RIGHT) || (argument->n == HALF_RIGHT)) {
      if ((page_offset > value) &&
          (page_offset < value + view_size)) {
        new_value = page_offset;
      } else if ((page_offset <= value) &&
                 (page_offset + page_size < value + view_size)) {
        new_value = page_offset + page_size + 1;
      } else if ((page_offset <= value) &&
                 (page_offset + page_size < new_value + view_size)) {
        new_value = page_offset + page_size - view_size + 1;
      }
    } else if ((argument->n == FULL_UP) || (argument->n == HALF_UP) ||
               (argument->n == FULL_LEFT) || (argument->n == HALF_LEFT)) {
      if ((page_offset + 1 >= value) &&
          (page_offset < value + view_size)) {
        new_value = page_offset - view_size;
      } else if ((page_offset <= value) &&
                 (page_offset + page_size + 1 < value + view_size)) {
        new_value = page_offset + page_size - view_size;
      } else if ((page_offset <= value) &&
                 (page_offset > new_value)) {
        new_value = page_offset;
      }
    }
  }

  zathura_adjustment_set_value(adjustment, new_value);

  return false;
}


bool
sc_jumplist(girara_session_t* session, girara_argument_t* argument,
            girara_event_t* UNUSED(event), unsigned int UNUSED(t))
{
  g_return_val_if_fail(session != NULL, false);
  g_return_val_if_fail(session->global.data != NULL, false);
  zathura_t* zathura = session->global.data;
  g_return_val_if_fail(argument != NULL, false);
  g_return_val_if_fail(zathura->document != NULL, false);

  zathura_jump_t* jump = NULL;
  switch(argument->n) {
    case FORWARD:
      zathura_jumplist_save(zathura);
      zathura_jumplist_forward(zathura);
      jump = zathura_jumplist_current(zathura);
      break;

    case BACKWARD:
      zathura_jumplist_save(zathura);
      zathura_jumplist_backward(zathura);
      jump = zathura_jumplist_current(zathura);
      break;
  }

  if (jump != NULL) {
    page_set(zathura, jump->page);
    const double s = zathura_document_get_scale(zathura->document);
    position_set_delayed(zathura, jump->x * s, jump->y * s);
  }

  return false;
}


bool
sc_bisect(girara_session_t* session, girara_argument_t* argument,
          girara_event_t* UNUSED(event), unsigned int t)
{
  g_return_val_if_fail(session != NULL, false);
  g_return_val_if_fail(session->global.data != NULL, false);
  zathura_t* zathura = session->global.data;
  g_return_val_if_fail(argument != NULL, false);
  g_return_val_if_fail(zathura->document != NULL, false);

  unsigned int number_of_pages, cur_page, prev_page, prev2_page;
  bool have_prev, have_prev2;

  zathura_jump_t* prev_jump = NULL;
  zathura_jump_t* prev2_jump = NULL;

  number_of_pages= zathura_document_get_number_of_pages(zathura->document);
  cur_page = zathura_document_get_current_page_number(zathura->document);

  prev_page = prev2_page = 0;
  have_prev = have_prev2 = false;

  /* save position at current jump point */
  zathura_jumplist_save(zathura);

  /* process arguments */
  int direction;
  if (t > 0 && t <= number_of_pages) {
    /* jump to page t, and bisect between cur_page and t */
    page_set(zathura, t-1);
    zathura_jumplist_add(zathura);
    if (t-1 > cur_page) {
      direction = BACKWARD;
    } else {
      direction = FORWARD;
    }

  } else if (argument != NULL)  {
    direction = argument->n;

  } else {
    return false;
  }

  cur_page = zathura_document_get_current_page_number(zathura->document);

  if (zathura_jumplist_has_previous(zathura)) {
    /* If there is a previous jump, get its page */
    zathura_jumplist_backward(zathura);
    prev_jump = zathura_jumplist_current(zathura);
    if (prev_jump) {
      prev_page = prev_jump->page;
      have_prev = true;
    }

    if (zathura_jumplist_has_previous(zathura)) {
      /* If there is a second previous jump, get its page. */
      zathura_jumplist_backward(zathura);
      prev2_jump = zathura_jumplist_current(zathura);
      if (prev2_jump) {
        prev2_page = prev2_jump->page;
        have_prev2 = true;
      }
      zathura_jumplist_forward(zathura);
    }
    zathura_jumplist_forward(zathura);
  }

  /* now, we are back at the initial jump. prev_page and prev2_page contain
   the pages for previous and second previous jump if they exist. */

  /* bisect */
  switch(direction) {
    case FORWARD:
      if (have_prev && cur_page <= prev_page) {
        /* add a new jump point */
        if (cur_page < prev_page) {
          page_set(zathura, (cur_page + prev_page)/2);
          zathura_jumplist_add(zathura);
        }

      } else if (have_prev2 && cur_page <= prev2_page) {
        /* save current position at previous jump point */
        if (cur_page < prev2_page) {
          zathura_jumplist_backward(zathura);
          zathura_jumplist_save(zathura);
          zathura_jumplist_forward(zathura);

          page_set(zathura, (cur_page + prev2_page)/2);
          zathura_jumplist_save(zathura);
        }
      } else {
        /* none of prev_page or prev2_page comes after cur_page */
        page_set(zathura, (cur_page + number_of_pages - 1)/2);
        zathura_jumplist_add(zathura);
      }
      break;

    case BACKWARD:
      if (have_prev && prev_page <= cur_page) {
        /* add a new jump point */
        if (prev_page < cur_page) {
          page_set(zathura, (cur_page + prev_page)/2);
          zathura_jumplist_add(zathura);
        }

      } else if (have_prev2 && prev2_page <= cur_page) {
        /* save current position at previous jump point */
        if (prev2_page < cur_page) {
          zathura_jumplist_backward(zathura);
          zathura_jumplist_save(zathura);
          zathura_jumplist_forward(zathura);

          page_set(zathura, (cur_page + prev2_page)/2);
          zathura_jumplist_save(zathura);
        }

      } else {
        /* none of prev_page or prev2_page comes before cur_page */
        page_set(zathura, cur_page/2);
        zathura_jumplist_add(zathura);
      }
      break;
  }

  /* adjust horizontal position */
  GtkAdjustment* hadjustment = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(session->gtk.view));
  cb_view_hadjustment_changed(hadjustment, zathura);

  return false;
}


bool
sc_search(girara_session_t* session, girara_argument_t* argument,
          girara_event_t* UNUSED(event), unsigned int UNUSED(t))
{
  g_return_val_if_fail(session != NULL, false);
  g_return_val_if_fail(session->global.data != NULL, false);
  zathura_t* zathura = session->global.data;
  g_return_val_if_fail(argument != NULL, false);
  g_return_val_if_fail(zathura->document != NULL, false);

  const int num_pages = zathura_document_get_number_of_pages(zathura->document);
  const int cur_page  = zathura_document_get_current_page_number(zathura->document);

  int diff = argument->n == FORWARD ? 1 : -1;
  if (zathura->global.search_direction == BACKWARD)
    diff = -diff;

  zathura_page_t* target_page = NULL;
  int target_idx = 0;

  for (int page_id = 0; page_id < num_pages; ++page_id) {
    int tmp = cur_page + diff * page_id;
    zathura_page_t* page = zathura_document_get_page(zathura->document, (tmp + num_pages) % num_pages);
    if (page == NULL) {
      continue;
    }

    GtkWidget* page_widget = zathura_page_get_widget(zathura, page);

    int num_search_results = 0, current = -1;
    g_object_get(page_widget, "search-current", &current,
                 "search-length", &num_search_results, NULL);
    if (num_search_results == 0 || current == -1) {
      continue;
    }

    if (diff == 1 && current < num_search_results - 1) {
      /* the next result is on the same page */
      target_page = page;
      target_idx = current + 1;
    } else if (diff == -1 && current > 0) {
      target_page = page;
      target_idx = current - 1;
    } else {
      /* the next result is on a different page */
      zathura_jumplist_save(zathura);

      g_object_set(page_widget, "search-current", -1, NULL);

      for (int npage_id = 1; page_id < num_pages; ++npage_id) {
        int ntmp = cur_page + diff * (page_id + npage_id);
        zathura_page_t* npage = zathura_document_get_page(zathura->document, (ntmp + 2*num_pages) % num_pages);
        zathura_document_set_current_page_number(zathura->document, zathura_page_get_index(npage));
        GtkWidget* npage_page_widget = zathura_page_get_widget(zathura, npage);
        g_object_get(npage_page_widget, "search-length", &num_search_results, NULL);
        if (num_search_results != 0) {
          target_page = npage;
          target_idx = diff == 1 ? 0 : num_search_results - 1;
          break;
        }
      }

      zathura_jumplist_add(zathura);
    }

    break;
  }

  if (target_page != NULL) {
    girara_list_t* results = NULL;
    GtkWidget* page_widget = zathura_page_get_widget(zathura, target_page);
    g_object_set(page_widget, "search-current", target_idx, NULL);
    g_object_get(page_widget, "search-results", &results, NULL);

    zathura_rectangle_t* rect = girara_list_nth(results, target_idx);
    zathura_rectangle_t rectangle = recalc_rectangle(target_page, *rect);
    page_offset_t offset;
    page_calculate_offset(zathura, target_page, &offset);

    GtkAdjustment* view_vadjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(zathura->ui.session->gtk.view));
    int y = offset.y - gtk_adjustment_get_page_size(view_vadjustment) / 2 + rectangle.y1;
    zathura_adjustment_set_value(view_vadjustment, y);

    bool search_hadjust = true;
    girara_setting_get(session, "search-hadjust", &search_hadjust);
    if (search_hadjust == true) {
      GtkAdjustment* view_hadjustment = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(zathura->ui.session->gtk.view));
      int x = offset.x - gtk_adjustment_get_page_size(view_hadjustment) / 2 + rectangle.x1;
      zathura_adjustment_set_value(view_hadjustment, x);
    }
  }

  return false;
}

bool
sc_navigate_index(girara_session_t* session, girara_argument_t* argument,
                  girara_event_t* UNUSED(event), unsigned int UNUSED(t))
{
  g_return_val_if_fail(session != NULL, false);
  g_return_val_if_fail(session->global.data != NULL, false);
  zathura_t* zathura = session->global.data;
  g_return_val_if_fail(argument != NULL, false);
  g_return_val_if_fail(zathura->document != NULL, false);

  if(zathura->ui.index == NULL) {
    return false;
  }

  GtkTreeView *tree_view = gtk_container_get_children(GTK_CONTAINER(zathura->ui.index))->data;
  GtkTreePath *path;

  gtk_tree_view_get_cursor(tree_view, &path, NULL);
  if (path == NULL) {
    return false;
  }

  GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
  GtkTreeIter   iter;
  GtkTreeIter   child_iter;

  gboolean is_valid_path = TRUE;

  switch(argument->n) {
    case UP:
      if (gtk_tree_path_prev(path) == FALSE) {
        /* For some reason gtk_tree_path_up returns TRUE although we're not
         * moving anywhere. */
        is_valid_path = gtk_tree_path_up(path) && (gtk_tree_path_get_depth(path) > 0);
      } else { /* row above */
        while(gtk_tree_view_row_expanded(tree_view, path)) {
          gtk_tree_model_get_iter(model, &iter, path);
          /* select last child */
          gtk_tree_model_iter_nth_child(model, &child_iter, &iter,
                                        gtk_tree_model_iter_n_children(model, &iter)-1);
          gtk_tree_path_free(path);
          path = gtk_tree_model_get_path(model, &child_iter);
        }
      }
      break;
    case COLLAPSE:
      if (gtk_tree_view_collapse_row(tree_view, path) == FALSE
          && gtk_tree_path_get_depth(path) > 1) {
        gtk_tree_path_up(path);
        gtk_tree_view_collapse_row(tree_view, path);
      }
      break;
    case DOWN:
      if (gtk_tree_view_row_expanded(tree_view, path) == TRUE) {
        gtk_tree_path_down(path);
      } else {
        do {
          gtk_tree_model_get_iter(model, &iter, path);
          if (gtk_tree_model_iter_next(model, &iter)) {
            gtk_tree_path_free(path);
            path = gtk_tree_model_get_path(model, &iter);
            break;
          }
        } while((is_valid_path = (gtk_tree_path_get_depth(path) > 1))
                && gtk_tree_path_up(path));
      }
      break;
    case EXPAND:
      if (gtk_tree_view_expand_row(tree_view, path, FALSE)) {
        gtk_tree_path_down(path);
      }
      break;
    case EXPAND_ALL:
      gtk_tree_view_expand_all(tree_view);
      break;
    case COLLAPSE_ALL:
      gtk_tree_view_collapse_all(tree_view);
      gtk_tree_path_free(path);
      path = gtk_tree_path_new_first();
      gtk_tree_view_set_cursor(tree_view, path, NULL, FALSE);
      break;
    case SELECT:
      cb_index_row_activated(tree_view, path, NULL, zathura);
      gtk_tree_path_free(path);
      return false;
  }

  if (is_valid_path) {
    gtk_tree_view_set_cursor(tree_view, path, NULL, FALSE);
  }

  gtk_tree_path_free(path);

  return false;
}

bool
sc_toggle_index(girara_session_t* session, girara_argument_t* UNUSED(argument),
                girara_event_t* UNUSED(event), unsigned int UNUSED(t))
{
  g_return_val_if_fail(session != NULL, false);
  g_return_val_if_fail(session->global.data != NULL, false);
  zathura_t* zathura = session->global.data;
  if (zathura->document == NULL) {
    return false;
  }

  girara_tree_node_t* document_index = NULL;
  GtkWidget* treeview                = NULL;
  GtkTreeModel* model                = NULL;
  GtkCellRenderer* renderer          = NULL;
  GtkCellRenderer* renderer2         = NULL;

  if (zathura->ui.index == NULL) {
    /* create new index widget */
    zathura->ui.index = gtk_scrolled_window_new(NULL, NULL);

    if (zathura->ui.index == NULL) {
      goto error_ret;
    }

    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(zathura->ui.index),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    /* create index */
    document_index = zathura_document_index_generate(zathura->document, NULL);
    if (document_index == NULL) {
      girara_notify(session, GIRARA_WARNING, _("This document does not contain any index"));
      goto error_free;
    }

    model = GTK_TREE_MODEL(gtk_tree_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER));
    if (model == NULL) {
      goto error_free;
    }

    treeview = gtk_tree_view_new_with_model(model);
    if (treeview == NULL) {
      goto error_free;
    }

    g_object_unref(model);

    renderer = gtk_cell_renderer_text_new();
    if (renderer == NULL) {
      goto error_free;
    }

    renderer2 = gtk_cell_renderer_text_new();
    if (renderer2 == NULL) {
      goto error_free;
    }

    document_index_build(model, NULL, document_index);
    girara_node_free(document_index);

    /* setup widget */
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW (treeview), 0, "Title", renderer, "markup", 0, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW (treeview), 1, "Target", renderer2, "text", 1, NULL);

    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(treeview), FALSE);
    g_object_set(G_OBJECT(renderer), "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    g_object_set(G_OBJECT(gtk_tree_view_get_column(GTK_TREE_VIEW(treeview), 0)), "expand", TRUE, NULL);
    gtk_tree_view_column_set_alignment(gtk_tree_view_get_column(GTK_TREE_VIEW(treeview), 1), 1.0f);
    gtk_tree_view_set_cursor(GTK_TREE_VIEW(treeview), gtk_tree_path_new_first(), NULL, FALSE);
    g_signal_connect(G_OBJECT(treeview), "row-activated", G_CALLBACK(cb_index_row_activated), zathura);

    gtk_container_add(GTK_CONTAINER(zathura->ui.index), treeview);
    gtk_widget_show(treeview);
  }

  static double vvalue = 0;
  static double hvalue = 0;

  if (gtk_widget_get_visible(GTK_WIDGET(zathura->ui.index))) {
    girara_set_view(session, zathura->ui.page_widget_alignment);
    gtk_widget_hide(GTK_WIDGET(zathura->ui.index));
    girara_mode_set(zathura->ui.session, zathura->modes.normal);

    /* reset adjustment */
    position_set_delayed(zathura, hvalue, vvalue);
  } else {
    /* save adjustment */
    GtkAdjustment* vadjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(session->gtk.view));
    GtkAdjustment* hadjustment = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(session->gtk.view));

    vvalue = gtk_adjustment_get_value(vadjustment);
    hvalue = gtk_adjustment_get_value(hadjustment);

    /* save current position to the jumplist */
    zathura_jumplist_save(zathura);

    girara_set_view(session, zathura->ui.index);
    gtk_widget_show(GTK_WIDGET(zathura->ui.index));
    girara_mode_set(zathura->ui.session, zathura->modes.index);
  }

  return false;

error_free:

  if (zathura->ui.index != NULL) {
    g_object_ref_sink(zathura->ui.index);
    zathura->ui.index = NULL;
  }

  if (document_index != NULL) {
    girara_node_free(document_index);
  }

error_ret:

  return false;
}

bool
sc_toggle_page_mode(girara_session_t* session, girara_argument_t*
                    UNUSED(argument), girara_event_t* UNUSED(event), unsigned int UNUSED(t))
{
  g_return_val_if_fail(session != NULL, false);
  g_return_val_if_fail(session->global.data != NULL, false);
  zathura_t* zathura = session->global.data;

  if (zathura->document == NULL) {
    girara_notify(session, GIRARA_WARNING, _("No document opened."));
    return false;
  }

  int pages_per_row = 1;
  girara_setting_get(zathura->ui.session, "pages-per-row", &pages_per_row);

  static int tmp = 2;
  int value = 1;
  if (pages_per_row == 1) {
    value = tmp;
  } else {
    tmp = pages_per_row;
  }

  girara_setting_set(zathura->ui.session, "pages-per-row", &value);

  return true;
}

bool
sc_toggle_fullscreen(girara_session_t* session, girara_argument_t*
                     UNUSED(argument), girara_event_t* UNUSED(event), unsigned int UNUSED(t))
{
  g_return_val_if_fail(session != NULL, false);
  g_return_val_if_fail(session->global.data != NULL, false);
  zathura_t* zathura = session->global.data;

  if (zathura->document == NULL) {
    girara_notify(session, GIRARA_WARNING, _("No document opened."));
    return false;
  }

  static bool fullscreen = false;
  static int pages_per_row = 1;
  static int first_page_column = 1;
  static double zoom = 1.0;

  if (fullscreen == true) {
    /* reset pages per row */
    girara_setting_set(session, "pages-per-row", &pages_per_row);

    /* reset first page column */
    girara_setting_set(session, "first-page-column", &first_page_column);

    /* show status bar if option set */
    bool statusbar_show = true;
    girara_setting_get(session, "statusbar-show", &statusbar_show);
    if (statusbar_show == true) {
      gtk_widget_show(GTK_WIDGET(session->gtk.statusbar));
    }

    /* set full screen */
    gtk_window_unfullscreen(GTK_WINDOW(session->gtk.window));

    /* reset scale */
    zathura_document_set_scale(zathura->document, zoom);
    render_all(zathura);
    page_set_delayed(zathura, zathura_document_get_current_page_number(zathura->document));

    /* set mode */
    girara_mode_set(session, zathura->modes.normal);
  } else {
    /* backup pages per row */
    girara_setting_get(session, "pages-per-row", &pages_per_row);

    /* backup first page column */
    girara_setting_get(session, "first-page-column", &first_page_column);

    /* set single view */
    int int_value = 1;
    girara_setting_set(session, "pages-per-row", &int_value);

    /* back up zoom */
    zoom = zathura_document_get_scale(zathura->document);

    /* adjust window */
    girara_argument_t argument = { ZATHURA_ADJUST_BESTFIT, NULL };
    sc_adjust_window(session, &argument, NULL, 0);

    /* hide status and inputbar */
    gtk_widget_hide(GTK_WIDGET(session->gtk.inputbar));
    gtk_widget_hide(GTK_WIDGET(session->gtk.statusbar));

    /* set full screen */
    gtk_window_fullscreen(GTK_WINDOW(session->gtk.window));
    page_set_delayed(zathura, zathura_document_get_current_page_number(zathura->document));

    /* set mode */
    girara_mode_set(session, zathura->modes.fullscreen);
  }

  fullscreen = fullscreen ? false : true;

  return false;
}

bool
sc_quit(girara_session_t* session, girara_argument_t* UNUSED(argument),
        girara_event_t* UNUSED(event), unsigned int UNUSED(t))
{
  g_return_val_if_fail(session != NULL, false);

  girara_argument_t arg = { GIRARA_HIDE, NULL };
  girara_isc_completion(session, &arg, NULL, 0);

  cb_destroy(NULL, NULL);

  return false;
}

bool
sc_zoom(girara_session_t* session, girara_argument_t* argument, girara_event_t*
        UNUSED(event), unsigned int t)
{
  g_return_val_if_fail(session != NULL, false);
  g_return_val_if_fail(session->global.data != NULL, false);
  zathura_t* zathura = session->global.data;
  g_return_val_if_fail(argument != NULL, false);
  g_return_val_if_fail(zathura->document != NULL, false);

  zathura_document_set_adjust_mode(zathura->document, ZATHURA_ADJUST_NONE);

  /* retreive zoom step value */
  int value = 1;
  girara_setting_get(zathura->ui.session, "zoom-step", &value);

  int nt = (t == 0) ? 1 : t;
  float zoom_step = value / 100.0f * nt;
  float old_zoom = zathura_document_get_scale(zathura->document);

  /* specify new zoom value */
  if (argument->n == ZOOM_IN) {
    zathura_document_set_scale(zathura->document, old_zoom + zoom_step);
  } else if (argument->n == ZOOM_OUT) {
    zathura_document_set_scale(zathura->document, old_zoom - zoom_step);
  } else if (argument->n == ZOOM_SPECIFIC) {
    if (t == 0) {
      zathura_document_set_scale(zathura->document, 1.0f);
    } else {
      zathura_document_set_scale(zathura->document, t / 100.0f);
    }
  } else {
    zathura_document_set_scale(zathura->document, 1.0f);
  }

  /* zoom limitations */
  int zoom_min_int = 10;
  int zoom_max_int = 1000;
  girara_setting_get(session, "zoom-min", &zoom_min_int);
  girara_setting_get(session, "zoom-max", &zoom_max_int);

  float zoom_min = zoom_min_int * 0.01f;
  float zoom_max = zoom_max_int * 0.01f;

  float scale = zathura_document_get_scale(zathura->document);
  if (scale < zoom_min) {
    zathura_document_set_scale(zathura->document, zoom_min);
  } else if (scale > zoom_max) {
    zathura_document_set_scale(zathura->document, zoom_max);
  }

  render_all(zathura);

  return false;
}
