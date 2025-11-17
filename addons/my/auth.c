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
#include <asterisk/channel.h>

#include "database.h"
#include "auth.h"

#define MAX_RETRIES 3

/*****************************************************************************
 * MyAuthenticate application                                                *
 *****************************************************************************/

static int
streamfile (struct ast_channel *chan,
            const char         *filename)
{
  int r = ast_streamfile (chan, filename, ast_channel_language(chan));
  if (r)
    ast_log (LOG_WARNING, "Unable to stream file %s\n", filename);
  return r;
}

static int
readstring (struct ast_channel *chan,
            char               *string,
            size_t              length)
{
  int r = ast_readstring (chan, string, length - 1, 2000, 10000, "#");
  if (r < 0)
    ast_log (LOG_WARNING, "Couldn't read string\n");
  return r;
}

static int
check_password (const char *query, const char *password)
{
  int res = 0;
  MYSQL *mysql;
  MYSQL_RES *result = NULL;
  int password_ok = 0;
  
  mysql = database_get ();
  if (!ensure_mysql_connection (mysql))
    {
      database_release (mysql);
      return 0;
    }

  res = mysql_query (mysql, query);
  if (res == 0)
    result = mysql_store_result (mysql);

  if (result != NULL)
    {
      unsigned num_rows = mysql_num_rows (result);
      unsigned num_fields = mysql_num_fields (result);
      MYSQL_ROW row;

      /* Fetch the result and verify the password. */
      if (num_rows >= 1 && num_fields >= 1)
        {
          row = mysql_fetch_row (result);
          password_ok = strcmp(row[0], password) == 0;
        }

      /* Discard the eventual remaining rows */
      if (num_rows >= 1)
        while (mysql_fetch_row (result));
      mysql_free_result (result);
      
      if (option_verbose >= 4)
        ast_verbose (VERBOSE_PREFIX_4 "MySQL Query returned %d rows\n", num_rows);
    }
  else if (mysql_field_count (mysql) != 0)
    {
      ast_log (LOG_WARNING, "[%d] %s\n", mysql_errno (mysql), mysql_error (mysql));
    }
  else
    {
      ast_log (LOG_WARNING, "Query did not return any value.");
    }

  database_release (mysql);
  return password_ok;
}

static int
app_my_authenticate_exec (struct ast_channel *chan,
                          const char               *data)
{
  const char *query = data;
  char password[AST_MAX_EXTENSION] = "";
  int valid = 0;
  int logretries = 0;

  /* Authenticate them and get their mailbox/password */
  while (!valid && logretries < MAX_RETRIES)
    {
      /* Prompt for, and read in the pin code */
      if (streamfile (chan, "vm-password"))
        return -1;

      if (readstring (chan, password, sizeof(password)) < 0)
        return -1;

      valid = check_password (query, password);
      if (!valid && option_verbose > 2)
         ast_verbose (VERBOSE_PREFIX_3 "Incorrect password\n");

      logretries++;

      if (!valid && logretries >= MAX_RETRIES)
        {
          if (streamfile (chan, "vm-incorrect"))
            return -1;
        }

      if (ast_waitstream (chan, ""))  /* Channel is hung up */
        return -1;
    }
    
  if (!valid)
    {
      ast_stopstream (chan);
      ast_play_and_wait (chan, "vm-goodbye");
      return -1;
    }

  return 0;
}

/*****************************************************************************
 * Initialization functions                                                  *
 *****************************************************************************/

static char *app = "MyAuthenticate";
static char *synopsis = "Authenticate someone against the result of a SQL database";
static char *descrip = "MyAuthenticate(query)\n";

void
auth_init (const struct ast_module_info *ast_module_info)
{
  ast_register_application (app, app_my_authenticate_exec, synopsis, descrip);
}

void
auth_clean (void)
{
  ast_unregister_application (app);
}

/* ex:set ts=2 et sw=2 ai: */
