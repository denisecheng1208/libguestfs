/* libguestfs - the guestfsd daemon
 * Copyright (C) 2012 Red Hat Inc.
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#include <json.h>

#include "daemon.h"
#include "actions.h"
#include "optgroups.h"

int
optgroup_ldm_available (void)
{
  return prog_exists ("ldmtool");
}

int
do_ldmtool_create_all (void)
{
  int r;
  CLEANUP_FREE char *err = NULL;

  r = command (NULL, &err, "ldmtool", "create", "all", NULL);
  if (r == -1) {
    reply_with_error ("%s", err);
    return -1;
  }
  return 0;
}

int
do_ldmtool_remove_all (void)
{
  int r;
  CLEANUP_FREE char *err = NULL;

  r = command (NULL, &err, "ldmtool", "remove", "all", NULL);
  if (r == -1) {
    reply_with_error ("%s", err);
    return -1;
  }
  return 0;
}

static json_object *
parse_json (const char *json, const char *caller)
{
  json_object *tree = NULL;
  json_tokener *tok = NULL;
  enum json_tokener_error err;

  if (verbose)
    fprintf (stderr, "%s: parsing json: %s\n", caller, json);

  tok = json_tokener_new ();
  json_tokener_set_flags (tok,
                          JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8);
  tree = json_tokener_parse_ex (tok, json, strlen (json));
  err = json_tokener_get_error (tok);
  if (err != json_tokener_success) {
    reply_with_error ("%s: parse error: %s",
                      caller, json_tokener_error_desc (err));
    json_tokener_free (tok);
    return NULL;
  }

  json_tokener_free (tok);

  /* Caller should free this by doing json_object_put (tree). */
  return tree;
}

#define TYPE_ERROR ((char **) -1)

static char **
json_value_to_string_list (json_object *node)
{
  CLEANUP_FREE_STRINGSBUF DECLARE_STRINGSBUF (strs);
  json_object *n;
  size_t i;

  if (json_object_get_type (node) != json_type_array)
    return TYPE_ERROR;

  for (i = 0; i < json_object_array_length (node); ++i) {
    n = json_object_array_get_idx (node, i); /* Doesn't incr the refcount. */
    if (json_object_get_type (n) != json_type_string)
      return TYPE_ERROR;
    if (add_string (&strs, json_object_get_string (n)) == -1)
      return NULL;
  }

  if (end_stringsbuf (&strs) == -1)
    return NULL;

  return take_stringsbuf (&strs);
}

static char **
parse_json_get_string_list (const char *json,
                            const char *caller, const char *cmd)
{
  char **ret;
  json_object *tree = NULL;

  tree = parse_json (json, caller);
  if (tree == NULL)
    return NULL;

  ret = json_value_to_string_list (tree);
  json_object_put (tree);
  if (ret == TYPE_ERROR) {
    reply_with_error ("output of '%s' was not a JSON array of strings", cmd);
    return NULL;
  }
  return ret;
}

#define GET_STRING_NULL_TO_EMPTY 1

static char *
parse_json_get_object_string (const char *json, const char *key, int flags,
                              const char *caller, const char *cmd)
{
  const char *str;
  char *ret;
  json_object *tree = NULL, *node;

  tree = parse_json (json, caller);
  if (tree == NULL)
    return NULL;

  if (json_object_get_type (tree) != json_type_object)
    goto bad_type;

  node = json_object_object_get (tree, key);
  if (node == NULL)
    goto bad_type;

  if ((flags & GET_STRING_NULL_TO_EMPTY) &&
      json_object_get_type (node) == json_type_null)
    ret = strdup ("");
  else {
    str = json_object_get_string (node);
    ret = strndup (str, strlen (str));
  }
  if (ret == NULL)
    reply_with_perror ("strdup");

  json_object_put (tree);

  return ret;

 bad_type:
  reply_with_error ("output of '%s' was not a JSON object "
                    "containing a key '%s' of type string", cmd, key);
  json_object_put (tree);
  return NULL;
}

static char **
parse_json_get_object_string_list (const char *json, const char *key,
                                   const char *caller, const char *cmd)
{
  char **ret;
  json_object *tree, *node;

  tree = parse_json (json, caller);
  if (tree == NULL)
    return NULL;

  if (json_object_get_type (tree) != json_type_object)
    goto bad_type;

  node = json_object_object_get (tree, key);
  if (node == NULL)
    goto bad_type;

  ret = json_value_to_string_list (node);
  if (ret == TYPE_ERROR)
    goto bad_type;
  json_object_put (tree);
  return ret;

 bad_type:
  reply_with_error ("output of '%s' was not a JSON object "
                    "containing a key '%s' of type array of strings",
                    cmd, key);
  json_object_put (tree);
  return NULL;
}

char **
do_ldmtool_scan (void)
{
  const char *empty_list[] = { NULL };

  return do_ldmtool_scan_devices ((char * const *) empty_list);
}

char **
do_ldmtool_scan_devices (char * const * devices)
{
  char **ret;
  size_t i, nr_devices;
  CLEANUP_FREE const char **argv = NULL;
  int r;
  CLEANUP_FREE char *out = NULL, *err = NULL;

  nr_devices = guestfs_int_count_strings (devices);
  argv = malloc ((3 + nr_devices) * sizeof (char *));
  if (argv == NULL) {
    reply_with_perror ("malloc");
    return NULL;
  }

  argv[0] = "ldmtool";
  argv[1] = "scan";
  for (i = 0; i < nr_devices; ++i)
    argv[2+i] = devices[i];
  argv[2+i] = NULL;

  r = commandv (&out, &err, argv);
  if (r == -1) {
    reply_with_error ("%s", err);
    return NULL;
  }

  ret = parse_json_get_string_list (out, __func__, "ldmtool scan");
  return ret;
}

char *
do_ldmtool_diskgroup_name (const char *diskgroup)
{
  int r;
  CLEANUP_FREE char *out = NULL, *err = NULL;

  r = command (&out, &err, "ldmtool", "show", "diskgroup", diskgroup, NULL);
  if (r == -1) {
    reply_with_error ("%s", err);
    return NULL;
  }

  return parse_json_get_object_string (out, "name", 0,
                                       __func__, "ldmtool show diskgroup");
}

char **
do_ldmtool_diskgroup_volumes (const char *diskgroup)
{
  int r;
  CLEANUP_FREE char *out = NULL, *err = NULL;

  r = command (&out, &err, "ldmtool", "show", "diskgroup", diskgroup, NULL);
  if (r == -1) {
    reply_with_error ("%s", err);
    return NULL;
  }

  return parse_json_get_object_string_list (out, "volumes",
                                            __func__, "ldmtool show diskgroup");
}

char **
do_ldmtool_diskgroup_disks (const char *diskgroup)
{
  int r;
  CLEANUP_FREE char *out = NULL, *err = NULL;

  r = command (&out, &err, "ldmtool", "show", "diskgroup", diskgroup, NULL);
  if (r == -1) {
    reply_with_error ("%s", err);
    return NULL;
  }

  return parse_json_get_object_string_list (out, "disks",
                                            __func__, "ldmtool show diskgroup");
}

char *
do_ldmtool_volume_type (const char *diskgroup, const char *volume)
{
  int r;
  CLEANUP_FREE char *out = NULL, *err = NULL;

  r = command (&out, &err,
               "ldmtool", "show", "volume", diskgroup, volume, NULL);
  if (r == -1) {
    reply_with_error ("%s", err);
    return NULL;
  }

  return parse_json_get_object_string (out, "type", 0,
                                       __func__, "ldmtool show volume");
}

char *
do_ldmtool_volume_hint (const char *diskgroup, const char *volume)
{
  int r;
  CLEANUP_FREE char *out = NULL, *err = NULL;

  r = command (&out, &err,
               "ldmtool", "show", "volume", diskgroup, volume, NULL);
  if (r == -1) {
    reply_with_error ("%s", err);
    return NULL;
  }

  return parse_json_get_object_string (out, "hint", GET_STRING_NULL_TO_EMPTY,
                                       __func__, "ldmtool show volume");
}

char **
do_ldmtool_volume_partitions (const char *diskgroup, const char *volume)
{
  int r;
  CLEANUP_FREE char *out = NULL, *err = NULL;

  r = command (&out, &err,
               "ldmtool", "show", "volume", diskgroup, volume, NULL);
  if (r == -1) {
    reply_with_error ("%s", err);
    return NULL;
  }

  return parse_json_get_object_string_list (out, "partitions",
                                            __func__, "ldmtool show volume");
}
