///////////////////////////////////////////////////////////////////////
//
//  ACE - Quake II Bot Base Code
//
//  Version 1.0
//
//  This file is Copyright(c), Steve Yeager 1998, All Rights Reserved
//
//
//	All other files are Copyright(c) Id Software, Inc.
//
//	Please see liscense.txt in the source directory for the copyright
//	information regarding those files belonging to Id Software, Inc.
//
//	Should you decide to release a modified version of ACE, you MUST
//	include the following text (minus the BEGIN and END lines) in the
//	documentation for your modification.
//
//	--- BEGIN ---
//
//	The ACE Bot is a product of Steve Yeager, and is available from
//	the ACE Bot homepage, at http://www.axionfx.com/ace.
//
//	This program is a modification of the ACE Bot, and is therefore
//	in NO WAY supported by Steve Yeager.

//	This program MUST NOT be sold in ANY form. If you have paid for
//	this product, you should contact Steve Yeager immediately, via
//	the ACE Bot homepage.
//
//	--- END ---
//
//	I, Steve Yeager, hold no responsibility for any harm caused by the
//	use of this source code, especially to small children and animals.
//  It is provided as-is with no implied warranty or support.
//
//  I also wish to thank and acknowledge the great work of others
//  that has helped me to develop this code.
//
//  John Cricket    - For ideas and swapping code.
//  Ryan Feltrin    - For ideas and swapping code.
//  SABIN           - For showing how to do true client based movement.
//  BotEpidemic     - For keeping us up to date.
//  Telefragged.com - For giving ACE a home.
//  Microsoft       - For giving us such a wonderful crash free OS.
//  id              - Need I say more.
//
//  And to all the other testers, pathers, and players and people
//  who I can't remember who the heck they were, but helped out.
//
///////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////
//
//  acebot_nodes.c -   This file contains all of the
//                     pathing routines for the ACE bot.
//
///////////////////////////////////////////////////////////////////////

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "game/g_local.h"
#include "acebot.h"

// flags
qboolean newmap=true;

// Total number of nodes that are items
int numitemnodes;

// Total number of nodes
int bot_numnodes;

// For debugging paths
int show_path_from = -1;
int show_path_to = -1;

// array for node data
node_t nodes[MAX_NODES];
edict_t *node_showents[MAX_NODES];
short int path_table[MAX_NODES][MAX_NODES];

///////////////////////////////////////////////////////////////////////
// NODE INFORMATION FUNCTIONS
///////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////
// Determin cost of moving from one node to another
///////////////////////////////////////////////////////////////////////
int ACEND_FindCost(int from, int to)
{
	int curnode;
	int cost=1; // Shortest possible is 1

	// If we can not get there then return invalid
	if (path_table[from][to] == INVALID)
	{
		//if(debug_mode)
		//		debug_printf("Found invalid path!");
		return INVALID;
	}
	// Otherwise check the path and return the cost
	curnode = path_table[from][to];

	// Find a path (linear time, very fast)
	while(curnode != to)
	{
		curnode = path_table[curnode][to];
		if(curnode == INVALID) // something has corrupted the path abort
		{
			//if(debug_mode)
			//	debug_printf("Found invalid path!");

			return INVALID;
		}
		cost++;
		if(cost > 500) {
			if(debug_mode)
				debug_printf("Cost exceeded maximum!\n");
			break;
		}
	}

	return cost;
}

///////////////////////////////////////////////////////////////////////
// Find a close node to the player within dist.
//
// Faster than looking for the closest node, but not very
// accurate.
///////////////////////////////////////////////////////////////////////
int ACEND_FindCloseReachableNode(edict_t *self, int range, int type)
{
	vec3_t v;
	int i;
	trace_t tr;
	float dist;

	range *= range;

	for(i=0;i<bot_numnodes;i++)
	{
		if(type == NODE_ALL || type == nodes[i].type) // check node type
		{

			VectorSubtract(nodes[i].origin,self->s.origin,v); // subtract first

			dist = v[0]*v[0] + v[1]*v[1] + v[2]*v[2];

			if(dist < range) // square range instead of sqrt
			{
				// make sure it is visible
				//trace = gi.trace (self->s.origin, vec3_origin, vec3_origin, nodes[i].origin, self, MASK_OPAQUE);
				tr = gi.trace (self->s.origin, self->mins, self->maxs, nodes[i].origin, self, BOTMASK_OPAQUE);

				if(tr.fraction == 1.0)
					return i;
			}
		}
	}

	return -1;
}

///////////////////////////////////////////////////////////////////////
// Find the closest node to the player within a certain range
///////////////////////////////////////////////////////////////////////
int ACEND_FindClosestReachableNode(edict_t *self, int range, int type)
{
	int i;
	float closest = 99999;
	float dist;
	int node=-1;
	vec3_t v;
	trace_t tr;
	float rng;
	vec3_t maxs,mins;

	VectorCopy(self->mins,mins);
	VectorCopy(self->maxs,maxs);

	// For Ladders, do not worry so much about reachability
	if(type == NODE_LADDER)
	{
		VectorCopy(vec3_origin,maxs);
		VectorCopy(vec3_origin,mins);
	}
	else
		mins[2] += 18; // Stepsize

	rng = (float)(range * range); // square range for distance comparison (eliminate sqrt)

	for(i=0;i<bot_numnodes;i++)
	{
		if(type == NODE_ALL || type == nodes[i].type) // check node type
		{
			VectorSubtract(nodes[i].origin, self->s.origin,v); // subtract first

			dist = v[0]*v[0] + v[1]*v[1] + v[2]*v[2];

			if(dist < closest && dist < rng)
			{
				// make sure it is visible
				tr = gi.trace (self->s.origin, mins, maxs, nodes[i].origin, self, MASK_SOLID);
				if(tr.fraction == 1.0)
				{
					node = i;
					closest = dist;
				}
			}
		}
	}

	return node;
}

///////////////////////////////////////////////////////////////////////
// BOT NAVIGATION ROUTINES
///////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////
// Set up the goal
///////////////////////////////////////////////////////////////////////
void ACEND_SetGoal(edict_t *self, int goal_node)
{
	int node;

	self->goal_node = goal_node;
	node = ACEND_FindClosestReachableNode(self, NODE_DENSITY*3, NODE_ALL);

	if(node == -1)
		return;

	if(debug_mode)
		debug_printf("%s new start node selected %d\n",self->client->pers.netname,node);


	self->current_node = node;
	self->next_node = self->current_node; // make sure we get to the nearest node first
	self->node_timeout = 0;

}

///////////////////////////////////////////////////////////////////////
// Move closer to goal by pointing the bot to the next node
// that is closer to the goal
///////////////////////////////////////////////////////////////////////
qboolean ACEND_FollowPath(edict_t *self)
{
	vec3_t v;

	//////////////////////////////////////////
	// Show the path
	if(debug_mode) {
		show_path_from = self->current_node;
		show_path_to = self->goal_node;
	}
	//////////////////////////////////////////

	// Try again?
	if(self->node_timeout ++ > 30)
	{
		if(self->tries++ > 3)
			return false;
		else
			ACEND_SetGoal(self,self->goal_node);
	}

	// Are we there yet?
	VectorSubtract(self->s.origin,nodes[self->next_node].origin,v);

	if(VectorLength(v) < 32)
	{
		// reset timeout
		self->node_timeout = 0;

		if(self->next_node == self->goal_node)
		{
			if(debug_mode)
				debug_printf("%s reached goal!\n",self->client->pers.netname);

			ACEAI_PickLongRangeGoal(self); // Pick a new goal
		}
		else
		{
			self->current_node = self->next_node;
			self->next_node = path_table[self->current_node][self->goal_node];
		}
	}

	if(self->current_node == -1 || self->next_node ==-1)
		return false;

	// Set bot's movement vector
	VectorSubtract (nodes[self->next_node].origin, self->s.origin , self->move_vector);

	return true;
}


///////////////////////////////////////////////////////////////////////
// MAPPING CODE
///////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////
// Capture when the grappling hook has been fired for mapping purposes.
///////////////////////////////////////////////////////////////////////
void ACEND_GrapFired(edict_t *self)
{


	if(!self->owner)
		return; // should not be here

	return;
}


///////////////////////////////////////////////////////////////////////
// Check for adding ladder nodes
///////////////////////////////////////////////////////////////////////
qboolean ACEND_CheckForLadder(edict_t *self)
{
	int closest_node;

	// If there is a ladder and we are moving up, see if we should add a ladder node
	if (gi.pointcontents(self->s.origin) & CONTENTS_LADDER && self->velocity[2] > 0)
	{
		//debug_printf("contents: %x\n",tr.contents);

		closest_node = ACEND_FindClosestReachableNode(self,NODE_DENSITY,NODE_LADDER);
		if(closest_node == -1)
		{
			closest_node = ACEND_AddNode(self,NODE_LADDER);

			// Now add link
		    ACEND_UpdateNodeEdge(self->last_node,closest_node);

			// Set current to last
			self->last_node = closest_node;
		}
		else
		{
			ACEND_UpdateNodeEdge(self->last_node,closest_node);
			self->last_node = closest_node; // set visited to last
		}
		return true;
	}
	return false;
}

///////////////////////////////////////////////////////////////////////
// This routine is called to hook in the pathing code and sets
// the current node if valid.
///////////////////////////////////////////////////////////////////////
void ACEND_PathMap(edict_t *self)
{
	int closest_node;
	static float last_update=0; // start off low
	vec3_t v;

	if(level.time < last_update)
		return;

	last_update = level.time + 0.15; // slow down updates a bit

	////////////////////////////////////////////////////////
	// Special check for ladder nodes
	///////////////////////////////////////////////////////
	if(ACEND_CheckForLadder(self)) // check for ladder nodes
		return;

	// Not on ground, and not in the water, so bail
    if(!self->groundentity && !self->waterlevel)
		return;

	////////////////////////////////////////////////////////
	// Lava/Slime
	////////////////////////////////////////////////////////
	VectorCopy(self->s.origin,v);
	v[2] -= 18;
	if(gi.pointcontents(v) & (CONTENTS_LAVA|CONTENTS_SLIME))
		return; // no nodes in slime

    ////////////////////////////////////////////////////////
	// Jumping
	///////////////////////////////////////////////////////
	if(self->is_jumping)
	{
	   // See if there is a closeby jump landing node (prevent adding too many)
		closest_node = ACEND_FindClosestReachableNode(self, 64, NODE_JUMP);

		if(closest_node == INVALID)
			closest_node = ACEND_AddNode(self,NODE_JUMP);

		// Now add link
		if(self->last_node != -1)
			ACEND_UpdateNodeEdge(self->last_node, closest_node);

		self->is_jumping = false;
		return;
	}


	// Iterate through all nodes to make sure far enough apart
	closest_node = ACEND_FindClosestReachableNode(self, NODE_DENSITY, NODE_ALL);

	////////////////////////////////////////////////////////
	// Special Check for Platforms
	////////////////////////////////////////////////////////
	if(self->groundentity && self->groundentity->use == Use_Plat)
	{
		if(closest_node == INVALID)
			return; // Do not want to do anything here.

		// Here we want to add links
		if(closest_node != self->last_node && self->last_node != INVALID)
			ACEND_UpdateNodeEdge(self->last_node,closest_node);

		self->last_node = closest_node; // set visited to last
		return;
	}

	 ////////////////////////////////////////////////////////
	 // Add Nodes as needed
	 ////////////////////////////////////////////////////////
	 if(closest_node == INVALID)
	 {
		// Add nodes in the water as needed
		if(self->waterlevel)
			closest_node = ACEND_AddNode(self,NODE_WATER);
		else
		    closest_node = ACEND_AddNode(self,NODE_MOVE);

		// Now add link
		if(self->last_node != -1)
			ACEND_UpdateNodeEdge(self->last_node, closest_node);

	 }
	 else if(closest_node != self->last_node && self->last_node != INVALID)
	 	ACEND_UpdateNodeEdge(self->last_node,closest_node);

	 self->last_node = closest_node; // set visited to last

}

///////////////////////////////////////////////////////////////////////
// Init node array (set all to INVALID)
///////////////////////////////////////////////////////////////////////
void ACEND_InitNodes(void)
{
	bot_numnodes = 1;
	numitemnodes = 1;
	memset(nodes,0,sizeof(node_t) * MAX_NODES);
	memset(path_table,INVALID,sizeof(short int)*MAX_NODES*MAX_NODES);
	memset(node_showents,0,sizeof(edict_t *)*MAX_NODES);

}

///////////////////////////////////////////////////////////////////////
// Show the node for debugging (utility function)
// Previously there was a warning comment here about overflows, however it 
// seems to be just fine on a private server.
///////////////////////////////////////////////////////////////////////
void ACEND_ShowNode(int node)
{
	edict_t *ent;

    if (node_showents[node]) {
        safe_bprintf(PRINT_MEDIUM, "node %d already being shown\n", node);
        return;
    }
    
	ent = G_Spawn();

	ent->movetype = MOVETYPE_NONE;
	ent->solid = SOLID_NOT;

	if(nodes[node].type == NODE_MOVE)
		ent->s.renderfx = RF_SHELL_BLUE;
	else if (nodes[node].type == NODE_WATER)
		ent->s.renderfx = RF_SHELL_RED;
	else
		ent->s.renderfx = RF_SHELL_GREEN; // action nodes

	ent->s.modelindex = gi.modelindex ("models/items/ammo/grenades/medium/tris.md2");
	ent->owner = ent;
	ent->nextthink = level.time + 200000.0;
	ent->think = G_FreeEdict;
	ent->dmg = 0;

	VectorCopy(nodes[node].origin,ent->s.origin);
	gi.linkentity (ent);
	
	node_showents[node] = ent;

}

///////////////////////////////////////////////////////////////////////
// Draws the current path (utility function)
///////////////////////////////////////////////////////////////////////
void ACEND_DrawPath()
{
	int current_node, goal_node, next_node;
	
	if (!debug_mode)
	    return;

	current_node = show_path_from;
	goal_node = show_path_to;

	next_node = path_table[current_node][goal_node];

	// Now set up and display the path
	while(current_node != goal_node && current_node != -1)
	{
		gi.WriteByte (svc_temp_entity);
		gi.WriteByte (TE_REDLASER);
		gi.WritePosition (nodes[current_node].origin);
		gi.WritePosition (nodes[next_node].origin);
		gi.multicast (nodes[current_node].origin, MULTICAST_PVS);
		current_node = next_node;
		next_node = path_table[current_node][goal_node];
	}
}

///////////////////////////////////////////////////////////////////////
// Turns on showing of the path, set goal to -1 to
// shut off. (utility function)
///////////////////////////////////////////////////////////////////////
void ACEND_ShowPath(edict_t *self, int goal_node)
{
	show_path_from = ACEND_FindClosestReachableNode(self, NODE_DENSITY, NODE_ALL);
	show_path_to = goal_node;
}

///////////////////////////////////////////////////////////////////////
// Add a node of type ?
///////////////////////////////////////////////////////////////////////
int ACEND_AddNode(edict_t *self, int type)
{
	vec3_t v1,v2;

	// Block if we exceed maximum
	if (bot_numnodes + 1 > MAX_NODES)
		return false;

	// Set location
	VectorCopy(self->s.origin,nodes[bot_numnodes].origin);

	// Set type
	nodes[bot_numnodes].type = type;
	
	/////////////////////////////////////////////////////
	// ITEMS
	// Move the z location up just a bit.
	if(type == NODE_ITEM)
	{
		nodes[bot_numnodes].origin[2] += 16;
		numitemnodes++;
	}

	// Teleporters
	if(type == NODE_TELEPORTER)
	{
		// Up 32
		nodes[bot_numnodes].origin[2] += 32;
	}

	if(type == NODE_LADDER)
	{
		nodes[bot_numnodes].type = NODE_LADDER;

		if(debug_mode)
		{
			debug_printf("Node added %d type: Ladder\n",bot_numnodes);
			ACEND_ShowNode(bot_numnodes);
		}

		bot_numnodes++;
		return bot_numnodes-1; // return the node added

	}

	// For platforms drop two nodes one at top, one at bottom
	if(type == NODE_PLATFORM)
	{
		VectorCopy(self->maxs,v1);
		VectorCopy(self->mins,v2);

		// To get the center
		nodes[bot_numnodes].origin[0] = (v1[0] - v2[0]) / 2 + v2[0];
		nodes[bot_numnodes].origin[1] = (v1[1] - v2[1]) / 2 + v2[1];
		nodes[bot_numnodes].origin[2] = self->maxs[2];

		if(debug_mode)
			ACEND_ShowNode(bot_numnodes);

		bot_numnodes++;

		nodes[bot_numnodes].origin[0] = nodes[bot_numnodes-1].origin[0];
		nodes[bot_numnodes].origin[1] = nodes[bot_numnodes-1].origin[1];
		nodes[bot_numnodes].origin[2] = self->mins[2]+64;

		nodes[bot_numnodes].type = NODE_PLATFORM;

		// Add a link
		ACEND_UpdateNodeEdge(bot_numnodes,bot_numnodes-1);

		if(debug_mode)
		{
			debug_printf("Node added %d type: Platform\n",bot_numnodes);
			ACEND_ShowNode(bot_numnodes);
		}

		bot_numnodes++;

		return bot_numnodes -1;
	}

	if(debug_mode)
	{
		if(nodes[bot_numnodes].type == NODE_MOVE)
			debug_printf("Node added %d type: Move\n",bot_numnodes);
		else if(nodes[bot_numnodes].type == NODE_TELEPORTER)
			debug_printf("Node added %d type: Teleporter\n",bot_numnodes);
		else if(nodes[bot_numnodes].type == NODE_ITEM)
			debug_printf("Node added %d type: Item\n",bot_numnodes);
		else if(nodes[bot_numnodes].type == NODE_WATER)
			debug_printf("Node added %d type: Water\n",bot_numnodes);
		else if(nodes[bot_numnodes].type == NODE_GRAPPLE)
			debug_printf("Node added %d type: Grapple\n",bot_numnodes);

		ACEND_ShowNode(bot_numnodes);
	}

	bot_numnodes++;

	return bot_numnodes-1; // return the node added
}

///////////////////////////////////////////////////////////////////////
// Add/Update node connections (paths)
///////////////////////////////////////////////////////////////////////
void ACEND_UpdateNodeEdge(int from, int to)
{
	int i;

	if(from == -1 || to == -1 || from == to)
		return; // safety

	// Add the link
	path_table[from][to] = to;

	// Now for the self-referencing part, linear time for each link added
	for(i=0;i<bot_numnodes;i++)
		if(path_table[i][from] != INVALID)
		{
			if(i == to)
				path_table[i][to] = INVALID; // make sure we terminate
			else
				path_table[i][to] = path_table[i][from];
		}

	if(debug_mode)
		debug_printf("Link %d -> %d\n", from, to);
}

///////////////////////////////////////////////////////////////////////
// Remove a node edge
///////////////////////////////////////////////////////////////////////
void ACEND_RemoveNodeEdge(edict_t *self, int from, int to)
{
	int i;

	if(debug_mode)
		debug_printf("%s: Removing Edge %d -> %d\n", self->client->pers.netname, from, to);

	path_table[from][to] = INVALID; // set to invalid

	// Make sure this gets updated in our path array
	for(i=0;i<bot_numnodes;i++)
		if(path_table[from][i] == to)
			path_table[from][i] = INVALID;
}

///////////////////////////////////////////////////////////////////////
// This function will resolve all paths that are incomplete
// usually called before saving to disk
///////////////////////////////////////////////////////////////////////
void ACEND_ResolveAllPaths()
{
	int i, from, to;
	int num=0;

	safe_bprintf(PRINT_HIGH,"Resolving all paths...");

	for(from=0;from<bot_numnodes;from++)
	for(to=0;to<bot_numnodes;to++)
	{
		// update unresolved paths
		// Not equal to itself, not equal to -1 and equal to the last link
		if(from != to && path_table[from][to] == to)
		{
			num++;

			// Now for the self-referencing part linear time for each link added
			for(i=0;i<bot_numnodes;i++)
				if(path_table[i][from] != -1)
				{
					if(i == to)
						path_table[i][to] = -1; // make sure we terminate
					else
						path_table[i][to] = path_table[i][from];
				}
		}
	}

	safe_bprintf(PRINT_MEDIUM,"done (%d updated)\n",num);
}

///////////////////////////////////////////////////////////////////////
// Save to disk file
//
// Since my compression routines are one thing I did not want to
// release, I took out the compressed format option. Most levels will
// save out to a node file around 50-200k, so compression is not really
// a big deal.
///////////////////////////////////////////////////////////////////////
void ACEND_SaveNodes()
{
	FILE *pOut;
	char filename[MAX_OSPATH];
	char relative_path[MAX_QPATH];
	int i,j;
	int version = 1;
	size_t sz;

	// Resolve paths
	ACEND_ResolveAllPaths();

	safe_bprintf(PRINT_MEDIUM,"Saving node table...");

	strcpy( relative_path, BOT_GAMEDATA"/nav/" );
	strcat( relative_path, level.mapname );
	strcat( relative_path, ".nod" );

	gi.FullWritePath( filename, sizeof(filename), relative_path );

	if((pOut = fopen(filename, "wb" )) == NULL)
	{
		gi.dprintf("ACEND_SaveNodes: failed fopen for write: %s\n", filename );
		return;
	}

	sz = fwrite(&version,sizeof(int),1,pOut); // write version
	sz = fwrite(&bot_numnodes,sizeof(int),1,pOut); // write count
	sz = fwrite(&num_items,sizeof(int),1,pOut); // write facts count

	sz = fwrite(nodes,sizeof(node_t),bot_numnodes,pOut); // write nodes

	for(i=0;i<bot_numnodes;i++)
		for(j=0;j<bot_numnodes;j++)
			sz = fwrite(&path_table[i][j],sizeof(short int),1,pOut); // write count

	sz = fwrite(item_table,sizeof(item_table_t),num_items,pOut); 		// write out the fact table

	fclose(pOut);

	safe_bprintf(PRINT_MEDIUM,"done.\n");
}

///////////////////////////////////////////////////////////////////////
// Read from disk file
///////////////////////////////////////////////////////////////////////
void ACEND_LoadNodes(void)
{
	FILE *pIn;
	int i,j;
	char relative_path[MAX_QPATH];
	char filename[MAX_OSPATH];
	int version;
	size_t sz;

	strcpy( relative_path, BOT_GAMEDATA"/nav/" );
	strcat( relative_path, level.mapname );
	strcat( relative_path, ".nod" );

	if ( !gi.FullPath( filename, sizeof(filename), relative_path ) )
	{
		gi.dprintf("ACEND_LoadNodes: not found: %s\n", filename );
	}

	if((pIn = fopen(filename, "rb" )) == NULL)
    {
		// Create item table
		gi.dprintf("ACE: No node file found, creating new one...");
		ACEIT_BuildItemNodeTable(false);
		safe_bprintf(PRINT_MEDIUM, "done.\n");
		return;
	}

	// determine version
	sz = fread(&version,sizeof(int),1,pIn); // read version

	if(version == 1)
	{
		gi.dprintf("ACE: Loading node table...");

        //FIXME: This code sucks so bad. Didn't anyone ever tell this Yeager
        //fellow that dumps of C structs don't make a proper file format? 
        //If your compiler does padding differently, it breaks. If you want to
        //add new field types, it breaks. 
		sz = fread(&bot_numnodes,sizeof(int),1,pIn); // read count
		sz = fread(&num_items,sizeof(int),1,pIn); // read facts count
		sz = fread(nodes,sizeof(node_t),bot_numnodes,pIn); 

		for(i=0;i<bot_numnodes;i++)
			for(j=0;j<bot_numnodes;j++)
				sz = fread(&path_table[i][j],sizeof(short int),1,pIn); // write count

	    for(i=0;i<num_items;i++)
			sz = fread(item_table,sizeof(item_table_t),1,pIn);
		fclose(pIn);
	}
	else
	{
		// Create item table
		gi.dprintf("ACE: No node file found, creating new one...");
		ACEIT_BuildItemNodeTable(false);
		safe_bprintf(PRINT_MEDIUM, "done.\n");
		return; // bail
	}

	gi.dprintf("done.\n");

	ACEIT_BuildItemNodeTable(true);

}

