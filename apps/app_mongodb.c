 /*
 * app_mongodb.c
 *
 * Author: Sperl Viktor <viktike32@gmail.com>
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*! \file
 *
 * \brief Mongodb Publisher Application
 *
 * \author Sperl Viktor <viktike32@gmail.com>
 */

/*** MODULEINFO
 	<depend>res_mongodb</depend>
    <depend>mongoc</depend>
    <depend>bson</depend>
    <support_level>extended</support_level>
 ***/


#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/res_mongodb.h"
#include "asterisk/config.h"
#include "asterisk/app.h"
#include "asterisk/pbx.h"

/*** DOCUMENTATION
	<application name="MongoPush" language="en_US">
		<synopsis>
			Push a BSON document to a collection in a MongoDB database
		</synopsis>
		<syntax>
			<parameter name="connection" required="true"/>
			<parameter name="dbname" required="false"/>
			<parameter name="collection" required="false"/>
			<parameter name="document" required="true"/>
         <parameter name="options" required="false">
				<optionlist>
					<option name="s">
						<argument name="ServerID" required="true" />
						<para>MongoDB ServerID</para>
					</option>
					<option name="a">
						<argument name="APM" required="true" />
						<para>If MongoDB APM should be started [0|1]</para>
					</option>
				</optionlist>
         </parameter>
		</syntax>
		<description>
			<para>Pushes a JSON document to a collection in a Mongo database coverted to BSON (binary JSON)</para>
		</description>
	</application>
 ***/

static char *app = "MongoPush";
#define CONFIG_FILE "ast_mongo.conf"
#define URI "uri"
#define DATABSE "database"
#define COLLECTION "collection"
#define SERVERID "serverid"

enum option_flags {
	OPTION_SERVER_ID  = (1 << 0),
	OPTION_APM        = (1 << 1),
};

enum option_args {
	OPTION_ARG_SERVER_ID,
	OPTION_ARG_APM,
	/* This *must* be the last value in this enum! */
	OPTION_ARG_ARRAY_SIZE,
};

AST_APP_OPTIONS(app_opts, {
	AST_APP_OPTION_ARG('s', OPTION_SERVER_ID, OPTION_ARG_SERVER_ID),
	AST_APP_OPTION_ARG('a', OPTION_APM, OPTION_ARG_APM),
});

static struct ast_config * load_config_file(const char * config_file)
{
   struct ast_config *cfg;
   struct ast_flags config_flags = { .flags = 0 };

	cfg = ast_config_load(config_file, config_flags);

	if (!cfg) {
        ast_log(LOG_ERROR, "Error reading config file: %s\n", config_file);
        return NULL;
    } else if (cfg == CONFIG_STATUS_FILEMISSING) {
		ast_log(LOG_WARNING, "Missing configuration file %s\n", config_file);
		return NULL;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Unable to load configuration file %s\n", config_file);
		return NULL;
	} else {
		return cfg;
	}
}

static int push_exec(struct ast_channel *chan, const char *data)
{
   static void* apm_context = NULL;
   static int apm_enabled;
   int res = 0;

   bson_oid_t *mongo_server_id = NULL;
   bson_t *doc = NULL;
   bson_error_t parse_error, insert_error;
   mongoc_collection_t *mongo_collection = NULL;
   mongoc_uri_t *mongo_uri = NULL;
   mongoc_client_t *mongo_client = NULL;
   mongoc_client_pool_t *mongo_connection = NULL;

   const char *uri, *database = NULL, *collection = NULL, *serverid = NULL, *apm = NULL;

   char *parse, *opts[OPTION_ARG_ARRAY_SIZE];
	struct ast_flags flags;
   struct ast_config *cfg;
   struct ast_variable *var; 

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(connection);
		AST_APP_ARG(database);
		AST_APP_ARG(collection);
		AST_APP_ARG(document);
      AST_APP_ARG(options);
	);

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.connection)) {
		ast_log(LOG_ERROR, "%s requires an MongoDB connection from res_mongodb or an URI\n", app);
		return -1;
	} else {
      cfg = load_config_file(CONFIG_FILE);
      if(cfg){
         var = ast_variable_browse(cfg, args.connection);
      }
      if(cfg && var){
         /* Mongo config from file */
         if((uri = ast_variable_retrieve(cfg, args.connection, URI)) == NULL){
            ast_log(LOG_ERROR, "no uri specified in category %s of config file %s\n", args.connection, CONFIG_FILE);
            return -1;
         }
         if((database = ast_variable_retrieve(cfg, args.connection, DATABSE)) == NULL){
            ast_log(LOG_WARNING, "no database specified in category %s of config file %s\n", args.connection, CONFIG_FILE);
         }
         if((collection = ast_variable_retrieve(cfg, args.connection, COLLECTION)) == NULL) {
            ast_log(LOG_WARNING, "no collection specified in category %s of config file %s\n", args.connection, CONFIG_FILE);
         }
         if((serverid = ast_variable_retrieve(cfg, args.connection, SERVERID)) != NULL){
            if(!bson_oid_is_valid(serverid, strlen(serverid))){
               ast_log(LOG_ERROR, "invalid server id specified in category %s of config file %s\n", args.connection, CONFIG_FILE);
               return -1;
            }
         }
         if((apm = ast_variable_retrieve(cfg, args.connection, "apm")) && (sscanf(apm, "%u", &apm_enabled) != 1)){
            ast_log(LOG_WARNING, "apm must be a 0|1, not '%s' in category %s of config file %s\n", apm, args.connection, CONFIG_FILE);
            apm_enabled = 0;
         }

         ast_config_destroy(cfg);
      } else {
         /* Mongo config from params */
         ast_log(LOG_NOTICE, "Unable to find category %s in configuration file %s, assuming it's an URI\n", args.connection, CONFIG_FILE);
         uri = ast_strdupa(args.connection);

         if(ast_strlen_zero(args.database)){
            ast_log(LOG_NOTICE, "no database (2nd parameter) specified for %s.\n", app);
         } else {
            database = ast_strdupa(args.database);
         }

         if(ast_strlen_zero(args.collection)){
            ast_log(LOG_NOTICE, "no collection (3rd parameter) specified for %s.\n", app);
         } else {
            collection = ast_strdupa(args.collection);
         }

         apm_enabled = 0;
      }

      /* Mongo commong config params */
      if(ast_strlen_zero(database) && !ast_strlen_zero(args.database)){
         database = ast_strdupa(args.database);
      }
      if(ast_strlen_zero(database)){
         ast_log(LOG_ERROR, "still no database selected for %s.\n", app);
         return -1;
      }

      if(ast_strlen_zero(collection) && !ast_strlen_zero(args.collection)){
         collection = ast_strdupa(args.collection);
      }
      if(ast_strlen_zero(collection)){
         ast_log(LOG_ERROR, "no database selected for %s.\n", app);
         return -1;
      }

      // Options field parsing
      if (args.argc == 5) {
         ast_app_parse_options(app_opts, &flags, opts, args.options);

         if (ast_test_flag(&flags, OPTION_SERVER_ID) && !ast_strlen_zero(opts[OPTION_SERVER_ID])) {
            serverid = ast_strdupa(opts[OPTION_ARG_SERVER_ID]);
            if(!bson_oid_is_valid(serverid, strlen(serverid))){
               ast_log(LOG_ERROR, "invalid server id specified in s(%s) option (5th parameter of %s)", serverid, app);
               return -1;
            }
         }

         if (ast_test_flag(&flags, OPTION_APM) && !ast_strlen_zero(opts[OPTION_ARG_APM])) {
            apm_enabled = atoi(opts[OPTION_ARG_APM]);
         }
      }

      /* Push document if provided */
      if (ast_strlen_zero(args.document)) {
         ast_log(LOG_ERROR, "%s requires a JSON document to push\n", app);
         return -1;
      } else {

         /* Mongo connect */
         if(serverid){
            mongo_server_id = ast_malloc(sizeof(bson_oid_t));
            if (mongo_server_id == NULL) {
                  ast_log(LOG_ERROR, "not enough memory for server_id allocation\n");
                  return -1;
            }
            bson_oid_init_from_string(mongo_server_id, serverid);
         }
         mongo_uri = mongoc_uri_new(uri);
         if (mongo_uri == NULL) {
            ast_log(LOG_ERROR, "parsing uri error: %s\n", uri);
            return -1;
         }
         mongo_connection = mongoc_client_pool_new(mongo_uri);
         if(mongo_uri){
            mongoc_uri_destroy(mongo_uri);
         }
         if(mongo_connection == NULL){
            ast_log(LOG_ERROR, "cannot make a connection pool for MongoDB\n");
            return -1;
         }
         if(apm_enabled){
            apm_context = ast_mongo_apm_start(mongo_connection);
         }

         mongo_client = mongoc_client_pool_pop(mongo_connection);
         if(mongo_client == NULL) {
            ast_log(LOG_ERROR, "unexpected error, no connection pool\n");
            if(apm_context){
               ast_mongo_apm_stop(apm_context);
            }
            if(mongo_connection){
               mongoc_client_pool_destroy(mongo_connection);
            }
            return -1;
         }
         mongo_collection = mongoc_client_get_collection(mongo_client, database, collection);
         if(mongo_collection == NULL) {
            ast_log(LOG_ERROR, "cannot get such a collection, %s, %s\n", database, collection);
            mongoc_client_pool_push(mongo_connection, mongo_client);
            if(apm_context){
               ast_mongo_apm_stop(apm_context);
            }
            if(mongo_connection){
               mongoc_client_pool_destroy(mongo_connection);
            }
            return -1;
         }

         /* Document */
         doc = bson_new_from_json((const uint8_t *)args.document, -1, &parse_error);
         if(!doc){
            ast_log(LOG_ERROR, "JSON to BSON conversion error: %s\n", parse_error.message);
            res = -1;
         } else {
            if(serverid){
               BSON_APPEND_OID(doc, SERVERID, mongo_server_id);
            }
            if(!mongoc_collection_insert(mongo_collection, MONGOC_INSERT_NONE, doc, NULL, &insert_error)){
               ast_log(LOG_ERROR, "insertion failed: %s\n", insert_error.message);
               res = -1;
            }
            bson_destroy(doc);
         }
      }

      /* Mongo disconnect */
      if(mongo_collection){
         mongoc_collection_destroy(mongo_collection);
      }
      mongoc_client_pool_push(mongo_connection, mongo_client);
      if(apm_context){
         ast_mongo_apm_stop(apm_context);
      }
      if(mongo_connection){
         mongoc_client_pool_destroy(mongo_connection);
      }

      return res;
	}
}

static int load_module(void)
{
	int res;
	
	res = ast_register_application_xml(app, push_exec);

	return res ? AST_MODULE_LOAD_DECLINE : AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int reload_module(void)
{
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "MongoDB Push Dialplan Application",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.requires = "res_mongodb",
);

