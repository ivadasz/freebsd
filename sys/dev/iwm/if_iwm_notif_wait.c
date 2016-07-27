/*-
 * Based on BSD-licensed source modules in the Linux iwlwifi driver,
 * which were used as the reference documentation for this implementation.
 *
 ******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2007 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2015 Intel Deutschland GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2014 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/queue.h>

#include "if_iwm_notif_wait.h"

struct iwm_notif_wait_data {
	struct mtx mtx;
	STAILQ_HEAD(, iwm_notification_wait) list;
};

struct iwm_notif_wait_data *
iwm_notification_wait_init(void)
{
	struct iwm_notif_wait_data *data;

	data = malloc(sizeof(*data), M_DEVBUF, M_WAITOK | M_ZERO);
	if (data != NULL) {
		mtx_init(&data->mtx, "iwm_notif", NULL, MTX_DEF);
		STAILQ_INIT(&data->list);
	}

	return data;
}

void
iwm_notification_wait_free(struct iwm_notif_wait_data *notif_data)
{
	KASSERT(STAILQ_EMPTY(&notif_data->list), ("notif queue not empty"));
	mtx_destroy(&notif_data->mtx);
	free(notif_data, M_DEVBUF);
}

void
iwm_notification_wait_notify(struct iwm_notif_wait_data *notif_data,
    uint16_t cmd)
{
	struct iwm_notification_wait *wait_entry;
	int i;

	mtx_lock(&notif_data->mtx);
	STAILQ_FOREACH(wait_entry, &notif_data->list, entry) {
		for (i = 0; i < wait_entry->n_cmds; i++) {
			if (cmd == wait_entry->cmds[i] &&
			    !wait_entry->triggered && !wait_entry->aborted) {
				wait_entry->triggered = 1;
				wakeup(wait_entry);
			}
		}
	}
	mtx_unlock(&notif_data->mtx);
}

void
iwm_abort_notification_waits(struct iwm_notif_wait_data *notif_data)
{
	struct iwm_notification_wait *wait_entry;

	mtx_lock(&notif_data->mtx);
	STAILQ_FOREACH(wait_entry, &notif_data->list, entry) {
		wait_entry->aborted = 1;
		wakeup(wait_entry);
	}
	mtx_unlock(&notif_data->mtx);
}

void
iwm_init_notification_wait(struct iwm_notif_wait_data *notif_data,
    struct iwm_notification_wait *wait_entry, const uint16_t *cmds, int n_cmds)
{
	int i;

	KASSERT(n_cmds <= IWM_MAX_NOTIF_CMDS, ("too many notif values"));
	for (i = 0; i < n_cmds; i++)
		wait_entry->cmds[i] = cmds[i];
	wait_entry->n_cmds = n_cmds;
	wait_entry->triggered = 0;
	wait_entry->aborted = 0;

	mtx_lock(&notif_data->mtx);
	STAILQ_INSERT_TAIL(&notif_data->list, wait_entry, entry);
	mtx_unlock(&notif_data->mtx);
}

int
iwm_wait_notification(struct iwm_notif_wait_data *notif_data,
    struct iwm_notification_wait *wait_entry, int timeout)
{
	int ret = 0;

	mtx_lock(&notif_data->mtx);
	if (!wait_entry->triggered && !wait_entry->aborted) {
		ret = msleep(wait_entry, &notif_data->mtx, 0, "iwm_notif",
		    timeout);
	}
	STAILQ_REMOVE(&notif_data->list, wait_entry, iwm_notification_wait,
	    entry);
	mtx_unlock(&notif_data->mtx);

	return ret;
}

void
iwm_remove_notification(struct iwm_notif_wait_data *notif_data,
    struct iwm_notification_wait *wait_entry)
{
	mtx_lock(&notif_data->mtx);
	STAILQ_REMOVE(&notif_data->list, wait_entry, iwm_notification_wait,
	    entry);
	mtx_unlock(&notif_data->mtx);
}
