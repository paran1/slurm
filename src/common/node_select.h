/*****************************************************************************\
 *  node_select.h - Define node selection plugin functions.
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifndef _NODE_SELECT_H
#define _NODE_SELECT_H

#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>
#include "src/common/list.h"
#include "src/common/plugrack.h"
#include "src/slurmctld/slurmctld.h"

typedef struct {
	bitstr_t *avail_nodes;      /* usable nodes are set on input, nodes
				     * not required to satisfy the request
				     * are cleared, other left set */
	struct job_record *job_ptr; /* pointer to job being scheduled
				     * start_time is set when we can
				     * possibly start job. Or must not
				     * increase for success of running
				     * other jobs.
				     */
	uint32_t max_nodes;         /* maximum count of nodes (0==don't care) */
	uint32_t min_nodes;         /* minimum count of nodes */
	uint32_t req_nodes;         /* requested (or desired) count of nodes */
} select_will_run_t;

/*
 * Local data
 */
typedef struct slurm_select_ops {
	uint32_t	(*plugin_id);
	int		(*state_save)		(char *dir_name);
	int		(*state_restore)	(char *dir_name);
	int		(*job_init)		(List job_list);
	int		(*node_init)		(struct node_record *node_ptr,
						 int node_cnt);
	int		(*block_init)		(List block_list);
	int		(*job_test)		(struct job_record *job_ptr,
						 bitstr_t *bitmap,
						 uint32_t min_nodes,
						 uint32_t max_nodes,
						 uint32_t req_nodes,
						 uint16_t mode,
						 List preeemptee_candidates,
						 List *preemptee_job_list);
	int		(*job_begin)		(struct job_record *job_ptr);
	int		(*job_ready)		(struct job_record *job_ptr);
	int		(*job_resized)		(struct job_record *job_ptr,
						 struct node_record *node_ptr);
	int		(*job_fini)		(struct job_record *job_ptr);
	int		(*job_suspend)		(struct job_record *job_ptr);
	int		(*job_resume)		(struct job_record *job_ptr);
	int		(*pack_select_info)	(time_t last_query_time,
						 uint16_t show_flags,
						 Buf *buffer_ptr,
						 uint16_t protocol_version);
	int		(*nodeinfo_pack)	(select_nodeinfo_t *nodeinfo,
						 Buf buffer,
						 uint16_t protocol_version);
	int		(*nodeinfo_unpack)	(select_nodeinfo_t **nodeinfo,
						 Buf buffer,
						 uint16_t protocol_version);
	select_nodeinfo_t *(*nodeinfo_alloc)	(uint32_t size);
	int		(*nodeinfo_free)	(select_nodeinfo_t *nodeinfo);
	int		(*nodeinfo_set_all)	(time_t last_query_time);
	int		(*nodeinfo_set)		(struct job_record *job_ptr);
	int		(*nodeinfo_get)		(select_nodeinfo_t *nodeinfo,
						 enum
						 select_nodedata_type dinfo,
						 enum node_states state,
						 void *data);
	select_jobinfo_t *(*jobinfo_alloc)	(void);
	int		(*jobinfo_free)		(select_jobinfo_t *jobinfo);
	int		(*jobinfo_set)		(select_jobinfo_t *jobinfo,
						 enum
						 select_jobdata_type data_type,
						 void *data);
	int		(*jobinfo_get)		(select_jobinfo_t *jobinfo,
						 enum
						 select_jobdata_type data_type,
						 void *data);
	select_jobinfo_t *(*jobinfo_copy)	(select_jobinfo_t *jobinfo);
	int		(*jobinfo_pack)		(select_jobinfo_t *jobinfo,
						 Buf buffer,
						 uint16_t protocol_version);
	int		(*jobinfo_unpack)	(select_jobinfo_t **jobinfo_pptr,
						 Buf buffer,
						 uint16_t protocol_version);
	char *		(*jobinfo_sprint)	(select_jobinfo_t *jobinfo,
						 char *buf, size_t size,
						 int mode);
	char *		(*jobinfo_xstrdup)	(select_jobinfo_t *jobinfo,
						 int mode);
	int		(*update_block)		(update_block_msg_t
						 *block_desc_ptr);
	int		(*update_sub_node)	(update_block_msg_t
						 *block_desc_ptr);
	int		(*get_info_from_plugin)	(enum
						 select_plugindata_info dinfo,
						 struct job_record *job_ptr,
						 void *data);
	int		(*update_node_config)	(int index);
	int		(*update_node_state)	(int index, uint16_t state);
	int		(*alter_node_cnt)	(enum select_node_cnt type,
						 void *data);
	int		(*reconfigure)		(void);
} slurm_select_ops_t;

typedef struct slurm_select_context {
	char		*select_type;
	plugrack_t	plugin_list;
	plugin_handle_t	cur_plugin;
	int		select_errno;
	slurm_select_ops_t ops;
} slurm_select_context_t;

/*******************************************\
 * GLOBAL SELECT STATE MANAGEMENT FUNCIONS *
\*******************************************/

extern int node_select_free_block_info(block_info_t *block_info);

extern void node_select_pack_block_info(block_info_t *block_info, Buf buffer,
					uint16_t protocol_version);
extern int node_select_unpack_block_info(block_info_t **block_info, Buf buffer,
					 uint16_t protocol_version);

/*
 * Initialize context for node selection plugin
 */
extern int slurm_select_init(bool only_default);

/*
 * Terminate plugin and free all associated memory
 */
extern int slurm_select_fini(void);

extern int select_get_plugin_id_pos(uint32_t plugin_id);
extern int select_get_plugin_id();

/*
 * Save any global state information
 * IN dir_name - directory into which the data can be stored
 */
extern int select_g_state_save(char *dir_name);

/*
 * Initialize context for node selection plugin and
 * restore any global state information
 * IN dir_name - directory from which the data can be restored
 */
extern int select_g_state_restore(char *dir_name);

/*
 * Note the initialization of job records, issued upon restart of
 * slurmctld and used to synchronize any job state.
 */
extern int select_g_job_init(List job_list);

/*
 * Note re/initialization of node record data structure
 * IN node_ptr - current node data
 * IN node_count - number of node entries
 */
extern int select_g_node_init(struct node_record *node_ptr, int node_cnt);

/*
 * Note re/initialization of partition record data structure
 * IN part_list - list of partition records
 */
extern int select_g_block_init(List part_list);

/*
 * Update specific block (usually something has gone wrong)
 * IN block_desc_ptr - information about the block
 */
extern int select_g_update_block (update_block_msg_t *block_desc_ptr);

/*
 * Update specific sub nodes (usually something has gone wrong)
 * IN block_desc_ptr - information about the block
 */
extern int select_g_update_sub_node (update_block_msg_t *block_desc_ptr);

/*
 * Get select data from a plugin
 * IN node_pts  - current node record
 * IN dinfo   - type of data to get from the node record
 *                (see enum select_plugindata_info)
 * IN job_ptr   - pointer to the job that's related to this query (may be NULL)
 * IN/OUT data  - the data to get from node record
 */
extern int select_g_get_info_from_plugin (enum select_plugindata_info dinfo,
					  struct job_record *job_ptr,
					  void *data);

/*
 * Updated a node configuration. This happens when a node registers with
 *	more resources than originally configured (e.g. memory).
 * IN index  - index into the node record list
 * RETURN SLURM_SUCCESS on success || SLURM_ERROR else wise
 */
extern int select_g_update_node_config (int index);

/*
 * Updated a node state in the plugin, this should happen when a node is
 * drained or put into a down state then changed back.
 * IN index  - index into the node record list
 * IN state  - state to update to
 * RETURN SLURM_SUCCESS on success || SLURM_ERROR else wise
 */
extern int select_g_update_node_state (int index, uint16_t state);

/*
 * Alter the node count for a job given the type of system we are on
 * IN/OUT job_desc  - current job desc
 */
extern int select_g_alter_node_cnt (enum select_node_cnt type, void *data);


/******************************************************\
 * JOB-SPECIFIC SELECT CREDENTIAL MANAGEMENT FUNCIONS *
\******************************************************/

#define SELECT_MODE_BASE         0x00ff
#define SELECT_MODE_FLAGS        0xff00

#define SELECT_MODE_RUN_NOW	 0x0000
#define SELECT_MODE_TEST_ONLY	 0x0001
#define SELECT_MODE_WILL_RUN	 0x0002

#define SELECT_MODE_PREEMPT_FLAG 0x0100
#define SELECT_MODE_CHECK_FULL   0x0200

#define SELECT_IS_MODE_RUN_NOW(_X) \
	(((_X & SELECT_MODE_BASE) == SELECT_MODE_RUN_NOW) \
	 && !SELECT_IS_PREEMPT_ON_FULL_TEST(_X))

#define SELECT_IS_MODE_TEST_ONLY(_X) \
	(_X & SELECT_MODE_TEST_ONLY)

#define SELECT_IS_MODE_WILL_RUN(_X) \
	(_X & SELECT_MODE_WILL_RUN)

#define SELECT_IS_PREEMPT_SET(_X) \
	(_X & SELECT_MODE_PREEMPT_FLAG)

#define SELECT_IS_CHECK_FULL_SET(_X) \
	(_X & SELECT_MODE_CHECK_FULL)

#define SELECT_IS_TEST(_X) \
	(SELECT_IS_MODE_TEST_ONLY(_X) || SELECT_IS_MODE_WILL_RUN(_X))

#define SELECT_IS_PREEMPT_ON_FULL_TEST(_X) \
	(SELECT_IS_CHECK_FULL_SET(_X) && SELECT_IS_PREEMPT_SET(_X))

#define SELECT_IS_PREEMPTABLE_TEST(_X) \
	((SELECT_IS_MODE_TEST_ONLY(_X) || SELECT_IS_MODE_WILL_RUN(_X))	\
	 && SELECT_IS_PREEMPT_SET(_X))


/*
 * Select the "best" nodes for given job from those available
 * IN/OUT job_ptr - pointer to job being considered for initiation,
 *                  set's start_time when job expected to start
 * IN/OUT bitmap - map of nodes being considered for allocation on input,
 *                 map of nodes actually to be assigned on output
 * IN min_nodes - minimum number of nodes to allocate to job
 * IN max_nodes - maximum number of nodes to allocate to job
 * IN req_nodes - requested (or desired) count of nodes
 * IN mode - SELECT_MODE_RUN_NOW: try to schedule job now
 *           SELECT_MODE_TEST_ONLY: test if job can ever run
 *           SELECT_MODE_WILL_RUN: determine when and where job can run
 * IN preemptee_candidates - List of pointers to jobs which can bee preempted
 * IN/OUT preemptee_job_list - Pointer to list of job pointers. These are the
 *		jobs to be preempted to initiate the pending job. Not set
 *		if mode=SELECT_MODE_TEST_ONLY or input pointer is NULL.
 *		Existing list is appended to.
 * RET zero on success, EINVAL otherwise
 */
extern int select_g_job_test(struct job_record *job_ptr, bitstr_t *bitmap,
			     uint32_t min_nodes, uint32_t max_nodes,
			     uint32_t req_nodes, uint16_t mode,
			     List preemptee_candidates,
			     List *preemptee_job_list);

/*
 * Note initiation of job is about to begin. Called immediately
 * after select_g_job_test(). Executed from slurmctld.
 * IN job_ptr - pointer to job being initiated
 */
extern int select_g_job_begin(struct job_record *job_ptr);

/*
 * determine if job is ready to execute per the node select plugin
 * IN job_ptr - pointer to job being tested
 * RET -1 on error, 1 if ready to execute, 0 otherwise
 */
extern int select_g_job_ready(struct job_record *job_ptr);

/*
 * Modify internal data structures for a job that has changed size
 *	Only support jobs shrinking now.
 * RET: 0 or an error code
 */
extern int select_g_job_resized(struct job_record *job_ptr,
				struct node_record *node_ptr);

/*
 * Note termination of job is starting. Executed from slurmctld.
 * IN job_ptr - pointer to job being terminated
 */
extern int select_g_job_fini(struct job_record *job_ptr);

/*
 * Suspend a job. Executed from slurmctld.
 * IN job_ptr - pointer to job being suspended
 * RET SLURM_SUCCESS or error code
 */
extern int select_g_job_suspend(struct job_record *job_ptr);

/*
 * Resume a job. Executed from slurmctld.
 * IN job_ptr - pointer to job being resumed
 * RET SLURM_SUCCESS or error code
 */
extern int select_g_job_resume(struct job_record *job_ptr);

/* allocate storage for a select job credential
 * RET jobinfo - storage for a select job credential
 * NOTE: storage must be freed using select_g_free_jobinfo
 */
extern dynamic_plugin_data_t *select_g_select_jobinfo_alloc(void);

/* free storage previously allocated for a select job credential
 * IN jobinfo  - the select job credential to be freed
 * RET         - slurm error code
 */
extern int select_g_select_jobinfo_free(dynamic_plugin_data_t *jobinfo);

/* fill in a previously allocated select job credential
 * IN/OUT jobinfo  - updated select job credential
 * IN data_type - type of data to enter into job credential
 * IN data - the data to enter into job credential
 */
extern int select_g_select_jobinfo_set(dynamic_plugin_data_t *jobinfo,
				       enum select_jobdata_type data_type,
				       void *data);

/* get data from a select job credential
 * IN jobinfo  - updated select job credential
 * IN data_type - type of data to enter into job credential
 * OUT data - the data to get from job credential, caller must xfree
 *	data for data_type == SELECT_JOBDATA_PART_ID
 */
extern int select_g_select_jobinfo_get(dynamic_plugin_data_t *jobinfo,
				       enum select_jobdata_type data_type,
				       void *data);

/* copy a select job credential
 * IN jobinfo - the select job credential to be copied
 * RET        - the copy or NULL on failure
 * NOTE: returned value must be freed using select_g_select_jobinfo_free
 */
extern dynamic_plugin_data_t *select_g_select_jobinfo_copy(
	dynamic_plugin_data_t *jobinfo);

/* pack a select job credential into a buffer in machine independent form
 * IN jobinfo  - the select job credential to be saved
 * OUT buffer  - buffer with select credential appended
 * IN protocol_version - slurm protocol version of client
 * RET         - slurm error code
 */
extern int select_g_select_jobinfo_pack(dynamic_plugin_data_t *jobinfo,
					Buf buffer,
					uint16_t protocol_version);

/* unpack a select job credential from a buffer
 * OUT jobinfo - the select job credential read
 * IN  buffer  - buffer with select credential read from current pointer loc
 * IN protocol_version - slurm protocol version of client
 * RET         - slurm error code
 * NOTE: returned value must be freed using select_g_select_jobinfo_free
 */
extern int select_g_select_jobinfo_unpack(dynamic_plugin_data_t **jobinfo,
					  Buf buffer,
					  uint16_t protocol_version);

/* write select job info to a string
 * IN jobinfo - a select job credential
 * OUT buf    - location to write job info contents
 * IN size    - byte size of buf
 * IN mode    - print mode, see enum select_print_mode
 * RET        - the string, same as buf
 */
extern char *select_g_select_jobinfo_sprint(dynamic_plugin_data_t *jobinfo,
					    char *buf, size_t size, int mode);

/* write select job info to a string
 * IN jobinfo - a select job credential
 * OUT buf    - location to write job info contents
 * IN mode    - print mode, see enum select_print_mode
 * RET        - the string, same as buf
 */
extern char *select_g_select_jobinfo_xstrdup(dynamic_plugin_data_t *jobinfo,
					     int mode);

/*******************************************************\
 * NODE-SPECIFIC SELECT CREDENTIAL MANAGEMENT FUNCIONS *
\*******************************************************/

extern int select_g_select_nodeinfo_pack(
	dynamic_plugin_data_t *nodeinfo, Buf buffer, uint16_t protocol_version);

extern int select_g_select_nodeinfo_unpack(
	dynamic_plugin_data_t **nodeinfo, Buf buffer,
	uint16_t protocol_version);

extern dynamic_plugin_data_t *select_g_select_nodeinfo_alloc(uint32_t size);

extern int select_g_select_nodeinfo_free(dynamic_plugin_data_t *nodeinfo);

extern int select_g_select_nodeinfo_set_all(time_t last_query_time);

extern int select_g_select_nodeinfo_set(struct job_record *job_ptr);

extern int select_g_select_nodeinfo_get(dynamic_plugin_data_t *nodeinfo,
					enum select_nodedata_type dinfo,
					enum node_states state,
					void *data);


/******************************************************\
 * NODE-SELECT PLUGIN SPECIFIC INFORMATION FUNCTIONS  *
\******************************************************/

/* pack node-select plugin specific information into a buffer in
 *	machine independent form
 * IN last_update_time - time of latest information consumer has
 * IN show_flags - flags to control information output
 * OUT buffer - location to hold the data, consumer must free
 * IN protocol_version - slurm protocol version of client
 * RET - slurm error code
 */
extern int select_g_pack_select_info(time_t last_query_time,
				     uint16_t show_flags, Buf *buffer,
				     uint16_t protocol_version);

/* Note reconfiguration or change in partition configuration */
extern int select_g_reconfigure(void);

#endif /*__SELECT_PLUGIN_API_H__*/
