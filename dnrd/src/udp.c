/*
 * udp.c - handle upd connections
 *
 * Copyright (C) 1999 Brad M. Garcia <garsh@home.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <assert.h>
#include "common.h"
#include "relay.h"
#include "cache.h"
#include "query.h"
#include "check.h"

#ifndef EXCLUDE_MASTER
#include "master.h"
#endif

/*
 * dnssend()						22OCT99wzk
 *
 * Abstract: A small wrapper for send()/sendto().  If an error occurs a
 *           message is written to syslog.
 *
 * Returns:  The return code from sendto().
 */
static int dnssend(srvnode_t *s, void *msg, int len)
{
    int	rc;
    time_t now = time(NULL);

    rc = sendto(s->sock, msg, len, 0,
		(const struct sockaddr *) &s->addr,
		sizeof(struct sockaddr_in));

    if (rc != len) {
	log_msg(LOG_ERR, "sendto error: %s: %m",
		inet_ntoa(s->addr.sin_addr));
	return (rc);
    }
    if ((s->send_time == 0)) s->send_time = now;
    return (rc);
}

/* send a query to current server. Returns len on success and 0 on
   failure */

int send2current(domnode_t *d, void *msg, const int len) {
    /* If we have domains associated with our servers, send it to the
       appropriate server as determined by srvr */
  while ((d->current != NULL) && (dnssend(d->current, msg, len) != len)) {
    if (reactivate_interval) deactivate_current(d);
  }
  if (d->current != NULL) {
    return len;
  } else return 0;
}

/*
 * handle_udprequest()
 *
 * This function handles udp DNS requests by either replying to them (if we
 * know the correct reply via master, caching, etc.), or forwarding them to
 * an appropriate DNS server.
 */
void udp_handle_request()
{
    unsigned           addr_len;
    int                len;
    const int          maxsize = UDP_MAXSIZE;
    static char        msg[maxsize+4];/* Do we really want this on the stack?*/
    struct sockaddr_in from_addr;
    int                fwd;
    domnode_t          *dptr;

    /* Read in the message */
    addr_len = sizeof(struct sockaddr_in);
    len = recvfrom(isock, msg, maxsize, 0,
		   (struct sockaddr *)&from_addr, &addr_len);
    if (len < 0) {
	log_debug("recvfrom error %s", strerror(errno));
	return;
    }

    /* do some basic checking */
    if (check_query(msg, len) < 0) return;

    /* Determine how query should be handled */
    if ((fwd = handle_query(&from_addr, msg, &len, &dptr)) < 0)
      return; /* if its bogus, just ignore it */

    /* If we already know the answer, send it and we're done */
    if (fwd == 0) {
	if (sendto(isock, msg, len, 0, (const struct sockaddr *)&from_addr,
		   addr_len) != len) {
	    log_debug("sendto error %s", strerror(errno));
	}
	return;
    }

    dnsquery_add(&from_addr, msg, len);
    if (!send2current(dptr, msg, len)) {
      /* we couldn't send the query */
#ifndef EXCLUDE_MASTER
      int	packetlen;
      char	packet[maxsize+4];

      /*
       * If we couldn't send the packet to our DNS servers,
       * perhaps the `network is unreachable', we tell the
       * client that we are unable to process his request
       * now.  This will show a `No address (etc.) records
       * available for host' in nslookup.  With this the
       * client won't wait hang around till he gets his
       * timeout.
       * For this feature dnrd has to run on the gateway
       * machine.
       */
      
      if ((packetlen = master_dontknow(msg, len, packet)) > 0) {
	   if (!dnsquery_find(msg, &from_addr)) {
	   log_debug("ERROR: couldn't find the original query");
	   return;
	   }
	if (sendto(isock, msg, len, 0, (const struct sockaddr *)&from_addr,
		   addr_len) != len) {
	  log_debug("sendto error %s", strerror(errno));
	  return;
	}
      }
#endif
    }
}

/*
 * dnsrecv()							22OCT99wzk
 *
 * Abstract: A small wrapper for recv()/recvfrom() with output of an
 *           error message if needed.
 *
 * Returns:  A positove number indicating of the bytes received, -1 on a
 *           recvfrom error and 0 if the received message is too large.
 */
static int dnsrecv(srvnode_t *s, void *msg, int len)
{
    int	rc, fromlen;
    struct sockaddr_in from;

    fromlen = sizeof(struct sockaddr_in);
    rc = recvfrom(s->sock, msg, len, 0,
		  (struct sockaddr *) &from, &fromlen);

    if (rc == -1) {
	log_msg(LOG_ERR, "recvfrom error: %s: %m",
		inet_ntoa(s->addr.sin_addr));
	return (-1);
    }
    else if (rc > len) {
	log_msg(LOG_NOTICE, "packet too large: %s",
		inet_ntoa(s->addr.sin_addr));
	return (0);
    }
    else if (memcmp(&from.sin_addr, &s->addr.sin_addr,
		    sizeof(from.sin_addr)) != 0) {
	log_msg(LOG_WARNING, "unexpected server: %s",
		inet_ntoa(from.sin_addr));
	return (0);
    }

    return (rc);
}

/*
 * handle_udpreply()
 *
 * This function handles udp DNS requests by either replying to them (if we
 * know the correct reply via master, caching, etc.), or forwarding them to
 * an appropriate DNS server.
 */
void udp_handle_reply(srvnode_t *srv)
{
    const int          maxsize = 512; /* According to RFC 1035 */
    static char        msg[maxsize+4];
    int                len;
    struct sockaddr_in from_addr;
    unsigned           addr_len;

    if ((len = dnsrecv(srv, msg, maxsize)) < 0)
      {
	log_debug("dnsrecv failed: %i", len);
	return; /* recv error */
      }
    
    /* do basic checking */
    /* we have already done some checking. Is this necessary? */
    if (check_reply(srv, msg, len) < 0) return;

    if (opt_debug) {
	char buf[256];
	sprintf_cname(&msg[12], len-12, buf, 256);
	log_debug("Received DNS reply for \"%s\"", buf);
    }

    dump_dnspacket("reply", msg, len);
    addr_len = sizeof(struct sockaddr_in);
    if (!dnsquery_find(msg, &from_addr)) {
      if (!srv->inactive) {
	log_debug("ERROR: couldn't find the original query");
#ifdef DEBUG
	dnsquery_dump();
#endif
      }
    }
    else {
      cache_dnspacket(msg, len, srv);
      log_debug("Forwarding the reply to the host");
      if (sendto(isock, msg, len, 0,
		 (const struct sockaddr *)&from_addr,
		 addr_len) != len) {
	log_debug("sendto error %s", strerror(errno));
      }
      
    }
      
    /* this server is obviously alive, we reset the counters */
    srv->send_count = 0; /* this is not used anymore? */
    srv->send_time = 0;
    if (srv->inactive) log_debug("Reactivating server %s",
				 inet_ntoa(srv->addr.sin_addr));
    srv->inactive = 0;
}


/* send a dummy packet to a deactivated server to check if its back*/
int udp_send_dummy(srvnode_t *s) {
  static unsigned char dnsbuf[] = {
  /* HEADER */
    /* will this work on a big endian system? */
    0x00, 0x00, /* ID */
    0x00, 0x00, /* QR|OC|AA|TC|RD -  RA|Z|RCODE  */
    0x00, 0x01, /* QDCOUNT */
    0x00, 0x00, /* ANCOUNT */
    0x00, 0x00, /* NSCOUNT */
    0x00, 0x00, /* ARCOUNT */
    
    /* QNAME */
    9, 'l','o','c','a','l','h','o','s','t',0,
    /* QTYPE */
    0x00,0x01,   /* A record */
    
    /* QCLASS */
    0x00,0x01   /* IN */
  };
  
  /* send the packet */

  /* should not happen */
  assert(s != NULL);

  log_debug("Sending dummy id=%i to %s",  (unsigned short int) dnsbuf[0]++,
	    inet_ntoa(s->addr.sin_addr));
  return dnssend(s, &dnsbuf, sizeof(dnsbuf));
}
