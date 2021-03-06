/*****************************************************************************\
 *  state_test.c - Test state of Bluegene base partitions and switches.
 *  DRAIN nodes in SLURM that are not usable.
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Dan Phung <phung4@llnl.gov> and Morris Jette <jette1@llnl.gov>
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

#include <stdio.h>
#include <string.h>
#include <slurm/slurm.h>

#include "src/common/log.h"
#include "src/slurmctld/proc_req.h"
#include "src/slurmctld/slurmctld.h"
#include "bluegene.h"

#define BUFSIZE 4096

#ifdef HAVE_BG_FILES

/* Find the specified BlueGene node ID and drain it from SLURM */
static void _configure_node_down(rm_bp_id_t bp_id, my_bluegene_t *my_bg)
{
	int bp_num, i, rc;
	rm_bp_id_t bpid;
	rm_BP_t *my_bp;
	rm_location_t bp_loc;
	rm_BP_state_t bp_state;
	char bg_down_node[128];

	if ((rc = bridge_get_data(my_bg, RM_BPNum, &bp_num)) != STATUS_OK) {
		error("bridge_get_data(RM_BPNum): %s", bg_err_str(rc));
		bp_num = 0;
	}

	for (i=0; i<bp_num; i++) {
		if (i) {
			if ((rc = bridge_get_data(my_bg, RM_NextBP, &my_bp))
			    != STATUS_OK) {
				error("bridge_get_data(RM_NextBP): %s",
				      bg_err_str(rc));
				continue;
			}
		} else {
			if ((rc = bridge_get_data(my_bg, RM_FirstBP, &my_bp))
			    !=
			    STATUS_OK) {
				error("bridge_get_data(RM_FirstBP): %s",
				      bg_err_str(rc));
				continue;
			}
		}

		if ((rc = bridge_get_data(my_bp, RM_BPID, &bpid))
		    != STATUS_OK) {
			error("bridge_get_data(RM_BPID): %s", bg_err_str(rc));
			continue;
		}

		if (!bpid) {
			error("No BPID was returned from database");
			continue;
		}

		if (strcmp(bp_id, bpid) != 0) {	/* different base partition */
			free(bpid);
			continue;
		}
		free(bpid);

		if ((rc = bridge_get_data(my_bp, RM_BPState, &bp_state))
		    != STATUS_OK) {
			error("bridge_get_data(RM_BPState): %s",
			      bg_err_str(rc));
			continue;
		}
		if  (bp_state != RM_BP_UP) 		/* already down */
			continue;

		if ((rc = bridge_get_data(my_bp, RM_BPLoc, &bp_loc))
		    != STATUS_OK) {
			error("bridge_get_data(RM_BPLoc): %s", bg_err_str(rc));
			continue;
		}

		/* make sure we have this midplane in the system */
		if (bp_loc.X >= DIM_SIZE[X]
		    || bp_loc.Y >= DIM_SIZE[Y]
		    || bp_loc.Z >= DIM_SIZE[Z]) {
			debug4("node %s%c%c%c isn't configured",
			       bg_conf->slurm_node_prefix,
			       alpha_num[bp_loc.X], alpha_num[bp_loc.Y],
			       alpha_num[bp_loc.Z]);
			continue;
		}

		snprintf(bg_down_node, sizeof(bg_down_node), "%s%c%c%c",
			 bg_conf->slurm_node_prefix,
			 alpha_num[bp_loc.X], alpha_num[bp_loc.Y],
			 alpha_num[bp_loc.Z]);


		if (node_already_down(bg_down_node))
			break;

		error("switch for node %s is bad", bg_down_node);
		slurm_drain_nodes(bg_down_node,
				  "select_bluegene: MMCS switch not UP",
				  slurm_get_slurm_user_id());
		break;
	}
}

static char *_get_bp_node_name(rm_BP_t *bp_ptr)
{
	rm_location_t bp_loc;
	int rc;

	errno = SLURM_SUCCESS;

	if ((rc = bridge_get_data(bp_ptr, RM_BPLoc, &bp_loc))
	    != STATUS_OK) {
		error("bridge_get_data(RM_BPLoc): %s", bg_err_str(rc));
		errno = SLURM_ERROR;
		return NULL;
	}

	/* make sure we have this midplane in the system */
	if (bp_loc.X >= DIM_SIZE[X]
	    || bp_loc.Y >= DIM_SIZE[Y]
	    || bp_loc.Z >= DIM_SIZE[Z]) {
		debug4("node %s%c%c%c isn't configured",
		       bg_conf->slurm_node_prefix,
		       alpha_num[bp_loc.X], alpha_num[bp_loc.Y],
		       alpha_num[bp_loc.Z]);
		return NULL;
	}

	return xstrdup_printf("%s%c%c%c",
			      bg_conf->slurm_node_prefix,
			      alpha_num[bp_loc.X], alpha_num[bp_loc.Y],
			      alpha_num[bp_loc.Z]);
}

/* To fake a nodecard down do this on the service node.
   db2 "update bg{l|p}nodecard set status = 'E' where location =
   'Rxx-Mx-Nx' and status='A'"
   Reverse the A, and E to bring it back up.
*/
static int _test_nodecard_state(rm_nodecard_t *ncard, int nc_id,
				char *node_name, bool slurmctld_locked)
{
	int rc = SLURM_SUCCESS;
	rm_nodecard_id_t nc_name = NULL;
	rm_nodecard_state_t state;
	int io_start = 0;

	if ((rc = bridge_get_data(ncard,
				  RM_NodeCardState,
				  &state)) != STATUS_OK) {
		error("bridge_get_data(RM_NodeCardState): %s",
		      bg_err_str(rc));
		return SLURM_ERROR;
	}

	if (state == RM_NODECARD_UP)
		return SLURM_SUCCESS;

	if ((rc = bridge_get_data(ncard,
				  RM_NodeCardID,
				  &nc_name)) != STATUS_OK) {
		error("bridge_get_data(RM_NodeCardID): %s", bg_err_str(rc));
		return SLURM_ERROR;
	}

	if (!nc_name) {
		error("We didn't get an RM_NodeCardID but rc was STATUS_OK?");
		return SLURM_ERROR;
	}

#ifdef HAVE_BGL
	if ((rc = bridge_get_data(ncard,
				  RM_NodeCardQuarter,
				  &io_start)) != STATUS_OK) {
		error("bridge_get_data(CardQuarter): %s", bg_err_str(rc));
		rc = SLURM_ERROR;
		goto clean_up;
	}
	io_start *= bg_conf->quarter_ionode_cnt;
	io_start += bg_conf->nodecard_ionode_cnt * (nc_id%4);
#else
	/* From the first nodecard id we can figure
	   out where to start from with the alloc of ionodes.
	*/
	io_start = atoi((char*)nc_name+1);
	io_start *= bg_conf->io_ratio;
#endif
	/* On small systems with less than a midplane the
	   database may see the nodecards there but in missing
	   state.  To avoid getting a bunch of warnings here just
	   skip over the ones missing.
	*/
	if (io_start >= bg_conf->numpsets) {
		rc = SLURM_SUCCESS;
		if (state == RM_NODECARD_MISSING) {
			debug3("Nodecard %s is missing",
			       nc_name);
		} else {
			error("We don't have the system configured "
			      "for this nodecard %s, we only have "
			      "%d ionodes and this starts at %d",
			      nc_name, io_start, bg_conf->numpsets);
		}
		goto clean_up;
	}

	/* if (!ionode_bitmap) */
	/* 	ionode_bitmap = bit_alloc(bg_conf->numpsets); */
	/* info("setting %s start %d of %d", */
	/*      nc_name,  io_start, bg_conf->numpsets); */
	/* bit_nset(ionode_bitmap, io_start, io_start+io_cnt); */

	/* we have to handle each nodecard separately to make
	   sure we don't create holes in the system */
	if (down_nodecard(node_name, io_start, slurmctld_locked)
	    == SLURM_SUCCESS) {
		debug("nodecard %s on %s is in an error state",
		      nc_name, node_name);
	} else
		debug2("nodecard %s on %s is in an error state, "
		       "but error was returned when trying to make it so",
		       nc_name, node_name);

	/* Here we want to keep track of any nodecard that
	   isn't up and return error if it is in the system. */
	rc = SLURM_ERROR;

clean_up:

	free(nc_name);
	return rc;
}

/*
 * This could potentially lock the node lock in the slurmctld with
 * slurm_drain_node, so if nodes_locked is called we will call the
 * drainning function without locking the lock again.
 */
static int _test_down_nodecards(rm_BP_t *bp_ptr, bool slurmctld_locked)
{
	rm_bp_id_t bp_id = NULL;
	int num = 0;
	int marked_down = 0;
	int i=0;
	int rc = SLURM_SUCCESS;
	rm_nodecard_list_t *ncard_list = NULL;
	rm_nodecard_t *ncard = NULL;
	//bitstr_t *ionode_bitmap = NULL;
	//bg_record_t *bg_record = NULL;
	char *node_name = NULL;
	//int bp_bit = 0;
	//int io_cnt = 1;

	/* Translate 1 nodecard count to ionode count */
/* 	if ((io_cnt *= bg_conf->io_ratio)) */
/* 		io_cnt--; */

	if ((rc = bridge_get_data(bp_ptr, RM_BPID, &bp_id))
	    != STATUS_OK) {
		error("bridge_get_data(RM_BPID): %s",
		      bg_err_str(rc));
		return SLURM_ERROR;
	}

	if ((rc = bridge_get_nodecards(bp_id, &ncard_list))
	    != STATUS_OK) {
		error("bridge_get_nodecards(%s): %d",
		      bp_id, rc);
		rc = SLURM_ERROR;
		goto clean_up;
	}

	/* The node_name will only be NULL if this system doesn't
	   really have the node.
	*/
	if (!(node_name = _get_bp_node_name(bp_ptr))) {
		rc = SLURM_ERROR;
		goto clean_up;
	}

	if ((rc = bridge_get_data(ncard_list, RM_NodeCardListSize, &num))
	    != STATUS_OK) {
		error("bridge_get_data(RM_NodeCardListSize): %s",
		      bg_err_str(rc));
		rc = SLURM_ERROR;
		goto clean_up;
	}

	for(i=0; i<num; i++) {
		if (i) {
			if ((rc = bridge_get_data(ncard_list,
						  RM_NodeCardListNext,
						  &ncard)) != STATUS_OK) {
				error("bridge_get_data"
				      "(RM_NodeCardListNext): %s",
				      bg_err_str(rc));
				rc = SLURM_ERROR;
				goto clean_up;
			}
		} else {
			if ((rc = bridge_get_data(ncard_list,
						  RM_NodeCardListFirst,
						  &ncard)) != STATUS_OK) {
				error("bridge_get_data"
				      "(RM_NodeCardListFirst: %s",
				      bg_err_str(rc));
				rc = SLURM_ERROR;
				goto clean_up;
			}
		}

		if (_test_nodecard_state(ncard, i, node_name, slurmctld_locked)
		    != SLURM_SUCCESS)
			marked_down++;
	}

	/* this code is here to bring up a block after it is in an
	   error state.  It is commented out because it hasn't been
	   tested very well yet.  If you ever want to use this code
	   there should probably be a configurable option in the
	   bluegene.conf file that gives you an option as to have this
	   happen or not automatically.
	*/
/* 	if (ionode_bitmap) { */
/* 		info("got ionode_bitmap"); */

/* 		bit_not(ionode_bitmap); */
/* 		up_nodecard(node_name, ionode_bitmap); */
/* 	} else { */
/* 		int ret = 0; */
/* 		info("no ionode_bitmap"); */
/* 		ListIterator itr = NULL; */
/* 		slurm_mutex_lock(&block_state_mutex); */
/* 		itr = list_iterator_create(bg_lists->main); */
/* 		while ((bg_record = list_next(itr))) { */
/* 			if (bg_record->job_running != BLOCK_ERROR_STATE) */
/* 				continue; */

/* 			if (!bit_test(bg_record->bitmap, bp_bit)) */
/* 				continue; */
/* 			info("bringing %s back to service", */
/* 			     bg_record->bg_block_id); */
/* 			bg_record->job_running = NO_JOB_RUNNING; */
/* 			bg_record->state = RM_PARTITION_FREE; */
/* 			last_bg_update = time(NULL); */
/* 		} */
/* 		list_iterator_destroy(itr); */
/* 		slurm_mutex_unlock(&block_state_mutex); */

/* 		/\* FIX ME: This needs to call the opposite of */
/* 		   slurm_drain_nodes which does not yet exist. */
/* 		*\/ */
/* 		if ((ret = node_already_down(node_name))) { */
/* 			/\* means it was drained *\/ */
/* 			if (ret == 2) { */
/* 				/\* debug("node %s put back into
 * 				service after " *\/ */
/* /\* 				      "being in an error state", *\/ */
/* /\* 				      node_name); *\/ */
/* 			} */
/* 		} */
/* 	} */

clean_up:
	if (ncard_list)
		bridge_free_nodecard_list(ncard_list);
	xfree(node_name);
/* 	if (ionode_bitmap) */
/* 		FREE_NULL_BITMAP(ionode_bitmap); */
	free(bp_id);

	/* If we marked any nodecard down we need to state it here */
	if ((rc == SLURM_SUCCESS) && marked_down)
		rc = SLURM_ERROR;

	return rc;
}

/* Test for nodes that are not UP in MMCS and DRAIN them in SLURM */
static void _test_down_nodes(my_bluegene_t *my_bg)
{
	int bp_num, i, rc;
	rm_BP_t *my_bp;

	debug2("Running _test_down_nodes");
	if ((rc = bridge_get_data(my_bg, RM_BPNum, &bp_num)) != STATUS_OK) {
		error("bridge_get_data(RM_BPNum): %s", bg_err_str(rc));
		bp_num = 0;
	}
	for (i=0; i<bp_num; i++) {
		if (i) {
			if ((rc = bridge_get_data(my_bg, RM_NextBP, &my_bp))
			    != STATUS_OK) {
				error("bridge_get_data(RM_NextBP): %s",
				      bg_err_str(rc));
				continue;
			}
		} else {
			if ((rc = bridge_get_data(my_bg, RM_FirstBP, &my_bp))
			    != STATUS_OK) {
				error("bridge_get_data(RM_FirstBP): %s",
				      bg_err_str(rc));
				continue;
			}
		}

		_test_down_nodecards(my_bp, 0);
	}
}

/* Test for switches that are not UP in MMCS,
 * when found DRAIN them in SLURM and configure their base partition DOWN */
static void _test_down_switches(my_bluegene_t *my_bg)
{
	int switch_num, i, rc;
	rm_switch_t *my_switch;
	rm_bp_id_t bp_id;
	rm_switch_state_t switch_state;

	debug2("Running _test_down_switches");
	if ((rc = bridge_get_data(my_bg, RM_SwitchNum, &switch_num))
	    != STATUS_OK) {
		error("bridge_get_data(RM_SwitchNum): %s", bg_err_str(rc));
		switch_num = 0;
	}
	for (i=0; i<switch_num; i++) {
		if (i) {
			if ((rc = bridge_get_data(my_bg, RM_NextSwitch,
						  &my_switch))
			    != STATUS_OK) {
				error("bridge_get_data(RM_NextSwitch): %s",
				      bg_err_str(rc));
				continue;
			}
		} else {
			if ((rc = bridge_get_data(my_bg, RM_FirstSwitch,
						  &my_switch))
			    != STATUS_OK) {
				error("bridge_get_data(RM_FirstSwitch): %s",
				      bg_err_str(rc));
				continue;
			}
		}

		if ((rc = bridge_get_data(my_switch, RM_SwitchState,
					  &switch_state)) != STATUS_OK) {
			error("bridge_get_data(RM_SwitchState): %s",
			      bg_err_str(rc));
			continue;
		}
		if (switch_state == RM_SWITCH_UP)
			continue;
		if ((rc = bridge_get_data(my_switch, RM_SwitchBPID, &bp_id))
		    != STATUS_OK) {
			error("bridge_get_data(RM_SwitchBPID): %s",
			      bg_err_str(rc));
			continue;
		}

		if (!bp_id) {
			error("No BPID was returned from database");
			continue;
		}

		_configure_node_down(bp_id, my_bg);
		free(bp_id);
	}
}
#endif

/* Determine if specific slurm node is already in DOWN or DRAIN state */
extern int node_already_down(char *node_name)
{
	struct node_record *node_ptr = find_node_record(node_name);

	if (node_ptr) {
		if (IS_NODE_DRAIN(node_ptr))
			return 2;
		else if (IS_NODE_DOWN(node_ptr))
			return 1;
		else
			return 0;
	}

	return 0;
}

/*
 * Search MMCS for failed switches and nodes. Failed resources are DRAINED in
 * SLURM. This relies upon rm_get_BG(), which is slow (10+ seconds) so run
 * this test infrequently.
 */
extern void test_mmcs_failures(void)
{
#ifdef HAVE_BG_FILES
	my_bluegene_t *local_bg;
	int rc;

	if ((rc = bridge_get_bg(&local_bg)) != STATUS_OK) {

		error("bridge_get_BG(): %s", bg_err_str(rc));
		return;
	}


	_test_down_switches(local_bg);
	_test_down_nodes(local_bg);
	if ((rc = bridge_free_bg(local_bg)) != STATUS_OK)
		error("bridge_free_BG(): %s", bg_err_str(rc));
#endif
}


/*
 * This could potentially lock the node lock in the slurmctld with
 * slurm_drain_node, so if slurmctld_locked is called we will call the
 * drainning function without locking the lock again.
 */
extern int check_block_bp_states(char *bg_block_id, bool slurmctld_locked)
{
	int rc = SLURM_SUCCESS;
#ifdef HAVE_BG_FILES
	rm_partition_t *block_ptr = NULL;
	rm_BP_t *bp_ptr = NULL;
	int cnt = 0;
	int i = 0;
	bool small = false;

	/* If no bg_record->bg_block_id we don't need to check this
	   since this block isn't really created.
	*/
	if (!bg_block_id)
		return SLURM_SUCCESS;

	if ((rc = bridge_get_block(bg_block_id, &block_ptr)) != STATUS_OK) {
		error("Block %s doesn't exist.", bg_block_id);
		rc = SLURM_ERROR;

		goto done;
	}


	if ((rc = bridge_get_data(block_ptr, RM_PartitionSmall, &small))
	    != STATUS_OK) {
		error("bridge_get_data(RM_PartitionSmall): %s",
		      bg_err_str(rc));
		rc = SLURM_ERROR;

		goto cleanup;
	}

	if (small) {
		rm_nodecard_t *ncard = NULL;
		char *node_name = NULL;

		/* If this is a small block we can just check the
		   nodecard list of the block.
		*/
		if ((rc = bridge_get_data(block_ptr,
					  RM_PartitionNodeCardNum,
					  &cnt))
		    != STATUS_OK) {
			error("bridge_get_data(RM_PartitionNodeCardNum): %s",
			      bg_err_str(rc));
			rc = SLURM_ERROR;
			goto cleanup;
		}

		if ((rc = bridge_get_data(block_ptr,
					  RM_PartitionFirstBP,
					  &bp_ptr))
		    != STATUS_OK) {
			error("bridge_get_data(RM_FirstBP): %s",
			      bg_err_str(rc));
			rc = SLURM_ERROR;
			goto cleanup;
		}

		if (!(node_name = _get_bp_node_name(bp_ptr))) {
			rc = errno;
			goto cleanup;
		}

		for(i=0; i<cnt; i++) {
			int nc_id = 0;
			if (i) {
				if ((rc = bridge_get_data(
					     block_ptr,
					     RM_PartitionNextNodeCard,
					     &ncard))
				    != STATUS_OK) {
					error("bridge_get_data("
					      "RM_PartitionNextNodeCard): %s",
					      bg_err_str(rc));
					rc = SLURM_ERROR;
					break;
				}
			} else {
				if ((rc = bridge_get_data(
					     block_ptr,
					     RM_PartitionFirstNodeCard,
					     &ncard))
				    != STATUS_OK) {
					error("bridge_get_data("
					      "RM_PartitionFirstNodeCard): %s",
					      bg_err_str(rc));
					rc = SLURM_ERROR;
					break;
				}
			}
#ifdef HAVE_BGL
			find_nodecard_num(block_ptr, ncard, &nc_id);
#endif
			/* If we find any nodecards in an error state just
			   break here since we are seeing if we can run.  If
			   any nodecard is down this can't happen.
			*/
			if (_test_nodecard_state(
				    ncard, nc_id, node_name, slurmctld_locked)
			    != SLURM_SUCCESS) {
				rc = SLURM_ERROR;
				break;
			}
		}
		xfree(node_name);
		goto cleanup;
	}

	/* If this isn't a small block we have to check the list of
	   nodecards on each midplane.
	*/
	if ((rc = bridge_get_data(block_ptr, RM_PartitionBPNum, &cnt))
	    != STATUS_OK) {
		error("bridge_get_data(RM_BPNum): %s", bg_err_str(rc));
		rc = SLURM_ERROR;
		goto cleanup;
	}

	for(i=0; i<cnt; i++) {
		if (i) {
			if ((rc = bridge_get_data(block_ptr,
						  RM_PartitionNextBP,
						  &bp_ptr))
			    != STATUS_OK) {
				error("bridge_get_data(RM_NextBP): %s",
				      bg_err_str(rc));
				rc = SLURM_ERROR;
				break;
			}
		} else {
			if ((rc = bridge_get_data(block_ptr,
						  RM_PartitionFirstBP,
						  &bp_ptr))
			    != STATUS_OK) {
				error("bridge_get_data(RM_FirstBP): %s",
				      bg_err_str(rc));
				rc = SLURM_ERROR;
				break;
			}
		}

		/* If we find any nodecards in an error state just
		   break here since we are seeing if we can run.  If
		   any nodecard is down this can't happen.
		*/
		if (_test_down_nodecards(bp_ptr, slurmctld_locked)
		    != SLURM_SUCCESS) {
			rc = SLURM_ERROR;
			break;
		}
	}

cleanup:
	bridge_free_block(block_ptr);
done:
#endif
	return rc;

}

