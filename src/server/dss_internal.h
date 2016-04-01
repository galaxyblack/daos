/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
 */

#include <daos_srv/daos_server.h>

struct dss_module_info {
	dtp_context_t	dmi_ctx;
};

extern struct dss_module_key	dss_module_key;

static inline struct dss_module_info *
dss_get_module_info(struct dss_thread_local_storage *dtc)
{
	struct dss_module_info *dmi;

	dmi = (struct dss_module_info *)
	      dss_module_key_get(dtc, &dss_module_key);
	return dmi;
}

/* module.cc */
int dss_module_init(void);
int dss_module_fini(bool force);
int dss_module_load(const char *modname);
int dss_module_unload(const char *modname);

/* rpc.cc */
int dss_rpc_register(struct dss_handler *hdlrs, int modid,
		     int server_only);
int dss_rpc_unregister(struct dss_handler *hdlrs);

/* srv.cc */
int dss_srv_init(void);
int dss_srv_fini();

/* tls.cc */
void dss_tls_fini(void *arg);
struct dss_thread_local_storage *dss_tls_init(int tag);