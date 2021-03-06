/**
 * (C) Copyright 2017 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * ds_pool: Pool Server Storage Layout Definitions
 */
#define D_LOGFAC	DD_FAC(pool)

#include <daos_srv/rdb.h>
#include "srv_layout.h"

/** Root KVS */
RDB_STRING_KEY(ds_pool_prop_, uid);
RDB_STRING_KEY(ds_pool_prop_, gid);
RDB_STRING_KEY(ds_pool_prop_, mode);
RDB_STRING_KEY(ds_pool_prop_, map_version);
RDB_STRING_KEY(ds_pool_prop_, map_buffer);
RDB_STRING_KEY(ds_pool_prop_, map_uuids);
RDB_STRING_KEY(ds_pool_prop_, label);
RDB_STRING_KEY(ds_pool_prop_, space_rb);
RDB_STRING_KEY(ds_pool_prop_, self_heal);
RDB_STRING_KEY(ds_pool_prop_, reclaim);
RDB_STRING_KEY(ds_pool_prop_, nhandles);

/** pool handle KVS */
RDB_STRING_KEY(ds_pool_prop_, handles);

/** user attributed KVS */
RDB_STRING_KEY(ds_pool_attr_, user);

/** default properties, should cover all optional pool properties */
#define POOL_PROP_NUM	(DAOS_PROP_PO_MAX - DAOS_PROP_PO_MIN - 1)
struct daos_prop_entry pool_prop_entries_default[POOL_PROP_NUM] = {
	{
		.dpe_type	= DAOS_PROP_PO_LABEL,
		.dpe_str	= "pool label not set",
	}, {
		.dpe_type	= DAOS_PROP_PO_SPACE_RB,
		.dpe_val	= 0,
	}, {
		.dpe_type	= DAOS_PROP_PO_SELF_HEAL,
		.dpe_val	= DAOS_SELF_HEAL_AUTO_EXCLUDE |
				  DAOS_SELF_HEAL_AUTO_REBUILD,
	}, {
		.dpe_type	= DAOS_PROP_PO_RECLAIM,
		.dpe_val	= DAOS_RECLAIM_SNAPSHOT,
	}, {
		.dpe_type	= DAOS_PROP_PO_ACL,
		.dpe_val_ptr	= NULL,
	}
};

daos_prop_t pool_prop_default = {
	.dpp_nr		= POOL_PROP_NUM,
	.dpp_entries	= pool_prop_entries_default,
};
