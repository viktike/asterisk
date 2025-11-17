/* app_my: damn simple MySQL application for asterisk
 * Copyright (C) 2010 - Steve Frécinaux
 * Licensed under the GPL2+
 */

#ifndef __AUTH_H__
#define __AUTH_H__

void  auth_init  (const struct ast_module_info *ast_module_info);
void	auth_clean (void);

#endif
