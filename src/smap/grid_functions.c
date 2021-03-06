/*****************************************************************************\
 *  grid_functions.c - Functions related to curses display of smap.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
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

#include "src/smap/smap.h"

static int _coord(char coord)
{
	if ((coord >= '0') && (coord <= '9'))
		return (coord - '0');
	if ((coord >= 'A') && (coord <= 'Z'))
		return (coord - 'A');
	return -1;
}

extern int set_grid_inx(int start, int end, int count)
{
	int x, y, z;

	for (y = DIM_SIZE[Y] - 1; y >= 0; y--) {
		for (z = 0; z < DIM_SIZE[Z]; z++) {
			for (x = 0; x < DIM_SIZE[X]; x++) {
				if ((ba_system_ptr->grid[x][y][z].index
				     < start)
				||  (ba_system_ptr->grid[x][y][z].index
				     > end))
					continue;
				if ((ba_system_ptr->grid[x][y][z].state
				     == NODE_STATE_DOWN)
				    ||  (ba_system_ptr->grid[x][y][z].state
					 & NODE_STATE_DRAIN))
					continue;

				ba_system_ptr->grid[x][y][z].letter =
					letters[count%62];
				ba_system_ptr->grid[x][y][z].color =
					colors[count%6];
			}
		}
	}
	return 1;
}

extern int set_grid_inx2(char *node_names, int count)
{
	hostlist_t hl;
	hostlist_iterator_t hl_iter;
	char *host;
	int i, x, y, z;

	hl = hostlist_create(node_names);
	hl_iter = hostlist_iterator_create(hl);
	while ((host = hostlist_next(hl_iter))) {
		i = strlen(host);
		x = _coord(host[i-3]);
		y = _coord(host[i-2]);
		z = _coord(host[i-1]);
		ba_system_ptr->grid[x][y][z].letter = letters[count%62];
		ba_system_ptr->grid[x][y][z].color  = colors[count%6];
		free(host);
	}
	hostlist_iterator_destroy(hl_iter);
	return 1;
}

/* This function is only called when HAVE_BG is set */
extern int set_grid_bg(int *start, int *end, int count, int set)
{
	int x=0, y=0, z=0;
	int i = 0;

	assert(end[X] < DIM_SIZE[X]);
	assert(start[X] >= 0);
	assert(count >= 0);
	assert(set >= 0);
	assert(set <= 2);
	assert(end[Y] < DIM_SIZE[Y]);
	assert(start[Y] >= 0);
	assert(end[Z] < DIM_SIZE[Z]);
	assert(start[Z] >= 0);

	for (x = start[X]; x <= end[X]; x++) {
		for (y = start[Y]; y <= end[Y]; y++) {
			for (z = start[Z]; z <= end[Z]; z++) {
				/* set the color and letter of the
				   block if the set flag is specified
				   or if the letter hasn't been set yet
				*/
				if(set
				   || ((ba_system_ptr->grid[x][y][z].letter
					== '.')
				       && (ba_system_ptr->grid[x][y][z].letter
					   != '#'))) {

						ba_system_ptr->
							grid[x][y][z].letter =
							letters[count%62];
						ba_system_ptr->
							grid[x][y][z].color =
							colors[count%6];
				}
				i++;
			}
		}
	}

	return i;
}

/* print_grid - print values of every grid point */
extern void print_grid(int dir)
{
	int x;
	int grid_xcord, grid_ycord = 2;
	int y, z, offset = DIM_SIZE[Z];

	for (y = DIM_SIZE[Y] - 1; y >= 0; y--) {
		offset = DIM_SIZE[Z] + 1;
		for (z = 0; z < DIM_SIZE[Z]; z++) {
			grid_xcord = offset;

			for (x = 0; x < DIM_SIZE[X]; x++) {
				if (ba_system_ptr->grid[x][y][z].color)
					init_pair(ba_system_ptr->
						  grid[x][y][z].color,
						  ba_system_ptr->
						  grid[x][y][z].color,
						  COLOR_BLACK);
				else
					init_pair(ba_system_ptr->
						  grid[x][y][z].color,
						  ba_system_ptr->
						  grid[x][y][z].color,
                                                  7);

				wattron(grid_win,
					COLOR_PAIR(ba_system_ptr->
						   grid[x][y][z].color));

				mvwprintw(grid_win,
					  grid_ycord, grid_xcord, "%c",
					  ba_system_ptr->grid[x][y][z].letter);
				wattroff(grid_win,
					 COLOR_PAIR(ba_system_ptr->
						    grid[x][y][z].color));
				grid_xcord++;
			}
			grid_ycord++;
			offset--;
		}
		grid_ycord++;
	}
	return;
}

bitstr_t *get_requested_node_bitmap()
{
	static bitstr_t *bitmap = NULL;
	static node_info_msg_t *old_node_ptr = NULL, *new_node_ptr;
	int error_code;
	int i = 0;
	node_info_t *node_ptr = NULL;

	if(!params.hl)
		return NULL;

	if (old_node_ptr) {
		error_code =
			slurm_load_node(old_node_ptr->last_update,
					&new_node_ptr, SHOW_ALL);
		if (error_code == SLURM_SUCCESS)
			slurm_free_node_info_msg(old_node_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA)
			return bitmap;
	} else
		error_code = slurm_load_node((time_t) NULL, &new_node_ptr,
					     SHOW_ALL);

	if(bitmap)
		FREE_NULL_BITMAP(bitmap);

	if (error_code) {
		slurm_perror("slurm_load_node");
		return NULL;
	}

	old_node_ptr = new_node_ptr;

	bitmap = bit_alloc(old_node_ptr->record_count);
	for(i=0; i<old_node_ptr->record_count; i++) {
		node_ptr = &(old_node_ptr->node_array[i]);
		if(hostlist_find(params.hl, node_ptr->name) != -1)
			bit_set(bitmap, i);
	}
	return bitmap;
}
