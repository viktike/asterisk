/* app_my: damn simple MySQL application for asterisk
 * Copyright (C) 2010 - Steve Frécinaux
 * Licensed under the GPL2+
 */

#ifndef __QUERY_H__
#define __QUERY_H__

void  query_init  (const struct ast_module_info *ast_module_info);
void	query_clean (void);

#endif
