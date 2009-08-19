/*
 * relay6_parser.c
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

/*
 * Copyright (C) NEC Europe Ltd., 2003
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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <string.h>

#include <glib.h>

#include "relay6_parser.h"

GSList *relay_msg_parser_list = NULL;

relay_msg_parser_t *create_parser_obj(void) {
    relay_msg_parser_t *msg;

    msg = (relay_msg_parser_t *) g_malloc0(sizeof(relay_msg_parser_t));
    if (msg == NULL) {
        g_error("%s: memory allocation error", __func__);
        exit(1);
    }

    msg->buffer = (guint8 *) g_malloc0(MAX_DHCP_MSG_LENGTH * sizeof(guint8));
    if (msg->buffer == NULL) {
        g_error("%s: memory allocation error", __func__);
        exit(1);
    }

    memcpy(msg->buffer, relaysock->databuf, MAX_DHCP_MSG_LENGTH);

    msg->sent = 0;
    msg->if_index = 0;

    msg->interface_in = relaysock->pkt_interface;
    memcpy(msg->src_addr, relaysock->src_addr, sizeof(relaysock->src_addr));
    msg->datalength = relaysock->buflength;
    msg->pointer_start = msg->buffer;
    msg->dst_addr_type = relaysock->dst_addr_type;

    relay_msg_parser_list = g_slist_append(relay_msg_parser_list, msg);

    g_debug("%s: received new message on interface: %d, source: %s",
            __func__, msg->interface_in, msg->src_addr);

    return msg;
}

gint check_buffer(gint ref, relay_msg_parser_t *mesg) {
    gint diff;

    diff = (int) (mesg->pstart - mesg->pointer_start);

    if ((((int) mesg->datalength) - diff) >= ref) {
        return 1;
    } else {
        return 0;
    }
}

gint put_msg_in_store(relay_msg_parser_t *mesg) {
    guint32 msg_type;
    guint8 *hop, msg;

    /* --------------------------- */
    mesg->pstart = mesg->buffer;

    if (check_buffer(MESSAGE_HEADER_LENGTH, mesg) == 0) {
        g_debug("%s: opt_length has 0 value for message header length, "
                "dropping", __func__);
        return 0;
    }

    msg_type = *((guint32 *) mesg->pstart);
    msg_type = (ntohl(msg_type) & 0xFF000000) >> 24;

    if (msg_type == DH6_SOLICIT) {
        if (check_interface_semafor(mesg->interface_in) == 0) {
            return 0;
        }

        g_debug("%s: relaying SOLICIT from client", __func__);
        mesg->isRF = 0;

        if (process_RELAY_FORW(mesg) == 0) {
            return 0;
        }

        return 1;
    } else if (msg_type == DH6_REBIND) {
        if (check_interface_semafor(mesg->interface_in) == 0) {
            return 0;
        }

        g_debug("%s: relaying REBIND from client", __func__);
        mesg->isRF = 0;

        if (process_RELAY_FORW(mesg) == 0) {
            return 0;
        }

        return 1;
    } else if (msg_type == DH6_INFORM_REQ) {
        if (check_interface_semafor(mesg->interface_in) == 0) {
            return 0;
        }

        g_debug("%s: relaying INFORMATION_REQUEST from client", __func__);
        mesg->isRF = 0;

        if (process_RELAY_FORW(mesg) == 0) {
            return 0;
        }

        return 1;
    } else if (msg_type == DH6_REQUEST) {
        if (check_interface_semafor(mesg->interface_in) == 0) {
            return 0;
        }

        g_debug("%s: relaying REQUEST from client", __func__);
        mesg->isRF = 0;

        if (process_RELAY_FORW(mesg) == 0) {
            return 0;
        }

        return 1;
    } else if (msg_type == DH6_REPLY) {
        if (check_interface_semafor(mesg->interface_in) == 0) {
            return 0;
        }

        g_debug("%s: relaying REPLY from client", __func__);
        mesg->isRF = 0;

        if (process_RELAY_FORW(mesg) == 0) {
            return 0;
        }

        return 1;
    } else if (msg_type == DH6_RENEW) {
        if (check_interface_semafor(mesg->interface_in) == 0) {
            return 0;
        }

        g_debug("%s: relaying RENEW from client", __func__);
        mesg->isRF = 0;

        if (process_RELAY_FORW(mesg) == 0) {
            return 0;
        }

        return 1;
    } else if (msg_type == DH6_RECONFIGURE) {
        if (check_interface_semafor(mesg->interface_in) == 0) {
            return 0;
        }

        g_debug("%s: relaying RECONFIGURE from client", __func__);
        mesg->isRF = 0;

        if (process_RELAY_FORW(mesg) == 0) {
            return 0;
        }

        return 1;
    } else if (msg_type == DH6_CONFIRM) {
        if (check_interface_semafor(mesg->interface_in) == 0) {
            return 0;
        }

        g_debug("%s: relaying CONFIRM from client", __func__);
        mesg->isRF = 0;

        if (process_RELAY_FORW(mesg) == 0) {
            return 0;
        }

        return 1;
    } else if (msg_type == DH6_ADVERTISE) {
        if (check_interface_semafor(mesg->interface_in) == 0) {
            return 0;
        }

        g_debug("%s: relaying ADVERTISE from client", __func__);
        mesg->isRF = 0;

        if (process_RELAY_FORW(mesg) == 0) {
            return 0;
        }

        return 1;
    } else if (msg_type == DH6_DECLINE) {
        if (check_interface_semafor(mesg->interface_in) == 0) {
            return 0;
        }

        g_debug("%s: relaying DECLINE from client", __func__);
        mesg->isRF = 0;

        if (process_RELAY_FORW(mesg) == 0) {
            return 0;
        }

        return 1;
    } else if (msg_type == DH6_RELEASE) {
        if (check_interface_semafor(mesg->interface_in) == 0) {
            return 0;
        }

        g_debug("%s: relaying RELEASE from client", __func__);
        mesg->isRF = 0;

        if (process_RELAY_FORW(mesg) == 0) {
            return 0;
        }

        return 1;
    }

    msg = *mesg->pstart;

    if (msg == DH6_RELAY_FORW) {
        if (check_interface_semafor(mesg->interface_in) == 0) {
            return 0;
        }

        g_debug("%s: relaying RELAY_FORW from relay agent", __func__);
        hop = (mesg->pstart + 1);

        if (*hop >= HOP_COUNT_LIMIT) {
            g_debug("%s: hop count exceeded, packet will be dropped", __func__);
            return 0;
        }

        mesg->hop_count = *hop;
        mesg->isRF = 1;

        if (process_RELAY_FORW(mesg) == 0) {
            return 0;
        }

        return 1;
    }

    if (msg == DH6_RELAY_REPL) {
        g_debug("%s: relaying RELAY_REPL from relay agent or server",
                __func__);

        if (process_RELAY_REPL(mesg) == 0) {
            return 0;
        }

        return 1;
    }

    g_error("%s: received unknown message, dropping", __func__);

    return 0;
}
