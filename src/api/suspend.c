/*****************************************************************************\
 *  suspend.c - job step suspend and resume functions.
 *****************************************************************************
 *  Copyright (C) 2005-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify file(s) with this exception, you may extend this
 *  exception to your version of the file(s), but you are not obligated to do
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in
 *  the program, then also delete it here.
 *
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "slurm/slurm.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/*
 * _suspend_op - perform a suspend/resume operation for some job.
 * IN op         - operation to perform
 * IN job_id     - job on which to perform operation or NO_VAL
 * RET 0 or a slurm error code
 * NOTE: Supply either job_id NO_VAL or job_id_str as NULL, not both
 */
static int _suspend_op(uint16_t op, uint32_t job_id)
{
	int rc = SLURM_SUCCESS;
	suspend_msg_t sus_req;
	slurm_msg_t req_msg;

	slurm_msg_t_init(&req_msg);
	memset(&sus_req, 0, sizeof(sus_req));
	sus_req.op         = op;
	sus_req.job_id     = job_id;
	sus_req.job_id_str = NULL;
	req_msg.msg_type   = REQUEST_SUSPEND;
	req_msg.data       = &sus_req;

	if (slurm_send_recv_controller_rc_msg(&req_msg, &rc,
					      working_cluster_rec) < 0)
		return SLURM_ERROR;

	errno = rc;
	return rc;
}

/*
 * slurm_suspend - suspend execution of a job.
 * IN job_id  - job on which to perform operation
 * RET 0 or a slurm error code
 */
extern int slurm_suspend(uint32_t job_id)
{
	return _suspend_op (SUSPEND_JOB, job_id);
}

/*
 * slurm_resume - resume execution of a previously suspended job.
 * IN job_id  - job on which to perform operation
 * RET 0 or a slurm error code
 */
extern int slurm_resume(uint32_t job_id)
{
	return _suspend_op(RESUME_JOB, job_id);
}

/*
 * _suspend_op2 - perform a suspend/resume operation for some job.
 * IN op         - operation to perform
 * IN job_id_str - job on which to perform operation in string format or NULL
 * OUT resp      - slurm error codes by job array task ID
 * RET 0 or a slurm error code
 * NOTE: Supply either job_id NO_VAL or job_id_str as NULL, not both
 */
static int _suspend_op2(uint16_t op, char *job_id_str,
			job_array_resp_msg_t **resp)
{
	int rc = SLURM_SUCCESS;
	suspend_msg_t sus_req;
	slurm_msg_t req_msg, resp_msg;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);
	memset(&sus_req, 0, sizeof(sus_req));
	sus_req.op         = op;
	sus_req.job_id     = NO_VAL;
	sus_req.job_id_str = job_id_str;
	req_msg.msg_type   = REQUEST_SUSPEND;
	req_msg.data       = &sus_req;

	rc = slurm_send_recv_controller_msg(&req_msg, &resp_msg,
					    working_cluster_rec);
	switch (resp_msg.msg_type) {
	case RESPONSE_JOB_ARRAY_ERRORS:
		*resp = (job_array_resp_msg_t *) resp_msg.data;
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		if (rc)
			errno = rc;
		break;
	default:
		errno = SLURM_UNEXPECTED_MSG_ERROR;
	}

	return rc;
}

/*
 * slurm_suspend2 - suspend execution of a job.
 * IN job_id in string form  - job on which to perform operation
 * OUT resp - per task response to the request,
 *	      free using slurm_free_job_array_resp()
 * RET 0 or a slurm error code
 */
extern int slurm_suspend2(char *job_id, job_array_resp_msg_t **resp)
{
	return _suspend_op2(SUSPEND_JOB, job_id, resp);
}


/*
 * slurm_resume2 - resume execution of a previously suspended job.
 * IN job_id in string form  - job on which to perform operation
 * OUT resp - per task response to the request,
 *	      free using slurm_free_job_array_resp()
 * RET 0 or a slurm error code
 */
extern int slurm_resume2(char *job_id, job_array_resp_msg_t **resp)
{
	return _suspend_op2(RESUME_JOB, job_id, resp);
}

/*
 * slurm_requeue - re-queue a batch job, if already running
 *	then terminate it first
 * IN job_id  - job on which to perform operation
 * IN flags - JOB_SPECIAL_EXIT - job should be placed special exit state and
 *		  held.
 *            JOB_REQUEUE_HOLD - job should be placed JOB_PENDING state and
 *		  held.
 *            JOB_RECONFIG_FAIL - Node configuration for job failed
 *            JOB_RUNNING - Operate only on jobs in a state of
 *		  CONFIGURING, RUNNING, STOPPED or SUSPENDED.
 * RET 0 or a slurm error code
 */
extern int slurm_requeue(uint32_t job_id, uint32_t flags)
{
	int rc = SLURM_SUCCESS;
	requeue_msg_t requeue_req;
	slurm_msg_t req_msg;

	slurm_msg_t_init(&req_msg);

	memset(&requeue_req, 0, sizeof(requeue_req));
	requeue_req.job_id	= job_id;
	requeue_req.job_id_str	= NULL;
	requeue_req.flags	= flags;
	req_msg.msg_type	= REQUEST_JOB_REQUEUE;
	req_msg.data		= &requeue_req;

	if (slurm_send_recv_controller_rc_msg(&req_msg, &rc,
					      working_cluster_rec) < 0)
		return SLURM_ERROR;

	errno = rc;
	return rc;
}

/*
 * slurm_requeue2 - re-queue a batch job, if already running
 *	then terminate it first
 * IN job_id in string form  - job on which to perform operation, may be job
 *            array specification (e.g. "123_1-20,44");
 * IN flags - JOB_SPECIAL_EXIT - job should be placed special exit state and
 *		  held.
 *            JOB_REQUEUE_HOLD - job should be placed JOB_PENDING state and
 *		  held.
 *            JOB_RECONFIG_FAIL - Node configuration for job failed
 *            JOB_RUNNING - Operate only on jobs in a state of
 *		  CONFIGURING, RUNNING, STOPPED or SUSPENDED.
 * OUT resp - per task response to the request,
 *	      free using slurm_free_job_array_resp()
 * RET 0 or a slurm error code
 */
extern int slurm_requeue2(char *job_id_str, uint32_t flags,
			  job_array_resp_msg_t **resp)
{
	int rc = SLURM_SUCCESS;
	requeue_msg_t requeue_req;
	slurm_msg_t req_msg, resp_msg;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);
	memset(&requeue_req, 0, sizeof(requeue_req));
	requeue_req.job_id	= NO_VAL;
	requeue_req.job_id_str	= job_id_str;
	requeue_req.flags	= flags;
	req_msg.msg_type	= REQUEST_JOB_REQUEUE;
	req_msg.data		= &requeue_req;

	rc = slurm_send_recv_controller_msg(&req_msg, &resp_msg,
					    working_cluster_rec);
	switch (resp_msg.msg_type) {
	case RESPONSE_JOB_ARRAY_ERRORS:
		*resp = (job_array_resp_msg_t *) resp_msg.data;
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		if (rc)
			errno = rc;
		break;
	default:
		errno = SLURM_UNEXPECTED_MSG_ERROR;
	}

	return rc;
}
