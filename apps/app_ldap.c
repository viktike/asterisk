/*
 * Asterisk -- A telephony toolkit for Unix.
 *
 * LDAP Directory lookup function
 *
 * Copyright (C) 2004,2005 Sven Slezak <sunny@mezzo.net>
 * Version: 2.0rc1 (Jan31.2007)
 * http://www.mezzo.net/asterisk
 *
 */

/*** MODULEINFO
 	<depend>ldap</depend>
 	<support_level>extended</support_level>
 ***/


#define ASTMM_LIBC ASTMM_IGNORE

#include "asterisk.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include "asterisk/options.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/astdb.h"
#include "asterisk/lock.h"
#include "asterisk/config.h"
#include "asterisk/utils.h"
#include "asterisk/callerid.h"

#define LDAP_DEPRECATED 1

#include <ctype.h>
#include <errno.h>
#include <iconv.h>
#include <ldap.h>

#define LDAP_CONFIG "ldap.conf"

int ldap_lookup(const char *host, int port, int version, int timeout, const char *user, const char *pass, const char *base, const char *scope, const char *filter, const char *attribute, char *result);
int strconvert(const char *incharset, const char *outcharset,
#ifdef __Darwin__
               const char *in,
#else
               char *in,
#endif
               char *out);

static char *app = "LDAPget";
static char *synopsis = "Retrieve a value from an ldap directory";

static char *descrip =
  "  LDAPget(varname=config-file-section/key): Retrieves a value from an LDAP\n"
  "directory and stores it in the given variable. Always returns 0.  If the\n"
  "requested key is not found, jumps to priority n+1 if available.\n";

static int ldap_exec (struct ast_channel *chan, const char *data)
{
  int arglen;
  struct ast_module_user *u;
  char *argv, *varname, *config, *keys = NULL, *key = NULL, *tail = NULL;
  char result[2048];
  char *result_conv;
  struct ast_config *cfg;
  struct ast_flags config_flags = { .flags = 0 };

  int port = LDAP_PORT, version = LDAP_VERSION2, timeout = 10;
  const char *temp, *host, *user, *pass, *_base, *scope, *_filter, *attribute;
  char *convert, *convert_from = NULL, *convert_to = NULL, *filter, *base;

  u = ast_module_user_add(chan);

  arglen = strlen(data);
  argv = alloca(arglen+1);
  if(!argv) {
    ast_log(LOG_DEBUG, "Memory allocation failed\n");
    ast_module_user_remove(u);
    return -1;
  }
  memcpy (argv, data, arglen+1);

  if(strchr(argv, '=')) {
    varname = strsep (&argv, "=");
    if(strchr(argv, '/')) {
      config = strsep(&argv, "/");
      keys = strsep(&argv, "\0");
      if(option_verbose > 2)
	ast_verbose(VERBOSE_PREFIX_3 "LDAPget: varname=%s, config-section=%s, keys=%s\n", varname, config, keys);
    } else {
      config = strsep(&argv, "\0");
      if(option_verbose > 2)
	ast_verbose(VERBOSE_PREFIX_3 "LDAPget: varname=%s, config-section=%s\n", varname, config);
    }
    if (!varname || !config) {
      ast_log(LOG_WARNING, "Ignoring; Syntax error in argument\n");
      ast_module_user_remove(u);
      return -1;
    }
  } else {
    ast_log(LOG_WARNING, "Ignoring, no parameters\n");
    ast_module_user_remove(u);
    return -1;
  }

  cfg = ast_config_load(LDAP_CONFIG, config_flags);
  if(!cfg) {
    ast_log(LOG_WARNING, "No such configuration file %s\n", LDAP_CONFIG);
    ast_module_user_remove(u);
    return -1;
  }
  if(!(host = ast_variable_retrieve(cfg, config, "host"))) {
    host = "localhost";
  }
  if((temp = ast_variable_retrieve(cfg, config, "port"))) {
    port = atoi(temp);
  }
  if((temp = ast_variable_retrieve(cfg, config, "timeout"))) {
    timeout = atoi(temp);
  }
  if((temp = ast_variable_retrieve(cfg, config, "version"))) {
    version = atoi(temp);
  }
  user = ast_variable_retrieve(cfg, config, "user");
  pass = ast_variable_retrieve(cfg, config, "pass");
  if (!(scope = ast_variable_retrieve(cfg, config, "scope"))) {
    scope = "sub";
  }
  if(!(_base = ast_variable_retrieve(cfg, config, "base"))) {
    _base = "";
  }
  if(!(_filter = ast_variable_retrieve(cfg, config, "filter"))) {
    _filter = "(&(objectClass=*)(telephoneNumber=${CALLERID(number)}))";
  }
  if(!(attribute = ast_variable_retrieve(cfg, config, "attribute"))) {
    attribute = "cn";
  }
	
  if((temp = ast_variable_retrieve(cfg, config, "convert"))) {
    convert = ast_strdupa(temp);
    if(strchr(convert, ',')) {
      convert_from = ast_strip(strsep(&convert, ","));
      convert_to = ast_strip(strsep(&convert, "\0"));
    } else {
      ast_log(LOG_WARNING, "syntax error: convert = <source-charset>,<destination charset>\n");
    }
  }
	
  if(option_verbose > 3)
    ast_verbose (VERBOSE_PREFIX_4 "LDAPget: ldap://%s/%s?%s?%s?%s\n", host, _base, attribute, scope, _filter);

  int slen = strlen(_filter)*3+200;
  filter = alloca(slen);
  memset(filter, 0, slen);
  pbx_substitute_variables_helper(chan, _filter, filter, slen);
  
  slen = strlen(_base)*3+200;
  base = alloca(slen);
  memset(base, 0, slen);
  pbx_substitute_variables_helper(chan, _base, base, slen);

  if(keys && strstr(filter, "%s") != NULL) {
    filter = (char *)ast_realloc(filter, (strlen(filter)+strlen(keys)+1)*sizeof(char));
    while((key = strsep(&keys, "|")) != NULL) {
      if((tail = strstr(filter, "%s")) != NULL) {
				memmove(tail+strlen(key), tail+2, strlen(tail+2)+1);
				memcpy(tail, key, strlen(key));
      }
    }
  }

  if(option_verbose > 2)
    ast_verbose (VERBOSE_PREFIX_3 "LDAPget: ldap://%s/%s?%s?%s?%s\n", host, base, attribute, scope, filter);

  if(ldap_lookup(host, port, version, timeout, user, pass, base, scope, filter, attribute, result)) {

    if(convert_from) {
      if(option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "LDAPget: convert: %s -> %s\n", convert_from, convert_to);
      result_conv = alloca(strlen(result) * 2);
      strconvert(convert_from, convert_to, result, result_conv);
      strcpy(result, result_conv);
    }


    if(strcmp("CALLERIDNAME", varname)==0 || strcmp("CALLERID(name)", varname)==0) {
      ast_set_callerid(chan, NULL, result, NULL);
    } else {
      pbx_builtin_setvar_helper(chan, varname, result);
    }
		if(option_verbose > 2)
			ast_verbose (VERBOSE_PREFIX_3 "LDAPget: set %s='%s'\n", varname, result);
  }
	
  ast_config_destroy(cfg);
	
  ast_module_user_remove(u);
  return 0;
}


int ldap_lookup(const char *host, int port, int version, int timeout, const char *user, const char *pass, 
		const char *base, const char *scope, const char *filter, const char *attribute, char *result) {
  char *attrs[] = { NULL };
  char **values;
  LDAP *ld;
  LDAPMessage *res, *entry;
  int ret, ldap_scope = LDAP_SCOPE_SUBTREE;

  ld = ldap_init(host, port);
  if(!ld) {
    ast_log(LOG_WARNING, "LDAPget: unable to initialize ldap connection to %s:%d\n", host, port);
    return 0;
  }

  ldap_set_option(ld, LDAP_OPT_TIMELIMIT, &timeout);
  ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &version);

  if(user) {
    if (option_verbose > 2)
      ast_verbose(VERBOSE_PREFIX_3 "LDAPget: bind to %s as %s\n", host, user);
    ret = ldap_simple_bind_s(ld, user, pass);
  } else {
    if (option_verbose > 2)
      ast_verbose(VERBOSE_PREFIX_3 "LDAPget: bind to %s anonymously\n", host);
    ret = ldap_simple_bind_s(ld, NULL, NULL);
  }
  if(ret) {
    ast_log(LOG_WARNING, "LDAPget: bind failed: %s\n", ldap_err2string(ret));
    ldap_unbind(ld);
    return 0;
  }

  if(strncmp(scope,"sub",3)==0) {
    ldap_scope = LDAP_SCOPE_SUBTREE;
  } else if(strncmp(scope,"base",4)==0) {
    ldap_scope = LDAP_SCOPE_BASE;
  } else if(strncmp(scope,"one",3)==0) {
    ldap_scope = LDAP_SCOPE_ONELEVEL;
  }

  ret = ldap_search_s(ld, base, ldap_scope, filter, attrs, 0, &res);
  if(ret) {
    ast_log(LOG_WARNING, "LDAPget: search failed: %s\n", ldap_err2string(ret));
    if(version==2)
      ast_log(LOG_WARNING, "LDAPget: you may try version=3 in your asterisk ldap.conf file.\n");
    ldap_msgfree(res);
    ldap_unbind(ld);
    return 0;
  }

  entry = ldap_first_entry(ld, res);
  if(!entry) {
    if(option_verbose > 2)
      ast_verbose (VERBOSE_PREFIX_3 "LDAPget: Value not found in directory.\n");
    ldap_msgfree(res);
    ldap_unbind(ld);
    return 0;
  }

  values = ldap_get_values(ld, entry, attribute);
  if(values && values[0]) {
    memset(result, 0, strlen(values[0]));
    // strncpy(result, values[0], strlen(values[0]));
    strcpy(result, values[0]);
    result[strlen(values[0])] = '\0';
    if (option_verbose > 2)
      ast_verbose(VERBOSE_PREFIX_3 "LDAPget: %s=%s\n", attribute, result);
  } else {
    if (option_verbose > 2)
      ast_verbose (VERBOSE_PREFIX_3 "LDAPget: %s not found.\n", attribute);
    ldap_msgfree(res);
    ldap_unbind(ld);
    return 0;
  }
  ldap_value_free(values);

  ldap_msgfree(res);
  ldap_unbind_s(ld);
  
  return 1;
}

int strconvert(const char *incharset, const char *outcharset,
#ifdef __Darwin__
               const char *in,
#else
               char *in,
#endif
               char *out) {
  iconv_t cd;
  size_t incount = strlen(in), outcount = strlen(in)*2, result;
  cd = iconv_open(outcharset, incharset);
  if(cd == (iconv_t) -1) {
    ast_log(LOG_ERROR, "conversion from '%s' to '%s' not available. type 'iconv -l' in a shell to list the supported charsets.\n", incharset, outcharset);
    memcpy(out, in, strlen(in)+1);
    return -1;
  }
  result = iconv(cd, &in, &incount, &out, &outcount);
  if(result == (size_t)-1) {
    if(errno == E2BIG) {
      ast_log(LOG_WARNING, "Iconv: output buffer too small.\n");
    } else if(errno == EILSEQ) {
      ast_log(LOG_WARNING,  "Iconv: illegal character.\n");
    } else if(errno == EINVAL) {
      ast_log(LOG_WARNING,  "Iconv: incomplete character sequence.\n");
    } else {
      ast_log(LOG_WARNING,  "Iconv: error.\n");
    }
  }
  iconv_close(cd);
  *out = '\0';
  return 1;
}

static int load_module(void)
{
  return ast_register_application(app, ldap_exec, synopsis, descrip);
}

static int unload_module(void)
{
  ast_module_user_hangup_all(); 
  return ast_unregister_application(app);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "LDAP directory lookup function for Asterisk extension logic.");
