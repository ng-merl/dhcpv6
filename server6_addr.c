/*	$Id: server6_addr.c,v 1.3 2003/01/23 18:44:33 shirleyma Exp $	*/

/*
 * Copyright (C) International Business Machines  Corp., 2003
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* Author: Shirley Ma, xma@us.ibm.com */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <openssl/md5.h>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <sys/ioctl.h>

#include <linux/ipv6.h>

#include <net/if.h>

#include <errno.h>
#include <syslog.h>
#include <string.h>
#include <unistd.h>


#include "server6_addr.h"
#include "timer.h"

#include "queue.h"

extern struct hash_table **hash_anchors;

#define iaidaddr_hash_table hash_anchors[HT_IAIDADDR]
#define lease_hash_table hash_anchors[HT_IPV6ADDR]

extern FILE *lease_file;

static struct dhcp6_timer *iaidaddr_lease_timo __P((void *));
static struct dhcp6_timer *server6_iaidaddr_timo __P((void *));
static struct server6_lease 
*iaidaddr_find_lease __P((struct server6_cl_iaidaddr *, struct dhcp6_addr *));
static int iaidaddr_remove_lease __P((struct server6_lease *));
static int iaidaddr_add_lease __P((struct server6_cl_iaidaddr *, struct dhcp6_addr *));
static int iaidaddr_update_lease __P((struct dhcp6_addr *, struct server6_lease *));
static u_int32_t do_hash __P((void *, u_int8_t ));
static int addr_on_addrlist __P((struct dhcp6_list *, struct in6_addr *));
static u_int32_t get_max_validlifetime __P((struct server6_cl_iaidaddr *));

static u_int32_t 
do_hash(key, len)
	void *key;
	u_int8_t len;
{
	int i;
	u_int32_t *p;
	u_int32_t index = 0;
	u_int32_t tempkey;
	for (i = 0, p = (u_int32_t *)key; i < len/sizeof(tempkey); i++, p++ ) {
		memcpy(&tempkey, p, sizeof(tempkey));
		index ^= tempkey;
	}
	memcpy(&tempkey, p, len%(sizeof(tempkey)));
	index ^= tempkey;
	return index;
}

unsigned int
iaid_hash(key)
	void *key;
{
	struct client_if *iaidkey = (struct client_if *)key;
	struct duid *duid = &iaidkey->clientid;
	unsigned int index;
	index = do_hash((void *)duid->duid_id, duid->duid_len);
	return index;
}

unsigned int
addr_hash(key)
	void *key;
{
	struct in6_addr *addrkey = (struct in6_addr *)key;
	unsigned int index;
	index = do_hash((void *)addrkey, sizeof(*addrkey));
	dprintf(LOG_DEBUG, "ipv6address is %s, len is %d",
		in6addr2str(addrkey, 0), sizeof(*addrkey));
	return index;
}

struct host_decl
*find_hostdecl(duid, iaid, hostlist)
	struct duid *duid;
	u_int32_t iaid;
	struct host_decl *hostlist;
{
	struct host_decl *host;
	dprintf(LOG_DEBUG, "%s" "called", FNAME);
	for (host = hostlist; host; host = host->next) {
		if (duidcmp(duid, &host->cid) && host->iaid == iaid)
			return host;
		continue;
	}
		
	return NULL;
}

/* for request/solicit rapid commit */
int
server6_add_iaidaddr(optinfo)
	struct dhcp6_optinfo *optinfo;
{
	struct dhcp6_list *addr_list = &optinfo->addr_list;
	struct server6_cl_iaidaddr *iaidaddr;
	struct dhcp6_listval *lv;
	struct server6_lease *s_lease;
	struct timeval timo;
		
	dprintf(LOG_DEBUG, "%s" "called", FNAME);
	iaidaddr = (struct server6_cl_iaidaddr *)malloc(sizeof(*iaidaddr));
	if (iaidaddr == NULL) {
		dprintf(LOG_ERR, "%s" "failed to allocate memory", FNAME);
		return (-1);
	}
	memset(iaidaddr, 0, sizeof(*iaidaddr));
	duidcpy(&iaidaddr->client_info.clientid, &optinfo->clientID);
	iaidaddr->client_info.client_iaid = optinfo->iaidinfo.iaid;
	if (optinfo->flags & DHCIFF_TEMP_ADDRS)
		iaidaddr->client_info.client_iatype = 1;
	else
		iaidaddr->client_info.client_iatype = 0;
	if ((iaidaddr->timer =
			dhcp6_add_timer(server6_iaidaddr_timo, iaidaddr)) == NULL) {
		dprintf(LOG_ERR, "%s" "failed to add a timer for duid %s iaid %",
			FNAME, iaidaddr->client_info.client_iaid,
				duidstr(&iaidaddr->client_info.clientid));
		free(iaidaddr);
		return (-1);
	}
	TAILQ_INIT(&iaidaddr->ifaddr_list);
	/* add new leases */
	for (lv = TAILQ_FIRST(&optinfo->addr_list); lv; lv = TAILQ_NEXT(lv, link)) {
		if (hash_search(lease_hash_table, (void *)&lv->val_dhcp6addr.addr) != NULL) {
			dprintf(LOG_INFO, "%s" "address for %s has been used",
				FNAME, in6addr2str(&lv->val_dhcp6addr.addr, 0));
			TAILQ_REMOVE(&optinfo->addr_list, lv, link);
			continue;
		}
		if (iaidaddr_add_lease(iaidaddr, &lv->val_dhcp6addr) != 0)
			TAILQ_REMOVE(&optinfo->addr_list, lv, link); 
	}
	if (hash_add(iaidaddr_hash_table, &iaidaddr->client_info, iaidaddr)) {
		dprintf(LOG_ERR, "%s" "failed to hash_add an iaidaddr %d for client duid %s", 
			FNAME, iaidaddr->client_info.client_iaid,
				duidstr(&iaidaddr->client_info.clientid));
		server6_remove_iaidaddr(iaidaddr);
		return (-1);
	}

	dprintf(LOG_DEBUG, "%s" "hash_add an iaidaddr %d for client duid %s", 
		FNAME, iaidaddr->client_info.client_iaid,
			duidstr(&iaidaddr->client_info.clientid));
	/* it's meaningless to have an iaid without any leases */	
	if (TAILQ_EMPTY(&iaidaddr->ifaddr_list)) {
		dprintf(LOG_INFO, "%s" "no leases are added for duid %s iaid %d", 
			FNAME, duidstr(&iaidaddr->client_info.clientid), 
				iaidaddr->client_info.client_iaid);
		server6_remove_iaidaddr(iaidaddr);
		return (0);
	}

	/* iaidaddr will be timed out when the max(validlifetime of all addresses 
	 * in this iaid) */
	time(&iaidaddr->start_date);
	dprintf(LOG_DEBUG, "%s" "start date is %ld", FNAME, iaidaddr->start_date);
	iaidaddr->state = ACTIVE;
	timo.tv_sec = get_max_validlifetime(iaidaddr);
	timo.tv_usec = 0;
	dhcp6_set_timer(&timo, iaidaddr->timer);
	dprintf(LOG_DEBUG, "%s" "add iaidaddr for client %s iaid %d", FNAME,
		duidstr(&iaidaddr->client_info.clientid),
			iaidaddr->client_info.client_iaid);
	return (0);
}

int 
server6_remove_iaidaddr(iaidaddr)
	struct server6_cl_iaidaddr *iaidaddr;
{
	struct client_if client_info;
	struct server6_lease *lv, *lv_next;
	struct server6_lease *lease;
	
	dprintf(LOG_DEBUG, "%s" "called", FNAME);
	/* remove all the leases in this iaid */
	for (lv = TAILQ_FIRST(&iaidaddr->ifaddr_list); lv; lv = lv_next) {
		lv_next = TAILQ_NEXT(lv, link);
		if ((lease = hash_search(lease_hash_table, 
				(void *)&lv->lease_addr.addr)) != NULL) {
			dprintf(LOG_DEBUG, "%s" "lease address is %s", FNAME,
				in6addr2str(&lv->lease_addr.addr, 0));
			if (iaidaddr_remove_lease(lv)) {
				dprintf(LOG_ERR, "%s" "failed to remove an iaid %d", FNAME,
					 iaidaddr->client_info.client_iaid);
				return (-1);
			}
		}
	}
	if (hash_delete(iaidaddr_hash_table, &iaidaddr->client_info)) {
		dprintf(LOG_ERR, "%s" "failed to remove an iaid %d from hash",
			FNAME, iaidaddr->client_info.client_iaid);
		return (-1);
	}
	if (iaidaddr->timer)
		dhcp6_remove_timer(&iaidaddr->timer);
	free(iaidaddr);
	return (0);
}

struct server6_cl_iaidaddr
*server6_find_iaidaddr(optinfo)
	struct dhcp6_optinfo *optinfo;
{
	struct server6_cl_iaidaddr *iaidaddr;
	struct client_if client_info;
	dprintf(LOG_DEBUG, "%s" "called", FNAME);
	duidcpy(&client_info.clientid, &optinfo->clientID);
	client_info.client_iaid = optinfo->iaidinfo.iaid;
	if ((iaidaddr = hash_search(iaidaddr_hash_table, (void *)&client_info)) == NULL) {
		dprintf(LOG_DEBUG, "%s" "iaid %d iaidaddr for client duid %s doesn't exists", 
			FNAME, client_info.client_iaid, 
			duidstr(&client_info.clientid));
	}
	return iaidaddr;
}

int
iaidaddr_remove_lease(lease)
	struct server6_lease *lease;
{
	dprintf(LOG_DEBUG, "%s" "removing lease %s", FNAME,
		in6addr2str(&lease->lease_addr.addr, 0));
	lease->state = INVALID;
	if (write_lease(lease, lease_file) != 0) {
		dprintf(LOG_ERR, "%s" "failed to write an invalid lease %s to lease file", 
			FNAME, in6addr2str(&lease->lease_addr.addr, 0));
		return (-1);
	}
	if (hash_delete(lease_hash_table, &lease->lease_addr.addr)) {
		dprintf(LOG_ERR, "%s" "failed to remove an address %s from hash", 
			FNAME, in6addr2str(&lease->lease_addr.addr, 0));
		return (-1);
	}
	if (lease->timer)
		dhcp6_remove_timer(&lease->timer);
	TAILQ_REMOVE(&lease->iaidinfo->ifaddr_list, lease, link);
	free(lease);
	return 0;
}
		
/* for renew/rebind/release/decline */
int
server6_update_iaidaddr(optinfo, flag)
	struct dhcp6_optinfo *optinfo;
	int flag;
{
	struct dhcp6_list *addr_list = &optinfo->addr_list;
	struct server6_cl_iaidaddr *iaidaddr;
	struct client_if client_info;
	struct server6_lease *lease;
	struct dhcp6_listval *lv;
	struct timeval timo;
	
	dprintf(LOG_DEBUG, "%s" "called", FNAME);
	
	if ((iaidaddr = server6_find_iaidaddr(optinfo)) == NULL) {
		return (-1);
	}
	
	if (iaidaddr->timer == NULL) {
		if ((iaidaddr->timer = dhcp6_add_timer(server6_iaidaddr_timo, 
						iaidaddr)) == NULL) {
			dprintf(LOG_ERR, "%s" "failed to create a new event "
	    			"timer", FNAME);
			return (-1); 
		}
	}
	if (flag == ADDR_UPDATE) {		
		/* add or update new lease */
		for (lv = TAILQ_FIRST(&optinfo->addr_list); lv; lv = TAILQ_NEXT(lv, link)) {
			if ((lease = iaidaddr_find_lease(iaidaddr, &lv->val_dhcp6addr)) 
					!= NULL) {
				iaidaddr_update_lease(&lv->val_dhcp6addr, lease);
			} else {
				dprintf(LOG_DEBUG, "%s" "failed to find lease", FNAME);
				/* update the preferlifetime and valid lifetime 0 */
				lv->val_dhcp6addr.validlifetime = 0;
				lv->val_dhcp6addr.preferlifetime = 0;
			}
		}
		/* add leases that to the reply list */
		for (lease = TAILQ_FIRST(&iaidaddr->ifaddr_list); lease; 
				lease = TAILQ_NEXT(lease, link)) {
			if (!addr_on_addrlist(&optinfo->addr_list, &lease->lease_addr.addr)) {
				dhcp6_add_listval(&optinfo->addr_list, &lease->lease_addr,
					DHCP6_LISTVAL_DHCP6ADDR);
				iaidaddr_update_lease(&lease->lease_addr, lease);
			}
		}
		/* iaidaddr will be timed out when the max
		 * (validlifetime of all addresses in this iaid) */
		time(&iaidaddr->start_date);
		iaidaddr->state = ACTIVE;
		timo.tv_sec = get_max_validlifetime(iaidaddr);
		timo.tv_usec = 0;
		dhcp6_set_timer(&timo, iaidaddr->timer);
		dprintf(LOG_DEBUG, "%s" "update iaidaddr for iaid %d", FNAME, 
			iaidaddr->client_info.client_iaid);
	} else if (flag == ADDR_REMOVE) {
		/* add or update new lease */
		for (lv = TAILQ_FIRST(&optinfo->addr_list); lv; lv = TAILQ_NEXT(lv, link)) {
			if (lease = iaidaddr_find_lease(iaidaddr, &lv->val_dhcp6addr)) {
				iaidaddr_remove_lease(lease);
			} else {
				dprintf(LOG_INFO, "%s" "address is not on the iaid", FNAME);
			}
		}
	
	}
	return (0);
}

int
server6_validate_iaidaddr(optinfo)
	struct dhcp6_optinfo *optinfo;
{
	struct server6_cl_iaidaddr *iaidaddr;
	struct dhcp6_listval *lv;

	dprintf(LOG_DEBUG, "%s" "called", FNAME);
	if ((iaidaddr = server6_find_iaidaddr(optinfo)) == NULL) {
		return (-1);
	}
	/* add or update new lease */
	for (lv = TAILQ_FIRST(&optinfo->addr_list); lv; lv = TAILQ_NEXT(lv, link)) {
		if (!iaidaddr_find_lease(iaidaddr, &lv->val_dhcp6addr)) 
			return (-1);
	}
	return 0;
}

int
iaidaddr_add_lease(iaidaddr, addr)
	struct server6_cl_iaidaddr *iaidaddr;
	struct dhcp6_addr *addr;
{
	struct server6_lease *sp;
	struct timeval timo;
	/* ignore meaningless address, this never happens */
	if (addr->validlifetime == 0 || addr->preferlifetime == 0) {
		dprintf(LOG_INFO, "%s" "zero address life time for %s",
			in6addr2str(&addr->addr, 0));
		return (0);
	}

	if (((sp = hash_search(lease_hash_table, (void *)&addr->addr))) != NULL) {
		dprintf(LOG_INFO, "%s" "duplicated address: %s",
		    FNAME, in6addr2str(&addr->addr, 0));
		return (-1);
	}

	if ((sp = (struct server6_lease *)malloc(sizeof(*sp))) == NULL) {
		dprintf(LOG_ERR, "%s" "failed to allocate memory"
			" for an address", FNAME);
		return (-1);
	}
	memset(sp, 0, sizeof(*sp));
	memcpy(&sp->lease_addr, addr, sizeof(sp->lease_addr));
	sp->iaidinfo = iaidaddr;	
	if ((sp->timer = dhcp6_add_timer(iaidaddr_lease_timo, sp)) == NULL) {
		dprintf(LOG_ERR, "%s" "failed to create a new event "
	    		"timer", FNAME);
		free(sp);
		return (-1); 
	}
	/* ToDo: preferlifetime EXPIRED; validlifetime DELETED; */
	/* if a finite lease perferlifetime is specified, set up a timer. */
	time(&sp->start_date);
	dprintf(LOG_DEBUG, "%s" "start date is %ld", FNAME, sp->start_date);
	sp->state = ACTIVE;
	if (write_lease(sp, lease_file) != 0) {
		dprintf(LOG_ERR, "%s" "failed to write a new lease address %s to lease file", 
			FNAME, in6addr2str(&sp->lease_addr.addr, 0));
		free(sp->timer);
		free(sp);
		return (-1);
	}
	dprintf(LOG_ERR, "%s" "write lease %s to lease file", FNAME,
		in6addr2str(&sp->lease_addr.addr, 0));
	if (hash_add(lease_hash_table, &sp->lease_addr.addr, sp)) {
		dprintf(LOG_ERR, "%s" "failed to add hash for an address", FNAME);
			free(sp->timer);
			free(sp);
			return (-1);
	}
	TAILQ_INSERT_TAIL(&iaidaddr->ifaddr_list, sp, link);
	timo.tv_sec = sp->lease_addr.preferlifetime; 
	timo.tv_usec = 0;
	dhcp6_set_timer(&timo, sp->timer);
	dprintf(LOG_DEBUG, "%s" "add lease for %s iaid %d", FNAME,
		in6addr2str(&sp->lease_addr.addr, 0), sp->iaidinfo->client_info.client_iaid);
	return (0);
}

static int
iaidaddr_update_lease(addr, sp)
	struct dhcp6_addr *addr;
	struct server6_lease *sp;
{
	struct timeval timo;
	if (addr->preferlifetime == DHCP6_DURATITION_INFINITE) {
		dprintf(LOG_DEBUG, "%s" "update an address %s/%d "
		    "with infinite preferlifetime", FNAME,
		    in6addr2str(&addr->addr, 0), addr->plen,
		    addr->preferlifetime);
	} else {
		dprintf(LOG_DEBUG, "%s" "update an address %s/%d "
		    "with preferlifetime %d", FNAME,
		    in6addr2str(&addr->addr, 0), addr->plen,
		    addr->preferlifetime);
	}
	if (addr->validlifetime == DHCP6_DURATITION_INFINITE) {
		dprintf(LOG_DEBUG, "%s" "update an address %s/%d "
		    "with infinite validlifetime", FNAME,
		    in6addr2str(&addr->addr, 0), addr->plen,
		    addr->validlifetime);
	} else {
		dprintf(LOG_DEBUG, "%s" "update an address %s/%d "
		    "with validlifetime %d", FNAME,
		    in6addr2str(&addr->addr, 0), addr->plen,
		    addr->validlifetime);
	}
	if (sp->timer == NULL) {
		if ((sp->timer = dhcp6_add_timer(iaidaddr_lease_timo, sp)) == NULL) {
			dprintf(LOG_ERR, "%s" "failed to create a new event "
	    			"timer", FNAME);
			return (-1); 
		}
	}
	 
	memcpy(&sp->lease_addr, addr, sizeof(sp->lease_addr));

	/* ToDo: update the renew/rebind, expire/release timer*/
	time(&sp->start_date);
	sp->state = ACTIVE;
	if (write_lease(sp, lease_file) != 0) {
		dprintf(LOG_ERR, "%s" "failed to write an updated lease %s to lease file", 
			FNAME, in6addr2str(&sp->lease_addr.addr, 0));
		return (-1);
	}
	timo.tv_sec = sp->lease_addr.preferlifetime; 
	timo.tv_usec = 0;
	dhcp6_set_timer(&timo, sp->timer);
	return (0);
}

static struct server6_lease *
iaidaddr_find_lease(iaidaddr, ifaddr)
	struct server6_cl_iaidaddr *iaidaddr;
	struct dhcp6_addr *ifaddr;
{
	struct server6_lease *sp;
	dprintf(LOG_DEBUG, "%s" "called", FNAME);
	for (sp = TAILQ_FIRST(&iaidaddr->ifaddr_list); sp;
	     sp = TAILQ_NEXT(sp, link)) {
		/* check for prefix length
		 * sp->lease_addr.plen == ifaddr->plen &&
		 */
      		dprintf(LOG_DEBUG, "%s" "request address is %s ", FNAME,
			in6addr2str(&ifaddr->addr, 0));		
      		dprintf(LOG_DEBUG, "%s" "lease address is %s ", FNAME,
			in6addr2str(&sp->lease_addr.addr, 0));		
	      	if (IN6_ARE_ADDR_EQUAL(&sp->lease_addr.addr, &ifaddr->addr)) {
			return (sp);
		}
	}
	return (NULL);
}

int 
addr_on_addrlist(addrlist, addr)
	struct dhcp6_list *addrlist;
	struct in6_addr *addr;
{
	struct dhcp6_listval *lv;

	for (lv = TAILQ_FIRST(addrlist); lv;
	     lv = TAILQ_NEXT(lv, link)) {
		   if (IN6_ARE_ADDR_EQUAL(&lv->val_dhcp6addr.addr, addr)) {
			return (1);
		}
	}
	return (0);
}

void
get_random_bytes(u_int8_t seed[], int num)
{
	int i;
	for (i = 0; i < num; i++)
		seed[i] = random();
	return;
}


void 
create_tempaddr(prefix, plen, tempaddr)
	struct in6_addr *prefix;
	int plen;
	struct in6_addr *tempaddr;
{
	int i, num_bytes;
	u_int8_t digest[16];
	MD5_CTX ctx;
	u_int8_t seed[16];
	get_random_bytes(seed, 16);
	
	MD5_Init(&ctx);
	MD5_Update(&ctx, seed, 16);
	MD5_Final(digest, &ctx);
	memcpy(seed, digest, 16);
	/* address mask */
	memset(tempaddr, 0, sizeof(*tempaddr));	
	num_bytes = plen / 8;
	for (i = 0; i < num_bytes; i++) {
		tempaddr->s6_addr[i] = prefix->s6_addr[i];
	}
	tempaddr->s6_addr[num_bytes] = (prefix->s6_addr[num_bytes] | (0xFF >> plen % 8)) 
		& (seed[num_bytes] | (0xFF << 8 - plen % 8));
	
	for (i = num_bytes + 1; i < 16; i++) {
		tempaddr->s6_addr[i] = seed[i];
	}
	return;
}

int
server6_create_addrlist(tempaddr, subnet, optinfo, roptinfo)
	int tempaddr;
	struct link_decl *subnet;
	struct dhcp6_optinfo *optinfo, *roptinfo;
{
	struct dhcp6_listval *v6addr;
	struct v6addrseg *seg;
	struct in6_addr *addr6;
	struct server6_cl_iaidaddr *iaidaddr;
	struct server6_lease *lease;

	struct dhcp6_list *reply_list = &roptinfo->addr_list;
	struct dhcp6_list *req_list = &optinfo->addr_list;
	
	struct duid *clientID = &optinfo->clientID;
	u_int32_t iaid = optinfo->iaidinfo.iaid;

	dprintf(LOG_DEBUG, "%s" "called", FNAME);
	/* do we allow the request addr list from client ?
	if (req_list) {
		if (server6_find_iaidaddr())
			server6_update_iaidaddr()
		replay_list = req_list;
		return;		
	}
	*/
	addr6 = (struct in6_addr *)malloc(sizeof(*addr6));
	if (addr6 == NULL) {
		dprintf(LOG_ERR, "%s" "failed to allocate memory", FNAME);
		return (-1);
	}
	for (seg = subnet->seglist; seg; seg = seg->next) {
		struct in6_addr current;
		int round = 0;
		memcpy(&current, seg->free, sizeof(current));
		do {
			memset(addr6, 0, sizeof(*addr6));
			if (tempaddr) 
				/* assume the temp addr never being run out */
				create_tempaddr(&seg->prefix.addr, seg->prefix.plen, addr6);
		
			else {
				memcpy(addr6, seg->free, sizeof(*addr6));
				/* set seg->free */
				seg->free = inc_ipv6addr(seg->free);
				if (ipv6addrcmp(seg->free, &seg->max) == 1 ) {
					round = 1;
					memcpy(seg->free, &seg->min, sizeof(*seg->free));
				}
				if (round && IN6_ARE_ADDR_EQUAL(&current, addr6)) {
					memset(addr6, 0, sizeof(*addr6));
					break;
				}
			}
			dprintf(LOG_INFO, "%s" "address for %s is got", FNAME,
				in6addr2str(addr6, 0));
		} while ((hash_search(lease_hash_table, (void *)addr6) != NULL 
				|| is_anycast(addr6, seg->prefix.plen)));
		if (IN6_IS_ADDR_UNSPECIFIED(addr6)) continue;
		v6addr = (struct dhcp6_listval *)malloc(sizeof(*v6addr));
		if (v6addr == NULL) {
			dprintf(LOG_ERR, "%s" "fail to allocate memory", 
				FNAME, strerror(errno));
			continue;
		}
		memset(v6addr, 0, sizeof(*v6addr));
		memcpy(&v6addr->val_dhcp6addr.addr, addr6, sizeof(v6addr->val_dhcp6addr.addr));
		v6addr->val_dhcp6addr.plen = seg->prefix.plen;
		if (seg->parainfo.prefer_life_time == 0 && seg->parainfo.valid_life_time == 0) {
			seg->parainfo.valid_life_time = DEFAULT_VALID_LIFE_TIME;
			seg->parainfo.prefer_life_time = DEFAULT_PREFERRED_LIFE_TIME;
		} else if (seg->parainfo.prefer_life_time == 0) {
			seg->parainfo.prefer_life_time = seg->parainfo.valid_life_time / 2;
		} else if (seg->parainfo.valid_life_time == 0) {
			seg->parainfo.valid_life_time = 2 * seg->parainfo.prefer_life_time;
		}
		v6addr->val_dhcp6addr.preferlifetime = seg->parainfo.prefer_life_time;
		v6addr->val_dhcp6addr.validlifetime = seg->parainfo.valid_life_time;
		TAILQ_INSERT_TAIL(reply_list, v6addr, link);
	}
	free(addr6);
	return (0);
}

struct link_decl 
*server6_allocate_link(rootgroup, relay)
	struct rootgroup *rootgroup;
	struct in6_addr *relay;
{
	struct link_decl *link;
	struct interface *ifnetwork;
	ifnetwork = rootgroup->iflist;
	for (ifnetwork = rootgroup->iflist; ifnetwork; ifnetwork = ifnetwork->next) {
		if (strcmp(ifnetwork->name, device) != 0)
			continue;
		else {
			for (link = ifnetwork->linklist; link; link = link->next) {
				/* without relay agent support, so far we assume
				 * that client and server on the same link
				 */
				struct v6addrlist *temp;
				if (relay == NULL) {
					if (link->relaylist != NULL) 
						continue;
					else
						return link;
				} else {
					for (temp = link->relaylist; temp; temp = temp->next) {
						if (IN6_ARE_ADDR_EQUAL(relay, &temp->v6addr.addr))
							return link;
						else
							continue;
					}
				}
			}
		}
	}
	return NULL;
}

static struct dhcp6_timer 
*server6_iaidaddr_timo(arg)
	void *arg;
{
	struct server6_cl_iaidaddr *sp = (struct server6_cl_iaidaddr *)arg;
	struct timeval timeo;
	double d;

	dprintf(LOG_DEBUG, "%s" "iaidaddr timeout for %d, state=%d", FNAME,
		sp->client_info.client_iaid, sp->state);
	switch(sp->state) {
	case ACTIVE:
		if (!TAILQ_EMPTY(&sp->ifaddr_list)) {
			struct timeval timo;
			timo.tv_sec = get_max_validlifetime(sp);
			timo.tv_usec = 0;
			dhcp6_set_timer(&timo, sp->timer);
			return (sp->timer);
			
		} else {
			sp->state = INVALID;
			server6_remove_iaidaddr(sp);
			return (NULL);
		}
	default:
		server6_remove_iaidaddr(sp);
		return (NULL);
	}
}

static struct dhcp6_timer
*iaidaddr_lease_timo(arg)
	void *arg;
{
	struct server6_lease *sp = (struct server6_lease *)arg;
	struct timeval timeo;
	double d;

	dprintf(LOG_DEBUG, "%s" "lease timeout for %s, state=%d", FNAME,
		in6addr2str(&sp->lease_addr.addr, 0), sp->state);

	switch(sp->state) {
	case ACTIVE:
		sp->state = EXPIRED;
		d = sp->lease_addr.validlifetime - sp->lease_addr.preferlifetime;
		timeo.tv_sec = (long)d;
		timeo.tv_usec = 0;
		dhcp6_set_timer(&timeo, sp->timer);
		break;
	case EXPIRED:
		sp->state = INVALID;
		iaidaddr_remove_lease(sp);
		return (NULL);
	default:
		if (sp->timer)
			dhcp6_remove_timer(&sp->timer);
		return (NULL);
	}
	return (sp->timer);
}

static u_int32_t
get_max_validlifetime(sp)
	struct server6_cl_iaidaddr *sp;
{
	struct server6_lease *lv, *first;
	u_int32_t max;
	if (TAILQ_EMPTY(&sp->ifaddr_list))
		return 0;
	first = TAILQ_FIRST(&sp->ifaddr_list);
	max = first->lease_addr.validlifetime;
	for (lv = TAILQ_FIRST(&sp->ifaddr_list); lv; lv = TAILQ_NEXT(lv, link)) {
		max = MAX(max, lv->lease_addr.validlifetime);
	}
	return max;
}
