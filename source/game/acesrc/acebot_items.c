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
//  acebot_items.c - This file contains all of the
//                   item handling routines for the
//                   ACE bot, including fact table support
//
///////////////////////////////////////////////////////////////////////

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "../g_local.h"
#include "acebot.h"

int num_items = 0;
item_table_t item_table[MAX_EDICTS];

static gitem_t *redflag;
static gitem_t *blueflag;


void ACEAI_InitKnownCTFItems(void)
{
	//int		i;

	//define gitem_t pointers commonly used for comparisons inside this archive.
	redflag = FindItemByClassname("item_flag_red");
	blueflag = FindItemByClassname("item_flag_blue");

}

///////////////////////////////////////////////////////////////////////
// Can we get there?
///////////////////////////////////////////////////////////////////////
qboolean ACEIT_IsReachable(edict_t *self, vec3_t goal)
{
	trace_t trace;
	vec3_t v;

	VectorCopy(self->mins,v);
	v[2] += 18; // Stepsize

	trace = gi.trace (self->s.origin, v, self->maxs, goal, self, BOTMASK_OPAQUE);

	// Yes we can see it
	if (trace.fraction == 1.0)
		return true;
	else
		return false;

}

///////////////////////////////////////////////////////////////////////
// Visiblilty check
///////////////////////////////////////////////////////////////////////
qboolean ACEIT_IsVisible(edict_t *self, vec3_t goal)
{
	trace_t trace;

	trace = gi.trace (self->s.origin, vec3_origin, vec3_origin, goal, self, BOTMASK_OPAQUE);

	// Yes we can see it
	if (trace.fraction == 1.0)
		return true;
	else
		return false;

}

///////////////////////////////////////////////////////////////////////
//  Weapon changing support
///////////////////////////////////////////////////////////////////////
qboolean ACEIT_ChangeWeapon (edict_t *ent, gitem_t *item)
{
	int			ammo_index;
	gitem_t		*ammo_item;

	// see if we're already using it
	if (item == ent->client->pers.weapon)
		return true;

	// Has not picked up weapon yet
	if(!ent->client->pers.inventory[ITEM_INDEX(item)])
		return false;

	// Do we have ammo for it?
	if (item->ammo)
	{
		ammo_item = FindItem(item->ammo);
		ammo_index = ITEM_INDEX(ammo_item);
		if (!ent->client->pers.inventory[ammo_index] && !g_select_empty->value)
			return false;
	}

	// Change to this weapon
	ent->client->newweapon = item;

	return true;
}


extern gitem_armor_t jacketarmor_info;
extern gitem_armor_t combatarmor_info;
extern gitem_armor_t bodyarmor_info;

///////////////////////////////////////////////////////////////////////
// Check if we can use the armor
///////////////////////////////////////////////////////////////////////
qboolean ACEIT_CanUseArmor (gitem_t *item, edict_t *other)
{
	int				old_armor_index;
	gitem_armor_t	*oldinfo;
	gitem_armor_t	*newinfo;
	int				newcount;
	float			salvage;
	int				salvagecount;

	// get info on new armor
	newinfo = (gitem_armor_t *)item->info;

	old_armor_index = ArmorIndex (other);

	// handle armor shards specially
	if (item->tag == ARMOR_SHARD)
		return true;

	// get info on old armor
	if (old_armor_index == ITEM_INDEX(FindItem("Jacket Armor")))
		oldinfo = &jacketarmor_info;
	else if (old_armor_index == ITEM_INDEX(FindItem("Combat Armor")))
		oldinfo = &combatarmor_info;
	else // (old_armor_index == body_armor_index)
		oldinfo = &bodyarmor_info;

	if (newinfo->normal_protection <= oldinfo->normal_protection)
	{
		// calc new armor values
		salvage = newinfo->normal_protection / oldinfo->normal_protection;
		salvagecount = salvage * newinfo->base_count;
		newcount = other->client->pers.inventory[old_armor_index] + salvagecount;

		if (newcount > oldinfo->max_count)
			newcount = oldinfo->max_count;

		// if we're already maxed out then we don't need the new armor
		if (other->client->pers.inventory[old_armor_index] >= newcount)
			return false;

	}

	return true;
}
//==========================================
// find needed flag
//
//==========================================
gitem_t	*ACEIT_WantedFlag (edict_t *self)
{
	qboolean	hasflag;

	if (!ctf->value)
		return NULL;

	//find out if the player has a flag, and what flag is it
	if (redflag && self->client->pers.inventory[ITEM_INDEX(redflag)])
		hasflag = true;
	else if (blueflag && self->client->pers.inventory[ITEM_INDEX(blueflag)])
		hasflag = true;
	else
		hasflag = false;

	//jalToDo: see if our flag is at base

	if (!hasflag)//if we don't have a flag we want other's team flag
	{
		if (self->dmteam == RED_TEAM)
			return blueflag;
		else
			return redflag;
	}
	else	//we have a flag
	{
		if (self->dmteam == BLUE_TEAM)
			return redflag;
		else
			return blueflag;
	}

	return NULL;
}


///////////////////////////////////////////////////////////////////////
// Determins the NEED for an item
//
// This function can be modified to support new items to pick up
// Any other logic that needs to be added for custom decision making
// can be added here. For now it is very simple.
///////////////////////////////////////////////////////////////////////
float ACEIT_ItemNeed(edict_t *self, int item)
{
	gitem_t		*wantedFlag;
	// Make sure item is at least close to being valid
	if(item < 0 || item > 100)
		return 0.0;

	switch(item)
	{
		// Health
		case ITEMLIST_HEALTH_SMALL:
		case ITEMLIST_HEALTH_MEDIUM:
		case ITEMLIST_HEALTH_LARGE:
		case ITEMLIST_HEALTH_MEGA:
			if(self->health < 100)
				return 1.0 - (float)self->health/100.0f; // worse off, higher priority
			else
				return 0.0;

		case ITEMLIST_QUADDAMAGE:
		case ITEMLIST_INVULNERABILITY:
		case ITEMLIST_HASTE:
		case ITEMLIST_SPROING:
		case ITEMLIST_ADRENALINE:
			return 0.6;

		// Weapons
		case ITEMLIST_ROCKETLAUNCHER:
		case ITEMLIST_RAILGUN:
		case ITEMLIST_CHAINGUN:
		case ITEMLIST_SHOTGUN:
		case ITEMLIST_SUPERSHOTGUN:
		case ITEMLIST_BFG10K:
		case ITEMLIST_HYPERBLASTER:
			if(!self->client->pers.inventory[item])
				return 0.9; //was .7
			else
				return 0.0;

		// Ammo
		case ITEMLIST_SLUGS:
			if(self->client->pers.inventory[ITEMLIST_SLUGS] < self->client->pers.max_slugs)
				return 0.4;
			else
				return 0.0;

		case ITEMLIST_BULLETS:
			if(self->client->pers.inventory[ITEMLIST_BULLETS] < self->client->pers.max_bullets)
				return 0.3;
			else
				return 0.0;

		case ITEMLIST_SHELLS:
		   if(self->client->pers.inventory[ITEMLIST_SHELLS] < self->client->pers.max_shells)
				return 0.3;
			else
				return 0.0;

		case ITEMLIST_CELLS:
			if(self->client->pers.inventory[ITEMLIST_CELLS] <	self->client->pers.max_cells)
				return 0.3;
			else
				return 0.0;

		case ITEMLIST_ROCKETS:
			if(self->client->pers.inventory[ITEMLIST_ROCKETS] < self->client->pers.max_rockets)
				return 1.5;
			else
				return 0.0;

		case ITEMLIST_GRENADES:
			if(self->client->pers.inventory[ITEMLIST_GRENADES] < self->client->pers.max_grenades)
				return 0.3;
			else
				return 0.0;

		case ITEMLIST_BODYARMOR:
			if(ACEIT_CanUseArmor (FindItem("Body Armor"), self))
				return 0.6;
			else
				return 0.0;

		case ITEMLIST_COMBATARMOR:
			if(ACEIT_CanUseArmor (FindItem("Combat Armor"), self))
				return 0.6;
			else
				return 0.0;

		case ITEMLIST_JACKETARMOR:
			if(ACEIT_CanUseArmor (FindItem("Jacket Armor"), self))
				return 0.6;
			else
				return 0.0;
		//flags
		case ITEMLIST_FLAG1:
			wantedFlag = ACEIT_WantedFlag (self); //Returns the flag gitem_t
			if (redflag != wantedFlag)
				return 0.0;
			else
				return 3.0;
		case ITEMLIST_FLAG2:
			wantedFlag = ACEIT_WantedFlag (self); //Returns the flag gitem_t
			if (blueflag != wantedFlag)
				return 0.0;
			else
				return 3.0;
		//vehicles
		case ITEMLIST_BOMBER:
		case ITEMLIST_STRAFER:
		case ITEMLIST_HOVER:
			if(!self->client->pers.inventory[item])
				return 0.9;
			else
				return 0.0;

		//deathball
		case ITEMLIST_DEATHBALL:
			return 3.0;

		default:
			return 0.0;

	}

}

///////////////////////////////////////////////////////////////////////
// Convert a classname to its index value
//
// I prefer to use integers/defines for simplicity sake. This routine
// can lead to some slowdowns I guess, but makes the rest of the code
// easier to deal with.
///////////////////////////////////////////////////////////////////////
int ACEIT_ClassnameToIndex(char *classname)
{
	if(strcmp(classname,"item_armor_body")==0)
		return ITEMLIST_BODYARMOR;

	if(strcmp(classname,"item_armor_combat")==0)
		return ITEMLIST_COMBATARMOR;

	if(strcmp(classname,"item_armor_jacket")==0)
		return ITEMLIST_JACKETARMOR;

	if(strcmp(classname,"item_armor_shard")==0)
		return ITEMLIST_ARMORSHARD;

	if(strcmp(classname,"weapon_blaster")==0)
		return ITEMLIST_BLASTER;

	if(strcmp(classname,"weapon_shotgun")==0)
		return ITEMLIST_SHOTGUN;

	if(strcmp(classname,"weapon_supershotgun")==0)
		return ITEMLIST_SUPERSHOTGUN;

	if(strcmp(classname,"weapon_chaingun")==0)
		return ITEMLIST_CHAINGUN;

	if(strcmp(classname,"ammo_grenades")==0)
		return ITEMLIST_GRENADES;

	if(strcmp(classname,"weapon_rocketlauncher")==0)
		return ITEMLIST_ROCKETLAUNCHER;

	if(strcmp(classname,"weapon_hyperblaster")==0)
		return ITEMLIST_HYPERBLASTER;

	if(strcmp(classname,"weapon_railgun")==0)
		return ITEMLIST_RAILGUN;

	if(strcmp(classname,"weapon_bfg")==0)
		return ITEMLIST_BFG10K;

	if(strcmp(classname,"ammo_shells")==0)
		return ITEMLIST_SHELLS;

	if(strcmp(classname,"ammo_bullets")==0)
		return ITEMLIST_BULLETS;

	if(strcmp(classname,"ammo_cells")==0)
		return ITEMLIST_CELLS;

	if(strcmp(classname,"ammo_rockets")==0)
		return ITEMLIST_ROCKETS;

	if(strcmp(classname,"ammo_slugs")==0)
		return ITEMLIST_SLUGS;

	if(strcmp(classname,"item_quad")==0)
		return ITEMLIST_QUADDAMAGE;

	if(strcmp(classname,"item_invunerability")==0)
		return ITEMLIST_INVULNERABILITY;

	if(strcmp(classname,"item_haste")==0)
		return ITEMLIST_HASTE;

	if(strcmp(classname,"item_sproing")==0)
		return ITEMLIST_SPROING;

	if(strcmp(classname,"item_adrenaline")==0)
		return ITEMLIST_ADRENALINE;

	if(strcmp(classname,"item_health")==0) // ??
		return ITEMLIST_HEALTH;

	if(strcmp(classname,"item_health_small")==0)
		return ITEMLIST_HEALTH_SMALL;

	if(strcmp(classname,"item_health_medium")==0)
		return ITEMLIST_HEALTH_MEDIUM;

	if(strcmp(classname,"item_health_large")==0)
		return ITEMLIST_HEALTH_LARGE;

	if(strcmp(classname,"item_health_mega")==0)
		return ITEMLIST_HEALTH_MEGA;

	if(strcmp(classname, "item_flag_red") == 0)
		return ITEMLIST_FLAG1;

	if(strcmp(classname, "item_flag_blue") == 0)
		return ITEMLIST_FLAG2;

	if(strcmp(classname, "item_bomber") == 0)
		return ITEMLIST_BOMBER;

	if(strcmp(classname, "item_strafer") == 0)
		return ITEMLIST_STRAFER;

	if(strcmp(classname, "item_deathball") == 0)
		return ITEMLIST_DEATHBALL;

	return INVALID;
}


///////////////////////////////////////////////////////////////////////
// Only called once per level, when saved will not be called again
//
// Downside of the routine is that items can not move about. If the level
// has been saved before and reloaded, it could cause a problem if there
// are items that spawn at random locations.
//
//#define DEBUG // uncomment to write out items to a file.
///////////////////////////////////////////////////////////////////////
void ACEIT_BuildItemNodeTable (qboolean rebuild)
{
	edict_t *items;
	int i,item_index;
	vec3_t v,v1,v2;

#ifdef DEBUG
	FILE *pOut; // for testing
	if((pOut = fopen("items.txt","wt"))==NULL)
		return;
#endif

	num_items = 0;

	// Add game items
	for(items = g_edicts; items < &g_edicts[globals.num_edicts]; items++)
	{
		// filter out crap
		if(items->solid == SOLID_NOT)
			continue;

		if(!items->classname)
			continue;

		/////////////////////////////////////////////////////////////////
		// Items
		/////////////////////////////////////////////////////////////////
		item_index = ACEIT_ClassnameToIndex(items->classname);

		////////////////////////////////////////////////////////////////
		// SPECIAL NAV NODE DROPPING CODE
		////////////////////////////////////////////////////////////////
		// Special node dropping for platforms
		if(strcmp(items->classname,"func_plat")==0)
		{
			if(!rebuild)
				ACEND_AddNode(items,NODE_PLATFORM);
			item_index = 99; // to allow to pass the item index test
		}

		// Special node dropping for teleporters
		if(strcmp(items->classname,"misc_teleporter_dest")==0 || strcmp(items->classname,"misc_teleporter")==0)
		{
			if(!rebuild)
				ACEND_AddNode(items,NODE_TELEPORTER);
			item_index = 99;
		}

		#ifdef DEBUG
		if(item_index == INVALID)
			fprintf(pOut,"Rejected item: %s node: %d pos: %f %f %f\n",items->classname,item_table[num_items].node,items->s.origin[0],items->s.origin[1],items->s.origin[2]);
		else
			fprintf(pOut,"item: %s node: %d pos: %f %f %f\n",items->classname,item_table[num_items].node,items->s.origin[0],items->s.origin[1],items->s.origin[2]);
		#endif

		if(item_index == INVALID)
			continue;

		// add a pointer to the item entity
		item_table[num_items].ent = items;
		item_table[num_items].item = item_index;

		// If new, add nodes for items
		if(!rebuild)
		{
			// Add a new node at the item's location.
			item_table[num_items].node = ACEND_AddNode(items,NODE_ITEM);
			num_items++;
		}
		else // Now if rebuilding, just relink ent structures
		{
			// Find stored location
			for(i=0;i<numnodes;i++)
			{
				if(nodes[i].type == NODE_ITEM ||
				   nodes[i].type == NODE_PLATFORM ||
				   nodes[i].type == NODE_TELEPORTER) // valid types
				{
					VectorCopy(items->s.origin,v);

					// Add 16 to item type nodes
					if(nodes[i].type == NODE_ITEM)
						v[2] += 16;

					// Add 32 to teleporter
					if(nodes[i].type == NODE_TELEPORTER)
						v[2] += 32;

					if(nodes[i].type == NODE_PLATFORM)
					{
						VectorCopy(items->maxs,v1);
						VectorCopy(items->mins,v2);

						// To get the center
						v[0] = (v1[0] - v2[0]) / 2 + v2[0];
						v[1] = (v1[1] - v2[1]) / 2 + v2[1];
						v[2] = items->mins[2]+64;
					}

					if(v[0] == nodes[i].origin[0] &&
 					   v[1] == nodes[i].origin[1] &&
					   v[2] == nodes[i].origin[2])
					{
						// found a match now link to facts
						item_table[num_items].node = i;

#ifdef DEBUG
						fprintf(pOut,"Relink item: %s node: %d pos: %f %f %f\n",items->classname,item_table[num_items].node,items->s.origin[0],items->s.origin[1],items->s.origin[2]);
#endif
						num_items++;
					}
				}
			}
		}


	}

#ifdef DEBUG
	fclose(pOut);
#endif

}
