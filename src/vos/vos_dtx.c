/**
 * (C) Copyright 2019 Intel Corporation.
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
 * This file is part of daos two-phase commit transaction.
 *
 * vos/vos_dtx.c
 */
#define D_LOGFAC	DD_FAC(vos)

#include <daos_srv/vos.h>
#include "vos_layout.h"
#include "vos_internal.h"

#define DTX_RETURN_INPROGRESS(dtx, pos)					 \
do {									 \
	if ((dtx) != NULL)						 \
		D_DEBUG(DB_TRACE, "Hit uncommitted DTX "DF_DTI		 \
			" with state %u, time %lu at %d\n",		 \
			DP_DTI(&(dtx)->te_xid), (dtx)->te_state,	 \
			(dtx)->te_sec, (pos));				 \
	else								 \
		D_DEBUG(DB_TRACE, "Hit uncommitted DTX at %d\n", (pos)); \
									 \
	return -DER_INPROGRESS;						 \
} while (0)

struct dtx_rec_bundle {
	umem_id_t	trb_ummid;
};

static int
dtx_hkey_size(struct btr_instance *tins)
{
	return sizeof(struct daos_tx_id);
}

static void
dtx_hkey_gen(struct btr_instance *tins, daos_iov_t *key_iov, void *hkey)
{
	D_ASSERT(key_iov->iov_len == sizeof(struct daos_tx_id));

	memcpy(hkey, key_iov->iov_buf, key_iov->iov_len);
}

static int
dtx_hkey_cmp(struct btr_instance *tins, struct btr_record *rec, void *hkey)
{
	struct daos_tx_id	*hkey1 = (struct daos_tx_id *)&rec->rec_hkey[0];
	struct daos_tx_id	*hkey2 = (struct daos_tx_id *)hkey;
	int			 rc;

	rc = memcmp(hkey1, hkey2, sizeof(struct daos_tx_id));

	return dbtree_key_cmp_rc(rc);
}

static int
dtx_rec_alloc(struct btr_instance *tins, daos_iov_t *key_iov,
	      daos_iov_t *val_iov, struct btr_record *rec)
{
	struct dtx_rec_bundle	*rbund;

	rbund = (struct dtx_rec_bundle *)val_iov->iov_buf;
	D_ASSERT(!UMMID_IS_NULL(rbund->trb_ummid));

	/* Directly reference the input addreass (in SCM). */
	rec->rec_mmid = rbund->trb_ummid;
	return 0;
}

static int
dtx_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	umem_id_t	*ummid = (umem_id_t *)args;

	D_ASSERT(args != NULL);
	D_ASSERT(!UMMID_IS_NULL(rec->rec_mmid));

	/* Return the record addreass (in SCM). The caller will release it
	 * after using.
	 */
	*ummid = rec->rec_mmid;
	rec->rec_mmid = UMMID_NULL;
	return 0;
}

static int
dtx_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
	      daos_iov_t *key_iov, daos_iov_t *val_iov)
{
	struct vos_dtx_entry_df	*dtx;

	D_ASSERT(val_iov != NULL);

	dtx = umem_id2ptr(&tins->ti_umm, rec->rec_mmid);
	daos_iov_set(val_iov, dtx, sizeof(*dtx));
	return 0;
}

static int
dtx_rec_update(struct btr_instance *tins, struct btr_record *rec,
	       daos_iov_t *key, daos_iov_t *val)
{
	D_ASSERTF(0, "Should never been called\n");
	return 0;
}

static btr_ops_t dtx_btr_ops = {
	.to_hkey_size	= dtx_hkey_size,
	.to_hkey_gen	= dtx_hkey_gen,
	.to_hkey_cmp	= dtx_hkey_cmp,
	.to_rec_alloc	= dtx_rec_alloc,
	.to_rec_free	= dtx_rec_free,
	.to_rec_fetch	= dtx_rec_fetch,
	.to_rec_update	= dtx_rec_update,
};

#define DTX_BTREE_ORDER		20

int
vos_dtx_table_register(void)
{
	int	rc;

	D_DEBUG(DB_DF, "Registering DTX table class: %d\n", VOS_BTR_DTX_TABLE);

	rc = dbtree_class_register(VOS_BTR_DTX_TABLE, 0, &dtx_btr_ops);
	if (rc != 0)
		D_ERROR("Failed to register DTX dbtree: rc = %d\n", rc);

	return rc;
}

int
vos_dtx_table_create(struct vos_pool *pool, struct vos_dtx_table_df *dtab_df)
{
	daos_handle_t	hdl;
	int		rc;

	if (pool == NULL || dtab_df == NULL) {
		D_ERROR("Invalid handle\n");
		return -DER_INVAL;
	}

	D_ASSERT(dtab_df->tt_active_btr.tr_class == 0);
	D_ASSERT(dtab_df->tt_committed_btr.tr_class == 0);

	D_DEBUG(DB_DF, "create DTX dbtree in-place for pool "DF_UUID": %d\n",
		DP_UUID(pool->vp_id), VOS_BTR_DTX_TABLE);

	rc = dbtree_create_inplace(VOS_BTR_DTX_TABLE, 0,
				   DTX_BTREE_ORDER, &pool->vp_uma,
				   &dtab_df->tt_active_btr, &hdl);
	if (rc != 0) {
		D_ERROR("Failed to create DTX active dbtree for pool "
			DF_UUID": rc = %d\n", DP_UUID(pool->vp_id), rc);
		return rc;
	}

	dbtree_close(hdl);

	rc = dbtree_create_inplace(VOS_BTR_DTX_TABLE, 0,
				   DTX_BTREE_ORDER, &pool->vp_uma,
				   &dtab_df->tt_committed_btr, &hdl);
	if (rc != 0) {
		D_ERROR("Failed to create DTX committed dbtree for pool "
			DF_UUID": rc = %d\n", DP_UUID(pool->vp_id), rc);
		return rc;
	}

	dtab_df->tt_time_last_shrink = ABT_get_wtime();
	dtab_df->tt_count = 0;
	dtab_df->tt_entry_head = UMMID_NULL;
	dtab_df->tt_entry_tail = UMMID_NULL;

	dbtree_close(hdl);
	return 0;
}

int
vos_dtx_table_destroy(struct vos_pool *pool, struct vos_dtx_table_df *dtab_df)
{
	daos_handle_t	hdl;
	int		rc = 0;

	if (pool == NULL || dtab_df == NULL) {
		D_ERROR("Invalid handle\n");
		return -DER_INVAL;
	}

	if (dtab_df->tt_active_btr.tr_class != 0) {
		rc = dbtree_open_inplace(&dtab_df->tt_active_btr,
					 &pool->vp_uma, &hdl);
		if (rc == 0)
			rc = dbtree_destroy(hdl);

		if (rc != 0)
			D_ERROR("Fail to destroy DTX active dbtree for pool"
				DF_UUID": rc = %d\n", DP_UUID(pool->vp_id), rc);
	}

	if (dtab_df->tt_committed_btr.tr_class != 0) {
		rc = dbtree_open_inplace(&dtab_df->tt_committed_btr,
					 &pool->vp_uma, &hdl);
		if (rc == 0)
			rc = dbtree_destroy(hdl);

		if (rc != 0)
			D_ERROR("Fail to destroy DTX committed dbtree for pool"
				DF_UUID": rc = %d\n", DP_UUID(pool->vp_id), rc);
	}

	return rc;
}

static int
dtx_rec_release(struct umem_instance *umm, umem_id_t ummid,
		bool abort, bool destroy)
{
	struct vos_dtx_entry_df		*dtx;

	dtx = umem_id2ptr(umm, ummid);
	/* The caller has started the PMDK transaction. */
	umem_tx_add_ptr(umm, dtx, sizeof(*dtx));

	while (!UMMID_IS_NULL(dtx->te_records)) {
		umem_id_t			 rec_mmid = dtx->te_records;
		struct vos_dtx_record_df	*rec;

		rec = umem_id2ptr(umm, rec_mmid);
		switch (rec->tr_type) {
		case DTX_RT_OBJ: {
			struct vos_obj_df	*obj;

			obj = umem_id2ptr(umm, rec->tr_record);
			umem_tx_add_ptr(umm, obj, sizeof(*obj));
			obj->vo_dtx_shares--;

			/* Other DTX that shares the object has committed. */
			if (UMMID_IS_NULL(obj->vo_dtx)) {
				if (dtx->te_epoch > obj->vo_latest)
					obj->vo_latest = dtx->te_epoch;
				if (dtx->te_epoch < obj->vo_earliest)
					obj->vo_earliest = dtx->te_epoch;

				break;
			}

			/* commit case */
			if (!abort) {
				obj->vo_dtx = UMMID_NULL;
				obj->vo_latest = dtx->te_epoch;
				obj->vo_earliest = dtx->te_epoch;
				break;
			}

			/* The last shared DTX is aborted. */
			if (obj->vo_dtx_shares == 0) {
				obj->vo_dtx = DTX_UMMID_ABORTED;
				break;
			}

			/* I am not the original DTX that create the object. */
			if (!umem_id_equal(umm, obj->vo_dtx, ummid))
				break;

			/* I am the original DTX that create the object that
			 * is still shared by others. Now, I will be aborted,
			 * set the reference as UNKNOWN for other left shares.
			 */
			obj->vo_dtx = DTX_UMMID_UNKNOWN;
			break;
		}
		case DTX_RT_KEY: {
			struct vos_krec_df	*key;

			key = umem_id2ptr(umm, rec->tr_record);
			umem_tx_add_ptr(umm, key, sizeof(*key));
			key->kr_dtx_shares--;

			if (UMMID_IS_NULL(key->kr_dtx)) {
				/* The exchange_src has been committed.
				 * I am the exchange_tgt, do nothing.
				 */
				if (rec->tr_flags == DTX_RF_EXCHANGE_TGT)
					break;

				/* Other DTX that shares the key has
				 * committed, must be for sharing of
				 * update cases.
				 */
				if (dtx->te_epoch > key->kr_latest)
					key->kr_latest = dtx->te_epoch;
				if (dtx->te_epoch < key->kr_earliest)
					key->kr_earliest = dtx->te_epoch;

				break;
			}

			/* commit case */
			if (!abort) {
				key->kr_dtx = UMMID_NULL;
				key->kr_latest = dtx->te_epoch;
				if (rec->tr_flags != DTX_RF_EXCHANGE_SRC &&
				    rec->tr_flags != DTX_RF_EXCHANGE_TGT)
					key->kr_earliest = dtx->te_epoch;

				goto exchange;
			}

			/* The last shared DTX is aborted. */
			if (key->kr_dtx_shares == 0) {
				key->kr_dtx = DTX_UMMID_ABORTED;
				goto exchange;
			}

			/* I am not the original DTX that create the key. */
			if (!umem_id_equal(umm, key->kr_dtx, ummid))
				goto exchange;

			/* I am the original DTX that create the key that
			 * is still shared by others. Now, I will be aborted,
			 * set the reference as UNKNOWN for other left shares.
			 */
			key->kr_dtx = DTX_UMMID_UNKNOWN;

exchange:
			if (rec->tr_flags != DTX_RF_EXCHANGE_SRC)
				break;

			D_ASSERT(key->kr_flags & VKF_DELETED);

			if (!abort) { /* commit case */
				struct vos_dtx_record_df	*tgt_rec;
				struct vos_krec_df		*tgt_key;
				struct btr_root			 tmp_btr;
				daos_epoch_t			 tmp_epoch;

				/*
				 * XXX: If the exchange target still exist,
				 *	it will be the next record. If it does
				 *	not exist, then either it is crashed
				 *	or it has already degistered from the
				 *	DTX records list. We cannot commit the
				 *	DTX under any the two cases. Failing
				 *	the DTX commit is meaningless, then we
				 *	have to give some warning message.
				 */
				if (UMMID_IS_NULL(rec->tr_next)) {
					D_ERROR(DF_UOID" miss the DTX ("DF_DTI
						") exchange pairs (1)\n",
						DP_UOID(dtx->te_oid),
						DP_DTI(&dtx->te_xid));
					break;
				}

				tgt_rec = umem_id2ptr(umm, rec->tr_next);
				if (tgt_rec->tr_flags != DTX_RF_EXCHANGE_TGT) {
					D_ERROR(DF_UOID" miss the DTX ("DF_DTI
						") exchange pairs (2)\n",
						DP_UOID(dtx->te_oid),
						DP_DTI(&dtx->te_xid));
					break;
				}

				/* Exchange the sub-tree between max epoch
				 * record and the give epoch record. The
				 * record with max epoch will be removed
				 * when aggregation.
				 */
				tgt_key = umem_id2ptr(umm, tgt_rec->tr_record);
				umem_tx_add_ptr(umm, tgt_key, sizeof(*tgt_key));

				tmp_btr = key->kr_btr;
				key->kr_btr = tgt_key->kr_btr;
				tgt_key->kr_btr = tmp_btr;

				if (key->kr_bmap & KREC_BF_EVT) {
					struct evt_root		tmp_evt;

					umem_tx_add_ptr(umm, &key->kr_evt[0],
							sizeof(key->kr_evt[0]));
					umem_tx_add_ptr(umm,
						&tgt_key->kr_evt[0],
						sizeof(tgt_key->kr_evt[0]));

					tmp_evt = key->kr_evt[0];
					key->kr_evt[0] = tgt_key->kr_evt[0];
					tgt_key->kr_evt[0] = tmp_evt;
				}

				tmp_epoch = key->kr_earliest;
				key->kr_earliest = tgt_key->kr_earliest;
				tgt_key->kr_earliest = tmp_epoch;

				D_DEBUG(DB_TRACE, "Exchanged DTX records for "
					DF_DTI"\n", DP_DTI(&dtx->te_xid));
			} else { /* abort case */
				/* Recover the visibility of the exchange source
				 * for the DTX abort case.
				 */
				key->kr_flags &= ~VKF_DELETED;
				key->kr_dtx = UMMID_NULL;
			}
			break;
		}
		case DTX_RT_SVT: {
			struct vos_irec_df	*svt;

			svt = umem_id2ptr(umm, rec->tr_record);
			umem_tx_add_ptr(umm, svt, sizeof(*svt));
			if (abort)
				svt->ir_dtx = DTX_UMMID_ABORTED;
			else
				svt->ir_dtx = UMMID_NULL;
			break;
		}
		case DTX_RT_EVT: {
			struct evt_desc		*evt;

			evt = umem_id2ptr(umm, rec->tr_record);
			umem_tx_add_ptr(umm, evt, sizeof(*evt));
			if (abort)
				evt->dc_dtx = DTX_UMMID_ABORTED;
			else
				evt->dc_dtx = UMMID_NULL;
			break;
		}
		default:
			D_ERROR(DF_UOID" unknown DTX "DF_DTI" type %u\n",
				DP_UOID(dtx->te_oid), DP_DTI(&dtx->te_xid),
				rec->tr_type);
			break;
		}

		dtx->te_records = rec->tr_next;
		umem_tx_add_ptr(umm, rec, sizeof(*rec));
		umem_free(umm, rec_mmid);
	}

	D_DEBUG(DB_TRACE, "dtx_rec_release: %s/%s the DTX "DF_DTI"\n",
		abort ? "abort" : "commit", destroy ? "destroy" : "keep",
		DP_DTI(&dtx->te_xid));

	if (destroy)
		umem_free(umm, ummid);
	else
		dtx->te_flags &= ~(DTX_EF_EXCHANGE_PENDING  | DTX_EF_SHARES);

	return 0;
}

static void
vos_dtx_unlink_entry(struct umem_instance *umm, struct vos_dtx_table_df *tab,
		     struct vos_dtx_entry_df *dtx)
{
	struct vos_dtx_entry_df	*ent;

	if (UMMID_IS_NULL(dtx->te_next)) { /* The tail of the DTXs list. */
		if (UMMID_IS_NULL(dtx->te_prev)) { /* The unique one on list. */
			tab->tt_entry_head = UMMID_NULL;
			tab->tt_entry_tail = UMMID_NULL;
		} else {
			ent = umem_id2ptr(umm, dtx->te_prev);
			umem_tx_add_ptr(umm, ent, sizeof(*ent));

			ent->te_next = UMMID_NULL;
			tab->tt_entry_tail = dtx->te_prev;
			dtx->te_prev = UMMID_NULL;
		}
	} else if (UMMID_IS_NULL(dtx->te_prev)) { /* The head of DTXs list */
		ent = umem_id2ptr(umm, dtx->te_next);
		umem_tx_add_ptr(umm, ent, sizeof(*ent));

		ent->te_prev = UMMID_NULL;
		tab->tt_entry_head = dtx->te_next;
		dtx->te_next = UMMID_NULL;
	} else {
		ent = umem_id2ptr(umm, dtx->te_next);
		umem_tx_add_ptr(umm, ent, sizeof(*ent));
		ent->te_prev = dtx->te_prev;

		ent = umem_id2ptr(umm, dtx->te_prev);
		umem_tx_add_ptr(umm, ent, sizeof(*ent));
		ent->te_next = dtx->te_next;

		dtx->te_prev = UMMID_NULL;
		dtx->te_next = UMMID_NULL;
	}
}

static int
vos_dtx_commit_one(struct vos_container *cont, struct daos_tx_id *dti)
{
	struct umem_instance	*umm;
	daos_iov_t		 kiov;
	daos_iov_t		 riov;
	umem_id_t		 ummid;
	int			 rc = 0;

	umm = &cont->vc_pool->vp_umm;
	daos_iov_set(&kiov, dti, sizeof(*dti));
	rc = dbtree_delete(cont->vc_dtx_active_hdl, &kiov, &ummid);
	if (rc == 0) {
		struct vos_dtx_entry_df		*dtx;
		struct vos_dtx_entry_df		*ent;
		struct dtx_rec_bundle		 rbund;

		dtx = umem_id2ptr(umm, ummid);
		umem_tx_add_ptr(umm, dtx, sizeof(*dtx));
		dtx->te_state = DTX_ST_COMMITTED;

		if (dtx->te_dkey_hash[0] != 0)
			vos_dtx_del_cos(cont, &dtx->te_oid, dti,
					dtx->te_dkey_hash[0],
					dtx->te_intent == DAOS_INTENT_PUNCH ?
					true : false);

		rbund.trb_ummid = ummid;
		daos_iov_set(&riov, &rbund, sizeof(rbund));
		rc = dbtree_upsert(cont->vc_dtx_committed_hdl,
				BTR_PROBE_EQ, DAOS_INTENT_UPDATE, &kiov, &riov);
		if (rc == 0) {
			struct vos_dtx_table_df	*tab;

			tab = &cont->vc_cont_df->cd_dtx_table_df;
			umem_tx_add_ptr(umm, tab, sizeof(*tab));

			tab->tt_count++;
			if (UMMID_IS_NULL(tab->tt_entry_tail)) {
				D_ASSERT(UMMID_IS_NULL(tab->tt_entry_head));

				tab->tt_entry_head = ummid;
				tab->tt_entry_tail = tab->tt_entry_head;
			} else {
				ent = umem_id2ptr(umm, tab->tt_entry_tail);
				umem_tx_add_ptr(umm, ent, sizeof(*ent));

				ent->te_next = ummid;
				dtx->te_prev = tab->tt_entry_tail;
				tab->tt_entry_tail = ent->te_next;
			}

			/* If there are pending exchange of records, then we
			 * need some additional work when commit, such as
			 * exchange the subtree under the source and target
			 * records, then related subtree can be exported
			 * correctly. That will be done when release related
			 * vos_dth_record_df(s) attached to the DTX.
			 */
			if (dtx->te_flags &
			    (DTX_EF_EXCHANGE_PENDING | DTX_EF_SHARES))
				rc = dtx_rec_release(umm, ummid, false, false);
		}
	} else if (rc == -DER_NONEXIST) {
		daos_iov_set(&riov, NULL, 0);
		rc = dbtree_lookup(cont->vc_dtx_committed_hdl, &kiov, &riov);
	}

	D_DEBUG(DB_TRACE, "Commit the DTX "DF_DTI": rc = %d\n",
		DP_DTI(dti), rc);

	return rc;
}

static int
vos_dtx_abort_one(struct vos_container *cont, struct daos_tx_id *dti,
		  bool force)
{
	daos_iov_t	 kiov;
	umem_id_t	 dtx;
	int		 rc;

	daos_iov_set(&kiov, dti, sizeof(*dti));
	rc = dbtree_delete(cont->vc_dtx_active_hdl, &kiov, &dtx);
	if (rc == 0)
		rc = dtx_rec_release(&cont->vc_pool->vp_umm, dtx, true, true);

	D_DEBUG(DB_TRACE, "Abort the DTX "DF_DTI": rc = %d\n", DP_DTI(dti), rc);

	if (rc != 0 && force)
		rc = 0;

	return rc;
}

int
vos_dtx_handle_resend(daos_handle_t coh, struct daos_tx_id *dti)
{
	struct vos_container	*cont;
	struct vos_dtx_entry_df	*dtx;
	daos_iov_t		 kiov;
	daos_iov_t		 riov;
	int			 rc;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	daos_iov_set(&kiov, dti, sizeof(*dti));
	daos_iov_set(&riov, NULL, 0);
	rc = dbtree_lookup(cont->vc_dtx_active_hdl, &kiov, &riov);
	if (rc == -DER_NONEXIST)
		rc = dbtree_lookup(cont->vc_dtx_committed_hdl, &kiov, &riov);

	if (rc == -DER_NONEXIST) {
		/* If the resent RPC is too old, then we cannot know whether
		 * the RPC has ever been executed before or not. Then return
		 * -DER_TIMEDOUT to the RPC sponsor. Means that modification
		 * for such RPC may have been executed, but nobody guarantee
		 * that. It is the caller's duty to check related record(s).
		 */
		if ((uint64_t)ABT_get_wtime() - dti->dti_sec >
		    DTX_AGGREGATION_THRESHOLD_TIME) {
			D_DEBUG(DB_IO, "Not sure about whether the RPC "
				DF_DTI" is resend or not.\n", DP_DTI(dti));
			return -DER_TIMEDOUT;
		}
	}

	if (rc != 0)
		return rc;

	dtx = (struct vos_dtx_entry_df *)riov.iov_buf;
	switch (dtx->te_state) {
	case DTX_ST_INIT:
		/* New DTX after the leader (re)start that is not
		 * completed yet, retry related RPC some time later.
		 */
		if (dtx->te_sec >= (uint64_t)vos_start_time)
			DTX_RETURN_INPROGRESS(dtx, 1);

		/* The INIT DTX in PRAM must be for an in-update object/key
		 * that was waiting for the bulk transfer (or remote abort)
		 * before the VOS restart. So here, it should be the case
		 * that client resends RPC after server restart.
		 */
		return 0;
	case DTX_ST_PREPARED:
		return 0;
	case DTX_ST_COMMITTED:
		return -DER_ALREADY;
	default:
		return -DER_INVAL;
	}
}

int
vos_dtx_begin(struct daos_tx_id *dti, daos_unit_oid_t *oid, daos_key_t *dkey,
	      daos_handle_t coh, daos_epoch_t epoch, uint32_t pm_ver,
	      uint32_t intent, uint32_t flags, struct daos_tx_handle **dtx)
{
	struct daos_tx_handle	*th;

	D_ALLOC_PTR(th);
	if (th == NULL)
		return -DER_NOMEM;

	th->dth_xid = *dti;
	th->dth_oid = *oid;
	th->dth_dkey = dkey;
	th->dth_coh = coh;
	th->dth_epoch = epoch;
	D_INIT_LIST_HEAD(&th->dth_shares);
	th->dth_ver = pm_ver;
	th->dth_intent = intent;
	th->dth_flags = flags;
	th->dth_sync = 0;
	th->dth_non_rep = 0;
	th->dth_ent = UMMID_NULL;

	*dtx = th;
	D_DEBUG(DB_TRACE, "Start the DTX "DF_DTI"\n", DP_DTI(dti));

	return 0;
}

int
vos_dtx_end(struct daos_tx_handle *dth, int result, bool leader)
{
	struct vos_container	*cont;
	struct vos_dtx_table_df	*tab;
	double			 now = ABT_get_wtime();
	int			 rc = 0;

	if (result < 0)
		D_GOTO(out, rc = result);

	cont = vos_hdl2cont(dth->dth_coh);
	D_ASSERT(cont != NULL);

	if (leader) {
		if (dth->dth_sync)
			D_GOTO(out, rc = 3);

		if (cont->vc_dtx_committable_count > DTX_THRESHOLD_COUNT)
			D_GOTO(out, rc = 2);

		if ((uint64_t)now - cont->vc_dtx_time_last_commit >
		     DTX_COMMIT_THRESHOLD_TIME)
			D_GOTO(out, rc = 2);
	}

	tab = &cont->vc_cont_df->cd_dtx_table_df;
	if (tab->tt_count > DTX_AGGREGATION_THRESHOLD_COUNT)
		D_GOTO(out, rc = 1);

	if ((uint64_t)now - tab->tt_time_last_shrink >
	    DTX_AGGREGATION_THRESHOLD_TIME)
		D_GOTO(out, rc = 1);

out:
	D_DEBUG(DB_TRACE,
		"Stop the DTX "DF_DTI" ver = %u, %s, %s, %s: rc = %d\n",
		DP_DTI(&dth->dth_xid), dth->dth_ver,
		dth->dth_sync ? "sync" : "async",
		dth->dth_non_rep ? "non-replicated" : "replicated",
		dth->dth_leader ? "leader" : "non-leader", rc);
	D_FREE_PTR(dth);
	return rc;
}

int
vos_dtx_check_committable(daos_handle_t coh, struct daos_tx_id *dti)
{
	struct vos_container	*cont;
	daos_iov_t		 kiov;
	daos_iov_t		 riov;
	int			 rc = 0;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	daos_iov_set(&kiov, dti, sizeof(*dti));
	daos_iov_set(&riov, NULL, 0);
	rc = dbtree_lookup(cont->vc_dtx_active_hdl, &kiov, &riov);
	if (rc == 0) {
		struct vos_dtx_entry_df	*dtx;

		dtx = (struct vos_dtx_entry_df *)riov.iov_buf;
		return dtx->te_state;
	}

	if (rc == -DER_NONEXIST) {
		rc = dbtree_lookup(cont->vc_dtx_committed_hdl, &kiov, &riov);
		if (rc == 0)
			return DTX_ST_COMMITTED;
	}

	if (rc == -DER_NONEXIST)
		return DTX_ST_INIT;

	return rc;
}

int
vos_dtx_commit(daos_handle_t coh, struct daos_tx_id *dti, int count)
{
	struct vos_container	*cont;
	struct umem_instance	*umm;
	int			 i;
	int			 rc = 0;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	umm = &cont->vc_pool->vp_umm;
	/* Commit multiple DTXs via single PMDK transaction. */
	rc = umem_tx_begin(umm, vos_txd_get());
	if (rc == 0) {
		for (i = 0; i < count; i++)
			vos_dtx_commit_one(cont, &dti[i]);

		cont->vc_dtx_time_last_commit = ABT_get_wtime();
		umem_tx_commit(umm);
	}

	return rc;
}

int
vos_dtx_abort(daos_handle_t coh, struct daos_tx_id *dti, int count, bool force)
{
	struct vos_container	*cont;
	struct umem_instance	*umm;
	int			 rc;
	int			 i;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	umm = &cont->vc_pool->vp_umm;
	/* Abort multiple DTXs via single PMDK transaction. */
	rc = umem_tx_begin(umm, vos_txd_get());
	for (i = 0; rc == 0 && i < count; i++)
		rc = vos_dtx_abort_one(cont, &dti[i], force);

	if (rc == 0)
		umem_tx_commit(umm);
	else
		umem_tx_abort(umm, rc);

	return rc;
}

void
vos_dtx_aggregate(daos_handle_t coh, uint64_t max, uint64_t age)
{
	struct vos_container		*cont;
	struct umem_instance		*umm;
	struct vos_dtx_table_df		*tab;
	umem_id_t			 dtx_mmid;
	daos_iov_t			 kiov;
	uint64_t			 count;
	double				 now;
	int				 rc = 0;
	bool				 stop = false;

	if (max == 0)
		max = DTX_AGGREGATION_THRESHOLD_COUNT;

	now = ABT_get_wtime();
	if (age == 0)
		age = now - DTX_AGGREGATION_THRESHOLD_TIME;
	else if (age != (uint64_t)(-1))
		age = now - age;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	umm = &cont->vc_pool->vp_umm;
	tab = &cont->vc_cont_df->cd_dtx_table_df;

	for (count = 0, dtx_mmid = tab->tt_entry_head;
	     count < max && !UMMID_IS_NULL(dtx_mmid);) {
		int	i;

		rc = umem_tx_begin(umm, vos_txd_get());
		if (rc != 0)
			return;

		umem_tx_add_ptr(umm, tab, sizeof(*tab));
		for (i = 0; i < DTX_AGGREGATION_YIELD_INTERVAL &&
		     !UMMID_IS_NULL(dtx_mmid) && count < max; i++, count++) {
			struct vos_dtx_entry_df	*dtx;
			umem_id_t		 ummid;

			dtx = umem_id2ptr(umm, dtx_mmid);
			if (dtx->te_sec > age) {
				stop = true;
				break;
			}

			umem_tx_add_ptr(umm, dtx, sizeof(*dtx));
			daos_iov_set(&kiov, &dtx->te_xid, sizeof(dtx->te_xid));
			rc = dbtree_delete(cont->vc_dtx_committed_hdl, &kiov,
					   &ummid);
			D_ASSERT(rc == 0);

			tab->tt_count--;
			dtx_mmid = dtx->te_next;
			vos_dtx_unlink_entry(umm, tab, dtx);
			dtx_rec_release(umm, ummid, false, true);
		}

		tab->tt_time_last_shrink = now;
		umem_tx_commit(umm);

		if (stop)
			return;

		/* Yield per DTX_AGGREGATION_YIELD_INTERVAL. */
		ABT_thread_yield();
	}
}