/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "g_local.h"



/*
======================================================================

INTERMISSION

======================================================================
*/

void MoveClientToIntermission (edict_t *ent)
{
	if (deathmatch->integer)
		ent->client->showscores = true;
	VectorCopy (level.intermission_origin, ent->s.origin);
	ent->client->ps.pmove.origin[0] = level.intermission_origin[0]*8;
	ent->client->ps.pmove.origin[1] = level.intermission_origin[1]*8;
	ent->client->ps.pmove.origin[2] = level.intermission_origin[2]*8;
	VectorCopy (level.intermission_angle, ent->client->ps.viewangles);

	// 2011-05 stop spectator from drifting,
	//  and, possibly, other intermission bogus movement.
	VectorClear( ent->velocity );
	VectorClear( ent->avelocity);

	ent->client->ps.pmove.pm_type = PM_FREEZE;
	ent->client->ps.gunindex = 0;
	ent->client->ps.blend[3] = 0;
	ent->client->ps.rdflags &= ~RDF_UNDERWATER;

	// clean up powerup info
	ent->client->quad_framenum = 0;
	ent->client->invincible_framenum = 0;
	ent->client->haste_framenum = 0;
    ent->client->sproing_framenum = 0;
	ent->client->invis_framenum = 0;
	ent->client->grenade_blew_up = false;
	ent->client->grenade_time = 0;

	ent->viewheight = 0;
	ent->s.modelindex = 0;
	ent->s.modelindex2 = 0;
	ent->s.modelindex3 = 0;
	ent->s.modelindex4 = 0;
	ent->s.effects = 0;
	ent->s.sound = 0;
	ent->solid = SOLID_NOT;

	// add the layout

	if (deathmatch->integer)
	{
		DeathmatchScoreboardMessage (ent, NULL, g_mapvote->integer);
		gi.unicast (ent, true);
	}

}
void dumb_think(edict_t *ent) {

	//just a generic think

    ent->nextthink = level.time + 0.100;
}
void PlaceWinnerOnVictoryPad(edict_t *winner, int offset)
{
	edict_t *pad;
	edict_t *chasecam;
	gclient_t *cl;
	vec3_t forward, right, movedir, origin;

	if(winner->in_vehicle)
		Reset_player(winner);

	VectorCopy (level.intermission_angle, winner->s.angles);

	//move it infront of everyone
	AngleVectors (level.intermission_angle, forward, right, NULL);

	VectorMA (level.intermission_origin, 100+abs(offset), forward, winner->s.origin);
	VectorMA (winner->s.origin, offset, right, winner->s.origin);
	winner->s.origin[2] +=8;

	winner->client->ps.pmove.origin[0] = winner->s.origin[0];
	winner->client->ps.pmove.origin[1] = winner->s.origin[1];
	winner->client->ps.pmove.origin[2] = winner->s.origin[2];

	VectorClear( winner->velocity );
	VectorClear( winner->avelocity );

	if (deathmatch->integer)
		winner->client->showscores = true;

	winner->client->ps.gunindex = 0;
	winner->client->ps.pmove.pm_type = PM_FREEZE;
	winner->client->ps.blend[3] = 0;
	winner->client->ps.rdflags &= ~RDF_UNDERWATER;

	// clean up powerup info
	winner->client->quad_framenum = 0;
	winner->client->invincible_framenum = 0;
	winner->client->haste_framenum = 0;
    winner->client->sproing_framenum = 0;
	winner->client->invis_framenum = 0;
	winner->client->grenade_blew_up = false;
	winner->client->grenade_time = 0;

	winner->s.effects = EF_ROTATE;
	winner->s.renderfx = (RF_FULLBRIGHT | RF_GLOW | RF_NOSHADOWS);

	winner->s.sound = 0;
	winner->solid = SOLID_NOT;

	// add the layout

	if (deathmatch->integer)
	{
		DeathmatchScoreboardMessage (winner, NULL, g_mapvote->integer);
		gi.unicast (winner, true);
	}

	//create a new entity for the pad
	pad = G_Spawn();
	VectorMA (winner->s.origin, 8, right, pad->s.origin);
	VectorCopy (level.intermission_angle, pad->s.angles);

	pad->s.origin[2] -= 8;
	pad->movetype = MOVETYPE_NONE;
	pad->solid = SOLID_NOT;
	pad->s.renderfx = (RF_FULLBRIGHT | RF_GLOW | RF_NOSHADOWS);
	pad->s.modelindex = gi.modelindex("models/objects/dmspot/tris.md2");
	pad->think = NULL;
	pad->classname = "pad";
	gi.linkentity (pad);

	movedir[0] = movedir[1] = 0;
	movedir[2] = -1;
	VectorCopy(pad->s.origin, origin);
	origin[2] -= 24;

	//if map is going to repeat - don't put these here as we have no way to remove them
	//if map is not reloaded
	if(strcmp(level.mapname, level.changemap)) {
		gi.WriteByte (svc_temp_entity);
		gi.WriteByte (TE_STEAM);
		gi.WriteByte (100);
		gi.WritePosition (origin);
		gi.WriteDir (movedir);
		gi.WriteByte (0);
		gi.multicast (origin, MULTICAST_PVS);
	}

	//now we want to allow the winners to actually see themselves on the podium, creating a
	//simple chasecam

	if(winner->is_bot) { //who cares if bots can not see themselves!
		winner->takedamage = DAMAGE_NO; //so they stop burning and suiciding
		gi.linkentity(winner); //link because we changed position!
		return;
	}

    winner->client->chasetoggle = 1;

    chasecam = G_Spawn ();
    chasecam->owner = winner;
    chasecam->solid = SOLID_NOT;
    chasecam->movetype = MOVETYPE_FLYMISSILE;

	VectorCopy (level.intermission_angle, chasecam->s.angles);

	VectorClear (chasecam->mins);
    VectorClear (chasecam->maxs);

	VectorCopy (level.intermission_origin, chasecam->s.origin);

    chasecam->classname = "chasecam";
    chasecam->think = NULL;
	winner->client->chasecam = chasecam;
    winner->client->oldplayer = G_Spawn();

	if (!winner->client->oldplayer->client)
    {
        cl = (gclient_t *) malloc(sizeof(gclient_t));
        winner->client->oldplayer->client = cl;
/*        printf("+++ Podiumcam = %p\n", winner->client->oldplayer->client); */
    }

    if (winner->client->oldplayer)
    {
		winner->client->oldplayer->s.frame = winner->s.frame;
	    VectorCopy (winner->s.origin, winner->client->oldplayer->s.origin);
		VectorCopy (winner->s.angles, winner->client->oldplayer->s.angles);
    }
	winner->client->oldplayer->s = winner->s;

	gi.linkentity (winner->client->oldplayer);

	 //move the winner back to the intermission point
	VectorCopy (level.intermission_origin, winner->s.origin);
	winner->client->ps.pmove.origin[0] = level.intermission_origin[0]*8;
	winner->client->ps.pmove.origin[1] = level.intermission_origin[1]*8;
	winner->client->ps.pmove.origin[2] = level.intermission_origin[2]*8;
	VectorCopy (level.intermission_angle, winner->client->ps.viewangles);

	winner->s.modelindex = 0;
	winner->s.modelindex2 = 0;
	winner->s.modelindex3 = 0;
	winner->s.modelindex = 0;
	winner->s.effects = 0;
	winner->s.sound = 0;
	winner->solid = SOLID_NOT;
}

void BeginIntermission (edict_t *targ)
{
	int		i;
	edict_t	*ent, *client=NULL;
	edict_t *winner = NULL;
	edict_t *firstrunnerup = NULL;
	edict_t *secondrunnerup = NULL;
	edict_t *cl_ent;
	int high_score, low_score;

	winner = NULL;
	firstrunnerup = NULL;
	secondrunnerup = NULL;

	if (level.intermissiontime)
		return;		// already activated

	if ((dmflags->integer & DF_BOT_AUTOSAVENODES))
			ACECM_Store(); //store the nodes automatically when changing levels.

	game.autosaved = false;

	for (i=0 ; i<g_maxclients->integer ; i++)
	{
		client = g_edicts + 1 + i;
		if (!client->inuse)
			continue;
		// remove from vehicle.
		//  note: vehicle hud is client-side, so may remain in view
		if (client->in_vehicle)
		{
			Reset_player( client );
		}
		// respawn the dead
		if (client->health <= 0)
		{
			respawn(client);
		}
		client->s.frame = 0;
		// disconnect spectators from target to prevent point-of-view errors
		if ( client->client && client->client->resp.spectator )
		{
			if ( client->client->chase_target != NULL )
			{
				client->client->chase_target = NULL;
				client->client->ps.pmove.pm_flags &= ~PMF_NO_PREDICTION;
			}
		}

		if(!client->is_bot && g_mapvote->integer)
			safe_centerprintf(client, "Use F1-F4 to vote for next map!");
	}

	level.intermissiontime = level.time;
	level.changemap = targ->map;

	level.exitintermission = 0;

	// find an intermission spot
	ent = G_Find (NULL, FOFS(classname), "info_player_intermission");
	if (!ent)
	{	// the map creator forgot to put in an intermission point...
		ent = G_Find (NULL, FOFS(classname), "info_player_start");
		if (!ent)
			ent = G_Find(NULL, FOFS(classname), "info_player_blue");
		if (!ent)
			ent = G_Find (NULL, FOFS(classname), "info_player_deathmatch");
	}
	else
	{	// chose one of four spots
		i = rand() & 3;
		while (i--)
		{
			ent = G_Find (ent, FOFS(classname), "info_player_intermission");
			if (!ent)	// wrap around the list
				ent = G_Find (ent, FOFS(classname), "info_player_intermission");
		}
	}
	if ( ent == NULL )
	{ /* can occur when running a demo .dm2 file */
		return;
	}

	VectorCopy (ent->s.origin, level.intermission_origin);
	VectorCopy (ent->s.angles, level.intermission_angle);

	low_score = 0;
	//get the lowest score in the game, and use that as the base high score to start
	for (i=0; i<game.maxclients; i++) {
		cl_ent = g_edicts + 1 + i;
		if (!cl_ent->inuse || game.clients[i].resp.spectator)
			continue;
		if(game.clients[i].resp.score <= low_score)
			low_score = game.clients[i].resp.score;
	}

	//get the winning player's info
	high_score = low_score;
	for (i=0 ; i<game.maxclients ; i++)
	{
		cl_ent = g_edicts + 1 + i;
		if (!cl_ent->inuse || game.clients[i].resp.spectator)
			continue;

		if(game.clients[i].resp.score >= high_score) {
			winner = cl_ent;
			high_score = game.clients[i].resp.score;
		}

	}
	//get the first runner up's info
	high_score = low_score;
	for (i=0 ; i<game.maxclients ; i++)
	{
		cl_ent = g_edicts + 1 + i;
		if (!cl_ent->inuse || game.clients[i].resp.spectator || (cl_ent == winner))
			continue;

		if(game.clients[i].resp.score >= high_score) {
			firstrunnerup = cl_ent;
			high_score = game.clients[i].resp.score;
		}

	}
	//get the second runner up's info
	high_score = low_score;
	for (i=0 ; i<game.maxclients ; i++)
	{
		cl_ent = g_edicts + 1 + i;
		if (!cl_ent->inuse || game.clients[i].resp.spectator || (cl_ent == winner) ||
			(cl_ent == firstrunnerup))
			continue;

		if(game.clients[i].resp.score >= high_score) {
			secondrunnerup = cl_ent;
			high_score = game.clients[i].resp.score;
		}
	}

	if(!winner)
		winner = g_edicts;
	if(!firstrunnerup)
		firstrunnerup = g_edicts;
	if(!secondrunnerup)
		secondrunnerup = g_edicts;

	// move all clients but the winners to the intermission point
	for (i=0 ; i<g_maxclients->integer ; i++)
	{
		client = g_edicts + 1 + i;
		if (!client->inuse)
			continue;
		if((client != winner) && (client != firstrunnerup) && (client != secondrunnerup))
			MoveClientToIntermission (client);
	}

	if ((dmflags->integer & DF_SKINTEAMS) || ctf->integer || tca->integer || cp->integer) //team stuff
	{
		if ( blue_team_score > red_team_score )
		{
			if(ctf->integer || tca->integer || cp->integer)
				gi.sound (client, CHAN_AUTO, gi.soundindex("misc/blue_wins_ctf.wav"), 1, ATTN_NONE, 0);
			else
				gi.sound (client, CHAN_AUTO, gi.soundindex("misc/blue_wins.wav"), 1, ATTN_NONE, 0);
		}
		else if ( blue_team_score < red_team_score )
		{
			if(ctf->integer || tca->integer || cp->integer)
				gi.sound (client, CHAN_AUTO, gi.soundindex("misc/red_wins_ctf.wav"), 1, ATTN_NONE, 0);

			else
				gi.sound (client, CHAN_AUTO, gi.soundindex("misc/red_wins.wav"), 1, ATTN_NONE, 0);
		}
		else
		{
			if ( ctf->integer )
			{
				gi.sound( client, CHAN_AUTO,
						gi.soundindex("misc/game_tied_ctf.wav"), 1, ATTN_NONE, 0);
			}
			else
			{
				gi.sound( client, CHAN_AUTO,
						gi.soundindex("misc/game_tied.wav"), 1, ATTN_NONE, 0);
			}
		}
	}
	else if ( !(dmflags->integer & DF_BOT_LEVELAD) )
	{
		if(winner->is_bot)
			gi.sound (ent, CHAN_AUTO, gi.soundindex("world/botwon.wav"), 1, ATTN_NONE, 0);
		else
			gi.sound (winner, CHAN_AUTO, gi.soundindex("world/youwin.wav"), 1, ATTN_STATIC, 0);
	}

	//place winner on victory pads, ala Q3
	if(winner && winner->client && winner->inuse)
		PlaceWinnerOnVictoryPad(winner, 0);
	if(firstrunnerup && firstrunnerup->client && firstrunnerup->inuse)
		PlaceWinnerOnVictoryPad(firstrunnerup, 32);
	if(secondrunnerup && secondrunnerup->client && secondrunnerup->inuse)
		PlaceWinnerOnVictoryPad(secondrunnerup, -32);

}

//duel code
int highestpos, numplayers;
void MoveEveryoneDownQueue(void) {
	int i, induel = 0;

	for (i = 0; i < g_maxclients->integer; i++) {
		if(g_edicts[i+1].inuse && g_edicts[i+1].client) {
			//move everyone else down a notch(never less than 0)
			if(induel > 1 && g_edicts[i+1].client->pers.queue <= 3) //houston, we have a problem
				return; //abort, stop moving people
			if(g_edicts[i+1].client->pers.queue > 1)
					g_edicts[i+1].client->pers.queue-=1;
			if(g_edicts[i+1].client->pers.queue < 3)
				induel++;
		}
	}
}
void CheckDuelWinner(void) {
	int	i;
	int highscore, induel;

	highscore = 0;
	numplayers = 0;
	highestpos = 0;
	induel = 0;

	for (i = 0; i < g_maxclients->integer; i++) {
		if(g_edicts[i+1].inuse && g_edicts[i+1].client) {
			if(g_edicts[i+1].client->resp.score > highscore)
				highscore = g_edicts[i+1].client->resp.score;
			if(g_edicts[i+1].client->pers.queue > highestpos)
				highestpos = g_edicts[i+1].client->pers.queue;
			numplayers++;
		}
	}
	if(highestpos < numplayers)
		highestpos = numplayers;

	for (i = 0; i < g_maxclients->integer; i++) {
		if(g_edicts[i+1].inuse && g_edicts[i+1].client) {
			if((g_edicts[i+1].client->resp.score < highscore) && g_edicts[i+1].client->pers.queue < 3) {
				g_edicts[i+1].client->pers.queue = highestpos+1; //loser, kicked to the back of the line
				highestpos++;
			}
		}
	}

	MoveEveryoneDownQueue();

	//last resort checking after positions have changed
	//check for any screwups and correct the queue
	while(induel < 2 && numplayers > 1) {
		induel = 0;
		for (i = 0; i < g_maxclients->integer; i++) {
			if(g_edicts[i+1].inuse && g_edicts[i+1].client) {
				if(g_edicts[i+1].client->pers.queue < 3 && g_edicts[i+1].client->pers.queue)
					induel++;
			}
		}
		if(induel < 2) //something is wrong(i.e winner left), move everyone down.
			MoveEveryoneDownQueue();
	}
}

void EndIntermission(void)
{
	int		i;
	edict_t	*ent;

	if(g_duel->integer)
		CheckDuelWinner();

	for (i=0 ; i<g_maxclients->integer; i++)
	{
		ent = g_edicts + 1 + i;
        if (!ent->inuse || ent->client->resp.spectator)
            continue;

        if(!ent->is_bot && ent->client->chasetoggle > 0)
        {
            ent->client->chasetoggle = 0;
            /* Stop the chasecam from moving */
            VectorClear (ent->client->chasecam->velocity);

            if(ent->client->oldplayer->client != NULL)
            {
/*                printf("--- Podiumcam = %p\n", ent->client->oldplayer->client);*/
                free(ent->client->oldplayer->client);
            }

            G_FreeEdict (ent->client->oldplayer);
            G_FreeEdict (ent->client->chasecam);
        }

    }

}

/*
==================
DeathmatchScoreboardMessage

==================
*/
void DeathmatchScoreboardMessage (edict_t *ent, edict_t *killer, int mapvote)
{
	char	entry[1024];
	char	string[1400];
	int		stringlength;
	int		i, j, k;
	int		sorted[MAX_CLIENTS];
	int		sortedscores[MAX_CLIENTS];
	int		score, total;
	int		x, y;
	gclient_t	*cl;
	edict_t		*cl_ent;
#if 0
	// 2010-12 unused. see below
	char	acc[16];
	char	weapname[16];
#endif

	if (ent->is_bot)
		return;

	if (g_tactical->integer)
		return; //no scoreboard in tactical mode

	if ((dmflags->integer & DF_SKINTEAMS) || ctf->value || tca->value || cp->value) {
		CTFScoreboardMessage (ent, killer, mapvote);
		return;
	}
	// sort the clients by score
	total = 0;
	for (i=0 ; i<game.maxclients ; i++)
	{
		cl_ent = g_edicts + 1 + i;
		if (!cl_ent->inuse || (!g_duel->integer && game.clients[i].resp.spectator))
			continue;

		score = game.clients[i].resp.score;
		for (j=0 ; j<total ; j++)
		{
			if (score > sortedscores[j])
				break;
		}
		for (k=total ; k>j ; k--)
		{
			sorted[k] = sorted[k-1];
			sortedscores[k] = sortedscores[k-1];
		}
		sorted[j] = i;
		sortedscores[j] = score;
		total++;
	}

	// print level name and exit rules
	string[0] = 0;

	stringlength = strlen(string);

	Com_sprintf (entry, sizeof(entry), "newsb ");
	j = strlen(entry);
	strcpy (string + stringlength, entry);
	stringlength += j;

	// add the clients in sorted order
	if (total > 12)
		total = 12;

	for (i=0 ; i<total ; i++)
	{
		cl = &game.clients[sorted[i]];
		cl_ent = g_edicts + 1 + sorted[i];

		x = 0;
		y = 32 + 32 * (i%12);

		// add a background
		Com_sprintf (entry, sizeof(entry),
			"xv %i yv %i picn %s ",x, y, "playerbox");

		j = strlen(entry);
		if (stringlength + j > 1024)
			break;
		strcpy (string + stringlength, entry);
		stringlength += j;

		// send the layout
		if(!cl->resp.spectator)
			Com_sprintf (entry, sizeof(entry),
				"client %i %i %i %i %i %i ",
				x, y, sorted[i], cl->resp.score, cl->ping, (level.framenum - cl->resp.enterframe)/600);
		else //duel mode will have queued spectators
			Com_sprintf (entry, sizeof(entry),
				"queued %i %i %i %i %i %i ",
				x, y, sorted[i], cl->resp.score, cl->ping, cl->pers.queue-2);

		j = strlen(entry);
		if (stringlength + j > 1024)
			break;
		strcpy (string + stringlength, entry);
		stringlength += j;
	}

#if 0
	/*
	 * 2010-12 Def'd out. Probable major contributer to net comm
	 * server->client overflow and dropped message errors.
	 */
	//weapon accuracy(don't do if map voting)
	if(!mapvote) {
		//add a background
		x = 96;
		y = 16*(total+1);

		Com_sprintf (entry, sizeof(entry),
			"xv %i yv %i picn %s ", x-4, y+48, "statbox");
		j = strlen(entry);
		if(stringlength + j < 1024) {
			strcpy(string + stringlength, entry);
			stringlength +=j;
		}

		Com_sprintf(entry, sizeof(entry),
			"xv %i yv %i string Accuracy ", x, y+56);
		j = strlen(entry);
		if(stringlength + j < 1024) {
			strcpy(string + stringlength, entry);
			stringlength +=j;
		}
		for(i = 0; i < 9; i++) {

			//in case of scenarios where a weapon is scoring multiple hits per shot(ie smartgun)
			if(ent->client->resp.weapon_hits[i] > ent->client->resp.weapon_shots[i])
				ent->client->resp.weapon_hits[i] = ent->client->resp.weapon_shots[i];

			if(ent->client->resp.weapon_shots[i] == 0)
				strcpy(acc, "0%");
			else {
				sprintf(acc, "%i", (100*ent->client->resp.weapon_hits[i]/ent->client->resp.weapon_shots[i]));
				strcat(acc, "%");
			}
			switch (i) {
				case 0:
					strcpy(weapname, "blaster");
					break;
				case 1:
					strcpy(weapname, "disruptor");
					break;
				case 2:
					strcpy(weapname, "smartgun");
					break;
				case 3:
					strcpy(weapname, "chaingun");
					break;
				case 4:
					strcpy(weapname, "flame");
					break;
				case 5:
					strcpy(weapname, "rocket");
					break;
				case 6:
					strcpy(weapname, "beamgun");
					break;
				case 7:
					strcpy(weapname, "vaporizer");
					break;
				case 8:
					strcpy(weapname, "violator");
					break;
			}
			Com_sprintf(entry, sizeof(entry),
				"xv %i yv %i string %s xv %i string %s ", x, y+((i+1)*9)+64, weapname, x+96, acc);
			j = strlen(entry);
			if(stringlength + j < 1024) {
				strcpy(string + stringlength, entry);
				stringlength +=j;
			}
		}
	}
#endif

	//map voting
	if(mapvote) {
		y = 64;
		x = 96;
		Com_sprintf(entry, sizeof(entry),
			"xv %i yt %i string Vote ", x, y);
		j = strlen(entry);
		if(stringlength + j < 1024) {
			strcpy(string + stringlength, entry);
			stringlength +=j;
		}
		x = 136;
		Com_sprintf(entry, sizeof(entry),
			"xv %i yt %i string for ", x, y);
		j = strlen(entry);
		if(stringlength + j < 1024) {
			strcpy(string + stringlength, entry);
			stringlength +=j;
		}
		x = 168;
		Com_sprintf(entry, sizeof(entry),
			"xv %i yt %i string next ", x, y);
		j = strlen(entry);
		if(stringlength + j < 1024) {
			strcpy(string + stringlength, entry);
			stringlength +=j;
		}
		x = 208;
		Com_sprintf(entry, sizeof(entry),
			"xv %i yt %i string map: ", x, y);
		j = strlen(entry);
		if(stringlength + j < 1024) {
			strcpy(string + stringlength, entry);
			stringlength +=j;
		}
		x = 96;
		for(i=0; i<4; i++) {

			Com_sprintf(entry, sizeof(entry),
			"xv %i yt %i string F%i.%s ", x, y+((i+1)*9)+9, i+1, votedmap[i].mapname);
			j = strlen(entry);
			if(stringlength + j < 1024) {
				strcpy(string + stringlength, entry);
				stringlength +=j;
			}
		}

	}

	gi.WriteByte (svc_layout);
	gi.WriteString (string);
}


/*
==================
DeathmatchScoreboard

Draw instead of help message.
Note that it isn't that hard to overflow the 1400 byte message limit!
==================
*/
void DeathmatchScoreboard (edict_t *ent)
{
	if (ent->is_bot)
		return;

	DeathmatchScoreboardMessage (ent, ent->enemy, false);
	gi.unicast (ent, true);
}


/*
==================
Cmd_Score_f

Display the scoreboard
==================
*/
void Cmd_Score_f (edict_t *ent)
{
	ent->client->showinventory = false;
	ent->client->showhelp = false;

	if (!deathmatch->integer)
		// Should we ever wish to do a singleplayer game, we could bring the 
		// "help computer" back here. 
		return;

	if (ent->client->showscores)
	{
		ent->client->showscores = false;
		return;
	}

	ent->client->showscores = true;
	DeathmatchScoreboard (ent);
}

//=======================================================================

/*
===============
G_SetScoreStats
Common with G_SetStats and G_SetSpectatorStats
===============
*/

void G_SetScoreStats (edict_t *ent)
{
	edict_t *e2;
	int i;
	int high_score = 0;
	
	// highest scorer
	for (i = 0, e2 = g_edicts + 1; i < g_maxclients->integer; i++, e2++) 
	{
		if (!e2->inuse)
			continue;

		if(e2->client->resp.score > high_score)
			high_score = e2->client->resp.score;
	}
	ent->client->ps.stats[STAT_HIGHSCORE] = high_score;
	
	ent->client->ps.stats[STAT_SCOREBOARD] = gi.imageindex ("i_score");

	//team
	ent->client->ps.stats[STAT_REDSCORE] = red_team_score;
	ent->client->ps.stats[STAT_BLUESCORE] = blue_team_score;
	
	if (tca->integer) 
	{
		ent->client->ps.stats[STAT_RED_MATCHES] = red_team_matches;
		ent->client->ps.stats[STAT_BLUE_MATCHES] = blue_team_matches;
	}

	if (g_tactical->integer) //just use the weapon slots since these aren't displayed in tactical anyway
	{
		ent->client->ps.stats[STAT_TACTICAL_SCORE] = gi.imageindex ("i_tactical");

		ent->client->ps.stats[STAT_WEAPN1] = tacticalScore.humanComputerHealth; 
		ent->client->ps.stats[STAT_WEAPN2] = tacticalScore.humanPowerSourceHealth; 
		ent->client->ps.stats[STAT_WEAPN3] = tacticalScore.humanAmmoDepotHealth; 

		ent->client->ps.stats[STAT_WEAPN4] = tacticalScore.alienComputerHealth; 
		ent->client->ps.stats[STAT_WEAPN5] = tacticalScore.alienPowerSourceHealth; 
		ent->client->ps.stats[STAT_WEAPN6] = tacticalScore.alienAmmoDepotHealth; 
	}
}

/*
===============
G_SetStats
===============
*/

void G_SetStats (edict_t *ent)
{
	gitem_t		*item;
	int			index;
	int			i;
	gitem_t		*flag1_item, *flag2_item;


	flag1_item = FindItemByClassname("item_flag_red");
	flag2_item = FindItemByClassname("item_flag_blue");

	//
	// health
	//
	ent->client->ps.stats[STAT_HEALTH_ICON] = level.pic_health;
	ent->client->ps.stats[STAT_HEALTH] = ent->health;

	//
	// ammo
	//
	if (!ent->client->ammo_index /* || !ent->client->pers.inventory[ent->client->ammo_index] */)
	{
		ent->client->ps.stats[STAT_AMMO_ICON] = 0;
		ent->client->ps.stats[STAT_AMMO] = 0;
	}
	else
	{
		item = &itemlist[ent->client->ammo_index];
		ent->client->ps.stats[STAT_AMMO_ICON] = gi.imageindex (item->icon);
		ent->client->ps.stats[STAT_AMMO] = ent->client->pers.inventory[ent->client->ammo_index];
	}

	//
	// armor
	//

	index = ArmorIndex (ent);
    if (index)
    {
        item = GetItemByIndex (index);
        ent->client->ps.stats[STAT_ARMOR_ICON] = 0;
        ent->client->ps.stats[STAT_ARMOR] = ent->client->pers.inventory[index];
    }
    else
    {
        ent->client->ps.stats[STAT_ARMOR_ICON] = 0;
        ent->client->ps.stats[STAT_ARMOR] = 0;
    }

	//
	// timers
	//
	if (ent->client->quad_framenum > level.framenum)
	{
		ent->client->ps.stats[STAT_TIMER_ICON] = gi.imageindex ("p_quad");
		ent->client->ps.stats[STAT_TIMER] = (ent->client->quad_framenum - level.framenum)/10;
	}
	else if (ent->client->invincible_framenum > level.framenum)
	{
		ent->client->ps.stats[STAT_TIMER_ICON] = gi.imageindex ("p_invulnerability");
		ent->client->ps.stats[STAT_TIMER] = (ent->client->invincible_framenum - level.framenum)/10;
	}
	else if (ent->client->haste_framenum > level.framenum)
	{
		ent->client->ps.stats[STAT_TIMER_ICON] = gi.imageindex ("p_haste");
		ent->client->ps.stats[STAT_TIMER] = (ent->client->haste_framenum - level.framenum)/10;
	}
	else if (ent->client->sproing_framenum > level.framenum)
	{
		ent->client->ps.stats[STAT_TIMER_ICON] = gi.imageindex ("p_sproing");
		ent->client->ps.stats[STAT_TIMER] = (ent->client->sproing_framenum - level.framenum)/10;
	}
	else if (ent->client->invis_framenum > level.framenum)
	{
		ent->client->ps.stats[STAT_TIMER_ICON] = gi.imageindex ("p_invis");
		ent->client->ps.stats[STAT_TIMER] = (ent->client->invis_framenum - level.framenum)/10;
	}
	else if (ent->client->resp.powered)
	{
		ent->client->ps.stats[STAT_TIMER_ICON] = gi.imageindex ("p_powered");
		ent->client->ps.stats[STAT_TIMER] = ent->client->resp.reward_pts;
	}
	else
	{
		ent->client->ps.stats[STAT_TIMER_ICON] = gi.imageindex ("p_rewardpts");
		ent->client->ps.stats[STAT_TIMER] = ent->client->resp.reward_pts;
	}

	//
	// selected item
	//
	ent->client->ps.stats[STAT_SELECTED_ITEM] = ent->client->pers.selected_item;

	//ctf
	if(ctf->integer) {
		if (ent->client->pers.inventory[ITEM_INDEX(flag1_item)])
			ent->client->ps.stats[STAT_FLAG_ICON] = gi.imageindex ("i_flag1");
		else if (ent->client->pers.inventory[ITEM_INDEX(flag2_item)])
			ent->client->ps.stats[STAT_FLAG_ICON] = gi.imageindex ("i_flag2");
		else { //do teams
			if(ent->dmteam == RED_TEAM)
				ent->client->ps.stats[STAT_FLAG_ICON] = gi.imageindex ( "i_team1");
			else if(ent->dmteam == BLUE_TEAM)
				ent->client->ps.stats[STAT_FLAG_ICON] = gi.imageindex ( "i_team2");
			else
				ent->client->ps.stats[STAT_FLAG_ICON] = gi.imageindex ( "bar_loading");
		}
	}
	else { //do teams
		if(ent->dmteam == RED_TEAM)
			ent->client->ps.stats[STAT_FLAG_ICON] = gi.imageindex ( "i_team1");
		else if(ent->dmteam == BLUE_TEAM)
			ent->client->ps.stats[STAT_FLAG_ICON] = gi.imageindex ( "i_team2");
		else
			ent->client->ps.stats[STAT_FLAG_ICON] = gi.imageindex ( "bar_loading");
	}

	//
	// layouts
	//
	ent->client->ps.stats[STAT_LAYOUTS] = 0;

	if (deathmatch->integer)
	{
		if (ent->client->pers.health <= 0 || level.intermissiontime
			|| ent->client->showscores)
			ent->client->ps.stats[STAT_LAYOUTS] |= 1;
		if (ent->client->showinventory && ent->client->pers.health > 0)
			ent->client->ps.stats[STAT_LAYOUTS] |= 2;
	}
	else
	{
		if (ent->client->showscores || ent->client->showhelp)
			ent->client->ps.stats[STAT_LAYOUTS] |= 1;
		if (ent->client->showinventory && ent->client->pers.health > 0)
			ent->client->ps.stats[STAT_LAYOUTS] |= 2;
	}

	//
	// frags and deaths
	//
	ent->client->ps.stats[STAT_FRAGS] = ent->client->resp.score;
	ent->client->ps.stats[STAT_DEATHS] = ent->client->resp.deaths;
	
	G_SetScoreStats (ent);

#ifndef ALTERIA

	if(!g_tactical->integer)
	{
		//weapon/ammo inventories
		for(i = 0; i < 7; i++)
			ent->client->ps.stats[STAT_WEAPN1+i] = 0;
		i = 0;
		if(ent->client->pers.inventory[ITEM_INDEX(FindItem("Alien Disruptor"))] &&
			ent->client->pers.weapon != (FindItem("Alien Disruptor"))) {
				ent->client->ps.stats[STAT_WEAPN1] = gi.imageindex ("disruptor");
				i++;
		}
		if(ent->client->pers.inventory[ITEM_INDEX(FindItem("Alien Smartgun"))] &&
			ent->client->pers.weapon != (FindItem("Alien Smartgun"))) {
				ent->client->ps.stats[STAT_WEAPN1+i] = gi.imageindex ("smartgun");
				i++;
		}
		if(ent->client->pers.inventory[ITEM_INDEX(FindItem("Pulse Rifle"))] &&
			ent->client->pers.weapon != (FindItem("Pulse Rifle"))) {
				ent->client->ps.stats[STAT_WEAPN1+i] = gi.imageindex ("chaingun");
				i++;
		}
		if(ent->client->pers.inventory[ITEM_INDEX(FindItem("Flame Thrower"))] &&
			ent->client->pers.weapon != (FindItem("Flame Thrower"))) {
				ent->client->ps.stats[STAT_WEAPN1+i] = gi.imageindex ("flamethrower");
				i++;
		}
		if(ent->client->pers.inventory[ITEM_INDEX(FindItem("Rocket Launcher"))] &&
			ent->client->pers.weapon != (FindItem("Rocket Launcher"))) {
				ent->client->ps.stats[STAT_WEAPN1+i] = gi.imageindex ("rocketlauncher");
				i++;
		}
		if(ent->client->pers.inventory[ITEM_INDEX(FindItem("Disruptor"))] &&
			ent->client->pers.weapon != (FindItem("Disruptor"))) {
				ent->client->ps.stats[STAT_WEAPN1+i] = gi.imageindex ("beamgun");
				i++;
		}
		if(ent->client->pers.inventory[ITEM_INDEX(FindItem("Alien Vaporizer"))] &&
			ent->client->pers.weapon != (FindItem("Alien Vaporizer"))) {
				ent->client->ps.stats[STAT_WEAPN1+i] = gi.imageindex ("vaporizor");
		}
	}
#endif

	//
	// current weapon
	//
	if (ent->client->pers.weapon)
		ent->client->ps.stats[STAT_HELPICON] = gi.imageindex (ent->client->pers.weapon->icon);
	else
		ent->client->ps.stats[STAT_HELPICON] = 0;
	
	ent->client->ps.stats[STAT_FLAGS] &= ~STAT_FLAGS_CROSSHAIRPOSITION;
	if	(	ent->client->pers.weapon == FindItem ("Alien Disruptor") || 
			ent->client->pers.weapon == FindItem ("Violator") || 
			ent->client->pers.weapon == FindItem ("Disruptor") ||
			ent->client->pers.weapon == FindItem ("Pulse Rifle"))
		ent->client->ps.stats[STAT_FLAGS] |= STAT_FLAGS_CROSSHAIRCENTER;
	else if (ent->client->pers.weapon == FindItem ("Rocket Launcher"))
		ent->client->ps.stats[STAT_FLAGS] |= STAT_FLAGS_CROSSHAIRPOS2;
}

/*
===============
G_SetSpectatorStats
===============
*/
void G_SetSpectatorStats (edict_t *ent)
{
	gclient_t *cl = ent->client;

	if (ent->client->showscores)
		cl->ps.stats[STAT_LAYOUTS] = 1;
	else
		cl->ps.stats[STAT_LAYOUTS] = 0;

	if (cl->chase_target && cl->chase_target->inuse)
	{
		// XXX: is this code ever active?
		cl->ps.stats[STAT_CHASE] = CS_PLAYERSKINS +
			(cl->chase_target - g_edicts) - 1;
	}
	else
	{
		cl->ps.stats[STAT_CHASE] = 0;
		G_SetScoreStats (ent);
	}
}

/**
 * @brief  Update stats for scoreboard display on
 *
 * @detail  Spectator issues complicate things. Spectator sees chase target's
 *          HUD information. But spectator needs to show zeroed scoring
 *          data in status responses, player lists, etc. Real player clients
 *          contain bot scoring info, which is updated by
 *          acebot_spawn.c::ACESP_UpdateBots(), called for every server frame
 *          and would be redundant if updated here. The server expects to see
 *          the bot info in the first client, regardless of it being in use
 *          or being a spectator.
 *
 * @param ent  entity for player or bot
 */
void G_UpdateStats( edict_t *ent )
{
	gclient_t *gcl = ent->client;

	if ( gcl->resp.spectator && !ent->is_bot )
	{
		if ( gcl->chase_target != NULL && level.intermissiontime <= 0.0f )
		{ // clone chase target's stats for hud
			memcpy( gcl->ps.stats, gcl->chase_target->client->ps.stats,
					sizeof( gcl->ps.stats ) );
		}
		else
		{
			memset( gcl->ps.stats, 0, sizeof(gcl->ps.stats));
			G_SetSpectatorStats( ent);
		}
		gcl->ps.stats[STAT_SPECTATOR] = 1;
	}
	else
	{
		ent->client->ps.stats[STAT_SPECTATOR] = 0;
		G_SetStats( ent );
	}
}
