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

%{
#include "config.h"

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <stdlib.h>
#include <syslog.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>

#include <glib.h>

#include "duid.h"
#include "dhcp6.h"
#include "confdata.h"
#include "server6_conf.h"
#include "common.h"
#include "lease.h"
#include "str.h"

extern gint server6lex (void);
extern void server6error(gchar *, ...) __attribute__((__format__(__printf__, 1, 2)));

extern gint num_lines;
extern gint sock;
extern GHashTable *host_addr_hash_table;

static GSList *ifnetworklist = NULL;
static GSList *linklist = NULL;
static GSList *hostlist = NULL;
static GSList *poollist = NULL;

static server_interface_t *ifnetwork = NULL;
static link_decl_t *link = NULL;
static host_decl_t *host = NULL;
static pool_decl_t *pool = NULL;
static GSList *currentscope = NULL;
static GSList *currentgroup = NULL;
static gint allow = 0;

static void cleanup(void);

#define ABORT \
    do { \
        cleanup(); \
        YYABORT; \
    } while (0)

extern gint server6_tokenlex(void);
%}
%token <str> INTERFACE IFNAME
%token <str> PREFIX
%token <str> LINK
%token <str> RELAY

%token <str> STRING
%token <num> NUMBER
%token <snum> SIGNEDNUMBER
%token <dec> DECIMAL
%token <bool> BOOLEAN
%token <addr> IPV6ADDR
%token <str> INFINITY

%token <str> HOST
%token <str> POOL
%token <str> RANGE
%token <str> GROUP
%token <str> LINKLOCAL
%token <str> OPTION ALLOW SEND
%token <str> PREFERENCE
%token <str> RENEWTIME
%token <str> REBINDTIME
%token <str> RAPIDCOMMIT
%token <str> ADDRESS
%token <str> VALIDLIFETIME
%token <str> PREFERLIFETIME
%token <str> UNICAST
%token <str> TEMPIPV6ADDR
%token <str> DNS_SERVERS
%token <str> DUID DUID_ID
%token <str> IAID IAIDINFO
%token <str> INFO_ONLY
%token <str> INFO_REFRESH_TIME
%token <str> TO

%token <str> BAD_TOKEN
%type <str> name
%type <num> number_or_infinity
%type <dhcp6addr> hostaddr6 hostprefix6 addr6para v6address

%union {
    guint num;
    gint snum;
    gchar *str;
    gint dec;
    gint bool;
    struct in6_addr addr;
    struct dhcp6_addr *dhcp6addr;
}

%%
statements
    :
    | statements networkdef
    ;

networkdef
    : ifdef
    | groupdef
    | confdecl
    | linkdef
    ;

ifdef
    : ifhead '{' ifbody  '}' ';' {
          if (linklist) {
              ifnetwork->linklist = linklist;
              linklist = NULL;
          }

          if (hostlist) {
              ifnetwork->hostlist = hostlist;
              hostlist = NULL;
          }

          if (currentgroup) {
              ifnetwork->group = (scope_t *) currentgroup->data;
          }

          g_debug("interface definition for %s is ok", ifnetwork->name);
          ifnetworklist = g_slist_append(ifnetworklist, ifnetwork);
          globalgroup->iflist = ifnetworklist;

          /*
           * leave interface scope we know the current scope
           * is not point to NULL
           */
          currentscope = g_slist_delete_link(currentscope, currentscope);
      }
    ;

ifhead
    : INTERFACE IFNAME {
          GSList *iterator = ifnetworklist;

          while (iterator) {
              server_interface_t *i = (server_interface_t *) iterator->data;

              if (!g_strcmp0(i->name, $2)) {
                  g_error("duplicate interface definition for %s", i->name);
                  ABORT;
              }

              iterator = g_slist_next(iterator);
          }

          ifnetwork = (server_interface_t *) g_malloc0(sizeof(*ifnetwork));

          if (ifnetwork == NULL) {
              g_error("failed to allocate memory");
              ABORT;
          }

          ifnetwork->ifscope.dnsinfo.servers = NULL;
          strncpy(ifnetwork->name, $2, strlen($2));

          if (get_linklocal(ifnetwork->name, &ifnetwork->linklocal) < 0) {
              g_error("get device %s linklocal failed", ifnetwork->name);
          }

          /* check device, if the device is not available,
           * it is OK, it might be added later
           * so keep this in the configuration file.
           */
          if (if_nametoindex(ifnetwork->name) == 0) {
              g_error("this device %s doesn't exist.", $2);
          }

          /* set up hw_addr, link local, primary ipv6addr */
          /* enter interface scope */
          currentscope = g_slist_prepend(currentscope, &ifnetwork->ifscope);
          if (currentscope == NULL) {
              ABORT;
          }
      }
    ;

ifbody
    :
    | ifbody ifparams
    ;

ifparams
    : linkdef
    | hostdef
    | groupdef
    | confdecl
    ;

linkdef
    : linkhead '{' linkbody '}' ';' {
          if (poollist) {
              link->poollist = poollist;
              poollist = NULL;
          }

          if (currentgroup) {
              link->group = (scope_t *) currentgroup->data;
          }

          linklist = g_slist_prepend(linklist, link);

          /* leave link scope we know the current scope is not point to NULL*/
          currentscope = g_slist_delete_link(currentscope, currentscope);
      }
    ;

linkhead
    : LINK name {
          GSList *iterator = linklist;

          /* memory allocation for link */
          link = (link_decl_t *) g_malloc0(sizeof(*link));

          if (link == NULL) {
              g_error("failed to allocate memory");
              ABORT;
          }

          link->linkscope.dnsinfo.servers = NULL;

          while (iterator) {
              link_decl_t *temp_link = (link_decl_t *) iterator->data;

              if (!strcmp(temp_link->name, $2)) {
                  g_error("duplicate link definition for %s", $2);
                  ABORT;
              }

              iterator = g_slist_next(iterator);
          }

          /* link set */
          strncpy(link->name, $2, strlen($2));
          if (ifnetwork) {
              link->network = ifnetwork;
          } else {
              /* create a ifnetwork for this interface */
          }

          link->relaylist = NULL;
          link->seglist = NULL;
          /* enter link scope */
          currentscope = g_slist_prepend(currentscope, &link->linkscope);
          if (currentscope == NULL) {
              ABORT;
          }
      }
    ;

linkbody
    :
    | linkbody linkparams
    ;

linkparams
    : pooldef
    | rangedef
    | prefixdef
    | hostdef
    | groupdef
    | confdecl
    | relaylist
    ;

relaylist
    : relaylist relaypara
    | relaypara
    ;

relaypara
    : RELAY IPV6ADDR '/' NUMBER ';' {
          struct v6addrlist *temprelay;

          if (!link) {
              g_error("relay must be defined under link");
              ABORT;
          }

          temprelay = (struct v6addrlist *) g_malloc0(sizeof(*temprelay));
          if (temprelay == NULL) {
              g_error("failed to allocate memory");
              ABORT;
          }

          memcpy(&temprelay->v6addr.addr, &$2, sizeof(temprelay->v6addr.addr));
          temprelay->v6addr.plen = $4;
          temprelay->next = link->relaylist;
          link->relaylist = temprelay;
          temprelay = NULL;
      }
    ;

pooldef
    : poolhead '{' poolbody '}' ';' {
          if (currentgroup) {
              pool->group = (scope_t *) currentgroup->data;
          }

          poollist = g_slist_prepend(poollist, pool);

          /* leave pool scope we know the current scope is not point to NULL*/
          currentscope = g_slist_delete_link(currentscope, currentscope);
      }
    ;

poolhead
    : POOL {
          if (!link) {
              g_error("pooldef must be defined under link");
              ABORT;
          }

          pool = (pool_decl_t *) g_malloc0(sizeof(*pool));

          if (pool == NULL) {
              g_error("fail to allocate memory");
              ABORT;
          }

          pool->poolscope.dnsinfo.servers = NULL;

          if (link) {
              pool->link = link;
          }

          /* enter pool scope */
          currentscope = g_slist_prepend(currentscope, &pool->poolscope);
          if (currentscope == NULL) {
              ABORT;
          }
      }
    ;

poolbody
    :
    | poolbody poolparas
    ;

poolparas
    : hostdef
    | groupdef
    | rangedef
    | prefixdef
    | confdecl
    | relaylist
    ;

prefixdef
    : PREFIX IPV6ADDR '/' NUMBER ';' {
          v6prefix_t *v6prefix = NULL, *v6prefix0 = NULL;
          struct v6addr *prefix = NULL;
          GSList *iterator = NULL;

          if (!link) {
              g_error("prefix must be defined under link");
              ABORT;
          }

          v6prefix = (v6prefix_t *) g_malloc0(sizeof(*v6prefix));

          if (v6prefix == NULL) {
              g_error("failed to allocate memory");
              ABORT;
          }

          v6prefix->link = link;

          if (pool)
              v6prefix->pool = pool;

          /* make sure the range ipv6 address within the prefixaddr */
          if ($4 > 128 || $4 < 0) {
              g_error("invalid prefix length in line %d", num_lines);
              ABORT;
          }

          prefix = getprefix(&$2, $4);
          iterator = link->prefixlist;

          while (iterator) {
              v6prefix0 = (v6prefix_t *) iterator->data;

              if (IN6_ARE_ADDR_EQUAL(prefix, &v6prefix0->prefix.addr) &&
                  $4 == v6prefix0->prefix.plen) {
                  g_error("duplicated prefix defined within same link");
                  ABORT;
              }

              iterator = g_slist_next(iterator);
          }

          /* check the assigned prefix is not reserved pv6 addresses */
          if (IN6_IS_ADDR_RESERVED(prefix)) {
              g_error("config reserved prefix");
              ABORT;
          }

          memcpy(&v6prefix->prefix, prefix, sizeof(v6prefix->prefix));
          link->prefixlist = g_slist_append(link->prefixlist, v6prefix);
          g_free(prefix);
          prefix = NULL;
      }
    ;

rangedef
    : RANGE IPV6ADDR TO IPV6ADDR '/' NUMBER ';' {
          v6addrseg_t *seg, *temp_seg;
          struct v6addr *prefix1, *prefix2;
          GSList *iterator = NULL;

          if (!link) {
              g_error("range must be defined under link");
              ABORT;
          }

          seg = (v6addrseg_t *) g_malloc0(sizeof(*seg));

          if (seg == NULL) {
              g_error("failed to allocate memory");
              ABORT;
          }

          seg->link = link;

          if (pool) {
              seg->pool = pool;
          }

          /* make sure the range ipv6 address within the prefixaddr */
          if ($6 > 128 || $6 < 0) {
              g_error("invalid prefix length in line %d", num_lines);
              ABORT;
          }

          prefix1 = getprefix(&$2, $6);
          prefix2 = getprefix(&$4, $6);

          if (!prefix1 || !prefix2) {
              g_error("address range defined error");
              ABORT;
          }

          if (ipv6addrcmp(&prefix1->addr, &prefix2->addr)) {
              g_error("address range defined doesn't in the "
                      "same prefix range");
              ABORT;
          }

          if (ipv6addrcmp(&$2, &$4) < 0) {
              memcpy(&seg->min, &$2, sizeof(seg->min));
              memcpy(&seg->max, &$4, sizeof(seg->max));
          } else {
              memcpy(&seg->max, &$2, sizeof(seg->max));
              memcpy(&seg->min, &$4, sizeof(seg->min));
          }

          /* check the assigned addresses are not reserved ipv6 addresses */
          if (IN6_IS_ADDR_RESERVED(&seg->max) ||
              IN6_IS_ADDR_RESERVED(&seg->max)) {
              g_error("config reserved ipv6address");
              ABORT;
          }

          memcpy(&seg->prefix, prefix1, sizeof(seg->prefix));
          memcpy(&seg->free, &seg->min, sizeof(seg->free));

          if (pool) {
              seg->pool = pool;
          }

          /* make sure there is no overlap in the rangelist */
          /* the segaddr is sorted by prefix len, thus most specific
           * ipv6 address is going to be assigned.
           */
          iterator = link->seglist;
          while (iterator) {
              temp_seg = (v6addrseg_t *) iterator->data;

              if (prefix1->plen == temp_seg->prefix.plen) {
                  if (!(ipv6addrcmp(&seg->min, &temp_seg->max) > 0 ||
                      ipv6addrcmp(&seg->max, &temp_seg->min) < 0)) {
                         g_error("overlap range addr defined");
                         ABORT;
                  }
              }

              iterator = g_slist_next(iterator);
          }

          link->seglist = g_slist_append(link->seglist, seg);

          g_free(prefix1);
          prefix1 = NULL;
          g_free(prefix2);
          prefix2 = NULL;
      }
    ;

groupdef
    : grouphead '{' groupbody  '}' ';' {
          /* return to prev group scope if any */
          currentgroup = g_slist_delete_link(currentgroup, currentgroup);

          /* leave current group scope */
          currentscope = g_slist_delete_link(currentscope, currentscope);
      }
    ;

groupbody
    :
    | groupbody groupparas
    ;

groupparas
    : hostdef
    | pooldef
    | linkdef
    | rangedef
    | prefixdef
    | ifdef
    | confdecl
    ;

grouphead
    : GROUP {
          scope_t *groupscope;

          groupscope = (scope_t *) g_malloc0(sizeof(*groupscope));
          if (groupscope == NULL) {
              g_error("group memory allocation failed");
              ABORT;
          }

          groupscope->dnsinfo.servers = NULL;
          /* set up current group */
          currentgroup = g_slist_prepend(currentgroup, &groupscope);
          if (currentgroup == NULL) {
              ABORT;
          }

          /* enter group scope  */
          currentscope = g_slist_prepend(currentscope, &groupscope);
          if (currentscope == NULL) {
              ABORT;
          }
      }
    ;

hostdef
    : hosthead '{' hostbody '}' ';' {
          GSList *iterator = hostlist;

          while (iterator) {
              host_decl_t *temp_host = (host_decl_t *) iterator->data;

              if (temp_host->iaidinfo.iaid == host->iaidinfo.iaid) {
                  if (0 == duidcmp(&temp_host->cid, &host->cid)) {
                      g_error("duplicated host DUID=%s IAID=%u redefined",
                              duidstr(&host->cid), host->iaidinfo.iaid);
                      ABORT;
                  }
              }

              iterator = g_slist_next(iterator);
          }

          if (currentgroup) {
              host->group = (scope_t *) currentgroup->data;
          }

          hostlist = g_slist_append(hostlist, host);

          /* leave host scope we know the current scope is not point to NULL*/
          currentscope = g_slist_delete_link(currentscope, currentscope);
      }
    ;


hosthead
    : HOST name {
          GSList *iterator = hostlist;

          while (iterator) {
              host_decl_t *temp_host = (host_decl_t *) iterator->data;

              if (!strcmp(temp_host->name, $2)) {
                  g_error("duplicated host %s redefined", $2);
                  ABORT;
              }

              iterator = g_slist_next(iterator);
          }

          host = (host_decl_t *) g_malloc0(sizeof(*host));
          if (host == NULL) {
              g_error("fail to allocate memory");
              ABORT;
          }

          host->addrlist = NULL;
          host->prefixlist = NULL;
          host->hostscope.dnsinfo.servers = NULL;
          host->network = ifnetwork;
          strncpy(host->name, $2, strlen($2));
          /* enter host scope */
          currentscope = g_slist_prepend(currentscope, &host->hostscope);
          if (currentscope == NULL) {
              ABORT;
          }
      }
    ;

hostbody
    : hostbody hostdecl
    | hostdecl
    ;

hostdecl
    : DUID DUID_ID ';' {
          if (host == NULL) {
              g_debug("duid should be defined under host decl");
              ABORT;
          }

          configure_duid($2, &host->cid);
      }
    | iaiddef
    | hostparas
    ;

iaiddef
    : IAIDINFO '{' iaidbody '}' ';' {
      }
    ;

iaidbody
    : iaidbody RENEWTIME number_or_infinity ';' {
          host->iaidinfo.renewtime = $3;
      }
    | iaidbody REBINDTIME number_or_infinity ';' {
          host->iaidinfo.rebindtime = $3;
      }
    | iaidpara
    ;

iaidpara
    : IAID NUMBER ';' {
          if (host == NULL) {
              g_debug("iaid should be defined under host decl");
              ABORT;
          }

          host->iaidinfo.iaid = $2;
      }
    ;

hostparas
    : hostparas hostpara
    | hostpara
    ;

hostpara
    : hostaddr6 {
          if (host == NULL) {
              g_debug("address should be defined under host decl");
              ABORT;
          }

          dhcp6_add_listval(host->addrlist, $1, DHCP6_LISTVAL_DHCP6ADDR);
          g_hash_table_insert(host_addr_hash_table, &($1->addr), $1);
      }
    | hostprefix6 {
          if (host == NULL) {
              g_debug("prefix should be defined under host decl");
              ABORT;
          }

          dhcp6_add_listval(host->prefixlist, $1, DHCP6_LISTVAL_DHCP6ADDR);
      }
    | optiondecl
    ;

hostaddr6
    : ADDRESS '{' addr6para '}' ';' {
          $3->type = IANA;
          $$ = $3;
      }
    ;

hostprefix6
    : PREFIX '{' addr6para '}' ';' {
          $3->type = IAPD;
          $$ = $3;
      }
    ;

addr6para
    : addr6para VALIDLIFETIME number_or_infinity ';' {
          $1->validlifetime = $3;
      }
    | addr6para PREFERLIFETIME number_or_infinity ';' {
          $1->preferlifetime = $3;
      }
    | v6address {
          $$ = $1;
      }
    ;

v6address
    : IPV6ADDR '/' NUMBER ';' {
          struct dhcp6_addr *temp;

          temp = (struct dhcp6_addr *) g_malloc0(sizeof(*temp));
          if (temp == NULL) {
              g_error("v6addr memory allocation failed");
              ABORT;
          }

          memcpy(&temp->addr, &$1, sizeof(temp->addr));

          if ($3 > 128 || $3 < 0) {
              g_error("invalid prefix length in line %d", num_lines);
              ABORT;
          }

          temp->plen = $3;
          $$ = temp;
      }
    ;

optiondecl
    : optionhead optionpara
    ;

optionhead
    : SEND {
          if (!currentscope) {
              currentscope = g_slist_prepend(currentscope, &globalgroup->scope);
              if (currentscope == NULL) {
                  ABORT;
              }
          }
      }
    | ALLOW {
          if (!currentscope) {
              currentscope = g_slist_prepend(currentscope, &globalgroup->scope);
              if (currentscope == NULL) {
                  ABORT;
              }
          }

          allow = 1;
      }
    | OPTION {
          if (!currentscope) {
              currentscope = g_slist_prepend(currentscope, &globalgroup->scope);
              if (currentscope == NULL) {
                  ABORT;
              }
          }
      }
    ;

optionpara
    : RAPIDCOMMIT ';' {
          scope_t *scope = (scope_t *) currentscope->data;

          if (allow) {
              scope->allow_flags |= DHCIFF_RAPID_COMMIT;
          } else {
              scope->send_flags |= DHCIFF_RAPID_COMMIT;
          }
      }
    | TEMPIPV6ADDR ';' {
          scope_t *scope = (scope_t *) currentscope->data;

          if (allow) {
              scope->allow_flags |= DHCIFF_TEMP_ADDRS;
          } else {
              scope->send_flags |= DHCIFF_TEMP_ADDRS;
          }
      }
    | UNICAST ';' {
          scope_t *scope = (scope_t *) currentscope->data;

          if (allow) {
              scope->allow_flags |= DHCIFF_UNICAST;
          } else {
              scope->send_flags |= DHCIFF_UNICAST;
          }
      }
    | INFO_ONLY ';' {
          scope_t *scope = (scope_t *) currentscope->data;

          if (allow) {
              scope->allow_flags |= DHCIFF_INFO_ONLY;
          } else {
              scope->send_flags |= DHCIFF_INFO_ONLY;
          }
      }
    | INFO_REFRESH_TIME number_or_infinity ';' {
          globalgroup->scope.irt = $2;

          if (!currentscope) {
              currentscope = g_slist_prepend(currentscope, &globalgroup->scope);
              if (currentscope == NULL) {
                  ABORT;
              }
          }

          if ($2 < IRT_MINIMUM || DHCP6_DURATITION_INFINITE < $2) {
              g_error("%s: bad information refresh time", __func__);
              ABORT;
          }
      }
    | DNS_SERVERS dns_paras ';' {
      }
    ;

dns_paras
    : dns_paras dns_para
    | dns_para
    ;

dns_para
    : IPV6ADDR {
          scope_t *scope = currentscope->data;
          GSList *servers = scope->dnsinfo.servers;
          servers = g_slist_prepend(servers, &$1);
      }
    | STRING {
          scope_t *scope = currentscope->data;
          GSList *domains = scope->dnsinfo.domains;
          GString *tmp = g_string_new(NULL);
          gint len = strlen($1);

          if (len > MAXDNAME) {
              ABORT;
          }

          g_string_printf(tmp, "%s", $1);
          g_debug("add domain name %s", tmp->str);
          domains = g_slist_prepend(domains, tmp->str);
          g_string_free(tmp, FALSE);
      }
    ;

confdecl
    : paradecl
    | optiondecl
    ;

paradecl
    : RENEWTIME number_or_infinity ';' {
          globalgroup->scope.renew_time = $2;

          if (!currentscope) {
              currentscope = g_slist_prepend(currentscope, &globalgroup->scope);
              if (currentscope == NULL) {
                  ABORT;
              }
          }
      }
    | REBINDTIME number_or_infinity ';' {
          globalgroup->scope.rebind_time = $2;

          if (!currentscope) {
              currentscope = g_slist_prepend(currentscope, &globalgroup->scope);
              if (currentscope == NULL) {
                  ABORT;
              }
          }
      }
    | VALIDLIFETIME number_or_infinity ';' {
          globalgroup->scope.valid_life_time = $2;

          if (!currentscope) {
              currentscope = g_slist_prepend(currentscope, &globalgroup->scope);
              if (currentscope == NULL) {
                  ABORT;
              }
          }

          scope_t *scope = currentscope->data;
          if (scope->prefer_life_time != 0 &&
              scope->valid_life_time < scope->prefer_life_time) {
              g_error("%s: validlifetime is less than preferlifetime",
                      __func__);
              ABORT;
          }
      }
    | PREFERLIFETIME number_or_infinity ';' {
          globalgroup->scope.prefer_life_time = $2;

          if (!currentscope) {
              currentscope = g_slist_prepend(currentscope, &globalgroup->scope);
              if (currentscope == NULL) {
                  ABORT;
              }
          }

          scope_t *scope = currentscope->data;
          if (scope->valid_life_time != 0 &&
              scope->valid_life_time < scope->prefer_life_time) {
              g_error("%s: validlifetime is less than preferlifetime",
                      __func__);
              ABORT;
          }
      }
    | PREFERENCE NUMBER ';' {
          globalgroup->scope.server_pref = $2;

          if (!currentscope) {
              currentscope = g_slist_prepend(currentscope, &globalgroup->scope);
              if (currentscope == NULL) {
                  ABORT;
              }
          }

          if ($2 < 0 || $2 > 255) {
              g_error("%s: bad server preference number", __func__);
          }
      }
    ;

number_or_infinity
    : NUMBER {
          $$ = $1;
      }
    | INFINITY {
          $$ = DHCP6_DURATITION_INFINITE;
      }
    ;

name
    : STRING {
          $$ = $1;
      }
    ;

%%

static void cleanup(void) {
    /* it is not necessary to free all the pre g_malloc(), if it fails,
     * exit will free them automatically.
     */
}
