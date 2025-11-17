/* app_my: damn simple MySQL application for asterisk
 * Copyright (C) 2010 - Steve Frécinaux
 * Licensed under the GPL2+
 */

#include <asterisk.h>
#include <asterisk/linkedlists.h>
#include <asterisk/app.h>
#include <ctype.h>
#include <stdio.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/module.h>
#include <asterisk/options.h>
#include <asterisk/pbx.h>

#include "database.h"
#include "query.h"

/*****************************************************************************
 * MyQuery application                                                       *
 *****************************************************************************/

static void
prepare_var_name (const char *field_name, char *var_name, unsigned length)
{
  unsigned i = 0;
  const char *f = field_name;
  char *v = var_name;

  while (++i < length && *f != '\0')
    {
      if (!isascii (*f) || !isalnum (*f))
        *v = '_';
      else if (islower (*f))
        *v = toupper (*f);
      else
        *v = *f;
      ++f;
      ++v;
    }
  *v = '\0';
}

static void
set_vars (struct ast_channel *chan,
          MYSQL_RES *result)
{
  unsigned int num_fields;
  MYSQL_FIELD *fields;
  MYSQL_ROW row;
  unsigned i;
  char var_name[20] = "MY_";

  num_fields = mysql_num_fields (result);
  fields = mysql_fetch_fields (result);
  row = mysql_fetch_row (result);

  for (i = 0; i < num_fields; i++)
    {
      prepare_var_name (fields[i].name, var_name + 3, sizeof (var_name) - 3);
      pbx_builtin_setvar_helper (chan, var_name, row[i] ? row[i] : "");

      if (option_verbose > 4)
        ast_verbose (VERBOSE_PREFIX_4 "Set variable %s = %s\n", var_name, row[i] ? row[i] : "");
    }
}

static int
app_my_query_exec (struct ast_channel *chan,
                   const char               *data)
{
  int res = 0;
  MYSQL *mysql;
  MYSQL_RES *result = NULL;
  
  mysql = database_get ();
  if (!ensure_mysql_connection (mysql))
    {
      database_release (mysql);
      return -1;
    }

  res = mysql_query (mysql, data);
  if (res == 0)
    result = mysql_store_result (mysql);

  if (result != NULL)
    {
      unsigned num_rows = mysql_num_rows (result);

      /* Fetch the result and set the variables. */
      if (num_rows >= 1)
        set_vars (chan, result);

      /* Discard the eventual remaining rows */
      if (num_rows > 1)
        while (mysql_fetch_row (result));
      mysql_free_result (result);
      
      if (option_verbose >= 4)
        ast_verbose (VERBOSE_PREFIX_4 "MySQL Query returned %d rows\n", num_rows);
    }
  else if (mysql_field_count (mysql) != 0)
    {
      ast_log (LOG_WARNING, "[%d] %s\n", mysql_errno (mysql), mysql_error (mysql));
      res = -1;
    }

  database_release (mysql);
  return res;
}

/****************************************************************************
 * MY_ESC function                                                          *
 ****************************************************************************/

static int
func_my_esc_read (struct ast_channel *chan,
                  const char         *cmd,
                  char               *data,
                  char               *buf,
                  size_t              len)
{
  char *tmpbuf;
  size_t data_len;
  size_t max_len;
  MYSQL *mysql;
  unsigned esc_len = 0;

  data_len = strlen (data);
  max_len = data_len * 2 + 1;

  tmpbuf = (len >= max_len) ? buf : alloca (max_len);

  mysql = database_get ();
  if (ensure_mysql_connection (mysql))
    {
      esc_len = mysql_real_escape_string (mysql, tmpbuf, data, data_len);
      database_release (mysql);
    }
  else
    {
      database_release (mysql);
      return -1;
    }

  if (esc_len != 0 && buf != tmpbuf)
    memcpy (buf, tmpbuf, esc_len);
  buf[esc_len] = '\0';

  return 0;
}


/*****************************************************************************
 * Initialization functions                                                  *
 *****************************************************************************/

static char *app = "MyQuery";
static char *synopsis = "Run a query on a SQL database";
static char *descrip = "MyQuery(query)\n";

static struct ast_custom_function func_my_esc = {
  .name = "MY_ESC",
  .read = func_my_esc_read,
};

void
query_init (const struct ast_module_info *ast_module_info)
{
  ast_register_application (app, app_my_query_exec, synopsis, descrip);
  ast_custom_function_register(&func_my_esc);
}

void
query_clean (void)
{
  ast_unregister_application (app);
  ast_custom_function_unregister(&func_my_esc);
}

/* ex:set ts=2 et sw=2 ai: */
