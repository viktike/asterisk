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
 * ODBC backend for AstDB by Sperl Viktor 
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
	<depend>generic_odbc</depend>
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

// unixODBC
#include <sql.h>
#include <sqlext.h>

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

#define	CONFIG	"astdb_odbc.conf"
#define MAX_DB_FIELD 256
#define MAX_DB_VALUE 2048
#define MAXSIZE 4096
char table[MAX_DB_FIELD];
char dsn_uri[MAX_DB_FIELD];

SQLHENV env;
SQLHDBC dbc;

AST_MUTEX_DEFINE_STATIC(dblock);

static int db_open(void)
{
	SQLRETURN fsts;
	/* Allocate an environment handle */
	SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env);
	/* We want ODBC 3 support */
	SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (void *) SQL_OV_ODBC3, 0);
	/* Allocate a connection handle */
	SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc);
	fsts = SQLDriverConnect(dbc, NULL, (unsigned char *) dsn_uri, SQL_NTS, NULL, 0, NULL, SQL_DRIVER_COMPLETE);
	if (fsts == SQL_SUCCESS || fsts == SQL_SUCCESS_WITH_INFO){
		return 0;
	} else {
		ast_log(LOG_ERROR, "AstDB connect failed[%d]\n", fsts);
	    return -1;
	}
}
static void db_close(void)
{
	SQLDisconnect(dbc);
	SQLFreeHandle(SQL_HANDLE_DBC, dbc);
	SQLFreeHandle(SQL_HANDLE_ENV, env);
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
		if (!strcasecmp(var->name, "dsn")) {
			sprintf(dsn_uri, "DSN=%s;", var->value);
			ast_log(LOG_NOTICE, "Found AstDB: %s\n", dsn_uri);
		} else if(!strcasecmp(var->name, "table")){
			ast_copy_string(table, var->value, sizeof(table));
			ast_log(LOG_NOTICE, "Found AstDB table: %s\n", table);
		} else {
			ast_log(LOG_WARNING, "Found unknown variable in " CONFIG " general section: %s = %s\n", var->name, var->value);
		}
	}
	return 0;
}
/* Query type statements that will produce a result-set */
static int db_query_odbc(const char *sql, SQLHSTMT *stmt)
{
	SQLRETURN ret;
	ret = SQLExecDirect(stmt, (unsigned char *) sql, SQL_NTS);
	if(SQL_SUCCEEDED(ret)){
		return -1;
	} else {
		ast_log(LOG_WARNING, "Abandoning statement[%s] after failed reconnect.\n", sql);
		db_close();
		db_open();
		return 0;
	}
}
/* Execute style statements that will not produce a result-set */
static int db_execute_odbc(const char *sql)
{
	SQLRETURN ret;
	SQLHSTMT stmt;
	SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);
	ret = SQLExecDirect(stmt, (unsigned char *) sql, SQL_NTS);
	if(SQL_SUCCEEDED(ret)){
		SQLFreeHandle(SQL_HANDLE_STMT, stmt);
		return 0;
	} else {
		SQLFreeHandle(SQL_HANDLE_STMT, stmt);
		db_close();
		if(db_open()){
			ast_log(LOG_WARNING, "Abandoning statement[%s] after failed reconnect.\n", sql);
			return -1;
		} else {
			SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);
			ret = SQLExecDirect(stmt, (unsigned char *) sql, SQL_NTS);
			if(SQL_SUCCEEDED(ret)){
				SQLFreeHandle(SQL_HANDLE_STMT, stmt);
				return 0;
			} else {
				SQLFreeHandle(SQL_HANDLE_STMT, stmt);
				return -1;
			}
		}
	}
}

static int db_create_astdb(void)
{
	struct ast_str *sql = ast_str_create(MAXSIZE);
    ast_str_append(&sql, 0, "CREATE TABLE IF NOT EXISTS %s (`key` VARCHAR(%d) NOT NULL, `value` VARCHAR(%d) NULL DEFAULT NULL, PRIMARY KEY (`key`));", table, MAX_DB_FIELD, MAX_DB_VALUE);
	return db_execute_odbc(ast_str_buffer(sql));
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
	struct ast_str *sql = ast_str_create(MAXSIZE);
	int res = 0;

	if (strlen(family) + strlen(key) + 2 > sizeof(fullkey) - 1){
		ast_log(LOG_WARNING, "Family and key length must be less than %zu bytes\n", sizeof(fullkey) - 3);
		return -1;
	}
    snprintf(fullkey, sizeof(fullkey), "/%s/%s", family, key);
    ast_str_append(&sql, 0, "INSERT INTO %s (`key`, `value`) VALUES ('%s', '%s') ON DUPLICATE KEY UPDATE `value`='%s';", table, fullkey, value, value);
	ast_mutex_lock(&dblock);
	res = db_execute_odbc(ast_str_buffer(sql));
	ast_mutex_unlock(&dblock);
	if(sql){
		ast_free(sql);
	}
	return res;
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
	struct ast_str *sql = ast_str_create(MAXSIZE);
    long int length = 0;
	char v[MAX_DB_VALUE];

	SQLRETURN ret;
	SQLHSTMT stmt;

	if (strlen(family) + strlen(key) + 2 > sizeof(fullkey) - 1) {
		ast_log(LOG_WARNING, "Family and key length must be less than %zu bytes\n", sizeof(fullkey) - 3);
		return -1;
	}

	snprintf(fullkey, sizeof(fullkey), "/%s/%s", family, key);

	ast_str_append(&sql, 0, "SELECT `value` FROM %s WHERE `key`='%s';", table, fullkey);
	ast_mutex_lock(&dblock);
	SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);
	db_query_odbc(ast_str_buffer(sql), stmt);
	if(sql){
		ast_free(sql);
	}
	SQLFetch(stmt);

	ret = SQLGetData(stmt, 1, SQL_C_CHAR, v, MAX_DB_VALUE, &length);
	SQLFreeHandle(SQL_HANDLE_STMT, stmt);
	ast_mutex_unlock(&dblock);
	const char *value = (const char *) v;
	if (bufferlen == -1) {
		*buffer = ast_strdup(value);
	} else {
		ast_copy_string(*buffer, value, bufferlen);
	}
	if (SQL_SUCCEEDED(ret)) {
		return 0;
	} else {
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

int ast_db_exists(const char *family, const char *key)
{
	char fullkey[MAX_DB_FIELD];
	struct ast_str *sql = ast_str_create(MAXSIZE);
	long int length = 0;
	char v[MAX_DB_VALUE];
	int exists = 0;

	SQLRETURN ret;
	SQLHSTMT stmt;

	if (strlen(family) + strlen(key) + 2 > sizeof(fullkey) - 1) {
		ast_log(LOG_WARNING, "Family and key length must be less than %zu bytes\n", sizeof(fullkey) - 3);
		return -1;
	}

	snprintf(fullkey, sizeof(fullkey), "/%s/%s", family, key);

	ast_str_append(&sql, 0, "SELECT CAST(COUNT(`value`) AS CHAR) FROM %s WHERE `key`='%s';", table, fullkey);
	ast_mutex_lock(&dblock);
	SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);
	db_query_odbc(ast_str_buffer(sql), stmt);
	if(sql){
		ast_free(sql);
	}
	SQLFetch(stmt);

	ret = SQLGetData(stmt, 1, SQL_C_CHAR, v, MAX_DB_VALUE, &length);
	SQLFreeHandle(SQL_HANDLE_STMT, stmt);
	ast_mutex_unlock(&dblock);
	if (SQL_SUCCEEDED(ret) && length == 1) {
		exists = v[0] - '0';
		return exists;
	} else {
		return -1;
	}
}

int ast_db_del(const char *family, const char *key)
{
	char fullkey[MAX_DB_FIELD];
	int res = 0;
	struct ast_str *sql = ast_str_create(MAXSIZE);

	if (strlen(family) + strlen(key) + 2 > sizeof(fullkey) - 1) {
		ast_log(LOG_WARNING, "Family and key length must be less than %zu bytes\n", sizeof(fullkey) - 3);
		return -1;
	}

	snprintf(fullkey, sizeof(fullkey), "/%s/%s", family, key);
	ast_str_append(&sql, 0, "DELETE FROM %s WHERE `key`='%s';", table, fullkey);
	ast_mutex_lock(&dblock);
	res = db_execute_odbc(ast_str_buffer(sql));
	ast_mutex_unlock(&dblock);
	if(sql){
		ast_free(sql);
	}
	return res;
}

int ast_db_del2(const char *family, const char *key)
{
	char fullkey[MAX_DB_FIELD];
	char tmp[1];
	int res = 0;
	struct ast_str *sql = ast_str_create(MAXSIZE);

	if (strlen(family) + strlen(key) + 2 > sizeof(fullkey) - 1) {
		ast_log(LOG_WARNING, "Family and key length must be less than %zu bytes\n", sizeof(fullkey) - 3);
		return -1;
	}

	snprintf(fullkey, sizeof(fullkey), "/%s/%s", family, key);

	ast_mutex_lock(&dblock);
	if (ast_db_get(family, key, tmp, sizeof(tmp))) {
		ast_log(LOG_WARNING, "AstDB key %s does not exist\n", fullkey);
		res = -1;
	} else {
		ast_str_append(&sql, 0, "DELETE FROM %s WHERE `key`='%s';", table, fullkey);
		db_execute_odbc(ast_str_buffer(sql));
		res = 0;
	}
	ast_mutex_unlock(&dblock);
	if(sql){
		ast_free(sql);
	}
	return res;
}

int ast_db_deltree(const char *family, const char *keytree)
{
	struct ast_str *sql = ast_str_create(MAXSIZE);
	int res = 0;

	if (!ast_strlen_zero(family)) {
		if (!ast_strlen_zero(keytree)) {
			ast_str_append(&sql, 0, "DELETE FROM %s WHERE `key` LIKE '/%s/%s%%';", table, family, keytree);
		} else {
			ast_str_append(&sql, 0, "DELETE FROM %s WHERE `key` LIKE '/%s/%%';", table, family);
		}
	} else {
		ast_str_append(&sql, 0, "TRUNCATE TABLE %s;", table);
	}

	ast_mutex_lock(&dblock);
	res = db_execute_odbc(ast_str_buffer(sql));
	ast_mutex_unlock(&dblock);
	if(sql){
		ast_free(sql);
	}
	return res;
}

static struct ast_db_entry *db_gettree_common(SQLHSTMT * stmt)
{
	SQLRETURN ret;
	struct ast_db_entry *head = NULL, *prev = NULL, *cur;
	while (SQL_SUCCEEDED(ret = SQLFetch(stmt))) {
		long int kindicator;
		long int vindicator;

		char k[MAX_DB_FIELD];
		char v[MAX_DB_VALUE];

		SQLGetData(stmt, 1, SQL_C_CHAR, k, sizeof(k), &kindicator);
		SQLGetData(stmt, 2, SQL_C_CHAR, v, sizeof(v), &vindicator);
	
		const char *key, *value;
		size_t key_len, value_len;

		key   = (const char *) k;
		value = (const char *) v;

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
	return head;
}

struct ast_db_entry *ast_db_gettree(const char *family, const char *keytree)
{
	struct ast_db_entry *ret = NULL;
	struct ast_str *sql = ast_str_create(MAXSIZE);

	SQLHSTMT stmt;

	if (!ast_strlen_zero(family)) {
		if (!ast_strlen_zero(keytree)) {
			ast_str_append(&sql, 0, "SELECT `key`, `value` FROM %s WHERE `key` LIKE '/%s/%s%%' ORDER BY `key`;", table, family, keytree);
		} else {
			ast_str_append(&sql, 0, "SELECT `key`, `value` FROM %s WHERE `key` LIKE '/%s%%' ORDER BY `key`;", table, family);
		}
	} else {
		ast_str_append(&sql, 0, "SELECT `key`, `value` FROM %s ORDER BY `key`;", table);
	}

	ast_mutex_lock(&dblock);
	SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);
	if(db_query_odbc(ast_str_buffer(sql),stmt)){
		ret = db_gettree_common(stmt);
	}
	SQLFreeHandle(SQL_HANDLE_STMT, stmt);
	ast_mutex_unlock(&dblock);
	if(sql){
		ast_free(sql);
	}
	return ret;
}

struct ast_db_entry *ast_db_gettree_by_prefix(const char *family, const char *key_prefix)
{
	struct ast_db_entry *ret = NULL;
	struct ast_str *sql = ast_str_create(MAXSIZE);
	
	SQLHSTMT stmt;

	ast_str_append(&sql, 0, "SELECT `key`, `value` FROM %s WHERE `key` LIKE '/%s/%s%%' ORDER BY `key`;", table, family, key_prefix);
	ast_mutex_lock(&dblock);
	SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);
	if(db_query_odbc(ast_str_buffer(sql),stmt)){
		ret = db_gettree_common(stmt);
	}
	SQLFreeHandle(SQL_HANDLE_STMT, stmt);
	ast_mutex_unlock(&dblock);
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
	char tmp[MAX_DB_VALUE];

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

	if (a->argc != 4){
		return CLI_SHOWUSAGE;
	}
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
	int res = 0;

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
		res = ast_db_deltree(a->argv[2], a->argv[3]);
	} else {
		res = ast_db_deltree(a->argv[2], NULL);
	}
	if (res == 0) {
		ast_cli(a->fd, "Database entries removed.\n");
	} else  {
		ast_cli(a->fd, "Database unavailable.\n");
	}

	return CLI_SUCCESS;
}

static char *handle_cli_database_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int counter = 0;
	struct ast_str *sql = ast_str_create(MAXSIZE);

	SQLHSTMT stmt;
	SQLRETURN ret;

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
		ast_str_append(&sql, 0, "SELECT `key`, `value` FROM %s WHERE `key` LIKE '/%s/%s%%' ORDER BY `key`;", table, a->argv[2], a->argv[3]);
	} else if (a->argc == 3) {
		ast_str_append(&sql, 0, "SELECT `key`, `value` FROM %s WHERE `key` LIKE '/%s%%' ORDER BY `key`;", table, a->argv[2]);
	} else if (a->argc == 2) {
		ast_str_append(&sql, 0, "SELECT `key`, `value` FROM %s ORDER BY `key`;", table);
	} else {
		return CLI_SHOWUSAGE;
	}

	ast_mutex_lock(&dblock);
	SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);
	if(db_query_odbc(ast_str_buffer(sql),stmt)){
		while (SQL_SUCCEEDED(ret = SQLFetch(stmt))) {
			long int kindicator;
			long int vindicator;

			char k[MAX_DB_FIELD];
			char v[MAX_DB_VALUE];

			SQLGetData(stmt, 1, SQL_C_CHAR, k, sizeof(k), &kindicator);
			SQLGetData(stmt, 2, SQL_C_CHAR, v, sizeof(v), &vindicator);

			const char *key_s, *value_s;
			if (!(key_s = (const char *) k)) {
				ast_log(LOG_WARNING, "Skipping invalid key!\n");
				continue;
			}
			if (!(value_s = (const char *) v)) {
				ast_log(LOG_WARNING, "Skipping invalid value!\n");
				continue;
			}
			counter++;
			ast_cli(a->fd, "%-50s: %-25s\n", key_s, value_s);
		}
	}
	SQLFreeHandle(SQL_HANDLE_STMT, stmt);
	ast_mutex_unlock(&dblock);

	ast_cli(a->fd, "%d results found.\n", counter);
	
	if(sql){
		ast_free(sql);
	}

	return CLI_SUCCESS;
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

static char *handle_cli_database_showkey(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int counter = 0;
	struct ast_str *sql = ast_str_create(MAXSIZE);
	
	SQLHSTMT stmt;
	SQLRETURN ret;

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

	ast_str_append(&sql, 0, "SELECT `key`, `value` FROM %s WHERE `key` LIKE '%%/%s' ORDER BY `key`;", table, a->argv[2]);
	ast_mutex_lock(&dblock);
	SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);
	if(db_query_odbc(ast_str_buffer(sql),stmt)){
		while (SQL_SUCCEEDED(ret = SQLFetch(stmt))) {
			long int kindicator;
			long int vindicator;

			char k[MAX_DB_FIELD];
			char v[MAX_DB_VALUE];

			SQLGetData(stmt, 1, SQL_C_CHAR, k, sizeof(k), &kindicator);
			SQLGetData(stmt, 2, SQL_C_CHAR, v, sizeof(v), &vindicator);

			const char *key_s, *value_s;
			if (!(key_s = (const char *) k)) {
				break;
			}
			if (!(value_s = (const char *) v)) {
				break;
			}
			counter++;
			ast_cli(a->fd, "%-50s: %-25s\n", key_s, value_s);
		}
	}
	SQLFreeHandle(SQL_HANDLE_STMT, stmt);
	ast_mutex_unlock(&dblock);
	ast_cli(a->fd, "%d results found.\n", counter);
	if(sql){
		ast_free(sql);
	}

	return CLI_SUCCESS;
}

static char *handle_cli_database_reload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
		case CLI_INIT:
			e->command = "database reload";
			e->usage =
				"Usage: database reload\n"
				"       Try to reconnect to the database.\n";
			return NULL;
		case CLI_GENERATE:
			return NULL;
	}
	if (a->argc != 2) {
		return CLI_SHOWUSAGE;
	}

    ast_mutex_lock(&dblock);
	db_close();
	load_config();
	if(db_open()){
		ast_mutex_unlock(&dblock);
		ast_cli(a->fd, "Reconnect failed.\n");
		return CLI_FAILURE;
	} else {
		ast_cli(a->fd, "Reconnect successful.\n");
		if(db_create_astdb()){
			ast_mutex_unlock(&dblock);
			ast_cli(a->fd, "Table create failed.\n");
			return CLI_FAILURE;
		} else {
			ast_mutex_unlock(&dblock);
		    return CLI_SUCCESS;
		}
	}
}

static char *handle_cli_database_query(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	SQLHSTMT stmt;
	SQLRETURN ret;
	SQLSMALLINT columns;

	int rcounter = 0;

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

	ast_mutex_lock(&dblock);
	SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);
	if(db_query_odbc(a->argv[2], stmt)){
		SQLNumResultCols(stmt, &columns);
		while (SQL_SUCCEEDED(ret = SQLFetch(stmt))){
			rcounter++;
			int fcounter = 0;
			ast_cli(a->fd, "%d row:\n", rcounter);
			for (SQLUSMALLINT i = 1; i <= columns; i++) {
				long int indicator;
				char buffer[MAX_DB_VALUE];
				char col[MAX_DB_FIELD];
				/* retrieve column data as a string */
				ret = SQLGetData(stmt, i, SQL_C_CHAR, buffer, sizeof(buffer), &indicator);
				if (SQL_SUCCEEDED(ret)) {
					fcounter++;
					sprintf(col, "%d column", fcounter);
					/* Handle null columns */
					if (indicator == SQL_NULL_DATA) strcpy(buffer, "NULL");
					ast_cli(a->fd, "%-5s: %-50s\n", col, buffer);
				}
			}
			ast_cli(a->fd, "\n");
		}
	}
	SQLFreeHandle(SQL_HANDLE_STMT, stmt);
	ast_mutex_unlock(&dblock);

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_database[] = {
	AST_CLI_DEFINE(handle_cli_database_show,    "Shows database contents"),
	AST_CLI_DEFINE(handle_cli_database_showkey, "Shows database contents"),
	AST_CLI_DEFINE(handle_cli_database_get,     "Gets database value"),
	AST_CLI_DEFINE(handle_cli_database_put,     "Adds/updates database value"),
	AST_CLI_DEFINE(handle_cli_database_del,     "Removes database key/value"),
	AST_CLI_DEFINE(handle_cli_database_deltree, "Removes database keytree/values"),
	AST_CLI_DEFINE(handle_cli_database_exists,  "Check if a key/tree exists or not"),
	AST_CLI_DEFINE(handle_cli_database_query,   "Run a user-specified query on the astdb"),
	AST_CLI_DEFINE(handle_cli_database_reload,  "Try to reconnect to the database"),
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
	char tmp[MAX_DB_VALUE];
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
	struct ast_str *sql = ast_str_create(MAXSIZE);
	
	SQLHSTMT stmt;
	SQLRETURN ret;
	int counter = 0;

	if (!ast_strlen_zero(family) && !ast_strlen_zero(key)) {
		ast_str_append(&sql, 0, "SELECT `key`, `value` FROM %s WHERE `key` LIKE '/%s/%s%%' ORDER BY `key`;", table, family, key);
	} else if (!ast_strlen_zero(family)) {
		ast_str_append(&sql, 0, "SELECT `key`, `value` FROM %s WHERE `key` LIKE '/%s%%' ORDER BY `key`;", table, family);
	} else {
		ast_str_append(&sql, 0, "SELECT `key`, `value` FROM %s ORDER BY `key`;", table);
	}

	idText[0] = '\0';
	if (!ast_strlen_zero(id)) {
		snprintf(idText, sizeof(idText) ,"ActionID: %s\r\n", id);
	}

	astman_send_listack(s, m, "Result will follow", "start");
	ast_mutex_lock(&dblock);

	SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);
	if(db_query_odbc(ast_str_buffer(sql),stmt)){
		while (SQL_SUCCEEDED(ret = SQLFetch(stmt))) {
			long int kindicator;
			long int vindicator;

			char k[MAX_DB_FIELD];
			char v[MAX_DB_VALUE];

			SQLGetData(stmt, 1, SQL_C_CHAR, k, sizeof(k), &kindicator);
			SQLGetData(stmt, 2, SQL_C_CHAR, v, sizeof(v), &vindicator);

			const char *key_s, *value_s;
			if (!(key_s = (const char *) k)) {
				ast_log(LOG_WARNING, "Skipping invalid key!\n");
				continue;
			}
			if (!(value_s = (const char *) v)) {
				ast_log(LOG_WARNING, "Skipping invalid value!\n");
				continue;
			}
			astman_append(s, "Event: DBGetTreeResponse\r\n"
				"Key: %s\r\n"
				"Val: %s\r\n"
				"%s"
				"\r\n",
				key_s, value_s, idText);
			counter++;
		}
	}
	SQLFreeHandle(SQL_HANDLE_STMT, stmt);
	ast_mutex_unlock(&dblock);
	if(sql){
		ast_free(sql);
	}

	astman_send_list_complete_start(s, m, "DBGetTreeComplete", counter);
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
	int res = 0;

	if (ast_strlen_zero(family)) {
		astman_send_error(s, m, "No family specified.");
		return 0;
	}

	if (!ast_strlen_zero(key)) {
		res = ast_db_deltree(family, key);
	} else {
		res = ast_db_deltree(family, NULL);
	}

	if(res == 0){
		astman_send_ack(s, m, "Key tree deleted successfully");
	} else {
		astman_send_error(s, m, "Database unavailable");
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

	db_close();
}

int astdb_init(void)
{
	if (db_init()) {
		return -1;
	} else {
		ast_register_atexit(astdb_atexit);
		ast_cli_register_multiple(cli_database, ARRAY_LEN(cli_database));
		ast_manager_register_xml_core("DBGet", EVENT_FLAG_SYSTEM | EVENT_FLAG_REPORTING, manager_dbget);
		ast_manager_register_xml_core("DBGetTree", EVENT_FLAG_SYSTEM | EVENT_FLAG_REPORTING, manager_db_tree_get);
		ast_manager_register_xml_core("DBPut", EVENT_FLAG_SYSTEM, manager_dbput);
		ast_manager_register_xml_core("DBDel", EVENT_FLAG_SYSTEM, manager_dbdel);
		ast_manager_register_xml_core("DBDelTree", EVENT_FLAG_SYSTEM, manager_dbdeltree);
		return 0;
	}
}
