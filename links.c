/* See LICENSE file for license and copyright information */

#include <glib.h>
#include <glib/gi18n.h>
#include <girara/utils.h>
#include <girara/session.h>
#include <girara/settings.h>

#include "links.h"
#include "zathura.h"
#include "document.h"
#include "utils.h"

struct zathura_link_s {
  zathura_rectangle_t position; /**< Position of the link */
  zathura_link_type_t type; /**< Link type */
  zathura_link_target_t target; /**< Link target */
};

/* forward declarations */
static void link_remote(zathura_t* zathura, const char* file);
static void link_launch(zathura_t* zathura, zathura_link_t* link);

zathura_link_t*
zathura_link_new(zathura_link_type_t type, zathura_rectangle_t position,
                 zathura_link_target_t target)
{
  zathura_link_t* link = g_malloc0(sizeof(zathura_link_t));

  link->type     = type;
  link->position = position;

  switch (type) {
    case ZATHURA_LINK_NONE:
    case ZATHURA_LINK_GOTO_DEST:
      link->target = target;

      if (target.value != NULL) {
        link->target.value = g_strdup(target.value);
      }
      break;
    case ZATHURA_LINK_GOTO_REMOTE:
    case ZATHURA_LINK_URI:
    case ZATHURA_LINK_LAUNCH:
    case ZATHURA_LINK_NAMED:
      if (target.value == NULL) {
        g_free(link);
        return NULL;
      }

      link->target.value = g_strdup(target.value);
      break;
    default:
      g_free(link);
      return NULL;
  }

  return link;
}

void
zathura_link_free(zathura_link_t* link)
{
  if (link == NULL) {
    return;
  }

  switch (link->type) {
    case ZATHURA_LINK_NONE:
    case ZATHURA_LINK_GOTO_DEST:
    case ZATHURA_LINK_GOTO_REMOTE:
    case ZATHURA_LINK_URI:
    case ZATHURA_LINK_LAUNCH:
    case ZATHURA_LINK_NAMED:
      if (link->target.value != NULL) {
        g_free(link->target.value);
      }
      break;
    default:
      break;
  }

  g_free(link);
}

zathura_link_type_t
zathura_link_get_type(zathura_link_t* link)
{
  if (link == NULL) {
    return ZATHURA_LINK_INVALID;
  }

  return link->type;
}

zathura_rectangle_t
zathura_link_get_position(zathura_link_t* link)
{
  if (link == NULL) {
    zathura_rectangle_t position = { 0, 0, 0, 0 };
    return position;
  }

  return link->position;
}

zathura_link_target_t
zathura_link_get_target(zathura_link_t* link)
{
  if (link == NULL) {
    zathura_link_target_t target = { 0 };
    return target;
  }

  return link->target;
}

void
zathura_link_evaluate(zathura_t* zathura, zathura_link_t* link)
{
  if (zathura == NULL || zathura->document == NULL || link == NULL) {
    return;
  }

  switch (link->type) {
    case ZATHURA_LINK_GOTO_DEST:
      if (link->target.destination_type != ZATHURA_LINK_DESTINATION_UNKNOWN) {
        if (link->target.scale != 0) {
          zathura_document_set_scale(zathura->document, link->target.scale);
        }

        /* get page */
        zathura_page_t* page = zathura_document_get_page(zathura->document,
            link->target.page_number);
        if (page == NULL) {
          return;
        }

          page_offset_t offset;
          page_calculate_offset(zathura, page, &offset);

        if (link->target.destination_type == ZATHURA_LINK_DESTINATION_XYZ) {
          if (link->target.left != -1) {
            offset.x += link->target.left * zathura_document_get_scale(zathura->document);
          }

          if (link->target.top != -1) {
            offset.y += link->target.top * zathura_document_get_scale(zathura->document);
          }
        }

        /* jump to the page */
        page_set(zathura, link->target.page_number);

        /* move to the target position */
        bool link_hadjust = true;
        girara_setting_get(zathura->ui.session, "link-hadjust", &link_hadjust);

        if (link_hadjust == true) {
          position_set_delayed(zathura, offset.x, offset.y);
        } else {
          position_set_delayed(zathura, -1, offset.y);
        }
      }
      break;
    case ZATHURA_LINK_GOTO_REMOTE:
      link_remote(zathura, link->target.value);
      break;
    case ZATHURA_LINK_URI:
      if (girara_xdg_open(link->target.value) == false) {
        girara_notify(zathura->ui.session, GIRARA_ERROR, _("Failed to run xdg-open."));
      }
      break;
    case ZATHURA_LINK_LAUNCH:
      link_launch(zathura, link);
      break;
    default:
      break;
  }
}

void
zathura_link_display(zathura_t* zathura, zathura_link_t* link)
{
  zathura_link_type_t type = zathura_link_get_type(link);
  zathura_link_target_t target = zathura_link_get_target(link);
  switch (type) {
    case ZATHURA_LINK_GOTO_DEST:
      girara_notify(zathura->ui.session, GIRARA_INFO, _("Link: page %d"),
          target.page_number);
      break;
    case ZATHURA_LINK_GOTO_REMOTE:
    case ZATHURA_LINK_URI:
    case ZATHURA_LINK_LAUNCH:
    case ZATHURA_LINK_NAMED:
      girara_notify(zathura->ui.session, GIRARA_INFO, _("Link: %s"),
          target.value);
      break;
    default:
      girara_notify(zathura->ui.session, GIRARA_ERROR, _("Link: Invalid"));
  }
}

static void
link_remote(zathura_t* zathura, const char* file)
{
  if (zathura == NULL || file == NULL || zathura->document == NULL) {
    return;
  }

  const char* path = zathura_document_get_path(zathura->document);
  char* dir        = g_path_get_dirname(path);
  char* uri        = g_build_filename(dir, file, NULL);

  char* argv[] = {
    *(zathura->global.arguments),
    uri,
    NULL
  };

  g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);

  g_free(uri);
  g_free(dir);
}

static void
link_launch(zathura_t* zathura, zathura_link_t* link)
{
  if (zathura == NULL || link == NULL || zathura->document == NULL) {
    return;
  }

  /* get file path */
  if (link->target.value == NULL) {
    return;
  };

  char* path = NULL;
  if (g_path_is_absolute(link->target.value) == TRUE) {
    path = g_strdup(link->target.value);
  } else {
    const char* document = zathura_document_get_path(zathura->document);
    char* dir  = g_path_get_dirname(document);
    path = g_build_filename(dir, link->target.value, NULL);
    g_free(dir);
  }

  if (girara_xdg_open(path) == false) {
    girara_notify(zathura->ui.session, GIRARA_ERROR, _("Failed to run xdg-open."));
  }

  g_free(path);
}
