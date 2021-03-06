//
// This file is part of the Catcierge project.
//
// Copyright (c) Joakim Soderberg 2013-2017
//
//    Catcierge is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 2 of the License, or
//    (at your option) any later version.
//
//    Catcierge is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with Catcierge.  If not, see <http://www.gnu.org/licenses/>.
//

#include "catcierge_fsm.h"
#include "catcierge_log.h"


static void catcierge_sigusr_none(catcierge_grb_t *grb)
{
	CATLOG("  Doing nothing...\n");
}

static void catcierge_sigusr_lock(catcierge_grb_t *grb)
{
	CATLOG("  Forcing lockout...\n");
	catcierge_state_transition_lockout(grb);
}

static void catcierge_sigusr_unlock(catcierge_grb_t *grb)
{
	CATLOG("  Forcing unlock...\n");
	catcierge_do_unlock(grb);
	catcierge_set_state(grb, catcierge_state_waiting);
}

static void catcierge_sigusr_ignore(catcierge_grb_t *grb)
{
	CATLOG("  Ignoring events until further notice...\n");
	catcierge_set_state(grb, catcierge_state_ignoring);
}

static void catcierge_sigusr_attention(catcierge_grb_t *grb)
{
	CATLOG("  Stopped ignoring events...\n");
	catcierge_set_state(grb, catcierge_state_waiting);
}

void catcierge_handle_sigusr(catcierge_grb_t *grb, const char *behavior)
{
	#define CATCIERGE_SIGUSR_BEHAVIOR(sigusr_name, sigusr_description) \
		if (!strcasecmp(behavior, #sigusr_name)) \
		{									 	 \
			CATLOG("SIGUSR: " #sigusr_name);	 \
			catcierge_sigusr_##sigusr_name(grb); \
		}										 \
		else

	#include "catcierge_sigusr_types.h"
	{
		CATERR("Error, unknown sigusr behavior (this is probably a bug)...\n");
	}
}
