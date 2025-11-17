/* app_my: damn simple MySQL application for asterisk
 * Copyright (C) 2010 - Steve Frécinaux
 * Licensed under the GPL2+
 */


/*** MODULEINFO
        <depend>mysqlclient</depend>
 ***/


#ifndef AST_MODULE
#define AST_MODULE "app_my"
#endif

#include <asterisk.h>
#include <asterisk/cli.h>
#include <asterisk/module.h>

#include "my/config.h"
#include "my/database.h"
#include "my/query.h"
#include "my/auth.h"

static int load_module   (void);
static int reload_module (void);
static int unload_module (void);

/*****************************************************************************
 * CLI                                                                       *
 *****************************************************************************/

static char *handle_cli_my_reload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)

{


        switch (cmd) {
                case CLI_INIT:
                        e->command = "my reload";
                        e->usage = "dbengine (my) action (reload)\n";
                        return NULL;
                case CLI_GENERATE:
                        switch (a->pos) {
                                case 0:
                                        return a->n == 0 ? ast_strdup("my") : NULL;
                                case 1:
                                        return a->n == 0 ? ast_strdup("reload") : NULL;
                                default:
                                        return NULL;
                        }
                default:
                        break;
        }



        if (a->argc != 2)
                return CLI_SHOWUSAGE;
	
	reload_module();
	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_entry[] = {
	AST_CLI_DEFINE(handle_cli_my_reload, "Reload the MySQL application"),
};

/*****************************************************************************
 * Module                                                                    *
 *****************************************************************************/


static int
load_module (void)
{
  config_init ();
  database_init ();
  query_init (ast_module_info);
  auth_init (ast_module_info);

  ast_cli_register_multiple (cli_entry, sizeof(cli_entry) / sizeof(struct ast_cli_entry));

  return 0;
}

static int
reload_module (void)
{
  config_reset ();
  database_reset ();

  return 0;
}

static int
unload_module (void)
{
  auth_clean ();
  query_clean ();
  database_clean ();
  config_clean ();

  ast_cli_unregister_multiple (cli_entry, sizeof(cli_entry) / sizeof(struct ast_cli_entry));

  return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY,
                AST_MODFLAG_DEFAULT,
                "BeIP MySQL Application",
                .load = load_module,
                .unload = unload_module,
                .reload = reload_module);

/* ex:set ts=2 et sw=2 ai: */
