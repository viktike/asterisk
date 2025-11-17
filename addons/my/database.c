/* app_my: damn simple MySQL application for asterisk
 * Copyright (C) 2010 - Steve Frécinaux
 * Licensed under the GPL2+
 */

#ifndef AST_MODULE
#define AST_MODULE "app_my"
#endif

#include <asterisk.h>
#include <asterisk/cli.h>
#include <asterisk/logger.h>
#include <mysql/mysql.h>
#include <mysql/errmsg.h>
#include <stdio.h>
#include <sys/types.h>
#include <time.h>

#include "config.h"
#include "database.h"

AST_MUTEX_DEFINE_STATIC(mysql_lock);
static MYSQL mysql;
static int connected = 0;
static time_t connect_time = 0;

/*****************************************************************************
 * Functions                                                                 *
 *****************************************************************************/

MYSQL *
database_get (void)
{
  ast_mutex_lock (&mysql_lock);
  return &mysql;
}

void
database_release (MYSQL *mysql)
{
  if (mysql != NULL)
    ast_mutex_unlock (&mysql_lock);
  else
    ast_log (LOG_ERROR, "Releasing NULL connection.");
}

void
ast_log_mysql_error (const char *func_name)
{
  ast_log (LOG_ERROR,
           "%s returned %s (%d)\n",
           func_name,
           mysql_error (&mysql),
           mysql_errno (&mysql));
}

int
ensure_mysql_connection (MYSQL *mysql)
{
  int retries = 5;
#if MYSQL_VERSION_ID >= 50013
  int true_value = 1;
#endif

db_reconnect:
  if (!connected)
    {
      mysql_init (mysql);

      /* Add option to quickly timeout the connection */
      if (config.timeout != 0 && mysql_options (mysql, MYSQL_OPT_CONNECT_TIMEOUT, (char *) &config.timeout))
        ast_log_mysql_error ("mysql_options");

#if MYSQL_VERSION_ID >= 50013
      /* Add option for automatic reconnection */
      if (mysql_options (mysql, MYSQL_OPT_RECONNECT, (char *) &true_value) != 0)
        ast_log_mysql_error ("mysql_options");
#endif
      if (mysql_real_connect (mysql, config.hostname, config.username, config.password,
                              config.database, config.port, NULL, 0))
        {
          connected = 1;
          connect_time = time (NULL);
        }
      else
        {
          ast_log (LOG_ERROR,
                   "cannot connect to database server %s.\n",
                   config.hostname);
          connected = 0;
        }
    }
  else
    {
      /* Long connection - ping the server */
      if (mysql_ping (mysql) != 0)
        {
          connected = 0;

          switch (mysql_errno (mysql))
            {
            case CR_SERVER_GONE_ERROR:
            case CR_SERVER_LOST:
              ast_log (LOG_ERROR, "Server has gone away. Attempting to reconnect.\n");
              break;
            default:
              ast_log_mysql_error ("mysql_ping");
              break;
            }

          retries--;
          if (retries)
            goto db_reconnect;

          ast_log (LOG_ERROR, "Retried to connect fives times, giving up.\n");
        }
    }

  return connected;
}

/**
 * Drop the existing mysql connection if there is one.
 **/
static void
drop_mysql_connection (MYSQL *mysql)
{
  if (connected)
    {
      mysql_close (mysql);
      connected = 0;
    }
}

/*****************************************************************************
 * CLI                                                                       *
 *****************************************************************************/


static char *handle_cli_my_show_status(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)

{


	switch (cmd) {
		case CLI_INIT:
			e->command = "my show status";
			e->usage = "dbengine (my) action (show) subject (status)\n";
			return NULL;
		case CLI_GENERATE:
			switch (a->pos) {
				case 0:
					return a->n == 0 ? ast_strdup("my") : NULL;
				case 1:
					return a->n == 0 ? ast_strdup("show") : NULL;
				case 2:
					return a->n == 0 ? ast_strdup("status") : NULL;
				default:
					return NULL;
			}
		default:
			break;
	}



	if (a->argc != 3)
    		return CLI_SHOWUSAGE;

  /* Refresh the connection status. */
  if (connected && mysql_ping (&mysql) != 0)
    connected = 0;

  /* The following if{} got borrowed from cdr_addon_mysql.c */
  if (connected)
    {
      char status[255], status2[100] = "", timestr[255];
      int ctime = time (NULL) - connect_time;

      snprintf (status, sizeof (status), "Connected to %s@%s", config.database, config.hostname);

      if (config.username && config.username[0])
        snprintf (status2, sizeof (status2), " with username %s", config.username);

      if (ctime > 31536000)
        {
          snprintf (timestr, sizeof (timestr), "%d years, %d days, %d hours, %d minutes, %d seconds",
                    ctime / 31536000, (ctime % 31536000) / 86400,
                    (ctime % 86400) / 3600, (ctime % 3600) / 60, ctime % 60);
        }
      else if (ctime > 86400)
        {
          snprintf (timestr, sizeof (timestr), "%d days, %d hours, %d minutes, %d seconds",
                    ctime / 86400, (ctime % 86400) / 3600, (ctime % 3600) / 60, ctime % 60);
        }
      else if (ctime > 3600)
        {
          snprintf (timestr, sizeof (timestr), "%d hours, %d minutes, %d seconds",
                    ctime / 3600, (ctime % 3600) / 60, ctime % 60);
        }
      else if (ctime > 60)
        {
          snprintf (timestr, sizeof (timestr), "%d minutes, %d seconds", ctime / 60, ctime % 60);
        }
      else
        {
          snprintf (timestr, sizeof (timestr), "%d seconds", ctime);
        }

      ast_cli(a->fd, "%s%s for %s.\n", status, status2, timestr);
    }
  else
    {
      ast_cli (a->fd, "Not currently connected to a MySQL server.\n");
    }

    return CLI_SUCCESS;
}

static struct ast_cli_entry cli_entry[] = {
	AST_CLI_DEFINE(handle_cli_my_show_status, "Show connection status of app_my"),
};


void
database_init (void)
{
  ast_mutex_lock (&mysql_lock);
  ensure_mysql_connection (&mysql);
  ast_mutex_unlock (&mysql_lock);

  ast_cli_register_multiple(cli_entry, sizeof(cli_entry) / sizeof(struct ast_cli_entry));
}

void
database_reset (void)
{
  ast_mutex_lock (&mysql_lock);
  drop_mysql_connection (&mysql);
  ensure_mysql_connection (&mysql);
  ast_mutex_unlock (&mysql_lock);
}

void
database_clean (void)
{
  ast_cli_unregister_multiple(cli_entry, sizeof(cli_entry) / sizeof(struct ast_cli_entry));

  ast_mutex_lock (&mysql_lock);
  drop_mysql_connection (&mysql);
  ast_mutex_unlock (&mysql_lock);
}

/* ex:set ts=2 et sw=2 ai: */
