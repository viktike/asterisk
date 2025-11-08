/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2011
 *
 * weixiang zhong - adapted to CEL, from:
 * weixiang zhong <zhongxiang721@gmail.com> 
 * Adapted from the MySQL CEL logger originally by James Sharp 
 *
 * Modified March 2011
 * weixiang zhong <zhongxiang721@gmail.com>

 * Modified March 2011
 * Jonathan Penny <jonathan@pennywellvoice.com>
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

/*! \file
 *
 * \brief Mysql CEL logger 
 * 
 * \author weixiang zhong <zhongxiang721@gmail.com>
 * \extref Mysql http://www.mysql.org/
 *
 * See also
 * \arg \ref Config_cel
 * \extref Mysql http://www.mysql.org/
 * \ingroup cel_drivers
 */

/*** MODULEINFO
	<depend>mysqlclient</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include <mysql/mysql.h>
#include <mysql/errmsg.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#include "asterisk/cli.h"
#include "asterisk/config.h"
#include "asterisk/options.h"
#include "asterisk/channel.h"
#include "asterisk/cel.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk.h"

#define DATE_FORMAT "%Y-%m-%d %T.%6q"
#define MYSQL_BACKEND_NAME "MYSQL CEL Backend"


static char *config = "cel_mysql.conf";
static char *myhostname = NULL, *mydbname = NULL, *mydbuser = NULL, *mypassword = NULL, *mydbsock = NULL, *table = NULL, *dbcharset = NULL;
static int mydbport = 0;

static int connected = 0;
static time_t connect_time = 0;  //reversed

static int maxsize = 512, maxsize2 = 512;

static MYSQL mysql;
static unsigned int timeout = 0;

AST_MUTEX_DEFINE_STATIC(mysql_lock);

struct columns {
        char *name;
        char *type;
        int len;
        unsigned int notnull:1;
        unsigned int hasdefault:1;
        AST_RWLIST_ENTRY(columns) list;
};

char sqldesc[128];

static AST_RWLIST_HEAD_STATIC(mysql_columns, columns);

#define LENGTHEN_BUF1(size) \
	do { \
		/* Lengthen buffer, if necessary */ \
		if (ast_str_strlen(sql) + size + 1 > ast_str_size(sql)) { \
			if (ast_str_make_space(&sql, ((ast_str_size(sql) + size + 3) / 512 + 1) * 512) != 0) { \
				ast_log(LOG_ERROR, "Unable to allocate sufficient memory.  Insert CEL failed.\n"); \
				ast_free(sql); \
				ast_free(sql2); \
				AST_RWLIST_UNLOCK(&mysql_columns); \
				return; \
			} \
		} \
	} while (0)

#define LENGTHEN_BUF2(size) \
	do { \
		if (ast_str_strlen(sql2) + size + 1 > ast_str_size(sql2)) { \
			if (ast_str_make_space(&sql2, ((ast_str_size(sql2) + size + 3) / 512 + 1) * 512) != 0) { \
				ast_log(LOG_ERROR, "Unable to allocate sufficient memory.  Insert CEL failed.\n"); \
				ast_free(sql); \
				ast_free(sql2); \
				AST_RWLIST_UNLOCK(&mysql_columns); \
				return; \
			} \
		} \
	} while (0)


static char *handle_cli_cel_mysql_status(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "cel mysql status";
		e->usage =
			"Usage: cel mysql status\n"
			"       Shows current connection status for cel_mysql\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3)
		return CLI_SHOWUSAGE;

	if (connected) {
		char status[256], status2[100] = "";
		int ctime = time(NULL) - connect_time;
		if (mydbport)
			snprintf(status, 255, "Connected to %s@%s, port %d", mydbname, myhostname, mydbport);
		else if (mydbsock)
			snprintf(status, 255, "Connected to %s on socket file %s", mydbname, S_OR(mydbsock, "default"));
		else
			snprintf(status, 255, "Connected to %s@%s", mydbname, myhostname);

		if (!ast_strlen_zero(mydbuser))
			snprintf(status2, 99, " with username %s", mydbuser);
		if (!ast_strlen_zero(table))
			snprintf(status2, 99, " using table %s", table);
		if (ctime > 31536000) {
			ast_cli(a->fd, "%s%s for %d years, %d days, %d hours, %d minutes, %d seconds.\n", status, status2, ctime / 31536000, (ctime % 31536000) / 86400, (ctime % 86400) / 3600, (ctime % 3600) / 60, ctime % 60);
		} else if (ctime > 86400) {
			ast_cli(a->fd, "%s%s for %d days, %d hours, %d minutes, %d seconds.\n", status, status2, ctime / 86400, (ctime % 86400) / 3600, (ctime % 3600) / 60, ctime % 60);
		} else if (ctime > 3600) {
			ast_cli(a->fd, "%s%s for %d hours, %d minutes, %d seconds.\n", status, status2, ctime / 3600, (ctime % 3600) / 60, ctime % 60);
		} else if (ctime > 60) {
			ast_cli(a->fd, "%s%s for %d minutes, %d seconds.\n", status, status2, ctime / 60, ctime % 60);
		} else {
			ast_cli(a->fd, "%s%s for %d seconds.\n", status, status2, ctime);
		}
	
	} else {
		ast_cli(a->fd, "Not currently connected to a MySQL server.\n");
	}

	return CLI_SUCCESS;
}

static struct ast_cli_entry cel_mysql_status_cli[] = {
	AST_CLI_DEFINE(handle_cli_cel_mysql_status, "Show connection status of cel_mysql"),
};



static void mysql_log(struct ast_event *event)
{
	struct ast_tm tm;
	char timestr[128];
	int retries = 5;
#if MYSQL_VERSION_ID >= 80000
	_Bool my_bool_true = 1;
#elif MYSQL_VERSION_ID >= 50013
	my_bool my_bool_true = 1;
#endif

	struct ast_cel_event_record record = {
		.version = AST_CEL_EVENT_RECORD_VERSION,
	};

	if (ast_cel_fill_record(event, &record)) {
		return;
	}

	ast_mutex_lock(&mysql_lock);

	ast_localtime(&record.event_time, &tm, NULL);
	ast_strftime(timestr, sizeof(timestr), DATE_FORMAT, &tm);

db_reconnect:
		if ((!connected) && myhostname && mydbuser && mypassword && mydbname && mydbsock && mydbport) {
		
		mysql_init(&mysql);
		
		if(dbcharset){
			snprintf(sqldesc, sizeof(sqldesc), "SET NAMES %s", dbcharset);
			mysql_options(&mysql, MYSQL_INIT_COMMAND, sqldesc);
			mysql_options(&mysql, MYSQL_SET_CHARSET_NAME, dbcharset);
		}

		/* Add option to quickly timeout the connection */
		if (timeout && mysql_options(&mysql, MYSQL_OPT_CONNECT_TIMEOUT, (char *)&timeout)!=0) {
			ast_log(LOG_ERROR, "cel_mysql: mysql_options returned (%d) %s\n", mysql_errno(&mysql), mysql_error(&mysql));
		}
#if MYSQL_VERSION_ID >= 50013
		/* Add option for automatic reconnection */
		if (mysql_options(&mysql, MYSQL_OPT_RECONNECT, &my_bool_true) != 0) {
			ast_log(LOG_ERROR, "cel_mysql: mysql_options returned (%d) %s\n", mysql_errno(&mysql), mysql_error(&mysql));
		}
#endif
		if (mysql_real_connect(&mysql, myhostname, mydbuser, mypassword, mydbname, mydbport, mydbsock, 0)) {
			connected = 1;
			connect_time = time(NULL);
			if (dbcharset) {
				snprintf(sqldesc, sizeof(sqldesc), "SET NAMES '%s'", dbcharset);
				mysql_real_query(&mysql, sqldesc, strlen(sqldesc));
				ast_debug(1, "SQL command as follows: %s\n", sqldesc);
			}
		} else {
			ast_log(LOG_ERROR, "cel_mysql: cannot connect to database server %s.\n", myhostname);
			connected = 0;
		}
	} else {
		int error;
		if ((error = mysql_ping(&mysql))) {
			connected = 0;
			switch (mysql_errno(&mysql)) {
				case CR_SERVER_GONE_ERROR:
				case CR_SERVER_LOST:
					ast_log(LOG_ERROR, "cel_mysql: Server has gone away. Attempting to reconnect.\n");
					break;
				default:
					ast_log(LOG_ERROR, "cel_mysql: Unknown connection error: (%d) %s\n", mysql_errno(&mysql), mysql_error(&mysql));
			}
			retries--;
			if (retries) {
				goto db_reconnect;
			} else {
				ast_log(LOG_ERROR, "cel_mysql: Retried to connect fives times, giving up.\n");
			}
		}

	}

	if (connected) {
		struct columns *cur;
		struct ast_str *sql = ast_str_create(maxsize), *sql2 = ast_str_create(maxsize2);
		char buf[257], escapebuf[513];
		const char *value;
		int first = 1;

		if (!sql || !sql2) {
			if (sql) {
				ast_free(sql);
			}
			if (sql2) {
				ast_free(sql2);
			}
			return;
		}

		ast_str_set(&sql, 0, "INSERT INTO %s (", table);
		ast_str_set(&sql2, 0, " VALUES (");

#define SEP (first ? "" : ",")

		AST_RWLIST_RDLOCK(&mysql_columns);
		AST_RWLIST_TRAVERSE(&mysql_columns, cur, list) {
			LENGTHEN_BUF1(strlen(cur->name) + 2);
			ast_str_append(&sql, 0, "%s%s", first ? "" : ",", cur->name);

			if (strcmp(cur->name, "eventtime") == 0) {
				if (strncmp(cur->type, "int", 3) == 0) {
					LENGTHEN_BUF2(13);
					ast_str_append(&sql2, 0, "%s%ld", SEP, (long) record.event_time.tv_sec);
				} else if (strncmp(cur->type, "float", 5) == 0) {
					LENGTHEN_BUF2(31);
					ast_str_append(&sql2, 0, "%s%f",
						SEP,
						(double) record.event_time.tv_sec +
						(double) record.event_time.tv_usec / 1000000.0);
				} else {
					/* char, hopefully */
					LENGTHEN_BUF2(31);
					ast_localtime(&record.event_time, &tm, NULL);
					ast_strftime(buf, sizeof(buf), DATE_FORMAT, &tm);
					ast_str_append(&sql2, 0, "%s'%s'", SEP, buf);
				}
			} else if (strcmp(cur->name, "eventtype") == 0) {
				if (cur->type[0] == 'i') {
					/* Get integer, no need to escape anything */
					LENGTHEN_BUF2(5);
					ast_str_append(&sql2, 0, "%s%d", SEP, (int) record.event_type);
				} else if (strncmp(cur->type, "float", 5) == 0) {
					LENGTHEN_BUF2(31);
					ast_str_append(&sql2, 0, "%s%f", SEP, (double) record.event_type);
				} else {
					/* Char field, probably */
					LENGTHEN_BUF2(strlen(record.event_name) + 1);
					ast_str_append(&sql2, 0, "%s'%s'", SEP, record.event_name);
				}
			} else if (strcmp(cur->name, "amaflags") == 0) {
				if (strncmp(cur->type, "int", 3) == 0) {
					/* Integer, no need to escape anything */
					LENGTHEN_BUF2(13);
					ast_str_append(&sql2, 0, "%s%d", SEP, record.amaflag);
				} else {
					/* Although this is a char field, there are no special characters in the values for these fields */
					LENGTHEN_BUF2(31);
					ast_str_append(&sql2, 0, "%s'%d'", SEP, record.amaflag);
				}
			} else {
				/* Arbitrary field, could be anything */
				if (strcmp(cur->name, "userdeftype") == 0) {
					value = record.user_defined_name;
				} else if (strcmp(cur->name, "cid_name") == 0) {
					value = record.caller_id_name;
				} else if (strcmp(cur->name, "cid_num") == 0) {
					value = record.caller_id_num;
				} else if (strcmp(cur->name, "cid_ani") == 0) {
					value = record.caller_id_ani;
				} else if (strcmp(cur->name, "cid_rdnis") == 0) {
					value = record.caller_id_rdnis;
				} else if (strcmp(cur->name, "cid_dnid") == 0) {
					value = record.caller_id_dnid;
				} else if (strcmp(cur->name, "exten") == 0) {
					value = record.extension;
				} else if (strcmp(cur->name, "context") == 0) {
					value = record.context;
				} else if (strcmp(cur->name, "channame") == 0) {
					value = record.channel_name;
				} else if (strcmp(cur->name, "appname") == 0) {
					value = record.application_name;
				} else if (strcmp(cur->name, "appdata") == 0) {
					value = record.application_data;
				} else if (strcmp(cur->name, "accountcode") == 0) {
					value = record.account_code;
				} else if (strcmp(cur->name, "peeraccount") == 0) {
					value = record.peer_account;
				} else if (strcmp(cur->name, "uniqueid") == 0) {
					value = record.unique_id;
				} else if (strcmp(cur->name, "linkedid") == 0) {
					value = record.linked_id;
				} else if (strcmp(cur->name, "userfield") == 0) {
					value = record.user_field;
				} else if (strcmp(cur->name, "peer") == 0) {
					value = record.peer;
				} else {
					value = NULL;
				}

				if (value == NULL) {
					ast_str_append(&sql2, 0, "%sDEFAULT", SEP);
				} else if (strncmp(cur->type, "int", 3) == 0) {
					long long whatever;
					if (value && sscanf(value, "%30lld", &whatever) == 1) {
						LENGTHEN_BUF2(26);
						ast_str_append(&sql2, 0, "%s%lld", SEP, whatever);
					} else {
						LENGTHEN_BUF2(2);
						ast_str_append(&sql2, 0, "%s0", SEP);
					}
				} else if (strncmp(cur->type, "float", 5) == 0) {
					long double whatever;
					if (value && sscanf(value, "%30Lf", &whatever) == 1) {
						LENGTHEN_BUF2(51);
						ast_str_append(&sql2, 0, "%s%30Lf", SEP, whatever);
					} else {
						LENGTHEN_BUF2(2);
						ast_str_append(&sql2, 0, "%s0", SEP);
					}
					/* XXX Might want to handle dates, times, and other misc fields here XXX */
				} else {
					if (value) {
						mysql_escape_string(escapebuf, value, strlen(value));
					} else {
						escapebuf[0] = '\0';
					}
					LENGTHEN_BUF2(strlen(escapebuf) + 3);
					ast_str_append(&sql2, 0, "%s'%s'", SEP, escapebuf);
				}
			}
			first = 0;
		}
		AST_RWLIST_UNLOCK(&mysql_columns);
		LENGTHEN_BUF1(ast_str_strlen(sql2) + 2);
		ast_str_append(&sql, 0, ")%s)", ast_str_buffer(sql2));

		ast_debug(1, "Inserting a CEL record.\n");
	
		if (option_debug)
			ast_log(LOG_DEBUG, "cel_mysql: SQL command as follows: %s\n",ast_str_buffer(sql));

		if (dbcharset) {
			snprintf(sqldesc, sizeof(sqldesc), "SET NAMES '%s'", dbcharset);
			mysql_real_query(&mysql, sqldesc, strlen(sqldesc));
			ast_debug(1, "SQL command as follows: %s\n", sqldesc);
		}
		if (mysql_real_query(&mysql, ast_str_buffer(sql), strlen(ast_str_buffer(sql)))) {
			ast_log(LOG_ERROR, "cel_mysql: Failed to insert into database: (%d) %s", mysql_errno(&mysql), mysql_error(&mysql));
			mysql_close(&mysql);
			connected = 0;
			ast_free(sql);
			ast_free(sql2);
		} 
		
	}		
	ast_mutex_unlock(&mysql_lock);
}

static int my_unload_module(void)
{
	struct columns *current;
	ast_cli_unregister_multiple(cel_mysql_status_cli, sizeof(cel_mysql_status_cli) / sizeof(struct ast_cli_entry));

	ast_cel_backend_unregister(MYSQL_BACKEND_NAME);
	if (connected) {
		mysql_close(&mysql);
		connected = 0;
	}
	if (myhostname) {
		ast_free(myhostname);
	}
	if (mydbname) {
		ast_free(mydbname);
	}
	if (mydbuser) {
		ast_free(mydbuser);
	}
	if (mypassword) {
		ast_free(mypassword);
	}
	if (mydbport) {
		mydbport = 0;
	}
	if (table) {
		ast_free(table);
	}
	if (mydbsock) {
		ast_free(mydbsock);
	}

	AST_RWLIST_WRLOCK(&mysql_columns);
	while ((current = AST_RWLIST_REMOVE_HEAD(&mysql_columns, list))) {
		ast_free(current);
	}
	AST_RWLIST_UNLOCK(&mysql_columns);
	return 0;
}

static int unload_module(void)
{
	return my_unload_module();
}

static int process_my_load_module(struct ast_config *cfg)
{
	struct ast_variable *var;
	const char *tmp;

        #if MYSQL_VERSION_ID >= 80000
                _Bool my_bool_true = 1;
	#elif MYSQL_VERSION_ID >= 50013
		my_bool my_bool_true = 1;
	#endif
	
	struct columns *cur;
	
	timeout = 0;
	if (!(var = ast_variable_browse(cfg, "global"))) {
		ast_log(LOG_WARNING,"CEL mysql config file missing global section.\n");
		return AST_MODULE_LOAD_DECLINE;
	}
	if (!(tmp = ast_variable_retrieve(cfg,"global","hostname"))) {
		ast_log(LOG_WARNING,"Mysql server hostname not specified.  Assuming unix socket connection\n");
		tmp = "";	/* connect via UNIX-socket by default */
	}
	if (myhostname)
		ast_free(myhostname);
	if (!(myhostname = ast_strdup(tmp))) {
		ast_log(LOG_WARNING,"Mysql Ran out of memory copying host info\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	if (!(tmp = ast_variable_retrieve(cfg, "global", "dbname"))) {
		ast_log(LOG_WARNING,"Mysql database not specified.  Assuming asterisk\n");
		tmp = "asteriskceldb";
	}
	if (mydbname)
		ast_free(mydbname);
	if (!(mydbname = ast_strdup(tmp))) {
		ast_log(LOG_WARNING,"Mysql Ran out of memory copying dbname info\n");
		return AST_MODULE_LOAD_DECLINE;
	}
	
	
	if (!(tmp = ast_variable_retrieve(cfg, "global", "charset"))) {
		ast_log(LOG_WARNING,"Mysql charset not defined. \n");
		
	} else {
		if (dbcharset)
			ast_free(dbcharset);
		if (!(dbcharset = ast_strdup(tmp))) {
			ast_log(LOG_WARNING,"Mysql Ran out of memory copying dbcharset\n");
			return AST_MODULE_LOAD_DECLINE;
		}
	}
	
	if (!(tmp = ast_variable_retrieve(cfg, "global", "user"))) {
		ast_log(LOG_WARNING,"Mysql database user not specified.  Assuming asterisk\n");
		tmp = "asterisk";
	}
	if (mydbuser)
		ast_free(mydbuser);
	if (!(mydbuser = ast_strdup(tmp))) {
		ast_log(LOG_WARNING,"Mysql Ran out of memory copying user info\n");
		return AST_MODULE_LOAD_DECLINE;
	}
	if (!(tmp = ast_variable_retrieve(cfg, "global", "password"))) {
		ast_log(LOG_WARNING, "Mysql database password not specified.  Assuming blank\n");
		tmp = "";
	}
	if (mypassword)
		ast_free(mypassword);
	if (!(mypassword = ast_strdup(tmp))) {
		ast_log(LOG_WARNING,"Mysql Ran out of memory copying password info\n");
		return AST_MODULE_LOAD_DECLINE;
	}
	
	tmp = ast_variable_retrieve(cfg, "global", "port");
	if (tmp) {
		if (sscanf(tmp, "%d", &mydbport) < 1) {
			ast_log(LOG_WARNING, "Invalid MySQL port number.  Using default\n");
			mydbport = 0;
		}
	}

	if (!(tmp = ast_variable_retrieve(cfg, "global", "table"))) {
		ast_log(LOG_WARNING,"CEL table not specified.  Assuming cel\n");
		tmp = "cel";
	}
	if (table)
		ast_free(table);
	if (!(table = ast_strdup(tmp))) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (!(tmp = ast_variable_retrieve(cfg, "global", "sock"))) {
		ast_log(LOG_WARNING, "Mysql database sock not specified.  Assuming null\n");
		tmp = "";
	}

	if (mydbsock)
		ast_free(mydbsock);
	if (!(mydbsock = ast_strdup(tmp))) {
		ast_log(LOG_WARNING,"Mysql Ran out of memory copying sock info\n");
		return AST_MODULE_LOAD_DECLINE;
	}
	if (!strcmp(mydbsock, ""))
		mydbsock = NULL;

	if (option_debug) {
		if (ast_strlen_zero(myhostname)) {
			ast_debug(3, "cel_mysql: using default unix socket\n");
		} else {
			ast_debug(3, "cel_mysql: got hostname of %s\n", myhostname);
		}
		ast_debug(3, "cel_mysql: got port of %d\n", mydbport);
		ast_debug(3, "cel_mysql: got user of %s\n", mydbuser);
		ast_debug(3, "cel_mysql: got dbname of %s\n", mydbname);
		ast_debug(3, "cel_mysql: got password of %s\n", mypassword);
		ast_debug(3, "cel_mysql: got sql table name of %s\n", table);
		if (mydbsock)
			ast_log(LOG_DEBUG, "cel_mysql: got sock file of %s\n", mydbsock);

	}

	mysql_init(&mysql);
	
	if(dbcharset){
		snprintf(sqldesc, sizeof(sqldesc), "SET NAMES %s", dbcharset);
		mysql_options(&mysql, MYSQL_INIT_COMMAND, sqldesc);
		mysql_options(&mysql, MYSQL_SET_CHARSET_NAME, dbcharset);
        }
	
	if (timeout && mysql_options(&mysql, MYSQL_OPT_CONNECT_TIMEOUT, (char *)&timeout)!=0) {
		ast_log(LOG_ERROR, "cel_mysql: mysql_options returned (%d) %s\n", mysql_errno(&mysql), mysql_error(&mysql));
	}

#if MYSQL_VERSION_ID >= 50013
	/* Add option for automatic reconnection */
	if (mysql_options(&mysql, MYSQL_OPT_RECONNECT, &my_bool_true) != 0) {
		ast_log(LOG_ERROR, "cel_mysql: mysql_options returned (%d) %s\n", mysql_errno(&mysql), mysql_error(&mysql));
	}
#endif

	ast_log(LOG_DEBUG, "try to login db=%s with host=%s:user=%s:password=%s:port=%d:sock=%s\n",
						mydbname, myhostname, mydbuser, mypassword, mydbport, mydbsock);

	if (!mysql_real_connect(&mysql, myhostname, mydbuser, mypassword, mydbname, mydbport, mydbsock, 0)) {
		ast_log(LOG_ERROR, "Failed to connect to mysql database %s on %s: CALLS WILL NOT BE LOGGED (%s)!!\n\n",
							mydbname, myhostname, mysql_error(&mysql));
		connected = 0;
	} else {
		connected = 1;
		connect_time = time(NULL);
		if (dbcharset) {
			snprintf(sqldesc, sizeof(sqldesc), "SET NAMES '%s'", dbcharset);
			mysql_real_query(&mysql, sqldesc, strlen(sqldesc));
			ast_debug(1, "SQL command as follows: %s\n", sqldesc);
		}
		char sqlcmd[512];
		char *fname, *ftype, *fnotnull, *fdef;
		char *tableptr;
		MYSQL_RES *res;
		
		if (option_debug)
			ast_log(LOG_DEBUG, "Successfully connected to MySQL database.\n");
		
		/* Remove any schema name from the table */
		if ((tableptr = strrchr(table, '.'))) {
			tableptr++;
		} else {
			tableptr = table;
		}

		/* Query the columns */
		snprintf(sqlcmd, sizeof(sqlcmd), "DESC %s", tableptr);
		ast_log(LOG_DEBUG, "sqlcmd: %s\n", sqlcmd);
		//result = PQexec(conn, sqlcmd);
		if (mysql_real_query(&mysql, sqlcmd, strlen(sqlcmd))) {
			ast_log(LOG_ERROR, "cel_mysql: Failed to query database: (%d) %s\n", mysql_errno(&mysql), mysql_error(&mysql));
			mysql_close(&mysql);
			connected = 0;
			return AST_MODULE_LOAD_FAILURE;
		}
		
		if (!(res = mysql_store_result(&mysql))) {
			ast_log(LOG_ERROR, "cel_mysql: Failed to query database: (%d) %s\n", mysql_errno(&mysql), mysql_error(&mysql));
			mysql_close(&mysql);
			connected = 0;
			return AST_MODULE_LOAD_FAILURE;
		
		}
		MYSQL_ROW row;
		while ((row = mysql_fetch_row(res)) != NULL){
			fname = row[0];
			ftype = row[1];
			fnotnull = row[2];
			fdef	 = row[4];
			ast_verb(4, "Found column '%s' of type '%s'\n", fname, ftype);
			cur = ast_calloc(1, sizeof(*cur) + strlen(fname) + strlen(ftype) + 2);
			if (cur) {
				//sscanf(flen, "%30d", &cur->len);
				cur->name = (char *)cur + sizeof(*cur);
				cur->type = (char *)cur + sizeof(*cur) + strlen(fname) + 1;
				strcpy(cur->name, fname);
				if (!strncmp(ftype, "int", 3)) {
					strcpy(cur->type, "int");
					cur->len = 4;
				} else if (!strncmp(ftype, "float", 5)) {
					strcpy(cur->type, "float");
					cur->len = 4;
				} else {
					strcpy(cur->type, "char");
					cur->len = -1;
				}

				if (*fnotnull == 'N') {
					cur->notnull = 1;
				} else {
					cur->notnull = 0;
				}
				if (!ast_strlen_zero(fdef)) {
					cur->hasdefault = 1;
				} else {
					cur->hasdefault = 0;
				}

				ast_verb(4, "Found column '%s' of type '%s' len=%d notnul=%d hasdefault=%d\n",
							cur->name, cur->type, cur->len, cur->notnull, cur->hasdefault);

				AST_RWLIST_INSERT_TAIL(&mysql_columns, cur, list);
			}
		}

		mysql_free_result(res);
		
	}
	return AST_MODULE_LOAD_SUCCESS;
}

static int my_load_module(int reload)
{
	struct ast_config *cfg;
	int res;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	

	if ((cfg = ast_config_load(config, config_flags)) == NULL) {
		ast_log(LOG_WARNING, "Unable to load config for Mysql CEL's: %s\n", config);
		return AST_MODULE_LOAD_DECLINE;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		return AST_MODULE_LOAD_SUCCESS;
	}


	res = process_my_load_module(cfg);
	ast_config_destroy(cfg);

	if (res != AST_MODULE_LOAD_SUCCESS) {
		ast_log(LOG_WARNING, "Fail to load cel_mysql.so -- DB Connect / Query Error\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_cli_register_multiple(cel_mysql_status_cli, sizeof(cel_mysql_status_cli) / sizeof(struct ast_cli_entry));


	if (ast_cel_backend_register(MYSQL_BACKEND_NAME, mysql_log)) {
                ast_log(LOG_WARNING, "Unable to subscribe to CEL events for mysql\n");
                return AST_MODULE_LOAD_DECLINE;
        }


	return AST_MODULE_LOAD_SUCCESS;
}

static int load_module(void)
{
	return my_load_module(0);
}

static int reload(void)
{
	int res;
	ast_mutex_lock(&mysql_lock);
	res = my_load_module(1);
	ast_mutex_unlock(&mysql_lock);
	
	return res;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Mysql CEL Backend",
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
);
