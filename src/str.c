/*
 * str.c
 * String conversion routines used in dhcpv6.
 *
 * Copyright (C) 2009  Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author(s): David Cantrell <dcantrell@redhat.com>
 */

/* ported from KAME: common.c,v 1.65 2002/12/06 01:41:29 suz Exp */

/*
 * Copyright (C) 1998 and 1999 WIDE Project.
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

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <glib.h>

#include "duid.h"
#include "dhcp6.h"
#include "confdata.h"
#include "common.h"
#include "str.h"
#include "gfunc.h"

gchar *addr2str(struct sockaddr *sa, socklen_t salen) {
    static gchar addrbuf[8][NI_MAXHOST + 1];
    static gint round = 0;
    gchar *cp;

    round = (round + 1) & 7;
    cp = addrbuf[round];
    memset(cp, '\0', NI_MAXHOST + 1);

    if (getnameinfo(sa, salen, cp, NI_MAXHOST, NULL, 0, NI_NUMERICHOST) != 0) {
        g_error("%s: getnameinfo return error", __func__);
    }

    return cp;
}

gchar *in6addr2str(struct in6_addr *in6, gint scopeid) {
    struct sockaddr_in6 sa6;

    memset(&sa6, 0, sizeof(sa6));
    sa6.sin6_family = AF_INET6;
    sa6.sin6_addr = *in6;
    sa6.sin6_scope_id = scopeid;

    return (addr2str((struct sockaddr *) &sa6, sizeof(sa6)));
}

gchar *dhcp6optstr(gint type) {
    gchar *ret = NULL;
    GString *tmp = g_string_new(NULL);

    if (type > 65535) {
        return "OPTION_INVALID";
    }

    if (type == DH6OPT_CLIENTID) {
        return "OPTION_CLIENTID";
    } else if (type == DH6OPT_SERVERID) {
        return "OPTION_SERVERID";
    } else if (type == DH6OPT_IA_NA) {
        return "OPTION_IA_NA";
    } else if (type == DH6OPT_IA_TA) {
        return "OPTION_IA_TA";
    } else if (type == DH6OPT_IADDR) {
        return "OPTION_IAADDR";
    } else if (type == DH6OPT_ORO) {
        return "OPTION_ORO";
    } else if (type == DH6OPT_PREFERENCE) {
        return "OPTION_PREFERENCE";
    } else if (type == DH6OPT_ELAPSED_TIME) {
        return "OPTION_ELAPSED_TIME";
    } else if (type == DH6OPT_RELAY_MSG) {
        return "OPTION_RELAY_MSG";
    } else if (type == DH6OPT_AUTH) {
        return "OPTION_AUTH";
    } else if (type == DH6OPT_UNICAST) {
        return "OPTION_UNICAST";
    } else if (type == DH6OPT_STATUS_CODE) {
        return "OPTION_STATUS_CODE";
    } else if (type == DH6OPT_RAPID_COMMIT) {
        return "OPTION_RAPID_COMMIT";
    } else if (type == DH6OPT_USER_CLASS) {
        return "OPTION_USER_CLASS";
    } else if (type == DH6OPT_VENDOR_CLASS) {
        return "OPTION_VENDOR_CLASS";
    } else if (type == DH6OPT_VENDOR_OPTS) {
        return "OPTION_VENDOR_OPTS";
    } else if (type == DH6OPT_INTERFACE_ID) {
        return "OPTION_INTERFACE_ID";
    } else if (type == DH6OPT_RECONF_MSG) {
        return "OPTION_RECONF_MSG";
    } else if (type == DH6OPT_RECONF_ACCEPT) {
        return "OPTION_RECONF_ACCEPT";
    } else if (type == DH6OPT_DNS_SERVERS) {
        return "OPTION_DNS_SERVERS";
    } else if (type == DH6OPT_DOMAIN_LIST) {
        return "OPTION_DOMAIN_LIST";
    } else if (type == DH6OPT_IA_PD) {
        return "OPTION_IA_PD";
    } else if (type == DH6OPT_IAPREFIX) {
        return "OPTION_IAPREFIX";
    } else if (type == DH6OPT_INFO_REFRESH_TIME) {
        return "OPTION_INFORMATION_REFRESH_TIME";
    } else {
        g_string_printf(tmp, "OPTION_%d", type);
        ret = g_strdup(tmp->str);

        if (g_string_free(tmp, TRUE) != NULL) {
            g_error("%s: erroring releasing temporary GString", __func__);
        }

        return ret;
    }
}

GString *dhcp6_options2str(GSList *options) {
    GString *ret = g_string_new(NULL);

    if (dhcp6_has_option(options, DH6OPT_CLIENTID)) {
        g_string_append_printf(ret, "%s ", dhcp6optstr(DH6OPT_CLIENTID));
    }

    if (dhcp6_has_option(options, DH6OPT_SERVERID)) {
        g_string_append_printf(ret, "%s ", dhcp6optstr(DH6OPT_SERVERID));
    }

    if (dhcp6_has_option(options, DH6OPT_IA_NA)) {
        g_string_append_printf(ret, "%s ", dhcp6optstr(DH6OPT_IA_NA));
    }

    if (dhcp6_has_option(options, DH6OPT_IA_TA)) {
        g_string_append_printf(ret, "%s ", dhcp6optstr(DH6OPT_IA_TA));
    }

    if (dhcp6_has_option(options, DH6OPT_IADDR)) {
        g_string_append_printf(ret, "%s ", dhcp6optstr(DH6OPT_IADDR));
    }

    if (dhcp6_has_option(options, DH6OPT_ORO)) {
        g_string_append_printf(ret, "%s ", dhcp6optstr(DH6OPT_ORO));
    }

    if (dhcp6_has_option(options, DH6OPT_PREFERENCE)) {
        g_string_append_printf(ret, "%s ", dhcp6optstr(DH6OPT_PREFERENCE));
    }

    if (dhcp6_has_option(options, DH6OPT_ELAPSED_TIME)) {
        g_string_append_printf(ret, "%s ", dhcp6optstr(DH6OPT_ELAPSED_TIME));
    }

    if (dhcp6_has_option(options, DH6OPT_RELAY_MSG)) {
        g_string_append_printf(ret, "%s ", dhcp6optstr(DH6OPT_RELAY_MSG));
    }

    if (dhcp6_has_option(options, DH6OPT_AUTH)) {
        g_string_append_printf(ret, "%s ", dhcp6optstr(DH6OPT_AUTH));
    }

    if (dhcp6_has_option(options, DH6OPT_UNICAST)) {
        g_string_append_printf(ret, "%s ", dhcp6optstr(DH6OPT_UNICAST));
    }

    if (dhcp6_has_option(options, DH6OPT_STATUS_CODE)) {
        g_string_append_printf(ret, "%s ", dhcp6optstr(DH6OPT_STATUS_CODE));
    }

    if (dhcp6_has_option(options, DH6OPT_RAPID_COMMIT)) {
        g_string_append_printf(ret, "%s ", dhcp6optstr(DH6OPT_RAPID_COMMIT));
    }

    if (dhcp6_has_option(options, DH6OPT_USER_CLASS)) {
        g_string_append_printf(ret, "%s ", dhcp6optstr(DH6OPT_USER_CLASS));
    }

    if (dhcp6_has_option(options, DH6OPT_VENDOR_CLASS)) {
        g_string_append_printf(ret, "%s ", dhcp6optstr(DH6OPT_VENDOR_CLASS));
    }

    if (dhcp6_has_option(options, DH6OPT_VENDOR_OPTS)) {
        g_string_append_printf(ret, "%s ", dhcp6optstr(DH6OPT_VENDOR_OPTS));
    }

    if (dhcp6_has_option(options, DH6OPT_INTERFACE_ID)) {
        g_string_append_printf(ret, "%s ", dhcp6optstr(DH6OPT_INTERFACE_ID));
    }

    if (dhcp6_has_option(options, DH6OPT_RECONF_MSG)) {
        g_string_append_printf(ret, "%s ", dhcp6optstr(DH6OPT_RECONF_MSG));
    }

    if (dhcp6_has_option(options, DH6OPT_RECONF_ACCEPT)) {
        g_string_append_printf(ret, "%s ", dhcp6optstr(DH6OPT_RECONF_ACCEPT));
    }

    if (dhcp6_has_option(options, DH6OPT_DNS_SERVERS)) {
        g_string_append_printf(ret, "%s ", dhcp6optstr(DH6OPT_DNS_SERVERS));
    }

    if (dhcp6_has_option(options, DH6OPT_DOMAIN_LIST)) {
        g_string_append_printf(ret, "%s ", dhcp6optstr(DH6OPT_DOMAIN_LIST));
    }

    if (dhcp6_has_option(options, DH6OPT_IA_PD)) {
        g_string_append_printf(ret, "%s ", dhcp6optstr(DH6OPT_IA_PD));
    }

    if (dhcp6_has_option(options, DH6OPT_IAPREFIX)) {
        g_string_append_printf(ret, "%s ", dhcp6optstr(DH6OPT_IAPREFIX));
    }

    if (dhcp6_has_option(options, DH6OPT_INFO_REFRESH_TIME)) {
        g_string_append_printf(ret, "%s ",
                               dhcp6optstr(DH6OPT_INFO_REFRESH_TIME));
    }

    ret = g_string_truncate(ret, ret->len - 1);
    return ret;
}

gchar *dhcp6msgstr(gint type) {
    gchar *ret = NULL;
    GString *tmp = g_string_new(NULL);

    if (type > 255) {
        return "INVALID msg";
    }

    if (type == DHCP6S_INIT) {
        return "INIT";
    } else if (type == DHCP6S_RELEASE) {
        return "RELEASE";
    } else if (type == DHCP6S_IDLE) {
        return "IDLE";
    } else if (type == DH6_SOLICIT || type == DHCP6S_SOLICIT) {
        return "SOLICIT";
    } else if (type == DH6_ADVERTISE) {
        return "ADVERTISE";
    } else if (type == DH6_RENEW || type == DHCP6S_RENEW) {
        return "RENEW";
    } else if (type == DH6_REBIND || type == DHCP6S_REBIND) {
        return "REBIND";
    } else if (type == DH6_REQUEST || type == DHCP6S_REQUEST) {
        return "REQUEST";
    } else if (type == DH6_REPLY) {
        return "REPLY";
    } else if (type == DH6_CONFIRM || type == DHCP6S_CONFIRM) {
        return "CONFIRM";
    } else if (type == DH6_RELEASE) {
        return "RELEASE";
    } else if (type == DH6_DECLINE || type == DHCP6S_DECLINE) {
        return "DECLINE";
    } else if (type == DH6_INFORM_REQ || type == DHCP6S_INFOREQ) {
        return "INFOREQ";
    } else if (type == DH6_RECONFIGURE) {
        return "RECONFIGURE";
    } else if (type == DH6_RELAY_FORW) {
        return "RELAY-FORW";
    } else if (type == DH6_RELAY_REPL) {
        return "RELAY-REPL";
    } else {
        g_string_printf(tmp, "UNKNOWN_MESSAGE_ID_%d", type);
        ret = g_strdup(tmp->str);

        if (g_string_free(tmp, TRUE) != NULL) {
            g_error("%s: erroring releasing temporary GString", __func__);
        }

        return ret;
    }
}

gchar *dhcp6_stcodestr(gint code) {
    gchar *ret = NULL;
    GString *tmp = g_string_new(NULL);

    if (code > 255) {
        return "STATUS_INVALID";
    }

    if (code == DH6OPT_STCODE_SUCCESS) {
        return "Success";
    } else if (code == DH6OPT_STCODE_UNSPECFAIL) {
        return "UnspecFail";
    } else if (code == DH6OPT_STCODE_AUTHFAILED) {
        return "AuthFail";
    } else if (code == DH6OPT_STCODE_ADDRUNAVAIL) {
        return "AddrUnavail";
    } else if (code == DH6OPT_STCODE_NOADDRAVAIL) {
        return "NoAddrsAvail";
    } else if (code == DH6OPT_STCODE_NOBINDING) {
        return "NoBinding";
    } else if (code == DH6OPT_STCODE_CONFNOMATCH) {
        return "ConfirmNoMatch";
    } else if (code == DH6OPT_STCODE_NOTONLINK) {
        return "NotOnLink";
    } else if (code == DH6OPT_STCODE_USEMULTICAST) {
        return "UseMulticast";
    } else {
        g_string_printf(tmp, "STATUS_CODE_%d", code);
        ret = g_strdup(tmp->str);

        if (g_string_free(tmp, TRUE) != NULL) {
            g_error("%s: erroring releasing temporary GString", __func__);
        }

        return ret;
    }
}

gchar *duidstr(const duid_t *duid) {
    gint i;
    gchar *cp;
    static gchar duidstr[sizeof("xx:") * 256 + sizeof("...")];

    duidstr[0] = '\0';

    cp = duidstr;

    for (i = 0; i < duid->duid_len && i <= 256; i++) {
        cp += sprintf(cp, "%s%02x", i == 0 ? "" : ":",
                      duid->duid_id[i] & 0xff);
    }

    if (i < duid->duid_len) {
        sprintf(cp, "%s", "...");
    }

    return duidstr;
}