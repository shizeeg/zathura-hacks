/* See LICENSE file for license and copyright information */

#ifndef DATABASE_H
#define DATABASE_H

#include <stdbool.h>
#include <girara/types.h>
#include <glib-object.h>

#include "bookmarks.h"

typedef struct zathura_fileinfo_s {
  unsigned int current_page;
  unsigned int page_offset;
  double scale;
  unsigned int rotation;
  unsigned int pages_per_row;
  unsigned int first_page_column;
  double position_x;
  double position_y;
} zathura_fileinfo_t;

#define ZATHURA_TYPE_DATABASE \
  (zathura_database_get_type ())
#define ZATHURA_DATABASE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), ZATHURA_TYPE_DATABASE, ZathuraDatabase))
#define ZATHURA_IS_DATABASE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), ZATHURA_TYPE_DATABASE))
#define ZATHURA_DATABASE_GET_INTERFACE(obj) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), ZATHURA_TYPE_DATABASE, ZathuraDatabaseInterface))

typedef struct _ZathuraDatabase          ZathuraDatabase;
typedef struct _ZathuraDatabaseInterface ZathuraDatabaseInterface;

struct _ZathuraDatabaseInterface
{
  GTypeInterface parent_iface;

  /* interface methords */
  bool (*add_bookmark)(ZathuraDatabase* db, const char* file, zathura_bookmark_t* bookmark);

  bool (*remove_bookmark)(ZathuraDatabase* db, const char* file, const char* id);

  girara_list_t* (*load_bookmarks)(ZathuraDatabase* db, const char* file);

  bool (*set_fileinfo)(ZathuraDatabase* db, const char* file, zathura_fileinfo_t* file_info);

  bool (*get_fileinfo)(ZathuraDatabase* db, const char* file, zathura_fileinfo_t* file_info);
};

GType zathura_database_get_type(void);

/**
 * Add or update bookmark in the database.
 *
 * @param db The database instance
 * @param file The file to which the bookmark belongs to.
 * @param bookmark The bookmark instance.
 * @return true on success, false otherwise
 */
bool zathura_db_add_bookmark(zathura_database_t* db, const char* file,
    zathura_bookmark_t* bookmark);

/**
 * Add or update bookmark in the database.
 *
 * @param db The database instance
 * @param file The file to which the bookmark belongs to.
 * @param id The id of the bookmark
 * @return true on success, false otherwise
 */
bool zathura_db_remove_bookmark(zathura_database_t* db, const char* file, const
    char* id);

/**
 * Loads all bookmarks from the database belonging to a specific file.
 *
 * @param db The database instance.
 * @param file The file for which the bookmarks should be loaded.
 * @return List of zathura_bookmark_t* or NULL on failure.
 */
girara_list_t* zathura_db_load_bookmarks(zathura_database_t* db, const char*
    file);

/**
 * Set file info (last site, ...) in the database.
 *
 * @param db The database instance
 * @param file The file to which the file info belongs to.
 * @param file_info The file info
 * @return true on success, false otherwise.
 */
bool zathura_db_set_fileinfo(zathura_database_t* db, const char* file,
    zathura_fileinfo_t* file_info);

/* Get file info (last site, ...) from the database.
 *
 * @param db The database instance
 * @param file The file to which the file info belongs to.
 * @param file_info The file info
 * @return true on success, false otherwise.
 */
bool zathura_db_get_fileinfo(zathura_database_t* db, const char* file,
    zathura_fileinfo_t* file_info);

#endif // DATABASE_H
