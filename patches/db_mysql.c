/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*
 * MySQL backend for AstDB by Sperl Viktor 
 */

/*! \file
 *
 * \brief ASTdb Management
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * \note DB3 is licensed under Sleepycat Public License and is thus incompatible
 * with GPL.  To avoid having to make another exception (and complicate
 * licensing even further) we elect to use DB1 which is BSD licensed
 */

/*** MODULEINFO
	<depend>mysqlclient</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/_private.h"
#include "asterisk/paths.h"	/* use ast_config_AST_DB */
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>

#include <mysql/mysql.h>
#include <mysql/errmsg.h>

#include "asterisk/channel.h"
#include "asterisk/file.h"
#include "asterisk/app.h"
#include "asterisk/dsp.h"
#include "asterisk/astdb.h"
#include "asterisk/cli.h"
#include "asterisk/utils.h"
#include "asterisk/manager.h"

/*** DOCUMENTATION
	<manager name="DBGet" language="en_US">
		<synopsis>
			Get DB Entry.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Family" required="true" />
			<parameter name="Key" required="true" />
		</syntax>
		<description>
		</description>
	</manager>
	<manager name="DBGetTree" language="en_US">
		<synopsis>
			Get DB entries, optionally at a particular family/key
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Family" required="false" />
			<parameter name="Key" required="false" />
		</syntax>
		<description>
		</description>
	</manager>
	<manager name="DBPut" language="en_US">
		<synopsis>
			Put DB entry.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Family" required="true" />
			<parameter name="Key" required="true" />
			<parameter name="Val" />
		</syntax>
		<description>
		</description>
	</manager>
	<manager name="DBDel" language="en_US">
		<synopsis>
			Delete DB entry.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Family" required="true" />
			<parameter name="Key" required="true" />
		</syntax>
		<description>
		</description>
	</manager>
	<manager name="DBDelTree" language="en_US">
		<synopsis>
			Delete DB Tree.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Family" required="true" />
			<parameter name="Key" />
		</syntax>
		<description>
		</description>
	</manager>
 ***/

#define	CONFIG	"astdb_mysql.conf"
#define MAX_DB_FIELD 256
#define MAX_DB_VAL 4096
char dbhost[MAX_DB_FIELD];
char dbname[MAX_DB_FIELD];
char dbuser[MAX_DB_FIELD];
char dbpass[MAX_DB_FIELD];
char dbport[MAX_DB_FIELD];
char dbcharset[128];
char dbhost[MAX_DB_FIELD];
char dbtable[MAX_DB_FIELD];

MYSQL *mysql;

#ifdef MYSQL_OPT_RECONNECT
my_bool trueval = 1;
#endif

static int db_open(void)
{
	char set_names[MAX_DB_FIELD];
	char statement[512];
	unsigned int port;

	if(!(mysql = mysql_init(NULL))){
		ast_log(LOG_WARNING, "AstDB mysql_init returned NULL!\n");
		return -1;
	}

	snprintf(set_names, sizeof(set_names), "SET NAMES %s", dbcharset);
	mysql_real_escape_string(mysql, statement, set_names, sizeof(set_names));
	mysql_options(mysql, MYSQL_INIT_COMMAND, set_names);
	mysql_options(mysql, MYSQL_SET_CHARSET_NAME, dbcharset);
#ifdef MYSQL_OPT_RECONNECT
	mysql_options(mysql, MYSQL_OPT_RECONNECT, &trueval);
#endif
	

	if (sscanf(dbport, "%u", &port) != 1) {
		ast_log(LOG_WARNING, "Invalid AstDB port: '%s'\n", dbport);
		port = 0;
		return -1;
	}
	if(!mysql_real_connect(mysql, dbhost, dbuser, dbpass, dbname, port, NULL, 0 )){
		ast_log(LOG_WARNING, "AstDB mysql_real_connect(mysql,%s,%s,dbpass,%s,...) failed(%d): %s\n", dbhost, dbuser, dbname, mysql_errno(mysql), mysql_error(mysql));
		return -1;
	} else {
		// mysql_autocommit(mysql, 1);
		return 0;
	}
}

static int load_config(void)
{
	struct ast_config *cfg;
	struct ast_variable *var;
	struct ast_flags config_flags = { 0 }; /* Part of our config comes from the database */

	cfg = ast_config_load(CONFIG, config_flags);
	if (!cfg || cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_WARNING, "Unable to load " CONFIG ".  AstDB failed!\n");
		return -1;
	}

	/* Process the general category */
	for (var = ast_variable_browse(cfg, "general"); var; var = var->next) {
		if (!strcasecmp(var->name, "dbhost")) {
			ast_copy_string(dbhost, var->value, sizeof(dbhost));
			ast_log(LOG_WARNING, "Found AstDB dbhost in config: %s\n", dbhost);
		} else if(!strcasecmp(var->name, "dbname")){
			ast_copy_string(dbname, var->value, sizeof(dbname));
			ast_log(LOG_WARNING, "Found AstDB dbname in config: %s\n", dbname);
		} else if(!strcasecmp(var->name, "dbuser")){
			ast_copy_string(dbuser, var->value, sizeof(dbuser));
			ast_log(LOG_WARNING, "Found AstDB dbuser in config: %s\n", dbuser);
		} else if(!strcasecmp(var->name, "dbpass")){
			ast_copy_string(dbpass, var->value, sizeof(dbpass));
			ast_log(LOG_WARNING, "Found AstDB dbpass in config.\n");
		} else if(!strcasecmp(var->name, "dbport")){
			ast_copy_string(dbport, var->value, sizeof(dbport));
			ast_log(LOG_WARNING, "Found AstDB dbport in config: %s\n", dbport);
		} else if(!strcasecmp(var->name, "dbcharset")){
			ast_copy_string(dbcharset, var->value, sizeof(dbcharset));
			ast_log(LOG_WARNING, "Found AstDB dbcharset in config: %s\n", dbcharset);
		} else if(!strcasecmp(var->name, "dbtable")){
			ast_copy_string(dbtable, var->value, sizeof(dbtable));
			ast_log(LOG_WARNING, "Found AstDB dbtable in config: %s\n", dbtable);
		} else {
			ast_log(LOG_WARNING, "Found unknown variable in astdb_mysql.conf general section: %s = %s\n", var->name, var->value);
		}
	}

	return 0;
}

static MYSQL_RES* db_query_mysql(const char *sql)
{    
	if(!mysql_ping(mysql)){
		// ast_log(LOG_VERBOSE, "AstDB connection lost, reconnecting to %s.\n", dbhost);
		db_open();
	}
	mysql_query(mysql, sql);
	return mysql_store_result(mysql);
}

static int db_execute_mysql(const char *sql)
{
	int mysql_query_res;
    
	if(!mysql_ping(mysql)){
		// ast_log(LOG_VERBOSE, "AstDB connection lost, reconnecting to %s.\n", dbhost);
		db_open();
	}
	if ((mysql_query_res = mysql_query(mysql, sql)) != 0){
		ast_log(LOG_WARNING, "AstDB mysql_query failed. Error: %s\n", mysql_error(mysql));
		return -1;
	} else {
		return mysql_affected_rows(mysql);
	}
}

static int db_create_astdb(void)
{
	struct ast_str *sql = ast_str_create(MAX_DB_VAL);
	
    ast_str_append(&sql, 0, "CREATE TABLE IF NOT EXISTS %s (`key` VARCHAR(%d) NOT NULL, `value` VARCHAR(%d) NULL DEFAULT NULL, PRIMARY KEY (`key`));", dbtable, MAX_DB_FIELD, MAX_DB_VAL);
	return db_execute_mysql(ast_str_buffer(sql));
}

static int db_init(void)
{
	if (load_config() || db_open() || db_create_astdb()) {
		return -1;
	} else {
		return 0;
	}
}

int ast_db_put(const char *family, const char *key, const char *value)
{
	char fullkey[MAX_DB_FIELD];
	struct ast_str *sql = ast_str_create(MAX_DB_VAL * 2);

	if (strlen(family) + strlen(key) + 2 > sizeof(fullkey) - 1){
		ast_log(LOG_WARNING, "Family and key length must be less than %zu bytes\n", sizeof(fullkey) - 3);
		return -1;
	}
        snprintf(fullkey, sizeof(fullkey), "/%s/%s", family, key);

        ast_str_append(&sql, 0, "INSERT INTO %s (`key`, `value`) VALUES ('%s', '%s') ON DUPLICATE KEY UPDATE `value`='%s';", dbtable, fullkey, value, value);
	if(db_execute_mysql(ast_str_buffer(sql)) == -1){
		return -1;
	} else {
		return 0;
	}
}

/*!
 * \internal
 * \brief Get key value specified by family/key.
 *
 * Gets the value associated with the specified \a family and \a key, and
 * stores it, either into the fixed sized buffer specified by \a buffer
 * and \a bufferlen, or as a heap allocated string if \a bufferlen is -1.
 *
 * \note If \a bufferlen is -1, \a buffer points to heap allocated memory
 *       and must be freed by calling ast_free().
 *
 * \retval -1 An error occurred
 * \retval 0 Success
 */
static int db_get_common(const char *family, const char *key, char **buffer, int bufferlen)
{
	char fullkey[MAX_DB_FIELD];
	struct ast_str *sql = ast_str_create(MAX_DB_VAL);
	MYSQL_RES *mysqlres;
	MYSQL_ROW row;

	if (strlen(family) + strlen(key) + 2 > sizeof(fullkey) - 1) {
		ast_log(LOG_WARNING, "Family and key length must be less than %zu bytes\n", sizeof(fullkey) - 3);
		return -1;
	}

	snprintf(fullkey, sizeof(fullkey), "/%s/%s", family, key);

	ast_str_append(&sql, 0, "SELECT `value` FROM %s WHERE `key`='%s';", dbtable, fullkey);
	mysqlres = db_query_mysql(ast_str_buffer(sql));
	if(mysqlres != NULL){
		if((row = mysql_fetch_row(mysqlres)) != NULL){
			const char *value = (const char *) row[0];
			if (bufferlen == -1) {
				*buffer = ast_strdup(value);
			} else {
				ast_copy_string(*buffer, value, bufferlen);
			}
			mysql_free_result(mysqlres);
			return 0;
		} else {
			mysql_free_result(mysqlres);
			return -1;
		}
	} else {
		mysql_free_result(mysqlres);
		return -1;	
	}
}

int ast_db_get(const char *family, const char *key, char *value, int valuelen)
{
	ast_assert(value != NULL);

	/* Make sure we initialize */
	value[0] = 0;

	return db_get_common(family, key, &value, valuelen);
}

int ast_db_get_allocated(const char *family, const char *key, char **out)
{
	*out = NULL;

	return db_get_common(family, key, out, -1);
}

int ast_db_del(const char *family, const char *key)
{
	char fullkey[MAX_DB_FIELD];
	struct ast_str *sql = ast_str_create(MAX_DB_VAL);

	if (strlen(family) + strlen(key) + 2 > sizeof(fullkey) - 1) {
		ast_log(LOG_WARNING, "Family and key length must be less than %zu bytes\n", sizeof(fullkey) - 3);
		return -1;
	}

	snprintf(fullkey, sizeof(fullkey), "/%s/%s", family, key);

	ast_str_append(&sql, 0, "DELETE FROM %s WHERE `key`='%s';", dbtable, fullkey);
	if(db_execute_mysql(ast_str_buffer(sql)) == -1){
		return -1;
	} else {
		return 0;
	}
}

int ast_db_del2(const char *family, const char *key)
{
	char fullkey[MAX_DB_FIELD];
	char tmp[1];
	struct ast_str *sql = ast_str_create(MAX_DB_VAL);

	if (strlen(family) + strlen(key) + 2 > sizeof(fullkey) - 1) {
		ast_log(LOG_WARNING, "Family and key length must be less than %zu bytes\n", sizeof(fullkey) - 3);
		return -1;
	}

	snprintf(fullkey, sizeof(fullkey), "/%s/%s", family, key);

	if (ast_db_get(family, key, tmp, sizeof(tmp))) {
		ast_log(LOG_WARNING, "AstDB key %s does not exist\n", fullkey);
		return -1;
	} else {
		ast_str_append(&sql, 0, "DELETE FROM %s WHERE `key`='%s';", dbtable, fullkey);
		db_execute_mysql(ast_str_buffer(sql));
		return 0;
	}
}

int ast_db_deltree(const char *family, const char *keytree)
{
	struct ast_str *sql = ast_str_create(MAX_DB_VAL);

	if (!ast_strlen_zero(family)) {
		if (!ast_strlen_zero(keytree)) {
			/* Family and key tree */
			ast_str_append(&sql, 0, "DELETE FROM %s WHERE `key` LIKE '/%s/%s/%%';", dbtable, family, keytree);
		} else {
			/* Family only */
			ast_str_append(&sql, 0, "DELETE FROM %s WHERE `key` LIKE '/%s/%%';", dbtable, family);
		}
	} else {
		/* Delete all */
		ast_str_append(&sql, 0, "TRUNCATE TABLE %s;", dbtable);
	}

	return db_execute_mysql(ast_str_buffer(sql));
}

static struct ast_db_entry *db_gettree_common(MYSQL_RES *mysqlres)
{
	struct ast_db_entry *head = NULL, *prev = NULL, *cur;
	MYSQL_ROW row;

	if(mysqlres != NULL){
		while ((row = mysql_fetch_row(mysqlres)) != NULL)
		{
			const char *key, *value;
			size_t key_len, value_len;

			key   = (const char *) row[0];
			value = (const char *) row[1];

			if (!key || !value) {
				break;
			}

			key_len = strlen(key);
			value_len = strlen(value);

			cur = ast_malloc(sizeof(*cur) + key_len + value_len + 2);
			if (!cur) {
				break;
			}

			cur->next = NULL;
			cur->key = cur->data + value_len + 1;
			memcpy(cur->data, value, value_len + 1);
			memcpy(cur->key, key, key_len + 1);

			if (prev) {
				prev->next = cur;
			} else {
				head = cur;
			}
			prev = cur;
		}
	}
	return head;
}

struct ast_db_entry *ast_db_gettree(const char *family, const char *keytree)
{
	struct ast_db_entry *ret;
	struct ast_str *sql = ast_str_create(MAX_DB_VAL);
	MYSQL_RES *mysqlres;

	if (!ast_strlen_zero(family)) {
		if (!ast_strlen_zero(keytree)) {
			/* Family and key tree */
			ast_str_append(&sql, 0, "SELECT `key`, `value` FROM %s WHERE `key` LIKE '/%s/%s/%%' ORDER BY `key`;", dbtable, family, keytree);
		} else {
			/* Family only */
			ast_str_append(&sql, 0, "SELECT `key`, `value` FROM %s WHERE `key` LIKE '/%s/%%' ORDER BY `key`;", dbtable, family);
		}
	} else {
		ast_str_append(&sql, 0, "SELECT `key`, `value` FROM  %s ORDER BY `key`;", dbtable);
	}

	mysqlres = db_query_mysql(ast_str_buffer(sql));
	ret = db_gettree_common(mysqlres);
	mysql_free_result(mysqlres);

	return ret;
}

struct ast_db_entry *ast_db_gettree_by_prefix(const char *family, const char *key_prefix)
{
	struct ast_db_entry *ret;
	struct ast_str *sql = ast_str_create(MAX_DB_VAL);
	MYSQL_RES *mysqlres;

	ast_str_append(&sql, 0, "SELECT `key`, `value` FROM %s WHERE `key` LIKE '/%s/%s%%' ORDER BY `key`;", dbtable, family, key_prefix);

	mysqlres = db_query_mysql(ast_str_buffer(sql));
	ret = db_gettree_common(mysqlres);
	mysql_free_result(mysqlres);

	return ret;
}

void ast_db_freetree(struct ast_db_entry *dbe)
{
	struct ast_db_entry *last;
	while (dbe) {
		last = dbe;
		dbe = dbe->next;
		ast_free(last);
	}
}

int ast_db_exists(const char *family, const char *key)
{       
        char fullkey[MAX_DB_FIELD];
        struct ast_str *sql = ast_str_create(MAX_DB_VAL);
        MYSQL_RES *mysqlres;

        if (strlen(family) + strlen(key) + 2 > sizeof(fullkey) - 1) {
                ast_log(LOG_WARNING, "Family and key length must be less than %zu bytes\n", sizeof(fullkey) - 3);
                return -1;
        }

        snprintf(fullkey, sizeof(fullkey), "/%s/%s", family, key);

        ast_str_append(&sql, 0, "SELECT CAST(COUNT(`value`) AS UNSIGNED) FROM %s WHERE `key`='%s';", table, fullkey);
        
	mysqlres = db_query_mysql(ast_str_buffer(sql));
        if(mysqlres != NULL){
                if((row = mysql_fetch_row(mysqlres)) != NULL){
                        unsigned int exists = row[0];
                        mysql_free_result(mysqlres);
                        return exists;
                } else {
                        mysql_free_result(mysqlres);
                        return -1;
                }
        } else {
                mysql_free_result(mysqlres);
                return -1;
        }
}

static char *handle_cli_database_exists(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{       
    int result;
        
        switch (cmd) {
                case CLI_INIT:
                        e->command = "database exists";
                        e->usage =
                                "Usage: database exists <family> <key>\n"
                                "       Counts Asterisk database keys, restricted to a given key.\n";
                        return NULL;
                case CLI_GENERATE:
                        return NULL;
        }
        
        if (a->argc != 4) {
                return CLI_SHOWUSAGE;
        }
                
        result = ast_db_exists(a->argv[2], a->argv[3]);
        if(result < 0){
                ast_cli(a->fd, "Count failed[%d].\n", result);
                return CLI_FAILURE;
        } else {
                ast_cli(a->fd, "Count: %d\n", result);
                return CLI_SUCCESS;
        }       
}

static char *handle_cli_database_put(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int res;

	switch (cmd) {
	case CLI_INIT:
		e->command = "database put";
		e->usage =
			"Usage: database put <family> <key> <value>\n"
			"       Adds or updates an entry in the Asterisk database for\n"
			"       a given family, key, and value.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 5)
		return CLI_SHOWUSAGE;
	res = ast_db_put(a->argv[2], a->argv[3], a->argv[4]);
	if (res)  {
		ast_cli(a->fd, "Failed to update entry\n");
	} else {
		ast_cli(a->fd, "Updated database successfully\n");
	}
	return CLI_SUCCESS;
}

static char *handle_cli_database_get(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int res;
	char tmp[MAX_DB_FIELD];

	switch (cmd) {
	case CLI_INIT:
		e->command = "database get";
		e->usage =
			"Usage: database get <family> <key>\n"
			"       Retrieves an entry in the Asterisk database for a given\n"
			"       family and key.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4)
		return CLI_SHOWUSAGE;
	res = ast_db_get(a->argv[2], a->argv[3], tmp, sizeof(tmp));
	if (res) {
		ast_cli(a->fd, "Database entry not found.\n");
	} else {
		ast_cli(a->fd, "Value: %s\n", tmp);
	}
	return CLI_SUCCESS;
}

static char *handle_cli_database_del(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int res;

	switch (cmd) {
	case CLI_INIT:
		e->command = "database del";
		e->usage =
			"Usage: database del <family> <key>\n"
			"       Deletes an entry in the Asterisk database for a given\n"
			"       family and key.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4)
		return CLI_SHOWUSAGE;
	res = ast_db_del2(a->argv[2], a->argv[3]);
	if (res) {
		ast_cli(a->fd, "Database entry could not be removed.\n");
	} else {
		ast_cli(a->fd, "Database entry removed.\n");
	}
	return CLI_SUCCESS;
}

static char *handle_cli_database_deltree(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int num_deleted;

	switch (cmd) {
	case CLI_INIT:
		e->command = "database deltree";
		e->usage =
			"Usage: database deltree <family> [keytree]\n"
			"   OR: database deltree <family>[/keytree]\n"
			"       Deletes a family or specific keytree within a family\n"
			"       in the Asterisk database.  The two arguments may be\n"
			"       separated by either a space or a slash.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if ((a->argc < 3) || (a->argc > 4))
		return CLI_SHOWUSAGE;
	if (a->argc == 4) {
		num_deleted = ast_db_deltree(a->argv[2], a->argv[3]);
	} else {
		num_deleted = ast_db_deltree(a->argv[2], NULL);
	}
	if (num_deleted < 0) {
		ast_cli(a->fd, "Database unavailable.\n");
	} else if (num_deleted == 0) {
		ast_cli(a->fd, "Database entries do not exist.\n");
	} else {
		ast_cli(a->fd, "%d database entries removed.\n",num_deleted);
	}
	return CLI_SUCCESS;
}

static char *handle_cli_database_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int counter = 0;
	struct ast_str *sql = ast_str_create(MAX_DB_VAL);
	MYSQL_RES *mysqlres;
	MYSQL_ROW row;

	switch (cmd) {
	case CLI_INIT:
		e->command = "database show";
		e->usage =
			"Usage: database show [family [keytree]]\n"
			"   OR: database show [family[/keytree]]\n"
			"       Shows Asterisk database contents, optionally restricted\n"
			"       to a given family, or family and keytree. The two arguments\n"
			"       may be separated either by a space or by a slash.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc == 4) {
		/* Family and key tree */
		ast_str_append(&sql, 0, "SELECT `key`, `value` FROM %s WHERE `key` LIKE '/%s/%s/%%' ORDER BY `key`;", dbtable, a->argv[2], a->argv[3]);
	} else if (a->argc == 3) {
		/* Family only */
		ast_str_append(&sql, 0, "SELECT `key`, `value` FROM %s WHERE `key` LIKE '/%s/%%' ORDER BY `key`;", dbtable, a->argv[2]);
	} else if (a->argc == 2) {
		/* Neither */
		ast_str_append(&sql, 0, "SELECT `key`, `value` FROM %s ORDER BY `key`;", dbtable);
	} else {
		return CLI_SHOWUSAGE;
	}

	mysqlres = db_query_mysql(ast_str_buffer(sql));
	if(mysqlres != NULL){
		while ((row = mysql_fetch_row(mysqlres)) != NULL) {
			const char *key_s, *value_s;
			if (!(key_s = (const char *) row[0])) {
				ast_log(LOG_WARNING, "Skipping invalid key!\n");
				continue;
			}
			if (!(value_s = (const char *) row[1])) {
				ast_log(LOG_WARNING, "Skipping invalid value!\n");
				continue;
			}
			++counter;
			ast_cli(a->fd, "%-50s: %-25s\n", key_s, value_s);
		}
	}

	mysql_free_result(mysqlres);

	ast_cli(a->fd, "%d results found.\n", counter);
	return CLI_SUCCESS;
}

static char *handle_cli_database_showkey(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int counter = 0;
	struct ast_str *sql = ast_str_create(MAX_DB_VAL);
	MYSQL_RES *mysqlres;
	MYSQL_ROW row;

	switch (cmd) {
		case CLI_INIT:
			e->command = "database showkey";
			e->usage =
				"Usage: database showkey <keytree>\n"
				"       Shows Asterisk database contents, restricted to a given key.\n";
			return NULL;
		case CLI_GENERATE:
			return NULL;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	ast_str_append(&sql, 0, "SELECT `key`, `value` FROM %s WHERE `key` LIKE '%%/%s' ORDER BY `key`;", dbtable, a->argv[2]);
	mysqlres = db_query_mysql(ast_str_buffer(sql));

	if(mysqlres != NULL){
		while ((row = mysql_fetch_row(mysqlres)) != NULL) {
			const char *key_s, *value_s;
			if (!(key_s = (const char *) row[0])) {
				break;
			}
			if (!(value_s = (const char *) row[1])) {
				break;
			}
			++counter;
			ast_cli(a->fd, "%-50s: %-25s\n", key_s, value_s);
		}
	}
	mysql_free_result(mysqlres);

	ast_cli(a->fd, "%d results found.\n", counter);
	return CLI_SUCCESS;
}

static char *handle_cli_database_query(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	MYSQL_RES *mysqlres;
	MYSQL_ROW row;
	unsigned int num_fields;
	unsigned int i;

	switch (cmd) {
		case CLI_INIT:
			e->command = "database query";
			e->usage =
				"Usage: database query \"<SQL Statement>\"\n"
				"       Run a user-specified SQL query on the database. Be careful.\n";
			return NULL;
		case CLI_GENERATE:
			return NULL;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	mysqlres = db_query_mysql(a->argv[2]);
	if(mysqlres != NULL){
		num_fields = mysql_num_fields(mysqlres);
		while ((row = mysql_fetch_row(mysqlres)) != NULL)
		{
			MYSQL_FIELD *fields;
			fields = mysql_fetch_fields(mysqlres);
			for(i = 0; i < num_fields; i++)
			{
				ast_cli(a->fd, "%-5s: %-50s\n", fields[i].name, row[i] ? row[i] : "NULL");
			}
			ast_cli(a->fd, "\n");
		}
	}
	mysql_free_result(mysqlres);

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_database[] = {
	AST_CLI_DEFINE(handle_cli_database_show,    "Shows database contents"),
	AST_CLI_DEFINE(handle_cli_database_showkey, "Shows database contents"),
	AST_CLI_DEFINE(handle_cli_database_get,     "Gets database value"),
	AST_CLI_DEFINE(handle_cli_database_exists,  "Check if a key/tree exists or not"),
	AST_CLI_DEFINE(handle_cli_database_put,     "Adds/updates database value"),
	AST_CLI_DEFINE(handle_cli_database_del,     "Removes database key/value"),
	AST_CLI_DEFINE(handle_cli_database_deltree, "Removes database keytree/values"),
	AST_CLI_DEFINE(handle_cli_database_query,   "Run a user-specified query on the astdb"),
};

static int manager_dbput(struct mansession *s, const struct message *m)
{
	const char *family = astman_get_header(m, "Family");
	const char *key = astman_get_header(m, "Key");
	const char *val = astman_get_header(m, "Val");
	int res;

	if (ast_strlen_zero(family)) {
		astman_send_error(s, m, "No family specified");
		return 0;
	}
	if (ast_strlen_zero(key)) {
		astman_send_error(s, m, "No key specified");
		return 0;
	}

	res = ast_db_put(family, key, S_OR(val, ""));
	if (res) {
		astman_send_error(s, m, "Failed to update entry");
	} else {
		astman_send_ack(s, m, "Updated database successfully");
	}
	return 0;
}

static int manager_dbget(struct mansession *s, const struct message *m)
{
	const char *id = astman_get_header(m,"ActionID");
	char idText[256];
	const char *family = astman_get_header(m, "Family");
	const char *key = astman_get_header(m, "Key");
	char tmp[MAX_DB_FIELD];
	int res;

	if (ast_strlen_zero(family)) {
		astman_send_error(s, m, "No family specified.");
		return 0;
	}
	if (ast_strlen_zero(key)) {
		astman_send_error(s, m, "No key specified.");
		return 0;
	}

	idText[0] = '\0';
	if (!ast_strlen_zero(id))
		snprintf(idText, sizeof(idText) ,"ActionID: %s\r\n", id);

	res = ast_db_get(family, key, tmp, sizeof(tmp));
	if (res) {
		astman_send_error(s, m, "Database entry not found");
	} else {
		astman_send_listack(s, m, "Result will follow", "start");

		astman_append(s, "Event: DBGetResponse\r\n"
				"Family: %s\r\n"
				"Key: %s\r\n"
				"Val: %s\r\n"
				"%s"
				"\r\n",
				family, key, tmp, idText);

		astman_send_list_complete_start(s, m, "DBGetComplete", 1);
		astman_send_list_complete_end(s);
	}
	return 0;
}

static int manager_db_tree_get(struct mansession *s, const struct message *m)
{
	char idText[256];
	const char *id = astman_get_header(m,"ActionID");
	const char *family = astman_get_header(m, "Family");
	const char *key = astman_get_header(m, "Key");
	struct ast_str *sql = ast_str_create(MAX_DB_VAL);
	MYSQL_RES *mysqlres;
	MYSQL_ROW row;
	int count = 0;

	if (!ast_strlen_zero(family) && !ast_strlen_zero(key)) {
		/* Family and key tree */
		ast_str_append(&sql, 0, "SELECT `key`, `value` FROM %s WHERE `key` LIKE '/%s/%s/%%' ORDER BY `key`;", dbtable, family, key);
	} else if (!ast_strlen_zero(family)) {
		/* Family only */
		ast_str_append(&sql, 0, "SELECT `key`, `value` FROM %s WHERE `key` LIKE '/%s/%%' ORDER BY `key`;", dbtable, family);
	} else {
		/* Neither */
		ast_str_append(&sql, 0, "SELECT `key`, `value` FROM %s ORDER BY `key`;", dbtable);
	}

	idText[0] = '\0';
	if (!ast_strlen_zero(id)) {
		snprintf(idText, sizeof(idText) ,"ActionID: %s\r\n", id);
	}

	astman_send_listack(s, m, "Result will follow", "start");
	mysqlres = db_query_mysql(ast_str_buffer(sql));
	if(mysqlres != NULL){
		while ((row = mysql_fetch_row(mysqlres)) != NULL) {
			const char *key_s, *value_s;
			if (!(key_s = (const char *) row[0])) {
				ast_log(LOG_WARNING, "Skipping invalid key!\n");
				continue;
			}
			if (!(value_s = (const char *) row[1])) {
				ast_log(LOG_WARNING, "Skipping invalid value!\n");
				continue;
			}
			astman_append(s, "Event: DBGetTreeResponse\r\n"
				"Key: %s\r\n"
				"Val: %s\r\n"
				"%s"
				"\r\n",
				key_s, value_s, idText);
			count++;
		}
	}
	mysql_free_result(mysqlres);

	astman_send_list_complete_start(s, m, "DBGetTreeComplete", count);
	astman_send_list_complete_end(s);

	return 0;
}

static int manager_dbdel(struct mansession *s, const struct message *m)
{
	const char *family = astman_get_header(m, "Family");
	const char *key = astman_get_header(m, "Key");
	int res;

	if (ast_strlen_zero(family)) {
		astman_send_error(s, m, "No family specified.");
		return 0;
	}

	if (ast_strlen_zero(key)) {
		astman_send_error(s, m, "No key specified.");
		return 0;
	}

	res = ast_db_del2(family, key);
	if (res)
		astman_send_error(s, m, "Database entry could not be deleted");
	else
		astman_send_ack(s, m, "Key deleted successfully");

	return 0;
}

static int manager_dbdeltree(struct mansession *s, const struct message *m)
{
	const char *family = astman_get_header(m, "Family");
	const char *key = astman_get_header(m, "Key");
	int num_deleted;

	if (ast_strlen_zero(family)) {
		astman_send_error(s, m, "No family specified.");
		return 0;
	}

	if (!ast_strlen_zero(key)) {
		num_deleted = ast_db_deltree(family, key);
	} else {
		num_deleted = ast_db_deltree(family, NULL);
	}

	if (num_deleted < 0) {
		astman_send_error(s, m, "Database unavailable");
	} else if (num_deleted == 0) {
		astman_send_error(s, m, "Database entry not found");
	} else {
		astman_send_ack(s, m, "Key tree deleted successfully");
	}

	return 0;
}

/*!
 * \internal
 * \brief Clean up resources on Asterisk shutdown
 */
static void astdb_atexit(void)
{
	ast_cli_unregister_multiple(cli_database, ARRAY_LEN(cli_database));
	ast_manager_unregister("DBGet");
	ast_manager_unregister("DBGetTree");
	ast_manager_unregister("DBPut");
	ast_manager_unregister("DBDel");
	ast_manager_unregister("DBDelTree");

	mysql_close(mysql);
}

int astdb_init(void)
{
	if (db_init()) {
		return -1;
	}

	ast_register_atexit(astdb_atexit);
	ast_cli_register_multiple(cli_database, ARRAY_LEN(cli_database));
	ast_manager_register_xml_core("DBGet", EVENT_FLAG_SYSTEM | EVENT_FLAG_REPORTING, manager_dbget);
	ast_manager_register_xml_core("DBGetTree", EVENT_FLAG_SYSTEM | EVENT_FLAG_REPORTING, manager_db_tree_get);
	ast_manager_register_xml_core("DBPut", EVENT_FLAG_SYSTEM, manager_dbput);
	ast_manager_register_xml_core("DBDel", EVENT_FLAG_SYSTEM, manager_dbdel);
	ast_manager_register_xml_core("DBDelTree", EVENT_FLAG_SYSTEM, manager_dbdeltree);
	return 0;
}
