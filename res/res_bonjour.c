/*
 *
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Bonjour Service Module
 * 
 * Sven Slezak <sunny@mezzo.net>
 * version 0.9 (Aug 2. 2005)
 *
 * Copyright (c)2004 Sunrise Telephone Systems Ltd.
 *           (c)2005 Astmasters.net Community Project
 *           (c)2005 mezzoConsult C.B.
 *
 * Licensed under the GNU Lesser General Public License (LGPL) version 2.
 *
 */

/*** MODULEINFO
	<depend>bonjour</depend>
	<support_level>extended</support_level>
 ***/

#define ASTMM_LIBC ASTMM_IGNORE

#include "asterisk.h"

#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <dns_sd.h>

#include "asterisk/options.h"
#include "asterisk/module.h"
#include "asterisk/cli.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/lock.h"
#include "asterisk/logger.h"
#include "asterisk/config.h"
#include "asterisk/utils.h"

#define BONJOUR_CONFIG "bonjour.conf"

enum { SUCCESS=0, FAILURE };

struct bonjour_service {
  struct bonjour_service* next;
  DNSServiceRef sdRef;
  int sdFD;
  char *handle;
  char *bindaddr;
  int interfaceIndex;
  char *name;
  char *regtype;
  char *domain;
  char *host;
  int port;
  char *txtRecord;
};

static struct bonjour_service* bonjour_services = NULL;
static struct bonjour_service* general_bonjour_services = NULL;

/*
 * forward declarations
 */

static struct bonjour_service* BuildServiceList(struct bonjour_service** globals);
static void DestroyServiceList(struct bonjour_service* services);

static int ServiceRegister(struct bonjour_service* const service);
static void ServiceUnregister(struct bonjour_service* const service);

static int ServicesRegister(void);
static void ServicesUnregister(void);

static void logServiceError(int errorCode);

/*
 * mDNS_thread
 */

AST_MUTEX_DEFINE_STATIC(services_lock);
static pthread_t mDNS_thread_id = 0; 
static int       mDNS_thread_continue = 1; 

static void* mDNS_thread(void* param)
{
  while(mDNS_thread_continue) {
    int result;
    int maxfd = -1;
    ast_fdset fdset;
    struct timeval timeout;
    struct bonjour_service* service;
    
    if (ast_mutex_lock(&services_lock)) {
      ast_log(LOG_WARNING, "Unable to get lock on service list.\n");
      continue;
    }
    FD_ZERO(&fdset);
    for(service = bonjour_services; service != NULL; service = service->next) {
      /* Only monitor valid sockets which have an active DNSServiceRef */
      if (service->sdFD >= 0 && service->sdRef) {
        FD_SET(service->sdFD, &fdset);
        if (service->sdFD > maxfd){
          maxfd = service->sdFD;
        }
      }
    }
    ast_mutex_unlock(&services_lock);

    timeout.tv_sec = 1; timeout.tv_usec = 0;
    result = ast_select(maxfd+1, &fdset, NULL, NULL, &timeout);
    if(result > 0) {
      if(ast_mutex_lock(&services_lock)) {
        ast_log(LOG_WARNING, "Unable to get lock on service list.\n");
        continue;
      }
      for(service = bonjour_services; service != NULL; service = service->next) {
        /* Ensure both fd and ref are valid before processing */
        if(service->sdFD >= 0 && service->sdRef && FD_ISSET(service->sdFD, &fdset)) {
          DNSServiceErrorType errorCode = DNSServiceProcessResult(service->sdRef);
          if(errorCode != kDNSServiceErr_NoError) {
    	    logServiceError(errorCode);
          }
        }
      }
      ast_mutex_unlock(&services_lock);
    }
  }
  return NULL;
}

static void mDNS_callback(DNSServiceRef sdRef,
			  DNSServiceFlags flags,
			  DNSServiceErrorType errorCode,
			  const char *name,
			  const char *regtype,
			  const char *domain,
			  void *context)
{
  struct bonjour_service* service = (struct bonjour_service*)context;
  if(errorCode == kDNSServiceErr_NoError) {
    char indexName[IF_NAMESIZE] = "";
    if_indextoname(service->interfaceIndex,indexName);
    ast_log(LOG_NOTICE, "Registered [%s]\t%s.%s port %d on %s\n",
	    service->handle,
	    service->regtype,
	    service->domain ? service->domain : general_bonjour_services->domain,
	    service->port,
	    indexName[0] ? indexName : "all interfaces"
	    );
  } else {
    logServiceError(errorCode);
    service->sdRef = NULL; // disable this service
  }
}

static void logServiceError(int errorCode) 
{
  switch (errorCode) {
  case kDNSServiceErr_NoError:
    break;
  case kDNSServiceErr_NoSuchName:
    ast_log(LOG_WARNING, "Internal error: No such name.");
    break;
  case kDNSServiceErr_NoMemory:
    ast_log(LOG_WARNING, "Not enough memory.");
    break;
  case kDNSServiceErr_BadParam:
    ast_log(LOG_WARNING, "A parameter contains bad data.");
    break;
  case kDNSServiceErr_BadReference:
    ast_log(LOG_WARNING, "Bad reference given.");
    break;
  case kDNSServiceErr_BadState:
    ast_log(LOG_WARNING, "Internal error: Bad state.");
    break;
  case kDNSServiceErr_BadFlags:
    ast_log(LOG_WARNING, "Invalid flag given.");
    break;
  case kDNSServiceErr_Unsupported:
    ast_log(LOG_WARNING, "Internal error: Unsupported.");
    break;
  case kDNSServiceErr_NotInitialized:
    ast_log(LOG_WARNING, "Reference given is not initialized.");
    break;
  case kDNSServiceErr_AlreadyRegistered:
    ast_log(LOG_NOTICE, "Service already registered.");
    break;
  case kDNSServiceErr_NameConflict:
    ast_log(LOG_WARNING, "Name already in use. Please choose a different one.");
    break;
  case kDNSServiceErr_Invalid:
    ast_log(LOG_WARNING, "Internal error: Invalid.");
    break;
  case kDNSServiceErr_Firewall:
    ast_log(LOG_WARNING, "A firewall prevents mDNSResponder from successful completing.");
    break;
  case kDNSServiceErr_Incompatible:
    ast_log(LOG_WARNING, "Client library incompatible. Use the free library from Apple.");
    break;
  case kDNSServiceErr_BadInterfaceIndex:
    ast_log(LOG_WARNING, "Bad interface index. Check configuration!");
    break;
  case kDNSServiceErr_Refused:
    ast_log(LOG_WARNING, "Server refused the request.");
    break;
  case kDNSServiceErr_NoSuchRecord:
    ast_log(LOG_NOTICE, "Cannot remove Service. No such Record.");
    break;
  case kDNSServiceErr_NoAuth:
    ast_log(LOG_WARNING, "Authentication required. Ask your DNS admin for a shared secret.");
    break;
  case kDNSServiceErr_NoSuchKey:
    ast_log(LOG_NOTICE, "No such key.");
    break;
  case kDNSServiceErr_NATTraversal:
    ast_log(LOG_NOTICE, "DNS Server is behind a NAT device which doen't support port mapping.");
    break;
  case kDNSServiceErr_DoubleNAT:
    ast_log(LOG_NOTICE, "DNS Server is behind more than one NAT device which is not supported. Sorry.");
    break;
  case kDNSServiceErr_BadTime:
    ast_log(LOG_NOTICE, "DNS Server time is outside the time interval specified by the dynamic update request.");
    break;
  case kDNSServiceErr_Unknown:
  default:
    ast_log(LOG_NOTICE, "Unknown error.");
    break;
  }
}

static char* appendDNStxtRecord(char *buffer, const char* parameter)
{
  int buflen = 0;
  const int length = strlen(parameter) + 2;
  
  if(!buffer) {
    buffer = malloc(length);
  } else {
    buflen = strlen(buffer);
    buffer = realloc(buffer,buflen+length);
  }
  if(!buffer) {
    ast_log(LOG_WARNING, "Out of memory.\n");
  } else {
    memcpy(buffer + buflen + 1, parameter, length - 2);
    buffer[buflen] = length - 2;
    buffer[buflen + length - 1] = '\0';
  }
  return buffer;  
}

static int ServiceRegister(struct bonjour_service* const service)
{
  if(!service) return FAILURE;
  
  typedef union { unsigned char b[2]; unsigned short NotAnInteger; } Opaque16;
  Opaque16 registerPort = { { service->port >> 8, service->port & 0xFF } };
  DNSServiceFlags flags = kDNSServiceFlagsAllowRemoteQuery;
  DNSServiceErrorType errorCode = DNSServiceRegister(&service->sdRef,
  					     flags,
						     service->interfaceIndex ? service->interfaceIndex : general_bonjour_services->interfaceIndex,
						     service->name,
						     service->regtype,
						     service->domain ? service->domain : general_bonjour_services->domain,
						     NULL, // hostname: localhost
						     registerPort.NotAnInteger,
						     service->txtRecord ? strlen(service->txtRecord) : 0,
						     service->txtRecord,
						     mDNS_callback,
						     service
						     );
  if(errorCode == kDNSServiceErr_Unsupported){
    DNSServiceFlags flags = 0;
    errorCode = DNSServiceRegister(&service->sdRef,
                 flags,
						     service->interfaceIndex ? service->interfaceIndex : general_bonjour_services->interfaceIndex,
						     service->name,
						     service->regtype,
						     service->domain ? service->domain : general_bonjour_services->domain,
						     NULL, // hostname: localhost
						     registerPort.NotAnInteger,
						     service->txtRecord ? strlen(service->txtRecord) : 0,
						     service->txtRecord,
						     mDNS_callback,
						     service
			);
  }

  if(errorCode == kDNSServiceErr_NoError){
    service->sdFD = DNSServiceRefSockFD(service->sdRef);
    if (service->sdFD < 0) {
      ast_log(LOG_WARNING, "invalid file descriptor from DNSServiceRefSockFD.\n");
      ServiceUnregister(service);
      return FAILURE;
    }
  } else {
    logServiceError(errorCode);
    service->sdRef = NULL; // disable service
    service->sdFD = -1; /* ensure we don't monitor fd 0 */
    return FAILURE;
  }
  return SUCCESS;
}

static void ServiceUnregister(struct bonjour_service* const service)
{
  if(service && service->sdRef){
    DNSServiceRefDeallocate(service->sdRef);
    service->sdRef = NULL;
    service->sdFD = -1;
    ast_log(LOG_NOTICE, "Unregistered [%s]\n",service->handle);
  }
}

static int ServicesRegister()
{
  if (ast_mutex_lock(&services_lock)) {
    ast_log(LOG_WARNING, "unable to lock service list.\n");
    return FAILURE;
  }
  int result = SUCCESS;
  struct bonjour_service* service;
  for(service = bonjour_services; service != NULL; service = service->next) {
    result = ServiceRegister(service);
    if(result != SUCCESS) {
      ast_log(LOG_WARNING, "Cannot register service '%s'.\n", service->handle);
    }
  }
  ast_mutex_unlock(&services_lock);
  return result;
}

static void ServicesUnregister()
{
  if(ast_mutex_lock(&services_lock)) { ast_log(LOG_WARNING, "unable to lock service list.\n"); return; }
  struct bonjour_service* service;
  for(service = bonjour_services; service != NULL; service = service->next) ServiceUnregister(service);
  ast_mutex_unlock(&services_lock);
}

static struct bonjour_service* Add2ServiceList(struct bonjour_service* services)
{
  struct bonjour_service *entry;

  entry = ast_calloc(1, sizeof(struct bonjour_service));
  if (!entry) return NULL;

  /* initialize safe defaults */
  entry->next = NULL;
  entry->sdRef = NULL;
  entry->sdFD = -1;
  entry->interfaceIndex = 0;
  entry->port = 0;
  entry->handle = entry->bindaddr = entry->name = entry->regtype =
    entry->domain = entry->host = entry->txtRecord = NULL;

  if(!services) return entry;
  while(services->next) services = services->next;
  services->next = entry;
  return services->next;
}

static struct bonjour_service* BuildServiceList(struct bonjour_service** globals)
{
  int status = SUCCESS;
  struct bonjour_service* services = NULL;
  struct ast_config* conf = NULL;
  struct ast_flags config_flags = { .flags = 0 };

  conf = ast_config_load(BONJOUR_CONFIG, config_flags);
  if (!conf){
    ast_log(LOG_WARNING, "res_bonjour failed to load configuration file.\n");
    status = FAILURE;
  } else {
  
    struct bonjour_service* current = NULL;
    
    char* context;
    for(context = ast_category_browse(conf, NULL);
				context != NULL && status == SUCCESS;
				context = ast_category_browse(conf, context)) {
      
      if(!strcasecmp(context, "general")) {
        current = *globals;
      } else {
        if(services){
          current->next = Add2ServiceList(NULL);
          current = current->next;
        } else {
          services = current = Add2ServiceList(NULL);
        }
      }
      
      if(current) {
        current->handle = strdup(context);
	if(general_bonjour_services && general_bonjour_services->domain) current->domain = general_bonjour_services->domain;
        struct ast_variable* variable;
        for(variable = ast_variable_browse(conf, context); 
	    variable != NULL && status == SUCCESS; 
	    variable = variable->next) {
	  if(!strcasecmp(variable->name, "name")) {
	    current->name = strdup(variable->value);
	  } else if(!strcasecmp(variable->name, "type")) {
	    current->regtype = strdup(variable->value);
	  } else if (!strcasecmp(variable->name,"port")) {
	    current->port = atoi(variable->value);
	  } else if(!strcasecmp(variable->name,"domain")) {
	    current->domain = strdup(variable->value);
	  } else if (!strcasecmp(variable->name,"bindaddr")) {
	    current->interfaceIndex = if_nametoindex(variable->value);
	    if (!current->interfaceIndex){
	      ast_log(LOG_WARNING, "The interface '%s' does not exist. Using all interfaces.\n", variable->value);
	    }
	  } else if(!strcasecmp(variable->name, "parameter")) {
	    current->txtRecord = appendDNStxtRecord(current->txtRecord, variable->value);
	  } else {
	    ast_log(LOG_WARNING, "Unknown variable '%s'. Ingoring.\n", variable->name);
	  }

        }
      } else {
        ast_log(LOG_WARNING, "Failed to allocate service struct.\n");
        status = FAILURE;
        break;				
      }
    }
    ast_config_destroy(conf);
  }
  if(status != SUCCESS){
    DestroyServiceList(services);
    services = NULL;
  }
  return services;
}

static void DestroyServiceList(struct bonjour_service* services)
{
  while(services) {
    struct bonjour_service* next = services->next;
    if(services->bindaddr) free(services->bindaddr);
    if(services->handle) free(services->handle);
    if(services->name) free(services->name);
    if(services->regtype) free(services->regtype);
    if(services->domain) free(services->domain);
    if(services->host) free(services->host);
    if(services->txtRecord) free(services->txtRecord);
    services = next;
  }
}
 
static int bonjour_load_module(void)
{
  struct bonjour_service* services = NULL;
  struct bonjour_service* general_services = NULL;

  if(!(general_services = Add2ServiceList(NULL))) {
    ast_log(LOG_WARNING, "Cannot allocate service list.\n");
    if(general_services) DestroyServiceList(general_services);
    if(services) DestroyServiceList(services);
    return FAILURE;
  }
  if(!(services = BuildServiceList(&general_services))){
    ast_log(LOG_WARNING, "Failed to load services from configuration file.\n");
    if(general_services) DestroyServiceList(general_services);
    if(services) DestroyServiceList(services);
    return FAILURE;
  }

  if(!mDNS_thread_id && ast_pthread_create(&mDNS_thread_id, NULL, mDNS_thread, NULL)) {
    ast_log(LOG_WARNING, "Cannot create mDNS callback thread.\n");
    if(general_services) DestroyServiceList(general_services);
    if(services) DestroyServiceList(services);
    return FAILURE;
  }

  if(ast_mutex_lock(&services_lock)) {
    ast_log(LOG_WARNING, "unable to lock service list.\n");
    if(general_services) DestroyServiceList(general_services);
    if(services) DestroyServiceList(services);
    return FAILURE;
  }
  
  DestroyServiceList(bonjour_services);
  bonjour_services = services;

  DestroyServiceList(general_bonjour_services);
  general_bonjour_services = general_services;

  ast_mutex_unlock(&services_lock);
  ServicesRegister();
  return SUCCESS;
}

/*
 *
 */

static int handle_show_bonjour(int fd)
{
  if(ast_mutex_lock(&services_lock)) {
    ast_log(LOG_WARNING, "Unable to lock services list\n");
    return -1;
  } else {
    ast_cli(fd,"\tBonjour Services:\n-----------------------------------------------------------------\n");
    struct bonjour_service *zs;
    for(zs=bonjour_services; zs; zs=zs->next) {
      if(zs->sdRef) {
	char indexName[IF_NAMESIZE];
        ast_cli(fd,"[%s]\n\t'%s'\n\t%s.%s port:%d on %s\n",
		zs->handle,
		zs->name,
		zs->regtype, 
		zs->domain ? zs->domain : general_bonjour_services->domain,
		zs->port,
		zs->interfaceIndex?if_indextoname(zs->interfaceIndex, indexName):"all interfaces");
      }
    }
    ast_mutex_unlock(&services_lock);
    return 0;
  }
}




static char *show_bonjour_cli(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a){

        switch (cmd) {
                case CLI_INIT:
                        e->command = "show bonjour";
                        e->usage = "Show advertised Bonjour services\n";
                        return NULL;
                case CLI_GENERATE:
                        switch (a->pos) {
                                case 0: 
                                        return a->n == 0 ? ast_strdup("show") : NULL;
                                case 1: 
                                        return a->n == 0 ? ast_strdup("bonjour") : NULL;
                                default:
                                        return NULL;
                        }
                default:
                        break;
        }

        if (a->argc != 2)
                return CLI_SHOWUSAGE;

	if(handle_show_bonjour(a->fd)){
		return CLI_FAILURE;
	} else {
        	return CLI_SUCCESS;
	}
}

static struct ast_cli_entry cli_entry[] = {
	AST_CLI_DEFINE(show_bonjour_cli, "Show advertised Bonjour services"),
};

/*
 * Standard module functions.
 */

int unload_module(void)
{
  ast_cli_unregister_multiple(cli_entry, sizeof(cli_entry) / sizeof(struct ast_cli_entry));
  ServicesUnregister();
  if (mDNS_thread_id) {
    mDNS_thread_continue = 0;
    pthread_join(mDNS_thread_id,NULL);
    ast_log(LOG_NOTICE, "mDNS callback thread terminated.\n");
  }
  DestroyServiceList(bonjour_services);
  DestroyServiceList(general_bonjour_services);
  ast_log(LOG_NOTICE, "unloaded.\n");
  return 0;
}

int load_module(void)
{
  if (bonjour_load_module() != SUCCESS) return -1;
	ast_cli_register_multiple(cli_entry, sizeof(cli_entry) / sizeof(struct ast_cli_entry));
  ast_log(LOG_NOTICE, "res_bonjour loaded.\n");
	return 0;
}

int reload(void)
{
  ServicesUnregister();
  if (ast_mutex_lock(&services_lock) == 0) {
    DestroyServiceList(bonjour_services);
    DestroyServiceList(general_bonjour_services);
    bonjour_services = NULL;
    general_bonjour_services = NULL;
    ast_mutex_unlock(&services_lock);
  }
  
  if (bonjour_load_module() != SUCCESS) return -1;
  ast_log(LOG_NOTICE, "reloaded.\n");
  
  return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS, "Bonjour service advertising module",
                .load = load_module,
                .unload = unload_module,
                .reload = reload,
                );
