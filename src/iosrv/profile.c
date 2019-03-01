/**
 * (C) Copyright 2016-2019 Intel Corporation.
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
 * This file is part of the DAOS server. It implements the DAOS server profile
 * API.
 */
#define D_LOGFAC       DD_FAC(server)

#include <abt.h>
#include <daos/common.h>
#include <daos/event.h>
#include <daos_errno.h>
#include <daos_srv/bio.h>
#include <daos_srv/smd.h>
#include <gurt/list.h>
#include "drpc_internal.h"
#include "srv_internal.h"

#define DEFAULT_CHUNK_SIZE  10240
struct srv_profile_chunk *
srv_profile_chunk_alloc(int chunk_size)
{
	struct srv_profile_chunk *chunk;

	D_ALLOC_PTR(chunk);
	if (chunk == NULL)
		return NULL;

	D_INIT_LIST_HEAD(&chunk->spc_chunk_list);

	D_ALLOC(chunk->spc_profiles,
		chunk_size * sizeof(*chunk->spc_profiles));
	if (chunk->spc_profiles == NULL) {
		D_FREE_PTR(chunk);
		return NULL;
	}

	chunk->spc_chunk_size = chunk_size;
	chunk->spc_idx = 0;
	return chunk;
}

void
srv_profile_chunk_destroy(struct srv_profile_chunk *chunk)
{
	d_list_del(&chunk->spc_chunk_list);
	D_FREE(chunk->spc_profiles);
	D_FREE_PTR(chunk);
}

struct srv_profile *
srv_profile_alloc()
{
	struct srv_profile	*sp;

	D_ALLOC_PTR(sp);
	if (sp)
		D_INIT_LIST_HEAD(&sp->sp_list);

	return sp;
}

void
srv_profile_destroy(struct srv_profile *sp)
{
	struct srv_profile_chunk *spc;
	struct srv_profile_chunk *tmp;

	d_list_for_each_entry_safe(spc, tmp, &sp->sp_list,
				      spc_chunk_list) {
		srv_profile_chunk_destroy(spc);
	}

	if (sp->sp_dir_path)
		D_FREE(sp->sp_dir_path);

	D_FREE_PTR(sp);
}

static int
srv_profile_alloc_new_chunk(struct srv_profile *sp)
{
	struct srv_profile_chunk *chunk;

	chunk = srv_profile_chunk_alloc(DEFAULT_CHUNK_SIZE);
	if (chunk == NULL)
		return -DER_NOMEM;

	d_list_add_tail(&chunk->spc_chunk_list, &sp->sp_list);
	sp->sp_current_chunk = chunk;

	return 0;
}

int
srv_profile_start(struct srv_profile **sp_p, char *path, char **names)
{
	struct srv_profile *sp;
	int rc;

	sp = srv_profile_alloc();
	if (sp == NULL)
		return -DER_NOMEM;

	if (path != NULL) {
		D_ALLOC(sp->sp_dir_path, strlen(path) + 1);
		if (sp->sp_dir_path != NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		strcpy(sp->sp_dir_path, path); 
	}

	rc = srv_profile_alloc_new_chunk(sp);
	if (rc)
		D_GOTO(out, rc);

	sp->sp_names = names;
	*sp_p = sp;
out:
	if (rc && sp != NULL)
		srv_profile_destroy(sp);

	return rc;
}

int
srv_profile_count(struct srv_profile *sp, int id, int time)
{
	struct srv_profile_chunk *current = sp->sp_current_chunk;
	int			 rc;

	D_ASSERT(sp != NULL);
	D_ASSERT(sp->sp_current_chunk != NULL);
	current = sp->sp_current_chunk;

	if (current->spc_idx == current->spc_chunk_size) {
		rc = srv_profile_alloc_new_chunk(sp);
		if (rc)
			return rc;
		current = sp->sp_current_chunk;
	}

	current->spc_profiles[current->spc_idx].pro_time = time;
	current->spc_profiles[current->spc_idx].pro_id = id;
	current->spc_idx++;

	return 0;
}

static void 
srv_profile_dump(void *arg)
{
	struct srv_profile *sp = arg;
	struct srv_profile_chunk *spc;
	struct srv_profile_chunk *tmp;
	d_rank_t	rank;
	FILE		*file;
	int		tgt_id;	
	char		name[64];
	char		*path;
	int		rc;

	tgt_id = dss_get_module_info()->dmi_xs_id;	
	rc = crt_group_rank(NULL, &rank);
	if (rc)
		return;

	if (sp->sp_dir_path) {
		D_ALLOC(path, strlen(sp->sp_dir_path) + 64);
		if (path == NULL)
			return;
		sprintf(name, "/profile-%d-%d.dump", rank, tgt_id);
		strcpy(path, sp->sp_dir_path);
		strcat(path, name);
	} else {
		sprintf(name, "./profile-%d-%d.dump", rank, tgt_id);
		path = name;
	}
 
	file = fopen(path, "a"); 
	if (file == NULL) {
		rc = daos_errno2der(errno);
		D_ERROR("%s: %s\n", path, strerror(errno));
		goto out;
	}

	d_list_for_each_entry_safe(spc, tmp, &sp->sp_list, spc_chunk_list) {
		int i;

		for (i = 0; i < spc->spc_idx; i++) {
			char string[64];
			int id = spc->spc_profiles[i].pro_id;

			/* Dump name and time cost to the file */
			sprintf(string, "%s %d\n", sp->sp_names[id],
				spc->spc_profiles[i].pro_time);
			fwrite(string, strlen(string), 1, file);
		}
		srv_profile_chunk_destroy(spc);
	}

	srv_profile_destroy(sp);
out:
	if (path != name)
		free(path);
}

int
srv_profile_stop(struct srv_profile *sp)
{
	return dss_ult_create(srv_profile_dump, sp, DSS_ULT_SELF,
			      dss_get_module_info()->dmi_xs_id, 0 , NULL);
}
