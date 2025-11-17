/* app_my: damn simple MySQL application for asterisk
 * Copyright (C) 2010 - Steve Frécinaux
 * Licensed under the GPL2+
 */

#ifndef AST_MODULE
#define AST_MODULE "app_my"
#endif


#include <asterisk.h>
#include <asterisk/config.h>
#include <asterisk/logger.h>
#include <asterisk/utils.h>
#include <stdio.h>
#include <string.h>

#define IN_CONFIG_H
#include "config.h"
struct my_config config = { 0 };

#define CONFIG_FILE "my.conf"

void
config_init (void)
{
  struct ast_config *cfg = NULL;
  struct ast_variable *var = NULL;
  struct ast_flags flag = { .flags = 0 };
  const char *value = NULL;

  memset (&config, 0, sizeof (struct my_config));

  cfg = ast_config_load (CONFIG_FILE, flag);
  if (!cfg)
    ast_log (LOG_WARNING, "Unable to load " CONFIG_FILE "\n");
  else
    var = ast_variable_browse (cfg, "global");

  if (var)
    value = ast_variable_retrieve (cfg, "global", "hostname");
  if (!value)
    {
      ast_log (LOG_WARNING, "MySQL server hostname not specified.  Assuming localhost\n");
      value = "localhost";
    }
  config.hostname = ast_strdup (value);

  if (var)
    value = ast_variable_retrieve (cfg, "global", "port");
  if (!value)
    {
      ast_log (LOG_WARNING, "MySQL server port not specified.  Assuming 3306\n");
      config.port = 3306;
    }
  else
    {
      config.port = atoi (value);
    }

  if (var)
    value = ast_variable_retrieve (cfg, "global", "user");
  if (!value)
    {
      ast_log (LOG_WARNING, "MySQL database user not specified.  Assuming root\n");
      value = "root";
    }
  config.username = ast_strdup (value);

  if (var)
    value = ast_variable_retrieve (cfg, "global", "password");
  if (!value)
    {
      ast_log (LOG_WARNING, "MySQL database password not specified.  Assuming blank\n");
      value = "";
    }
  config.password = ast_strdup (value);

  if (var)
    value = ast_variable_retrieve (cfg, "global", "dbname");
  if (!value)
    {
      ast_log(LOG_WARNING, "MySQL database not specified.  Assuming beip\n");
      value = "beip";
    }
  config.database = ast_strdup (value);

  config.timeout = 10;

  if (cfg)
    ast_config_destroy (cfg);
}

void
config_reset (void)
{
  config_clean ();
  config_init ();
}

void
config_clean (void)
{
  ast_free (config.hostname);
  ast_free (config.username);
  ast_free (config.password);
  ast_free (config.database);
}

/* ex:set ts=2 et sw=2 ai: */
