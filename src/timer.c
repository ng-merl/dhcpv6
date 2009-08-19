/*
 * timer.c
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

/* ported from KAME: timer.c,v 1.3 2002/09/24 14:20:50 itojun Exp */

/*
 * Copyright (C) 2002 WIDE Project.
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

#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <netinet/in.h>
#include <net/if.h>

#ifdef TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# include <time.h>
#endif

#include <glib.h>

#include "timer.h"

GSList *timer_list = NULL;
static struct timeval tm_sentinel;
static struct timeval tm_max = { 0x7fffffff, 0x7fffffff };

/* BEGIN STATIC FUNCTIONS */

/* result = a + b */
static void _timeval_add(struct timeval *a, struct timeval *b,
                         struct timeval *result) {
    glong l;

    if ((l = a->tv_usec + b->tv_usec) < 1000000) {
        result->tv_usec = l;
        result->tv_sec = a->tv_sec + b->tv_sec;
    } else {
        result->tv_usec = l - 1000000;
        result->tv_sec = a->tv_sec + b->tv_sec + 1;
    }
}

static gboolean is_timeval_equal(struct timeval *a, struct timeval *b) {
    if ((a->tv_sec == b->tv_sec) && (a->tv_usec == b->tv_usec)) {
        return TRUE;
    } else {
        return FALSE;
    }
}

static gboolean is_timeval_lt(struct timeval *a, struct timeval *b) {
    if ((a->tv_sec < b->tv_sec) ||
        ((a->tv_sec == b->tv_sec) && (a->tv_usec < b->tv_usec))) {
        return TRUE;
    } else {
        return FALSE;
    }
}

/* END STATIC FUNCTIONS */

/*
 * result = a - b
 * XXX: this function assumes that a >= b.
 */
void timeval_sub(struct timeval *a, struct timeval *b, struct timeval *result) {
    glong l;

    if ((l = a->tv_usec - b->tv_usec) >= 0) {
        result->tv_usec = l;
        result->tv_sec = a->tv_sec - b->tv_sec;
    } else {
        result->tv_usec = 1000000 + l;
        result->tv_sec = a->tv_sec - b->tv_sec - 1;
    }
}

void dhcp6_timer_init(void) {
    timer_list = NULL;
    tm_sentinel = tm_max;
}

dhcp6_timer_t *dhcp6_add_timer(dhcp6_timer_t *(*timeout) (void *),
                               void *timeodata) {
    dhcp6_timer_t *newtimer;

    if ((newtimer = g_malloc0(sizeof(*newtimer))) == NULL) {
        g_error("%s: can't allocate memory", __func__);
        return NULL;
    }

    if (timeout == NULL) {
        g_error("%s: timeout function unspecified", __func__);
        return NULL;
    }

    newtimer->expire = timeout;
    newtimer->expire_data = timeodata;
    newtimer->tm = tm_max;

    timer_list = g_slist_prepend(timer_list, newtimer);

    return newtimer;
}

void dhcp6_remove_timer(dhcp6_timer_t *timer) {
    timer->flag |= MARK_REMOVE;
}

void dhcp6_set_timer(struct timeval *tm, dhcp6_timer_t *timer) {
    struct timeval now;

    timer->flag |= MARK_CLEAR;
    /* reset the timer */
    gettimeofday(&now, NULL);

    _timeval_add(&now, tm, &timer->tm);

    /* update the next expiration time */
    if (is_timeval_lt(&timer->tm, &tm_sentinel)) {
        tm_sentinel = timer->tm;
    }

    return;
}

/*
 * Check expiration for each timer. If a timer is expired,
 * call the expire function for the timer and update the timer.
 * Return the next interval for select() call.
 */
struct timeval *dhcp6_check_timer(void) {
    static struct timeval returnval;
    struct timeval now;
    dhcp6_timer_t *tm = NULL;
    GSList *iterator = timer_list;

    tm_sentinel = tm_max;

    while (iterator) {
        tm = (dhcp6_timer_t *) iterator->data;

        gettimeofday(&now, NULL);

        if (tm->flag & MARK_REMOVE) {
            timer_list = g_slist_remove_all(timer_list, tm);
            g_free(tm);
            tm = NULL;
            continue;
        }

        if (is_timeval_leq(&tm->tm, &now)) {
            if ((*tm->expire) (tm->expire_data) == NULL) {
                continue;       /* timer has been freed */
            }
        }

        if (is_timeval_lt(&tm->tm, &tm_sentinel)) {
            tm_sentinel = tm->tm;
        }

        iterator = g_slist_next(iterator);
    }

    if (is_timeval_equal(&tm_max, &tm_sentinel)) {
        /* no need to timeout */
        return NULL;
    } else if (is_timeval_lt(&tm_sentinel, &now)) {
        /* this may occur when the interval is too small */
        returnval.tv_sec = returnval.tv_usec = 0;
    } else {
        timeval_sub(&tm_sentinel, &now, &returnval);
    }

    return &returnval;
}

struct timeval *dhcp6_timer_rest(dhcp6_timer_t *timer) {
    struct timeval now;
    static struct timeval returnval;    /* XXX */

    gettimeofday(&now, NULL);

    if (is_timeval_leq(&timer->tm, &now)) {
        g_debug("%s: a timer must be expired, but not yet", __func__);
        returnval.tv_sec = returnval.tv_usec = 0;
    } else {
        timeval_sub(&timer->tm, &now, &returnval);
    }

    return &returnval;
}

gboolean is_timeval_leq(struct timeval *a, struct timeval *b) {
    if ((a->tv_sec < b->tv_sec) ||
        ((a->tv_sec == b->tv_sec) && (a->tv_usec <= b->tv_usec))) {
        return TRUE;
    } else {
        return FALSE;
    }
}
