/*****************************************************************************\
 *  sinfo.c - Report overall state the system
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010 SchedMD <http://www.schedmd.com>.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Joey Ekstrom <ekstrom1@llnl.gov>, Morris Jette <jette1@llnl.gov>
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include "src/common/xstring.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/sinfo/sinfo.h"
#include "src/sinfo/print.h"

#include "src/plugins/select/bluegene/wrap_rm_api.h"
#include "src/plugins/select/bluegene/plugin/bluegene.h"

/********************
 * Global Variables *
 ********************/
struct sinfo_parameters params;

static int g_node_scaling = 1;

/************
 * Funtions *
 ************/
static int  _bg_report(block_info_msg_t *block_ptr);
static int  _build_sinfo_data(List sinfo_list,
			      partition_info_msg_t *partition_msg,
			      node_info_msg_t *node_msg);
static sinfo_data_t *_create_sinfo(partition_info_t* part_ptr,
				   uint16_t part_inx, node_info_t *node_ptr,
				   uint32_t node_scaling);
static bool _filter_out(node_info_t *node_ptr);
static int  _get_info(bool clear_old);
static void _sinfo_list_delete(void *data);
static bool _match_node_data(sinfo_data_t *sinfo_ptr,
                             node_info_t *node_ptr);
static bool _match_part_data(sinfo_data_t *sinfo_ptr,
                             partition_info_t* part_ptr);
static int  _multi_cluster(List clusters);
static int  _query_server(partition_info_msg_t ** part_pptr,
			  node_info_msg_t ** node_pptr,
			  block_info_msg_t ** block_pptr, bool clear_old);
static void _sort_hostlist(List sinfo_list);
static int  _strcmp(char *data1, char *data2);
static void _update_sinfo(sinfo_data_t *sinfo_ptr, node_info_t *node_ptr,
			  uint32_t node_scaling);

static int _insert_node_ptr(List sinfo_list, uint16_t part_num,
			    partition_info_t *part_ptr,
			    node_info_t *node_ptr, uint32_t node_scaling);
static int _handle_subgrps(List sinfo_list, uint16_t part_num,
			   partition_info_t *part_ptr,
			   node_info_t *node_ptr, uint32_t node_scaling);

int main(int argc, char *argv[])
{
	log_options_t opts = LOG_OPTS_STDERR_ONLY;
	int rc = 0;

	log_init(xbasename(argv[0]), opts, SYSLOG_FACILITY_USER, NULL);
	parse_command_line(argc, argv);
	if (params.verbose) {
		opts.stderr_level += params.verbose;
		log_alter(opts, SYSLOG_FACILITY_USER, NULL);
	}

	while (1) {
		if ((!params.no_header) &&
		    (params.iterate || params.verbose || params.long_output))
			print_date();

		if (!params.clusters) {
			if (_get_info(false))
				rc = 1;
		} else if (_multi_cluster(params.clusters) != 0)
			rc = 1;
		if (params.iterate) {
			printf("\n");
			sleep(params.iterate);
		} else
			break;
	}

	exit(rc);
}

static int _multi_cluster(List clusters)
{
	ListIterator itr;
	bool first = true;
	int rc = 0, rc2;

	itr = list_iterator_create(clusters);
	while ((working_cluster_rec = list_next(itr))) {
		if (first)
			first = false;
		else
			printf("\n");
		printf("CLUSTER: %s\n", working_cluster_rec->name);
		rc2 = _get_info(true);
		rc = MAX(rc, rc2);
	}
	list_iterator_destroy(itr);

	return rc;
}

/* clear_old IN - if set then don't preserve old info (it might be from
 *		  another cluster) */
static int _get_info(bool clear_old)
{
	partition_info_msg_t *partition_msg = NULL;
	node_info_msg_t *node_msg = NULL;
	block_info_msg_t *block_msg = NULL;
	List sinfo_list = NULL;
	int rc = 0;

	if (_query_server(&partition_msg, &node_msg, &block_msg, clear_old))
		rc = 1;
	else if (params.bg_flag)
		(void) _bg_report(block_msg);
	else {
		sinfo_list = list_create(_sinfo_list_delete);
		_build_sinfo_data(sinfo_list, partition_msg, node_msg);
		sort_sinfo_list(sinfo_list);
		print_sinfo_list(sinfo_list);
		FREE_NULL_LIST(sinfo_list);
	}

	return rc;
}

/*
 * _bg_report - download and print current bgblock state information
 */
static int _bg_report(block_info_msg_t *block_ptr)
{
	int i;

	if (!block_ptr) {
		slurm_perror("No block_ptr given");
		return SLURM_ERROR;
	}

	if (!params.no_header)
		printf("BG_BLOCK         NODES        OWNER    STATE    CONNECTION USE\n");
/*                      1234567890123456 123456789012 12345678 12345678 1234567890 12345+ */
/*                      RMP_22Apr1544018 bg[123x456]  name     READY    TORUS      COPROCESSOR */

	for (i=0; i<block_ptr->record_count; i++) {
		printf("%-16.16s %-12.12s %-8.8s %-8.8s %-10.10s %s\n",
		       block_ptr->block_array[i].bg_block_id,
		       block_ptr->block_array[i].nodes,
		       block_ptr->block_array[i].owner_name,
		       bg_block_state_string(
			       block_ptr->block_array[i].state),
		       conn_type_string(
			       block_ptr->block_array[i].conn_type),
		       node_use_string(
			       block_ptr->block_array[i].node_use));
	}

	return SLURM_SUCCESS;
}

/*
 * _query_server - download the current server state
 * part_pptr IN/OUT - partition information message
 * node_pptr IN/OUT - node information message
 * block_pptr IN/OUT - BlueGene block data
 * clear_old IN - If set, then always replace old data, needed when going 
 *		  between clusters.
 * RET zero or error code
 */
static int
_query_server(partition_info_msg_t ** part_pptr,
	      node_info_msg_t ** node_pptr,
	      block_info_msg_t ** block_pptr, bool clear_old)
{
	static partition_info_msg_t *old_part_ptr = NULL, *new_part_ptr;
	static node_info_msg_t *old_node_ptr = NULL, *new_node_ptr;
	static block_info_msg_t *old_bg_ptr = NULL, *new_bg_ptr;

	int error_code;
	uint16_t show_flags = 0;

	if (params.all_flag)
		show_flags |= SHOW_ALL;

	if (old_part_ptr) {
		if (clear_old)
			old_part_ptr->last_update = 0;
		error_code = slurm_load_partitions(old_part_ptr->last_update,
						   &new_part_ptr, show_flags);
		if (error_code == SLURM_SUCCESS)
			slurm_free_partition_info_msg(old_part_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_part_ptr = old_part_ptr;
		}
	} else {
		error_code = slurm_load_partitions((time_t) NULL, &new_part_ptr,
						   show_flags);
	}
	if (error_code) {
		slurm_perror("slurm_load_partitions");
		return error_code;
	}

	old_part_ptr = new_part_ptr;
	*part_pptr = new_part_ptr;

	if (old_node_ptr) {
		if (clear_old)
			old_node_ptr->last_update = 0;
		error_code = slurm_load_node(old_node_ptr->last_update,
					     &new_node_ptr, show_flags);
		if (error_code == SLURM_SUCCESS)
			slurm_free_node_info_msg(old_node_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_node_ptr = old_node_ptr;
		}
	} else {
		error_code = slurm_load_node((time_t) NULL, &new_node_ptr,
					     show_flags);
	}

	if (error_code) {
		slurm_perror("slurm_load_node");
		return error_code;
	}
	old_node_ptr = new_node_ptr;
	*node_pptr = new_node_ptr;

	if (!params.bg_flag)
		return SLURM_SUCCESS;

	if (params.cluster_flags & CLUSTER_FLAG_BG) {
		if (old_bg_ptr) {
			if (clear_old)
				old_bg_ptr->last_update = 0;
			error_code = slurm_load_block_info(
				old_bg_ptr->last_update,
				&new_bg_ptr, show_flags);
			if (error_code == SLURM_SUCCESS)
				slurm_free_block_info_msg(old_bg_ptr);
			else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
				error_code = SLURM_SUCCESS;
				new_bg_ptr = old_bg_ptr;
			}
		} else {
			error_code = slurm_load_block_info((time_t) NULL,
							   &new_bg_ptr,
							   show_flags);
		}
	}

	if (error_code) {
		slurm_perror("slurm_load_block");
		return error_code;
	}
	old_bg_ptr = new_bg_ptr;
	*block_pptr = new_bg_ptr;
	return SLURM_SUCCESS;
}

/*
 * _build_sinfo_data - make a sinfo_data entry for each unique node
 *	configuration and add it to the sinfo_list for later printing.
 * sinfo_list IN/OUT - list of unique sinfo_data records to report
 * partition_msg IN - partition info message
 * node_msg IN - node info message
 * RET zero or error code
 */
static int _build_sinfo_data(List sinfo_list,
			     partition_info_msg_t *partition_msg,
			     node_info_msg_t *node_msg)
{
	node_info_t *node_ptr = NULL;
	partition_info_t *part_ptr = NULL;
	int j, j2;

	g_node_scaling = node_msg->node_scaling;

	/* by default every partition is shown, even if no nodes */
	if ((!params.node_flag) && params.match_flags.partition_flag) {
		part_ptr = partition_msg->partition_array;
		for (j=0; j<partition_msg->record_count; j++, part_ptr++) {
			if ((!params.partition) ||
			    (_strcmp(params.partition, part_ptr->name) == 0)) {
				list_append(sinfo_list, _create_sinfo(
						    part_ptr, (uint16_t) j,
						    NULL,
						    node_msg->node_scaling));
			}
		}
	}

	/* make sinfo_list entries for every node in every partition */
	for (j=0; j<partition_msg->record_count; j++, part_ptr++) {
		part_ptr = &(partition_msg->partition_array[j]);

		if (params.filtering && params.partition &&
		    _strcmp(part_ptr->name, params.partition))
			continue;

		j2 = 0;
		while(part_ptr->node_inx[j2] >= 0) {
			int i2 = 0;
			uint16_t subgrp_size = 0;
			for(i2 = part_ptr->node_inx[j2];
			    i2 <= part_ptr->node_inx[j2+1];
			    i2++) {
				node_ptr = &(node_msg->node_array[i2]);

				if (node_ptr->name == NULL ||
				    (params.filtering &&
				     _filter_out(node_ptr)))
					continue;

				if(select_g_select_nodeinfo_get(
					   node_ptr->select_nodeinfo,
					   SELECT_NODEDATA_SUBGRP_SIZE,
					   0,
					   &subgrp_size) == SLURM_SUCCESS
				   && subgrp_size)
					_handle_subgrps(sinfo_list,
							(uint16_t) j,
							part_ptr,
							node_ptr,
							node_msg->
							node_scaling);
				else
					_insert_node_ptr(sinfo_list,
							 (uint16_t) j,
							 part_ptr,
							 node_ptr,
							 node_msg->
							 node_scaling);
			}
			j2 += 2;
		}
	}
	_sort_hostlist(sinfo_list);
	return SLURM_SUCCESS;
}

/*
 * _filter_out - Determine if the specified node should be filtered out or
 *	reported.
 * node_ptr IN - node to consider filtering out
 * RET - true if node should not be reported, false otherwise
 */
static bool _filter_out(node_info_t *node_ptr)
{
	static hostlist_t host_list = NULL;

	if (params.nodes) {
		if (host_list == NULL)
			host_list = hostlist_create(params.nodes);
		if (hostlist_find (host_list, node_ptr->name) == -1)
			return true;
	}

	if (params.dead_nodes && !IS_NODE_NO_RESPOND(node_ptr))
		return true;

	if (params.responding_nodes && IS_NODE_NO_RESPOND(node_ptr))
		return true;

	if (params.state_list) {
		int *node_state;
		bool match = false;
		uint16_t base_state;
		ListIterator iterator;
		uint16_t cpus = 0;
		node_info_t tmp_node, *tmp_node_ptr = &tmp_node;

		iterator = list_iterator_create(params.state_list);
		while ((node_state = list_next(iterator))) {
			tmp_node_ptr->node_state = *node_state;
			if (*node_state == NODE_STATE_DRAIN) {
				/* We search for anything that has the
				 * drain flag set */
				if (IS_NODE_DRAIN(node_ptr)) {
					match = true;
					break;
				}
			} else if (IS_NODE_DRAINING(tmp_node_ptr)) {
				/* We search for anything that gets mapped to
				 * DRAINING in node_state_string */
				if (IS_NODE_DRAINING(node_ptr)) {
					match = true;
					break;
				}
			} else if (IS_NODE_DRAINED(tmp_node_ptr)) {
				/* We search for anything that gets mapped to
				 * DRAINED in node_state_string */
				if (IS_NODE_DRAINED(node_ptr)) {
					match = true;
					break;
				}
			} else if (*node_state & NODE_STATE_FLAGS) {
				if (*node_state & node_ptr->node_state) {
					match = true;
					break;
				}
			} else if (*node_state == NODE_STATE_ERROR) {
				slurm_get_select_nodeinfo(
					node_ptr->select_nodeinfo,
					SELECT_NODEDATA_SUBCNT,
					NODE_STATE_ERROR,
					&cpus);
				if(cpus) {
					match = true;
					break;
				}
			} else if (*node_state == NODE_STATE_ALLOCATED) {
				slurm_get_select_nodeinfo(
					node_ptr->select_nodeinfo,
					SELECT_NODEDATA_SUBCNT,
					NODE_STATE_ALLOCATED,
					&cpus);
				if(cpus) {
					match = true;
					break;
				}
			} else if (*node_state == NODE_STATE_IDLE) {
				base_state = node_ptr->node_state &
					(~NODE_STATE_NO_RESPOND);
				if (base_state == NODE_STATE_IDLE) {
					match = true;
					break;
				}
			} else {
				base_state =
					node_ptr->node_state & NODE_STATE_BASE;
				if (base_state == *node_state) {
					match = true;
					break;
				}
			}
		}
		list_iterator_destroy(iterator);
		if (!match)
			return true;
	}

	return false;
}

static void _sort_hostlist(List sinfo_list)
{
	ListIterator i;
	sinfo_data_t *sinfo_ptr;

	i = list_iterator_create(sinfo_list);
	while ((sinfo_ptr = list_next(i)))
		hostlist_sort(sinfo_ptr->nodes);
	list_iterator_destroy(i);
}

static bool _match_node_data(sinfo_data_t *sinfo_ptr, node_info_t *node_ptr)
{
	if (sinfo_ptr->nodes &&
	    params.match_flags.features_flag &&
	    (_strcmp(node_ptr->features, sinfo_ptr->features)))
		return false;

	if (sinfo_ptr->nodes &&
	    params.match_flags.gres_flag &&
	    (_strcmp(node_ptr->gres, sinfo_ptr->gres)))
		return false;

	if (sinfo_ptr->nodes &&
	    params.match_flags.reason_flag &&
	    (_strcmp(node_ptr->reason, sinfo_ptr->reason)))
		return false;

	if (params.match_flags.state_flag) {
		char *state1, *state2;
		state1 = node_state_string(node_ptr->node_state);
		state2 = node_state_string(sinfo_ptr->node_state);
		if (strcmp(state1, state2))
			return false;
	}

	/* If no need to exactly match sizes, just return here
	 * otherwise check cpus, disk, memory and weigth individually */
	if (!params.exact_match)
		return true;

	if (params.match_flags.cpus_flag &&
	    ((node_ptr->cpus / g_node_scaling) != sinfo_ptr->min_cpus))
		return false;

	if (params.match_flags.sockets_flag &&
	    (node_ptr->sockets     != sinfo_ptr->min_sockets))
		return false;
	if (params.match_flags.cores_flag &&
	    (node_ptr->cores       != sinfo_ptr->min_cores))
		return false;
	if (params.match_flags.threads_flag &&
	    (node_ptr->threads     != sinfo_ptr->min_threads))
		return false;
	if (params.match_flags.sct_flag &&
	    ((node_ptr->sockets     != sinfo_ptr->min_sockets) ||
	     (node_ptr->cores       != sinfo_ptr->min_cores) ||
	     (node_ptr->threads     != sinfo_ptr->min_threads)))
		return false;
	if (params.match_flags.disk_flag &&
	    (node_ptr->tmp_disk    != sinfo_ptr->min_disk))
		return false;
	if (params.match_flags.memory_flag &&
	    (node_ptr->real_memory != sinfo_ptr->min_mem))
		return false;
	if (params.match_flags.weight_flag &&
	    (node_ptr->weight      != sinfo_ptr->min_weight))
		return false;

	return true;
}

static bool _match_part_data(sinfo_data_t *sinfo_ptr,
                             partition_info_t* part_ptr)
{
	if (part_ptr == sinfo_ptr->part_info) /* identical partition */
		return true;
	if ((part_ptr == NULL) || (sinfo_ptr->part_info == NULL))
		return false;

	if (params.match_flags.avail_flag &&
	    (part_ptr->state_up != sinfo_ptr->part_info->state_up))
		return false;

	if (params.match_flags.groups_flag &&
	    (_strcmp(part_ptr->allow_groups,
	             sinfo_ptr->part_info->allow_groups)))
		return false;

	if (params.match_flags.job_size_flag &&
	    (part_ptr->min_nodes != sinfo_ptr->part_info->min_nodes))
		return false;

	if (params.match_flags.job_size_flag &&
	    (part_ptr->max_nodes != sinfo_ptr->part_info->max_nodes))
		return false;

	if (params.match_flags.default_time_flag &&
	    (part_ptr->default_time != sinfo_ptr->part_info->default_time))
		return false;

	if (params.match_flags.max_time_flag &&
	    (part_ptr->max_time != sinfo_ptr->part_info->max_time))
		return false;

	if (params.match_flags.partition_flag &&
	    (_strcmp(part_ptr->name, sinfo_ptr->part_info->name)))
		return false;

	if (params.match_flags.root_flag &&
	    ((part_ptr->flags & PART_FLAG_ROOT_ONLY) !=
	     (sinfo_ptr->part_info->flags & PART_FLAG_ROOT_ONLY)))
		return false;

	if (params.match_flags.share_flag &&
	    (part_ptr->max_share != sinfo_ptr->part_info->max_share))
		return false;

	if (params.match_flags.preempt_mode_flag &&
	    (part_ptr->preempt_mode != sinfo_ptr->part_info->preempt_mode))
		return false;

	if (params.match_flags.priority_flag &&
	    (part_ptr->priority != sinfo_ptr->part_info->priority))
		return false;

	return true;
}

static void _update_sinfo(sinfo_data_t *sinfo_ptr, node_info_t *node_ptr,
			  uint32_t node_scaling)
{
	uint16_t base_state;
	uint16_t used_cpus = 0, error_cpus = 0;
	int total_cpus = 0, total_nodes = 0;
	/* since node_scaling could be less here we need to use the
	   global node scaling which should never change. */
	int single_node_cpus = (node_ptr->cpus / g_node_scaling);

 	base_state = node_ptr->node_state & NODE_STATE_BASE;

	if (sinfo_ptr->nodes_total == 0) {	/* first node added */
		sinfo_ptr->node_state = node_ptr->node_state;
		sinfo_ptr->features   = node_ptr->features;
		sinfo_ptr->gres       = node_ptr->gres;
		sinfo_ptr->reason     = node_ptr->reason;
		sinfo_ptr->reason_time= node_ptr->reason_time;
		sinfo_ptr->reason_uid = node_ptr->reason_uid;
		sinfo_ptr->min_cpus   = single_node_cpus;
		sinfo_ptr->max_cpus   = single_node_cpus;
		sinfo_ptr->min_sockets = node_ptr->sockets;
		sinfo_ptr->max_sockets = node_ptr->sockets;
		sinfo_ptr->min_cores   = node_ptr->cores;
		sinfo_ptr->max_cores   = node_ptr->cores;
		sinfo_ptr->min_threads = node_ptr->threads;
		sinfo_ptr->max_threads = node_ptr->threads;
		sinfo_ptr->min_disk   = node_ptr->tmp_disk;
		sinfo_ptr->max_disk   = node_ptr->tmp_disk;
		sinfo_ptr->min_mem    = node_ptr->real_memory;
		sinfo_ptr->max_mem    = node_ptr->real_memory;
		sinfo_ptr->min_weight = node_ptr->weight;
		sinfo_ptr->max_weight = node_ptr->weight;
	} else if (hostlist_find(sinfo_ptr->nodes, node_ptr->name) != -1) {
		/* we already have this node in this record,
		 * just return, don't duplicate */
		return;
	} else {
		if (sinfo_ptr->min_cpus > single_node_cpus)
			sinfo_ptr->min_cpus = single_node_cpus;
		if (sinfo_ptr->max_cpus < single_node_cpus)
			sinfo_ptr->max_cpus = single_node_cpus;

		if (sinfo_ptr->min_sockets > node_ptr->sockets)
			sinfo_ptr->min_sockets = node_ptr->sockets;
		if (sinfo_ptr->max_sockets < node_ptr->sockets)
			sinfo_ptr->max_sockets = node_ptr->sockets;

		if (sinfo_ptr->min_cores > node_ptr->cores)
			sinfo_ptr->min_cores = node_ptr->cores;
		if (sinfo_ptr->max_cores < node_ptr->cores)
			sinfo_ptr->max_cores = node_ptr->cores;

		if (sinfo_ptr->min_threads > node_ptr->threads)
			sinfo_ptr->min_threads = node_ptr->threads;
		if (sinfo_ptr->max_threads < node_ptr->threads)
			sinfo_ptr->max_threads = node_ptr->threads;

		if (sinfo_ptr->min_disk > node_ptr->tmp_disk)
			sinfo_ptr->min_disk = node_ptr->tmp_disk;
		if (sinfo_ptr->max_disk < node_ptr->tmp_disk)
			sinfo_ptr->max_disk = node_ptr->tmp_disk;

		if (sinfo_ptr->min_mem > node_ptr->real_memory)
			sinfo_ptr->min_mem = node_ptr->real_memory;
		if (sinfo_ptr->max_mem < node_ptr->real_memory)
			sinfo_ptr->max_mem = node_ptr->real_memory;

		if (sinfo_ptr->min_weight> node_ptr->weight)
			sinfo_ptr->min_weight = node_ptr->weight;
		if (sinfo_ptr->max_weight < node_ptr->weight)
			sinfo_ptr->max_weight = node_ptr->weight;
	}

	hostlist_push(sinfo_ptr->nodes, node_ptr->name);

	total_cpus = node_ptr->cpus;
	total_nodes = node_scaling;

	select_g_select_nodeinfo_get(node_ptr->select_nodeinfo,
				     SELECT_NODEDATA_SUBCNT,
				     NODE_STATE_ALLOCATED,
				     &used_cpus);
	select_g_select_nodeinfo_get(node_ptr->select_nodeinfo,
				     SELECT_NODEDATA_SUBCNT,
				     NODE_STATE_ERROR,
				     &error_cpus);

	if (params.cluster_flags & CLUSTER_FLAG_BG) {
		if (!params.match_flags.state_flag &&
		    (used_cpus || error_cpus)) {
			/* We only get one shot at this (because all states
			   are combined together), so we need to make
			   sure we get all the subgrps accounted. (So use
			   g_node_scaling for safe measure) */
			total_nodes = g_node_scaling;

			sinfo_ptr->nodes_alloc += used_cpus;
			sinfo_ptr->nodes_other += error_cpus;
			sinfo_ptr->nodes_idle +=
				(total_nodes - (used_cpus + error_cpus));
			used_cpus *= single_node_cpus;
			error_cpus *= single_node_cpus;
		} else {
			/* process only for this subgrp and then return */
			total_cpus = total_nodes * single_node_cpus;

			if ((base_state == NODE_STATE_ALLOCATED) ||
			    (node_ptr->node_state & NODE_STATE_COMPLETING)) {
				sinfo_ptr->nodes_alloc += total_nodes;
				sinfo_ptr->cpus_alloc += total_cpus;
			} else if (IS_NODE_DRAIN(node_ptr) ||
				   (base_state == NODE_STATE_DOWN)) {
				sinfo_ptr->nodes_other += total_nodes;
				sinfo_ptr->cpus_other += total_cpus;
			} else {
				sinfo_ptr->nodes_idle += total_nodes;
				sinfo_ptr->cpus_idle += total_cpus;
			}

			sinfo_ptr->nodes_total += total_nodes;
			sinfo_ptr->cpus_total += total_cpus;

			return;
		}
	} else {
		if ((base_state == NODE_STATE_ALLOCATED) ||
		    IS_NODE_COMPLETING(node_ptr))
			sinfo_ptr->nodes_alloc += total_nodes;
		else if (IS_NODE_DRAIN(node_ptr)
			 || (base_state == NODE_STATE_DOWN))
			sinfo_ptr->nodes_other += total_nodes;
		else
			sinfo_ptr->nodes_idle += total_nodes;
	}

	sinfo_ptr->nodes_total += total_nodes;


	sinfo_ptr->cpus_alloc += used_cpus;
	sinfo_ptr->cpus_total += total_cpus;
	total_cpus -= used_cpus + error_cpus;

	if (error_cpus) {
		sinfo_ptr->cpus_idle += total_cpus;
		sinfo_ptr->cpus_other += error_cpus;
	} else if (IS_NODE_DRAIN(node_ptr) ||
		   (base_state == NODE_STATE_DOWN)) {
		sinfo_ptr->cpus_other += total_cpus;
	} else
		sinfo_ptr->cpus_idle += total_cpus;
}

static int _insert_node_ptr(List sinfo_list, uint16_t part_num,
			    partition_info_t *part_ptr,
			    node_info_t *node_ptr, uint32_t node_scaling)
{
	int rc = SLURM_SUCCESS;
	sinfo_data_t *sinfo_ptr = NULL;
	ListIterator itr = NULL;

	if (params.cluster_flags & CLUSTER_FLAG_BG) {
		uint16_t error_cpus = 0;
		select_g_select_nodeinfo_get(node_ptr->select_nodeinfo,
					     SELECT_NODEDATA_SUBCNT,
					     NODE_STATE_ERROR,
					     &error_cpus);

		if (error_cpus && !node_ptr->reason)
			node_ptr->reason = xstrdup("Block(s) in error state");
	}

	itr = list_iterator_create(sinfo_list);
	while ((sinfo_ptr = list_next(itr))) {
		if (!_match_part_data(sinfo_ptr, part_ptr))
			continue;
		if (sinfo_ptr->nodes_total &&
		    (!_match_node_data(sinfo_ptr, node_ptr)))
			continue;
		_update_sinfo(sinfo_ptr, node_ptr, node_scaling);
		break;
	}
	list_iterator_destroy(itr);

	/* if no match, create new sinfo_data entry */
	if (!sinfo_ptr)
		list_append(sinfo_list,
			    _create_sinfo(part_ptr, part_num,
					  node_ptr, node_scaling));

	return rc;
}

static int _handle_subgrps(List sinfo_list, uint16_t part_num,
			   partition_info_t *part_ptr,
			   node_info_t *node_ptr, uint32_t node_scaling)
{
	uint16_t size;
	int *node_state;
	int i=0, state_cnt = 2;
	ListIterator iterator = NULL;
	enum node_states state[] =
		{ NODE_STATE_ALLOCATED, NODE_STATE_ERROR };

	/* If we ever update the hostlist stuff to support this stuff
	 * then we can use this to tack on the end of the node name
	 * the subgrp stuff.  On bluegene systems this would be nice
	 * to see the ionodes in certain states.
	 */
	if (params.state_list)
		iterator = list_iterator_create(params.state_list);

	for(i=0; i<state_cnt; i++) {
		if(iterator) {
			node_info_t tmp_node, *tmp_node_ptr = &tmp_node;
			while ((node_state = list_next(iterator))) {
				tmp_node_ptr->node_state = *node_state;
				if((((state[i] == NODE_STATE_ALLOCATED)
				     && IS_NODE_DRAINING(tmp_node_ptr))
				    || (*node_state == NODE_STATE_DRAIN))
				   || (*node_state == state[i]))
					break;
			}
			list_iterator_reset(iterator);
			if(!node_state)
				continue;
		}
		if(select_g_select_nodeinfo_get(node_ptr->select_nodeinfo,
						SELECT_NODEDATA_SUBCNT,
						state[i],
						&size) == SLURM_SUCCESS
		   && size) {
			node_scaling -= size;
			node_ptr->node_state &= NODE_STATE_FLAGS;
			node_ptr->node_state |= state[i];
			_insert_node_ptr(sinfo_list, part_num, part_ptr,
					 node_ptr, size);
		}
	}

	/* now handle the idle */
	if(iterator) {
		while ((node_state = list_next(iterator))) {
			node_info_t tmp_node, *tmp_node_ptr = &tmp_node;
			tmp_node_ptr->node_state = *node_state;
			if(((*node_state == NODE_STATE_DRAIN)
			    || IS_NODE_DRAINED(tmp_node_ptr))
			   || (*node_state == NODE_STATE_IDLE))
				break;
		}
		list_iterator_destroy(iterator);
		if(!node_state)
			return SLURM_SUCCESS;
	}
	node_ptr->node_state &= NODE_STATE_FLAGS;
	node_ptr->node_state |= NODE_STATE_IDLE;
	if((int)node_scaling > 0)
		_insert_node_ptr(sinfo_list, part_num, part_ptr,
				 node_ptr, node_scaling);

	return SLURM_SUCCESS;
}

/*
 * _create_sinfo - create an sinfo record for the given node and partition
 * sinfo_list IN/OUT - table of accumulated sinfo records
 * part_ptr IN       - pointer to partition record to add
 * part_inx IN       - index of partition record (0-origin)
 * node_ptr IN       - pointer to node record to add
 */
static sinfo_data_t *_create_sinfo(partition_info_t* part_ptr,
				   uint16_t part_inx, node_info_t *node_ptr,
				   uint32_t node_scaling)
{
	sinfo_data_t *sinfo_ptr;
	/* create an entry */
	sinfo_ptr = xmalloc(sizeof(sinfo_data_t));

	sinfo_ptr->part_info = part_ptr;
	sinfo_ptr->part_inx = part_inx;
	sinfo_ptr->nodes = hostlist_create("");

	if (node_ptr)
		_update_sinfo(sinfo_ptr, node_ptr, node_scaling);

	return sinfo_ptr;
}

static void _sinfo_list_delete(void *data)
{
	sinfo_data_t *sinfo_ptr = data;

	hostlist_destroy(sinfo_ptr->nodes);
	xfree(sinfo_ptr);
}

/* like strcmp, but works with NULL pointers */
static int _strcmp(char *data1, char *data2)
{
	static char null_str[] = "(null)";

	if (data1 == NULL)
		data1 = null_str;
	if (data2 == NULL)
		data2 = null_str;
	return strcmp(data1, data2);
}

