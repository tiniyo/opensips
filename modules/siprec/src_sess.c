/*
 * Copyright (C) 2017 OpenSIPS Project
 *
 * This file is part of opensips, a free SIP server.
 *
 * opensips is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * opensips is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *
 * History:
 * ---------
 *  2017-06-20  created (razvanc)
 */

#include "src_sess.h"
#include "srs_body.h"

struct tm_binds srec_tm;
struct dlg_binds srec_dlg;
static str srec_dlg_name = str_init("siprecX_ctx");

struct src_sess *src_create_session(str *srs, str *rtp, str *grp)
{
	struct src_sess *ss = shm_malloc(sizeof *ss +
			(rtp ? rtp->len : 0) + (grp ? grp->len : 0));
	if (!ss) {
		LM_ERR("not enough memory for creating siprec session!\n");
		return NULL;
	}
	memset(ss, 0, sizeof *ss);
	/* uri might be changed, so we store it separately */
	ss->srs_uri.s = shm_malloc(srs->len);
	if (!ss->srs_uri.s) {
		LM_ERR("not enough memory for siprec uri!\n");
		shm_free(ss);
		return NULL;
	}
	if (rtp) {
		ss->rtpproxy.s = (char *)(ss + 1);
		memcpy(ss->rtpproxy.s, rtp->s, rtp->len);
		ss->rtpproxy.len = rtp->len;
	}

	if (grp) {
		ss->group.s = (char *)(ss + 1) + ss->rtpproxy.len;
		memcpy(ss->group.s, grp->s, grp->len);
		ss->group.len = grp->len;
	}
	siprec_build_uuid(ss->uuid);
	ss->participants_no = 0;
	ss->ts = time(NULL);

	memcpy(ss->srs_uri.s, srs->s, srs->len);
	ss->srs_uri.len = srs->len;

	lock_init(&ss->lock);
	ss->ref = 0;

	return ss;
}

void src_free_participant(struct src_part *part)
{
	struct srs_sdp_stream *stream;
	struct list_head *it, *tmp;

	list_for_each_safe(it, tmp, &part->streams) {
		stream = list_entry(it, struct srs_sdp_stream, list);
		srs_free_stream(stream);
	}
	if (part->aor.s)
		shm_free(part->aor.s);
}

void src_unref_session(void *p)
{
	SIPREC_UNREF((struct src_sess *)p);
}

void src_free_session(struct src_sess *sess)
{
	int p;

	/* extra check here! */
	if (sess->ref != 0) {
		LM_BUG("freeing session=%p with ref=%d\n", sess, sess->ref);
		return;
	}
	/* unref the dialog */
	srec_dlg.unref_dlg(sess->dlg, 1);

	for (p = 0; p < sess->participants_no; p++)
		src_free_participant(&sess->participants[p]);
	if (sess->b2b_key.s)
		shm_free(sess->b2b_key.s);
	if (sess->srs_uri.s)
		shm_free(sess->srs_uri.s);
	lock_destroy(&sess->lock);
	shm_free(sess);
}

int src_add_participant(struct src_sess *sess, str *aor, str *name)
{
	struct src_part *part;
	if (sess->participants_no >= SRC_MAX_PARTICIPANTS) {
		LM_ERR("no more space for new participants (have %d)!\n",
				sess->participants_no);
		return -1;
	}
	part = &sess->participants[sess->participants_no];
	INIT_LIST_HEAD(&part->streams);
	siprec_build_uuid(part->uuid);

	part->aor.s = shm_malloc(aor->len + (name ? name->len: 0));
	if (!part->aor.s) {
		LM_ERR("out of shared memory!\n");
		return -1;
	}

	part->aor.len = aor->len;
	memcpy(part->aor.s, aor->s, aor->len);
	if (name) {
		part->name.len = name->len;
		part->name.s = part->aor.s + part->aor.len;
		memcpy(part->name.s, name->s, name->len);
	}
	sess->participants_no++;

	return 1;
}