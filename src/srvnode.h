/*

    File: srvnode.h
    
    Copyright (C) 2004 by Natanael Copa <n@tanael.org>

    This source is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2, or (at your option)
    any later version.

    This source is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/



#ifndef SRVNODE_H
#define SRVNODE_H

#include <netinet/in.h>

typedef struct _srvnode {
  int                 sock; /* the communication socket */
  struct sockaddr_in     addr;      /* IP address of server */
  int                 active; /* is this server active? */
  int                 nexttry; /* next time to retry */
  struct _srvnode     *next; /* ptr to next server */

} srvnode_t;


srvnode_t *alloc_srvnode(void);
srvnode_t *init_srvlist(void);
srvnode_t *ins_srvnode (srvnode_t *list, srvnode_t *p);
srvnode_t *del_srvnode(srvnode_t *list);
srvnode_t *destroy_srvnode(srvnode_t *p);
srvnode_t *empty_srvlist(srvnode_t *head);
srvnode_t *destroy_srvlist(srvnode_t *head);
srvnode_t *add_srv(srvnode_t *head, char *ipaddr);






#endif



