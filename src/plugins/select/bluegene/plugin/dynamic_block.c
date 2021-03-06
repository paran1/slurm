/*****************************************************************************\
 *  dynamic_block.c - functions for creating blocks in a dynamic environment.
 *
 *  $Id: dynamic_block.c 12954 2008-01-04 20:37:49Z da $
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#include "dynamic_block.h"

static int _split_block(List block_list, List new_blocks,
			bg_record_t *bg_record, int cnodes);

static int _breakup_blocks(List block_list, List new_blocks,
			   ba_request_t *request, List my_block_list,
			   bool only_free, bool only_small);

/*
 * create_dynamic_block - create new block(s) to be used for a new
 * job allocation.
 * RET - a list of created block(s) or NULL on failure errno is set.
 */
extern List create_dynamic_block(List block_list,
				 ba_request_t *request, List my_block_list,
				 bool track_down_nodes)
{
	int rc = SLURM_SUCCESS;

	ListIterator itr, itr2;
	bg_record_t *bg_record = NULL, *found_record = NULL;
	List results = NULL;
	List new_blocks = NULL;
	bitstr_t *my_bitmap = NULL;
	blockreq_t blockreq;
	int cnodes = request->procs / bg_conf->cpu_ratio;
	char *unusable_nodes = NULL;

	if (cnodes < bg_conf->smallest_block) {
		error("Can't create this size %d "
		      "on this system numpsets is %d",
		      request->procs,
		      bg_conf->numpsets);
		goto finished;
	}
	memset(&blockreq, 0, sizeof(blockreq_t));

	/* We need to lock this just incase a blocks_overlap is called
	   which will in turn reset and set the system as it sees fit.
	*/
	slurm_mutex_lock(&block_state_mutex);
	if (my_block_list) {
		reset_ba_system(track_down_nodes);
		itr = list_iterator_create(my_block_list);
		while ((bg_record = list_next(itr))) {
			if (bg_record->magic != BLOCK_MAGIC) {
				/* This should never happen since we
				   only call this on copies of blocks
				   and we check on this during the
				   copy.
				*/
				error("create_dynamic_block: "
				      "got a block with bad magic?");
				continue;
			}
			if (bg_record->free_cnt) {
				if (bg_conf->slurm_debug_flags
				    & DEBUG_FLAG_BG_PICK) {
					char *start_geo = give_geo(
						bg_record->start);
					char *geo = give_geo(bg_record->geo);

					info("not adding %s(%s) %s %s %s %u "
					     "(free_cnt)",
					     bg_record->bg_block_id,
					     bg_record->nodes,
					     bg_block_state_string(
						     bg_record->state),
					     start_geo,
					     geo,
					     bg_record->node_cnt);
					xfree(start_geo);
					xfree(geo);
				}
				continue;
			}

			if (!my_bitmap) {
				my_bitmap =
					bit_alloc(bit_size(bg_record->bitmap));
			}

			if (!bit_super_set(bg_record->bitmap, my_bitmap)) {
				bit_or(my_bitmap, bg_record->bitmap);

				if (bg_conf->slurm_debug_flags
				    & DEBUG_FLAG_BG_PICK) {
					char *start_geo =
						give_geo(bg_record->start);
					char *geo =
						give_geo(bg_record->geo);

					info("adding %s(%s) %s %s %s %u",
					     bg_record->bg_block_id,
					     bg_record->nodes,
					     bg_block_state_string(
						     bg_record->state),
					     start_geo, geo,
					     bg_record->node_cnt);
					xfree(start_geo);
					xfree(geo);
				}
				if (check_and_set_node_list(
					    bg_record->bg_block_list)
				    == SLURM_ERROR) {
					if (bg_conf->slurm_debug_flags
					    & DEBUG_FLAG_BG_PICK)
						info("something happened in "
						     "the load of %s",
						     bg_record->bg_block_id);
					list_iterator_destroy(itr);
					FREE_NULL_BITMAP(my_bitmap);
					rc = SLURM_ERROR;
					goto finished;
				}
			} else if (bg_conf->slurm_debug_flags
				   & DEBUG_FLAG_BG_PICK) {
				char *start_geo = give_geo(bg_record->start);
				char *geo = give_geo(bg_record->geo);

				info("not adding %s(%s) %s %s %s %u ",
				     bg_record->bg_block_id,
				     bg_record->nodes,
				     bg_block_state_string(
					     bg_record->state),
				     start_geo,
				     geo,
				     bg_record->node_cnt);
				xfree(start_geo);
				xfree(geo);
			}
		}
		list_iterator_destroy(itr);
		FREE_NULL_BITMAP(my_bitmap);
	} else {
		reset_ba_system(false);
		if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
			info("No list was given");
	}

	if (request->avail_node_bitmap) {
		bitstr_t *bitmap = bit_alloc(node_record_count);

		/* we want the bps that aren't in this partition to
		 * mark them as used
		 */
		bit_or(bitmap, request->avail_node_bitmap);
		bit_not(bitmap);
		unusable_nodes = bitmap2node_name(bitmap);

		//info("not using %s", nodes);
		removable_set_bps(unusable_nodes);

		FREE_NULL_BITMAP(bitmap);
	}

	if (request->size==1 && cnodes < bg_conf->bp_node_cnt) {
		switch(cnodes) {
#ifdef HAVE_BGL
		case 32:
			blockreq.small32 = 4;
			blockreq.small128 = 3;
			break;
		case 128:
			blockreq.small128 = 4;
			break;
#else
		case 16:
			blockreq.small16 = 2;
			blockreq.small32 = 1;
			blockreq.small64 = 1;
			blockreq.small128 = 1;
			blockreq.small256 = 1;
			break;
		case 32:
			blockreq.small32 = 2;
			blockreq.small64 = 1;
			blockreq.small128 = 1;
			blockreq.small256 = 1;
			break;
		case 64:
			blockreq.small64 = 2;
			blockreq.small128 = 1;
			blockreq.small256 = 1;
			break;
		case 128:
			blockreq.small128 = 2;
			blockreq.small256 = 1;
			break;
		case 256:
			blockreq.small256 = 2;
			break;
#endif
		default:
			error("This size %d is unknown on this system", cnodes);
			goto finished;
			break;
		}

		/* Sort the list so the small blocks are in the order
		 * of ionodes. */
		list_sort(block_list, (ListCmpF)bg_record_cmpf_inc);
		request->conn_type = SELECT_SMALL;
		new_blocks = list_create(destroy_bg_record);
		/* check only blocks that are free and small */
		if (_breakup_blocks(block_list, new_blocks,
				    request, my_block_list,
				    true, true)
		    == SLURM_SUCCESS)
			goto finished;

		/* check only blocks that are free and any size */
		if (_breakup_blocks(block_list, new_blocks,
				    request, my_block_list,
				    true, false)
		    == SLURM_SUCCESS)
			goto finished;

		/* check usable blocks that are small with any state */
		if (_breakup_blocks(block_list, new_blocks,
				    request, my_block_list,
				    false, true)
		    == SLURM_SUCCESS)
			goto finished;

		/* check all usable blocks */
		if (_breakup_blocks(block_list, new_blocks,
				    request, my_block_list,
				    false, false)
		    == SLURM_SUCCESS)
			goto finished;

		/* Re-sort the list back to the original order. */
		list_sort(block_list, (ListCmpF)bg_record_sort_aval_inc);
		list_destroy(new_blocks);
		new_blocks = NULL;
		if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
			info("small block not able to be placed inside others");
	}

	if (request->conn_type == SELECT_NAV)
		request->conn_type = SELECT_TORUS;

	//debug("going to create %d", request->size);
	if (!new_ba_request(request)) {
		if (request->geometry[X] != (uint16_t)NO_VAL) {
			char *geo = give_geo(request->geometry);
			error("Problems with request for size %d geo %s",
			      request->size, geo);
			xfree(geo);
		} else {
			error("Problems with request for size %d.  "
			      "No geo given.",
			      request->size);
		}
		rc = ESLURM_INTERCONNECT_FAILURE;
		goto finished;
	}

	/* try on free midplanes */
	rc = SLURM_SUCCESS;
	if (results)
		list_flush(results);
	else
		results = list_create(NULL);

	if (allocate_block(request, results))
		goto setup_records;

	if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
		info("allocate failure for size %d base "
		     "partitions of free midplanes",
		     request->size);
	rc = SLURM_ERROR;

	if (!list_count(block_list) || !my_block_list)
		goto finished;

	/*Try to put block starting in the smallest of the exisiting blocks*/
	itr = list_iterator_create(block_list);
	itr2 = list_iterator_create(block_list);
	while ((bg_record = (bg_record_t *) list_next(itr)) != NULL) {
		/* never check a block being deleted or with a job running */
		if (bg_record->free_cnt
		    || bg_record->job_running != NO_JOB_RUNNING)
			continue;

		/* Here we are only looking for the first
		   block on the midplane.  So either the count
		   is greater or equal than
		   bg_conf->bp_node_cnt or the first bit is
		   set in the ionode_bitmap.
		*/
		if (bg_record->node_cnt < bg_conf->bp_node_cnt) {
			bool found = 0;
			if (bit_ffs(bg_record->ionode_bitmap) != 0)
				continue;
			/* Check to see if we have other blocks in
			   this midplane that have jobs running.
			*/
			while ((found_record = list_next(itr2))) {
				if ((found_record->job_running
				     != NO_JOB_RUNNING)
				    && bit_overlap(bg_record->bitmap,
						   found_record->bitmap)) {
					found = 1;
					break;
				}
			}
			list_iterator_reset(itr2);
			if (found)
				continue;
		}

		if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
			info("removing %s for request %d",
			     bg_record->nodes, request->size);
		remove_block(bg_record->bg_block_list, (int)NO_VAL,
			     (int)bg_record->conn_type);
		/* need to set any unusable nodes that this last block
		   used */
		removable_set_bps(unusable_nodes);
		rc = SLURM_SUCCESS;
		if (results)
			list_flush(results);
		else
			results = list_create(NULL);
		if (allocate_block(request, results))
			break;

		if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
			info("allocate failure for size %d base partitions",
			     request->size);
		rc = SLURM_ERROR;
	}
	list_iterator_destroy(itr);
	list_iterator_destroy(itr2);

setup_records:
	if (rc == SLURM_SUCCESS) {
		/*set up bg_record(s) here */
		new_blocks = list_create(destroy_bg_record);

		blockreq.block = request->save_name;
#ifdef HAVE_BGL
		blockreq.blrtsimage = request->blrtsimage;
#endif
		blockreq.linuximage = request->linuximage;
		blockreq.mloaderimage = request->mloaderimage;
		blockreq.ramdiskimage = request->ramdiskimage;
		blockreq.conn_type = request->conn_type;

		add_bg_record(new_blocks, results, &blockreq, 0, 0);
	}

finished:
	reset_all_removed_bps();
	slurm_mutex_unlock(&block_state_mutex);

	xfree(unusable_nodes);
	xfree(request->save_name);

	if (request->elongate_geos) {
		list_destroy(request->elongate_geos);
		request->elongate_geos = NULL;
	}
	if (results)
		list_destroy(results);
	errno = rc;

	return new_blocks;
}

extern bg_record_t *create_small_record(bg_record_t *bg_record,
					bitstr_t *ionodes, int size)
{
	bg_record_t *found_record = NULL;
	ba_node_t *new_ba_node = NULL;
	ba_node_t *ba_node = NULL;
	char bitstring[BITSIZE];

	found_record = (bg_record_t*) xmalloc(sizeof(bg_record_t));
	found_record->magic = BLOCK_MAGIC;

	found_record->job_running = NO_JOB_RUNNING;
	found_record->user_name = xstrdup(bg_record->user_name);
	found_record->user_uid = bg_record->user_uid;
	found_record->bg_block_list = list_create(destroy_ba_node);
	if (bg_record->bg_block_list)
		ba_node = list_peek(bg_record->bg_block_list);
	if (!ba_node) {
		if (bg_record->nodes) {
			hostlist_t hl = hostlist_create(bg_record->nodes);
			char *host = hostlist_shift(hl);
			hostlist_destroy(hl);
			found_record->nodes = xstrdup(host);
			free(host);
			error("you gave me a list with no ba_nodes using %s",
			      found_record->nodes);
		} else {
			found_record->nodes = xstrdup_printf(
				"%s%c%c%c",
				bg_conf->slurm_node_prefix,
				alpha_num[found_record->start[X]],
				alpha_num[found_record->start[Y]],
				alpha_num[found_record->start[Z]]);
			error("you gave me a record with no ba_nodes "
			      "and no nodes either using %s",
			      found_record->nodes);
		}
	} else {
		int i=0,j=0;
		new_ba_node = ba_copy_node(ba_node);
		for (i=0; i<SYSTEM_DIMENSIONS; i++){
			for(j=0;j<NUM_PORTS_PER_NODE;j++) {
				ba_node->axis_switch[i].int_wire[j].used = 0;
				if (i!=X) {
					if (j==3 || j==4)
						ba_node->axis_switch[i].
							int_wire[j].
							used = 1;
				}
				ba_node->axis_switch[i].int_wire[j].
					port_tar = j;
			}
		}
		list_append(found_record->bg_block_list, new_ba_node);
		found_record->bp_count = 1;
		found_record->nodes = xstrdup_printf(
			"%s%c%c%c",
			bg_conf->slurm_node_prefix,
			alpha_num[ba_node->coord[X]],
			alpha_num[ba_node->coord[Y]],
			alpha_num[ba_node->coord[Z]]);
	}
#ifdef HAVE_BGL
	found_record->node_use = SELECT_COPROCESSOR_MODE;
	found_record->blrtsimage = xstrdup(bg_record->blrtsimage);
#endif
	found_record->linuximage = xstrdup(bg_record->linuximage);
	found_record->mloaderimage = xstrdup(bg_record->mloaderimage);
	found_record->ramdiskimage = xstrdup(bg_record->ramdiskimage);

	process_nodes(found_record, false);

	found_record->conn_type = SELECT_SMALL;

	xassert(bg_conf->cpu_ratio);
	found_record->cpu_cnt = bg_conf->cpu_ratio * size;
	found_record->node_cnt = size;

	found_record->ionode_bitmap = bit_copy(ionodes);
	bit_fmt(bitstring, BITSIZE, found_record->ionode_bitmap);
	found_record->ionodes = xstrdup(bitstring);
	if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
		info("made small block of %s[%s]",
		     found_record->nodes, found_record->ionodes);
	return found_record;
}

/*********************** Local Functions *************************/

static int _split_block(List block_list, List new_blocks,
			bg_record_t *bg_record, int cnodes)
{
	bool full_bp = false;
	bitoff_t start = 0;
	blockreq_t blockreq;

	memset(&blockreq, 0, sizeof(blockreq_t));

	switch(bg_record->node_cnt) {
#ifdef HAVE_BGL
	case 32:
		error("We got a 32 we should never have this");
		goto finished;
		break;
	case 128:
		switch(cnodes) {
		case 32:
			blockreq.small32 = 4;
			break;
		default:
			error("We don't make a %d from size %d",
			      cnodes, bg_record->node_cnt);
			goto finished;
			break;
		}
		break;
	default:
		switch(cnodes) {
		case 32:
			blockreq.small32 = 4;
			blockreq.small128 = 3;
			break;
		case 128:
			blockreq.small128 = 4;
			break;
		default:
			error("We don't make a %d from size %d",
			      cnodes, bg_record->node_cnt);
			goto finished;
			break;
		}
		full_bp = true;
		break;
#else
	case 16:
		error("We got a 16 we should never have this");
		goto finished;
		break;
	case 32:
		switch(cnodes) {
		case 16:
			blockreq.small16 = 2;
			break;
		default:
			error("We don't make a %d from size %d",
			      cnodes, bg_record->node_cnt);
			goto finished;
			break;
		}
		break;
	case 64:
		switch(cnodes) {
		case 16:
			blockreq.small16 = 2;
			blockreq.small32 = 1;
			break;
		case 32:
			blockreq.small32 = 2;
			break;
		default:
			error("We don't make a %d from size %d",
			      cnodes, bg_record->node_cnt);
			goto finished;
			break;
		}
		break;
	case 128:
		switch(cnodes) {
		case 16:
			blockreq.small16 = 2;
			blockreq.small32 = 1;
			blockreq.small64 = 1;
			break;
		case 32:
			blockreq.small32 = 2;
			blockreq.small64 = 1;
			break;
		case 64:
			blockreq.small64 = 2;
			break;
		default:
			error("We don't make a %d from size %d",
			      cnodes, bg_record->node_cnt);
			goto finished;
			break;
		}
		break;
	case 256:
		switch(cnodes) {
		case 16:
			blockreq.small16 = 2;
			blockreq.small32 = 1;
			blockreq.small64 = 1;
			blockreq.small128 = 1;
			break;
		case 32:
			blockreq.small32 = 2;
			blockreq.small64 = 1;
			blockreq.small128 = 1;
			break;
		case 64:
			blockreq.small64 = 2;
			blockreq.small128 = 1;
			break;
		case 128:
			blockreq.small128 = 2;
			break;
		default:
			error("We don't make a %d from size %d",
			      cnodes, bg_record->node_cnt);
			goto finished;
			break;
		}
		break;
	default:
		switch(cnodes) {
		case 16:
			blockreq.small16 = 2;
			blockreq.small32 = 1;
			blockreq.small64 = 1;
			blockreq.small128 = 1;
			blockreq.small256 = 1;
			break;
		case 32:
			blockreq.small32 = 2;
			blockreq.small64 = 1;
			blockreq.small128 = 1;
			blockreq.small256 = 1;
			break;
		case 64:
			blockreq.small64 = 2;
			blockreq.small128 = 1;
			blockreq.small256 = 1;
			break;
		case 128:
			blockreq.small128 = 2;
			blockreq.small256 = 1;
			break;
		case 256:
			blockreq.small256 = 2;
			break;
		default:
			error("We don't make a %d from size %d",
			      cnodes, bg_record->node_cnt);
			goto finished;
			break;
		}
		full_bp = true;
		break;
#endif
	}

	if (!full_bp && bg_record->ionode_bitmap) {
		if ((start = bit_ffs(bg_record->ionode_bitmap)) == -1)
			start = 0;
	}

#ifdef HAVE_BGL
	if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
		info("Asking for %u 32CNBlocks, and %u 128CNBlocks "
		     "from a %u block, starting at ionode %d.",
		     blockreq.small32, blockreq.small128,
		     bg_record->node_cnt, start);
#else
	if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
		info("Asking for %u 16CNBlocks, %u 32CNBlocks, "
		     "%u 64CNBlocks, %u 128CNBlocks, and %u 256CNBlocks "
		     "from a %u block, starting at ionode %d.",
		     blockreq.small16, blockreq.small32,
		     blockreq.small64, blockreq.small128,
		     blockreq.small256, bg_record->node_cnt, start);
#endif
	handle_small_record_request(new_blocks, &blockreq, bg_record, start);

finished:
	return SLURM_SUCCESS;
}

static int _breakup_blocks(List block_list, List new_blocks,
			   ba_request_t *request, List my_block_list,
			   bool only_free, bool only_small)
{
	int rc = SLURM_ERROR;
	bg_record_t *bg_record = NULL;
	ListIterator itr = NULL, bit_itr = NULL;
	int total_cnode_cnt=0;
	char tmp_char[256];
	bitstr_t *ionodes = bit_alloc(bg_conf->numpsets);
	int cnodes = request->procs / bg_conf->cpu_ratio;
	int curr_bp_bit = -1;

	if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
		info("cpu_count=%d cnodes=%d o_free=%d o_small=%d",
		     request->procs, cnodes, only_free, only_small);

	switch(cnodes) {
	case 16:
		/* a 16 can go anywhere */
		break;
	case 32:
		bit_itr = list_iterator_create(bg_lists->valid_small32);
		break;
	case 64:
		bit_itr = list_iterator_create(bg_lists->valid_small64);
		break;
	case 128:
		bit_itr = list_iterator_create(bg_lists->valid_small128);
		break;
	case 256:
		bit_itr = list_iterator_create(bg_lists->valid_small256);
		break;
	default:
		error("We shouldn't be here with this size %d", cnodes);
		goto finished;
		break;
	}

	/* First try with free blocks a midplane or less.  Then try with the
	 * smallest blocks.
	 */
	itr = list_iterator_create(block_list);
	while ((bg_record = list_next(itr))) {
		if (bg_record->free_cnt) {
			if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
				info("%s being free for other job(s), skipping",
				     bg_record->bg_block_id);
			continue;
		}
		/* never look at a block if a job is running */
		if (bg_record->job_running != NO_JOB_RUNNING)
			continue;
		/* on the third time through look for just a block
		 * that isn't used */

		/* check for free blocks on the first and second time */
		if (only_free && (bg_record->state != RM_PARTITION_FREE))
			continue;

		/* check small blocks first */
		if (only_small && (bg_record->node_cnt > bg_conf->bp_node_cnt))
			continue;

		if (request->avail_node_bitmap &&
		    !bit_super_set(bg_record->bitmap,
				   request->avail_node_bitmap)) {
			if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
				info("bg block %s has nodes not usable "
				     "by this job",
				     bg_record->bg_block_id);
			continue;
		}

		if (bg_record->node_cnt == cnodes) {
			if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
				info("found it here %s, %s",
				     bg_record->bg_block_id,
				     bg_record->nodes);
			request->save_name = xstrdup_printf(
				"%c%c%c",
				alpha_num[bg_record->start[X]],
				alpha_num[bg_record->start[Y]],
				alpha_num[bg_record->start[Z]]);

			rc = SLURM_SUCCESS;
			goto finished;
		}
		/* lets see if we can combine some small ones */
		if (bg_record->node_cnt < cnodes) {
			char bitstring[BITSIZE];
			bitstr_t *bitstr = NULL;
			int num_over = 0;
			int num_cnodes = bg_record->node_cnt;
			int rec_bp_bit = bit_ffs(bg_record->bitmap);

			if (curr_bp_bit != rec_bp_bit) {
				/* Got a different node than
				 * previously, since the list should
				 * be in order of nodes for small blocks
				 * just clear here since the last node
				 * doesn't have any more. */
				curr_bp_bit = rec_bp_bit;
				bit_nclear(ionodes, 0, (bg_conf->numpsets-1));
				total_cnode_cnt = 0;
			}

			/* On really busy systems we can get
			   overlapping blocks here.  If that is the
			   case only add that which doesn't overlap.
			*/
			if ((num_over = bit_overlap(
				     ionodes, bg_record->ionode_bitmap))) {
				/* Since the smallest block size is
				   the number of cnodes in an io node,
				   just multiply the num_over by that to
				   get the number of cnodes to remove.
				*/
				if ((num_cnodes -=
				     num_over * bg_conf->smallest_block) <= 0)
					continue;
			}
			bit_or(ionodes, bg_record->ionode_bitmap);

			/* check and see if the bits set are a valid
			   combo */
			if (bit_itr) {
				while ((bitstr = list_next(bit_itr))) {
					if (bit_super_set(ionodes, bitstr))
						break;
				}
				list_iterator_reset(bit_itr);
			}
			if (!bitstr) {
				bit_nclear(ionodes, 0, (bg_conf->numpsets-1));
				bit_or(ionodes, bg_record->ionode_bitmap);
				total_cnode_cnt = num_cnodes =
					bg_record->node_cnt;
			} else
				total_cnode_cnt += num_cnodes;

			bit_fmt(bitstring, BITSIZE, ionodes);
			if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
				info("combine adding %s %s %d got %d set "
				     "ionodes %s total is %s",
				     bg_record->bg_block_id, bg_record->nodes,
				     num_cnodes, total_cnode_cnt,
				     bg_record->ionodes, bitstring);
			if (total_cnode_cnt == cnodes) {
				request->save_name = xstrdup_printf(
					"%c%c%c",
					alpha_num[bg_record->start[X]],
					alpha_num[bg_record->start[Y]],
					alpha_num[bg_record->start[Z]]);
				if (!my_block_list) {
					rc = SLURM_SUCCESS;
					goto finished;
				}

				bg_record = create_small_record(bg_record,
								ionodes,
								cnodes);
				list_append(new_blocks, bg_record);

				rc = SLURM_SUCCESS;
				goto finished;
			}
			continue;
		}
		/* we found a block that is bigger than requested */
		break;
	}

	if (bg_record) {
		/* It appears we don't need this original record
		 * anymore, just work off the copy if indeed it is a copy. */

		/* bg_record_t *found_record = NULL; */

		/* if (bg_record->original) { */
		/* 	if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK) */
		/* 		info("1 This was a copy %s", */
		/* 		     bg_record->bg_block_id); */
		/* 	found_record = bg_record->original; */
		/* } else { */
		/* 	if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK) */
		/* 		info("looking for original"); */
		/* 	found_record = find_org_in_bg_list( */
		/* 		bg_lists->main, bg_record); */
		/* } */
		if ((bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK)
		    && bg_record->original
		    && (bg_record->original->magic != BLOCK_MAGIC)) {
			info("This record %s has bad magic, it must be "
			     "getting freed.  No worries it will all be "
			     "figured out later.",
			     bg_record->bg_block_id);
		}

		/* if (!found_record || found_record->magic != BLOCK_MAGIC) { */
		/* 	error("this record wasn't found in the list!"); */
		/* 	rc = SLURM_ERROR; */
		/* 	goto finished; */
		/* } */

		if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_PICK) {
			format_node_name(bg_record, tmp_char,
					 sizeof(tmp_char));
			info("going to split %s, %s",
			     bg_record->bg_block_id,
			     tmp_char);
		}
		request->save_name = xstrdup_printf(
			"%c%c%c",
			alpha_num[bg_record->start[X]],
			alpha_num[bg_record->start[Y]],
			alpha_num[bg_record->start[Z]]);
		if (!my_block_list) {
			rc = SLURM_SUCCESS;
			goto finished;
		}
		_split_block(block_list, new_blocks, bg_record, cnodes);
		rc = SLURM_SUCCESS;
		goto finished;
	}

finished:
	if (bit_itr)
		list_iterator_destroy(bit_itr);

	FREE_NULL_BITMAP(ionodes);
	if (itr)
		list_iterator_destroy(itr);

	return rc;
}
