/**
 * (C) Copyright 2015-2019 Intel Corporation.
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

#ifndef __DAOS_DEBUG_H__
#define __DAOS_DEBUG_H__

#include <stdio.h>

#define DD_FAC(name)	daos_##name##_logfac
#ifndef D_LOGFAC
#define D_LOGFAC DD_FAC(daos)
#endif /* !D_LOGFAC */

#include <gurt/debug_setup.h>

/**
 * predefined debug facilities (subsystems/modules), they have to be declared
 * before including any libgurt headers
 */
#define DAOS_FOREACH_LOG_FAC(ACTION, arg)	\
	ACTION(daos,      daos,      arg)	\
	ACTION(addons,    addons,    arg)	\
	ACTION(common,    common,    arg)	\
	ACTION(tree,      tree,      arg)	\
	ACTION(vos,       vos,       arg)	\
	ACTION(client,    client,    arg)	\
	ACTION(server,    server,    arg)	\
	ACTION(rdb,       rdb,       arg)	\
	ACTION(rsvc,      rsvc,      arg)	\
	ACTION(pool,      pool,      arg)	\
	ACTION(container, container, arg)	\
	ACTION(object,    object,    arg)	\
	ACTION(placement, placement, arg)	\
	ACTION(rebuild,   rebuild,   arg)	\
	ACTION(mgmt,      mgmt,      arg)	\
	ACTION(bio,       bio,       arg)	\
	ACTION(tests,     tests,     arg)	\
	ACTION(dfs,       dfs,       arg)	\
	ACTION(drpc,      drpc,      arg)	\
	ACTION(security,  security,  arg)

#define DAOS_FOREACH_DB(ACTION, arg)				\
	/** metadata operation */				\
	ACTION(DB_MD,	   md,	    metadata,       0, arg)	\
	/** placement operation */				\
	ACTION(DB_PL,	   pl,	    placement,      0, arg)	\
	/** pool operation */					\
	ACTION(DB_MGMT,	   mgmt,    management,	    0, arg)	\
	/** epoch operation */					\
	ACTION(DB_EPC,	   epc,	    epoch,          0, arg)	\
	/** durable format operation */				\
	ACTION(DB_DF,	   df,	    durable_format, 0, arg)	\
	/** rebuild operation */				\
	ACTION(DB_REBUILD, rebuild, rebuild,	    0, arg)	\
	/** security check */					\
	ACTION(DB_SEC,	   sec,	    security,       0, arg)

DAOS_FOREACH_DB(D_LOG_DECLARE_DB, D_NOOP);
DAOS_FOREACH_LOG_FAC(D_LOG_DECLARE_FAC, DAOS_FOREACH_DB);

#include <gurt/debug.h>
#include <gurt/common.h>

#define DB_DEFAULT	DLOG_DBG
#define DB_NULL		0
/** XXX Temporary things, should be replaced by debug bits above */
#define DF_DSMC		DB_ANY
#define DF_DSMS		DB_ANY
#define DF_MISC		DB_ANY

/** initialize the debug system */
int  daos_debug_init(char *logfile);
/** finalize the debug system */
void daos_debug_fini(void);

#endif /* __DAOS_DEBUG_H__ */
