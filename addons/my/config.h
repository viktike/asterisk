/* app_my: damn simple MySQL application for asterisk
 * Copyright (C) 2010 - Steve Frécinaux
 * Licensed under the GPL2+
 */

#ifndef __CONFIG_H__
#define __CONFIG_H__

struct my_config {
  char *hostname;
  int port;
  char *username;
  char *password;
  char *database;
  int timeout;
};

#ifndef IN_CONFIG_C
extern struct my_config config;
#endif


void	config_init  (void);
void	config_reset (void);
void	config_clean (void);

#endif
