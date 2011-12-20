/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 20?? COR Entertainment, LLC.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "g_local.h"

int floater_timer;
int prox_timer;

/*
=================
fire_lead

This is an internal support routine used for bullet/pellet based weapons.
=================
*/
static void fire_lead (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int kick, int te_impact, int hspread, int vspread, int mod, int silentHit)
{
	trace_t		tr;
	vec3_t		dir;
	vec3_t		forward, right, up;
	vec3_t		end;
	float		r;
	float		u;
	vec3_t		water_start;
	qboolean	water = false;
	int			content_mask = MASK_SHOT | MASK_WATER;

	if ( g_antilag->integer)
		G_DoTimeShiftFor( self );

	self->client->resp.weapon_shots[3]++;

	tr = gi.trace (self->s.origin, NULL, NULL, start, self, MASK_SHOT);
	if (!(tr.fraction < 1.0))
	{
		vectoangles (aimdir, dir);
		AngleVectors (dir, forward, right, up);

		r = crandom()*hspread;
		u = crandom()*vspread;
		VectorMA (start, 8192, forward, end);
		VectorMA (end, r, right, end);
		VectorMA (end, u, up, end);

		if (gi.pointcontents (start) & MASK_WATER)
		{
			water = true;
			VectorCopy (start, water_start);
			content_mask &= ~MASK_WATER;
		}

		tr = gi.trace (start, NULL, NULL, end, self, content_mask);

		// see if we hit water
		if (tr.contents & MASK_WATER)
		{
			int		color;

			water = true;
			VectorCopy (tr.endpos, water_start);

			if (!VectorCompare (start, tr.endpos))
			{
				if (tr.contents & CONTENTS_WATER)
				{
					if (strcmp(tr.surface->name, "*brwater") == 0)
						color = SPLASH_BROWN_WATER;
					else
						color = SPLASH_BLUE_WATER;
				}
				else if (tr.contents & CONTENTS_SLIME)
					color = SPLASH_SLIME;
				else if (tr.contents & CONTENTS_LAVA)
					color = SPLASH_LAVA;
				else
					color = SPLASH_UNKNOWN;

				if (color != SPLASH_UNKNOWN)
				{
					gi.WriteByte (svc_temp_entity);
					gi.WriteByte (TE_SPLASH);
					gi.WriteByte (8);
					gi.WritePosition (tr.endpos);
					gi.WriteDir (tr.plane.normal);
					gi.WriteByte (color);
					gi.multicast (tr.endpos, MULTICAST_PVS);
				}

				// change bullet's course when it enters water
				VectorSubtract (end, start, dir);
				vectoangles (dir, dir);
				AngleVectors (dir, forward, right, up);
				r = crandom()*hspread*2;
				u = crandom()*vspread*2;
				VectorMA (water_start, 8192, forward, end);
				VectorMA (end, r, right, end);
				VectorMA (end, u, up, end);
			}

			// re-trace ignoring water this time
			tr = gi.trace (water_start, NULL, NULL, end, self, MASK_SHOT);
		}
	}

	// send gun puff / flash
	if (!((tr.surface) && (tr.surface->flags & SURF_SKY)))
	{
		if (tr.fraction < 1.0)
		{
			if (tr.ent->takedamage)
			{
				T_Damage (tr.ent, self, self, aimdir, tr.endpos, tr.plane.normal, damage, kick, DAMAGE_BULLET, mod);
				self->client->resp.weapon_hits[3]++;
				if(!silentHit)
					gi.sound (self, CHAN_VOICE, gi.soundindex("misc/hit.wav"), 1, ATTN_STATIC, 0);
			}
			else
			{
				if (strncmp (tr.surface->name, "sky", 3) != 0)
				{
					gi.WriteByte (svc_temp_entity);
					gi.WriteByte (te_impact);
					gi.WritePosition (tr.endpos);
					gi.WriteDir (tr.plane.normal);
					gi.multicast (tr.endpos, MULTICAST_PVS);

					if (self->client)
						PlayerNoise(self, tr.endpos, PNOISE_IMPACT);
				}
			}
		}
	}

	// if went through water, determine where the end and make a bubble trail
	if (water)
	{
		vec3_t	pos;

		VectorSubtract (tr.endpos, water_start, dir);
		VectorNormalize (dir);
		VectorMA (tr.endpos, -2, dir, pos);
		if (gi.pointcontents (pos) & MASK_WATER)
			VectorCopy (pos, tr.endpos);
		else
			tr = gi.trace (pos, NULL, NULL, water_start, tr.ent, MASK_WATER);

		VectorAdd (water_start, tr.endpos, pos);
		VectorScale (pos, 0.5, pos);

		gi.WriteByte (svc_temp_entity);
		gi.WriteByte (TE_BUBBLETRAIL);
		gi.WritePosition (water_start);
		gi.WritePosition (tr.endpos);
		gi.multicast (pos, MULTICAST_PVS);
	}

	if ( g_antilag->integer)
		G_UndoTimeShiftFor( self );
}


/*
=================
fire_bullet

Fires a single round.  Used for machinegun and chaingun.  Would be fine for
pistols, rifles, etc....
=================
*/
void fire_bullet (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int kick, int hspread, int vspread, int mod)
{
	fire_lead (self, start, aimdir, damage, kick, TE_GUNSHOT, hspread, vspread, mod, false);
}


/*
=================
fire_shotgun

Shoots shotgun pellets.  Used by shotgun and super shotgun.
=================
*/
void fire_shotgun (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int kick, int hspread, int vspread, int count, int mod)
{
	int		i;

	for (i = 0; i < count; i++)
		fire_lead (self, start, aimdir, damage, kick, TE_GUNSHOT, hspread, vspread, mod, i);
}


/*
=================
fire_blaster
Fires a single blaster bolt.  Used by the blaster and hyper blaster.
=================
*/
void blaster_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	int		mod;

	if (other == self->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (self);
		return;
	}

	if (self->owner->client)
		PlayerNoise(self->owner, self->s.origin, PNOISE_IMPACT);

	if (other->takedamage)
	{
		if (self->spawnflags & 1)
			mod = MOD_BEAMGUN;
		else
			mod = MOD_BLASTER;
		T_Damage (other, self, self->owner, self->velocity, self->s.origin, plane->normal, self->dmg, 1, DAMAGE_ENERGY, mod);
		self->owner->client->resp.weapon_hits[0]++;
		gi.sound (self->owner, CHAN_VOICE, gi.soundindex("misc/hit.wav"), 1, ATTN_STATIC, 0);
	}
	else
	{
		gi.WriteByte (svc_temp_entity);
		gi.WriteByte (TE_BLASTER); //gonna change this badboy
		gi.WritePosition (self->s.origin);
		if (!plane)
			gi.WriteDir (vec3_origin);
		else
			gi.WriteDir (plane->normal);
		gi.multicast (self->s.origin, MULTICAST_PVS);

	}

	G_FreeEdict (self);
}

void blasterball_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	int		mod;

	if (other == self->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (self);
		return;
	}

	if (self->owner->client)
		PlayerNoise(self->owner, self->s.origin, PNOISE_IMPACT);

	if (other->takedamage)
	{
		if (self->spawnflags & 1)
			mod = MOD_BEAMGUN;
		else
			mod = MOD_BLASTER;
		T_Damage (other, self, self->owner, self->velocity, self->s.origin, plane->normal, self->dmg, 1, DAMAGE_ENERGY, mod);
		self->owner->client->resp.weapon_hits[0]++;
		gi.sound (self->owner, CHAN_VOICE, gi.soundindex("misc/hit.wav"), 1, ATTN_STATIC, 0);
	}
	else
	{
		gi.WriteByte (svc_temp_entity);
		gi.WriteByte (TE_BLASTER);
		gi.WritePosition (self->s.origin);
		if (!plane)
			gi.WriteDir (vec3_origin);
		else
			gi.WriteDir (plane->normal);
		gi.multicast (self->s.origin, MULTICAST_PVS);
	}
	T_RadiusDamage(self, self->owner, 95, other, 150, MOD_PLASMA_SPLASH, 0);
	G_FreeEdict (self);
}

void fire_blasterball (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, int effect, qboolean hyper)
{
	edict_t	*bolt;
	trace_t	tr;

	self->client->resp.weapon_shots[0]++;

	VectorNormalize (dir);

	bolt = G_Spawn();
	bolt->svflags = SVF_DEADMONSTER;
	VectorCopy (start, bolt->s.origin);
	VectorCopy (start, bolt->s.old_origin);
	vectoangles (dir, bolt->s.angles);
	VectorScale (dir, speed, bolt->velocity);
	bolt->movetype = MOVETYPE_FLYMISSILE;
	bolt->clipmask = MASK_SHOT;
	bolt->solid = SOLID_BBOX;
	if(effect == EF_ROCKET) { //ack, kinda assbackwards, but the past mistakes haunt us
		bolt->s.effects |= EF_BLASTER;
		bolt->s.modelindex = gi.modelindex ("models/objects/laser/tris.md2");
	}
	else {
		bolt->s.effects |= EF_PLASMA;
		bolt->s.modelindex = gi.modelindex ("models/objects/fireball/tris.md2");
	}
	VectorClear (bolt->mins);
	VectorClear (bolt->maxs);
	bolt->s.sound = gi.soundindex ("misc/lasfly.wav");
	bolt->owner = self;
	bolt->touch = blasterball_touch;
	bolt->nextthink = level.time + 2;
	bolt->think = G_FreeEdict;
	bolt->dmg = damage;

	bolt->classname = "bolt";

	gi.linkentity (bolt);

	tr = gi.trace (self->s.origin, NULL, NULL, bolt->s.origin, bolt, MASK_SHOT);
	if (tr.fraction < 1.0)
	{
		VectorMA (bolt->s.origin, -10, dir, bolt->s.origin);
		bolt->touch (bolt, tr.ent, NULL, NULL);
	}
}
void fire_blaster (edict_t *self, vec3_t start, vec3_t aimdir, int damage,
int speed, int effect, qboolean hyper)
{
	vec3_t		from;
	vec3_t		end;
	trace_t		tr;
	edict_t		*ignore;
	int			mask;
	qboolean	water;

	if ( g_antilag->integer)
		G_DoTimeShiftFor( self );

	self->client->resp.weapon_shots[6]++;

	VectorMA (start, 8192, aimdir, end);
	VectorCopy (start, from);
	ignore = self;
	water = false;
	mask = MASK_SHOT|CONTENTS_SLIME|CONTENTS_LAVA;

	tr = gi.trace (from, NULL, NULL, end, ignore, mask);

	if (tr.contents & (CONTENTS_SLIME|CONTENTS_LAVA))
	{
		mask &= ~(CONTENTS_SLIME|CONTENTS_LAVA);
		water = true;
	}
	else
	{
		if ((tr.ent != self) && (tr.ent->takedamage)) {
			T_Damage (tr.ent, self, self, aimdir, tr.endpos, tr.plane.normal, damage, 0, 0, MOD_BLASTER);
			self->client->resp.weapon_hits[6]++;
			gi.sound (self, CHAN_VOICE, gi.soundindex("misc/hit.wav"), 1, ATTN_STATIC, 0);
		}
	}
	VectorCopy (tr.endpos, from);

	VectorMA (start, 8192, aimdir, end);
	VectorCopy (start, from);

	// trace for end point of laser beam.      // the laser aim is perfect.
	// no random aim like the machinegun
	tr = gi.trace (from, NULL, NULL, end, self, MASK_SHOT);

	// send laser beam temp entity to clients
	VectorCopy (tr.endpos, from);

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_LASERBEAM);
	gi.WritePosition (start);
	gi.WritePosition (tr.endpos);
	gi.multicast (self->s.origin, MULTICAST_PHS);

	if ((tr.ent != self) && (tr.ent->takedamage))
	{
        	T_Damage (tr.ent, self, self, aimdir, tr.endpos, tr.plane.normal, damage, 0, 0, MOD_BEAMGUN);
		self->client->resp.weapon_hits[6]++;
		gi.sound (self, CHAN_VOICE, gi.soundindex("misc/hit.wav"), 1, ATTN_STATIC, 0);
	}
	else if (!((tr.surface) && (tr.surface->flags & SURF_SKY)))
	{
		gi.WriteByte (svc_temp_entity);
		gi.WriteByte (TE_SCREEN_SPARKS);
		gi.WritePosition (tr.endpos);
		gi.WriteDir (tr.plane.normal);
		gi.multicast (self->s.origin, MULTICAST_PVS);
	}

	if ( g_antilag->integer)
		G_UndoTimeShiftFor( self );
}

/*
=================
fire_rocket
=================
*/
void rocket_touch (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	vec3_t		origin;

	if (other == ent->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (ent);
		return;
	}

	if (ent->owner->client)
		PlayerNoise(ent->owner, ent->s.origin, PNOISE_IMPACT);

	// calculate position for the explosion entity
	VectorMA (ent->s.origin, -0.02, ent->velocity, origin);

	if (other->takedamage)
	{
		T_Damage (other, ent, ent->owner, ent->velocity, ent->s.origin, plane->normal, ent->dmg, 0, 0, MOD_ROCKET);
		ent->owner->client->resp.weapon_hits[5]++;
		gi.sound (ent->owner, CHAN_VOICE, gi.soundindex("misc/hit.wav"), 1, ATTN_STATIC, 0);
	}

	T_RadiusDamage(ent, ent->owner, ent->radius_dmg, other, ent->dmg_radius, MOD_R_SPLASH, 5);

	gi.WriteByte (svc_temp_entity);
	if (ent->waterlevel)
		gi.WriteByte (TE_ROCKET_EXPLOSION_WATER);
	else
		gi.WriteByte (TE_ROCKET_EXPLOSION);
	gi.WritePosition (origin);
	gi.multicast (ent->s.origin, MULTICAST_PHS);

	G_FreeEdict (ent);
}

void fire_rocket (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, float damage_radius, int radius_damage)
{
	edict_t	*rocket;

	self->client->resp.weapon_shots[5]++;

	rocket = G_Spawn();
	VectorCopy (start, rocket->s.origin);
	VectorCopy (dir, rocket->movedir);
	vectoangles (dir, rocket->s.angles);
	VectorScale (dir, speed, rocket->velocity);
	rocket->movetype = MOVETYPE_FLYMISSILE;
	rocket->clipmask = MASK_SHOT;
	rocket->solid = SOLID_BBOX;
	rocket->s.effects |= EF_ROCKET | EF_ROCKETEXHAUST ;
	VectorClear (rocket->mins);
	VectorClear (rocket->maxs);
	rocket->s.modelindex = gi.modelindex ("models/objects/rocket/tris.md2");
	rocket->owner = self;
	rocket->touch = rocket_touch;
	rocket->nextthink = level.time + 8000/speed;
	rocket->think = G_FreeEdict;
	rocket->dmg = damage;
	rocket->radius_dmg = radius_damage;
	rocket->dmg_radius = damage_radius;
	rocket->s.sound = gi.soundindex ("weapons/rockfly.wav");
	rocket->classname = "rocket";

	gi.linkentity (rocket);
}

/*
=================
fire_stinger
=================
*/
void stinger_touch (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	vec3_t		origin;
	int			n;

	if (other == ent->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (ent);
		return;
	}

	if (ent->owner->client)
		PlayerNoise(ent->owner, ent->s.origin, PNOISE_IMPACT);

	// calculate position for the explosion entity
	VectorMA (ent->s.origin, -0.02, ent->velocity, origin);

	if (other->takedamage)
	{
		T_Damage (other, ent, ent->owner, ent->velocity, ent->s.origin, plane->normal, ent->dmg, 0, 0, MOD_ROCKET);
		ent->owner->client->resp.weapon_hits[5]++;
		gi.sound (ent->owner, CHAN_VOICE, gi.soundindex("misc/hit.wav"), 1, ATTN_STATIC, 0);
	}
	else
	{
		// don't throw any debris in net games
		if (!deathmatch->value)
		{
			if ((surf) && !(surf->flags & (SURF_WARP|SURF_TRANS33|SURF_TRANS66|SURF_FLOWING)))
			{
				n = rand() % 5;
				while(n--)
					ThrowDebris (ent, "models/objects/debris2/tris.md2", 2, ent->s.origin);
			}
		}
	}

	T_RadiusDamage(ent, ent->owner, ent->radius_dmg, other, ent->dmg_radius, MOD_R_SPLASH, 5);

	gi.WriteByte (svc_temp_entity);
	if (ent->waterlevel)
		gi.WriteByte (TE_ROCKET_EXPLOSION_WATER);
	else
		gi.WriteByte (TE_ROCKET_EXPLOSION);
	gi.WritePosition (origin);
	gi.multicast (ent->s.origin, MULTICAST_PHS);

	G_FreeEdict (ent);
}

void fire_blaster_beam (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int kick, qboolean detonate)
{
	vec3_t		from;
	vec3_t		end;
	vec3_t		dir;
	trace_t		tr;
	edict_t		*ignore, *bomb;
	int			mask;
	qboolean	water;
	vec3_t		water_start;
	int			content_mask = MASK_SHOT | MASK_WATER;

	if ( g_antilag->integer)
		G_DoTimeShiftFor( self );

	self->client->resp.weapon_shots[0]++;

	VectorMA (start, 8192, aimdir, end);
	VectorCopy (start, from);
	ignore = self;
	water = false;
	mask = MASK_SHOT|CONTENTS_SLIME|CONTENTS_LAVA;

	tr = gi.trace (from, NULL, NULL, end, ignore, mask);

	if (tr.contents & (CONTENTS_SLIME|CONTENTS_LAVA))
	{
		mask &= ~(CONTENTS_SLIME|CONTENTS_LAVA);
		water = true;
	}
	else
	{

		if ((tr.ent != self) && (tr.ent->takedamage)) {
			T_Damage (tr.ent, self, self, aimdir, tr.endpos, tr.plane.normal, damage, kick, 0, MOD_BLASTER);
			self->client->resp.weapon_hits[0]++;
			gi.sound (self, CHAN_VOICE, gi.soundindex("misc/hit.wav"), 1, ATTN_STATIC, 0);
		}
	}

	VectorCopy (tr.endpos, from);

	VectorMA (start, 8192, aimdir, end);
	VectorCopy (start, from);

	// trace for end point of laser beam.      // the laser aim is perfect.
	// no random aim like the machinegun
	tr = gi.trace (from, NULL, NULL, end, self, MASK_SHOT);

	// send laser beam temp entity to clients
	VectorCopy (tr.endpos, from);

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_BLASTERBEAM);
	gi.WritePosition (start);
	gi.WritePosition (tr.endpos);
	gi.multicast (self->s.origin, MULTICAST_PHS);

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_BLASTER);
	gi.WritePosition (tr.endpos);
	gi.WriteDir (tr.plane.normal);
	gi.multicast (tr.endpos, MULTICAST_PVS);

	if(detonate) {
		//spawn a new ent at end of explosion - and detonate it
		bomb = G_Spawn();
		VectorCopy (tr.endpos, bomb->s.origin);
		bomb->movetype = MOVETYPE_NONE;
		bomb->solid = SOLID_NOT;
		bomb->s.modelindex = 0;
		bomb->owner = self;
		bomb->think = G_FreeEdict;
		bomb->classname = "bomb";
		gi.linkentity (bomb);
		T_RadiusDamage(bomb, self, 50, NULL, 200, MOD_VAPORALTFIRE, 0);
		G_FreeEdict (bomb);
	}

	if (gi.pointcontents (start) & MASK_WATER)
		{
			water = true;
			VectorCopy (start, water_start);
			content_mask &= ~MASK_WATER;
		}

	tr = gi.trace (start, NULL, NULL, end, self, content_mask);

	// see if we hit water
	if (tr.contents & MASK_WATER)
	{
		int		color;
		water = true;
		VectorCopy (tr.endpos, water_start);

		if (!VectorCompare (start, tr.endpos))
		{
			if (tr.contents & CONTENTS_WATER)
			{
				if (strcmp(tr.surface->name, "*brwater") == 0)
					color = SPLASH_BROWN_WATER;
				else
					color = SPLASH_BLUE_WATER;
			}
			else if (tr.contents & CONTENTS_SLIME)
				color = SPLASH_SLIME;
			else if (tr.contents & CONTENTS_LAVA)
				color = SPLASH_LAVA;
			else
				color = SPLASH_UNKNOWN;

			if (color != SPLASH_UNKNOWN)
			{
				gi.WriteByte (svc_temp_entity);
				gi.WriteByte (TE_SPLASH);
				gi.WriteByte (8);
				gi.WritePosition (tr.endpos);
				gi.WriteDir (tr.plane.normal);
				gi.WriteByte (color);
				gi.multicast (tr.endpos, MULTICAST_PVS);
			}

		}

	}
	// if went through water, determine where the end and make a bubble trail
	if (water)
	{
		vec3_t	pos;

		VectorSubtract (tr.endpos, water_start, dir);
		VectorNormalize (dir);
		VectorMA (tr.endpos, -2, dir, pos);
		if (gi.pointcontents (pos) & MASK_WATER)
			VectorCopy (pos, tr.endpos);
		else
			tr = gi.trace (pos, NULL, NULL, water_start, tr.ent, MASK_WATER);

		VectorAdd (water_start, tr.endpos, pos);
		VectorScale (pos, 0.5, pos);

		gi.WriteByte (svc_temp_entity);
		gi.WriteByte (TE_BUBBLETRAIL);
		gi.WritePosition (water_start);
		gi.WritePosition (tr.endpos);
		gi.multicast (pos, MULTICAST_PVS);
	}

	if ( g_antilag->integer)
		G_UndoTimeShiftFor( self );

}
void fire_hover_beam (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int kick, qboolean detonate)
{
	vec3_t		from;
	vec3_t		end;
	vec3_t		dir;
	trace_t		tr;
	edict_t		*ignore, *bomb;
	int			mask;
	qboolean	water;
	vec3_t		water_start;
	int			content_mask = MASK_SHOT | MASK_WATER;

	if ( g_antilag->integer)
		G_DoTimeShiftFor( self );

	VectorMA (start, 8192, aimdir, end);
	VectorCopy (start, from);
	ignore = self;
	water = false;
	mask = MASK_SHOT|CONTENTS_SLIME|CONTENTS_LAVA;

	tr = gi.trace (from, NULL, NULL, end, ignore, mask);

	if (tr.contents & (CONTENTS_SLIME|CONTENTS_LAVA))
	{
		mask &= ~(CONTENTS_SLIME|CONTENTS_LAVA);
		water = true;
	}
	else
	{
		if ((tr.ent != self) && (tr.ent->takedamage))
			T_Damage (tr.ent, self, self, aimdir, tr.endpos, tr.plane.normal, damage, kick, 0, MOD_BLASTER);
			if (tr.ent->health > 0)
			{
				gi.sound (self, CHAN_VOICE, gi.soundindex("misc/hit.wav"), 1, ATTN_STATIC, 0);
			}
		}
	VectorCopy (tr.endpos, from);

	VectorMA (start, 8192, aimdir, end);
	VectorCopy (start, from);

	// trace for end point of laser beam.      // the laser aim is perfect.
	// no random aim like the machinegun
	tr = gi.trace (from, NULL, NULL, end, self, MASK_SHOT);

	// send laser beam temp entity to clients
	VectorCopy (tr.endpos, from);

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_REDLASER);
	gi.WritePosition (start);
	gi.WritePosition (tr.endpos);
	gi.multicast (self->s.origin, MULTICAST_PHS);


	if(detonate) {
		//spawn a new ent at end of explosion - and detonate it
		bomb = G_Spawn();
		VectorCopy (tr.endpos, bomb->s.origin);
		bomb->movetype = MOVETYPE_NONE;
		bomb->solid = SOLID_NOT;
		bomb->s.modelindex = 0;
		bomb->owner = self;
		bomb->think = G_FreeEdict;
		bomb->classname = "bomb";
		gi.linkentity (bomb);
		T_RadiusDamage(bomb, self, 100, NULL, 200, MOD_VAPORALTFIRE, -1);

		gi.WriteByte (svc_temp_entity);
		if (bomb->waterlevel)
		gi.WriteByte (TE_ROCKET_EXPLOSION_WATER);
			else
		gi.WriteByte (TE_ROCKET_EXPLOSION);
		gi.WritePosition (bomb->s.origin);
		gi.multicast (bomb->s.origin, MULTICAST_PHS);

		G_FreeEdict (bomb);
	}

	if (gi.pointcontents (start) & MASK_WATER)
		{
			water = true;
			VectorCopy (start, water_start);
			content_mask &= ~MASK_WATER;
		}

	tr = gi.trace (start, NULL, NULL, end, self, content_mask);

	// see if we hit water
	if (tr.contents & MASK_WATER)
	{
		int		color;
		water = true;
		VectorCopy (tr.endpos, water_start);

		if (!VectorCompare (start, tr.endpos))
		{
			if (tr.contents & CONTENTS_WATER)
			{
				if (strcmp(tr.surface->name, "*brwater") == 0)
					color = SPLASH_BROWN_WATER;
				else
					color = SPLASH_BLUE_WATER;
			}
			else if (tr.contents & CONTENTS_SLIME)
				color = SPLASH_SLIME;
			else if (tr.contents & CONTENTS_LAVA)
				color = SPLASH_LAVA;
			else
				color = SPLASH_UNKNOWN;

			if (color != SPLASH_UNKNOWN)
			{
				gi.WriteByte (svc_temp_entity);
				gi.WriteByte (TE_SPLASH);
				gi.WriteByte (8);
				gi.WritePosition (tr.endpos);
				gi.WriteDir (tr.plane.normal);
				gi.WriteByte (color);
				gi.multicast (tr.endpos, MULTICAST_PVS);
			}

		}

	}
	// if went through water, determine where the end and make a bubble trail
	if (water)
	{
		vec3_t	pos;

		VectorSubtract (tr.endpos, water_start, dir);
		VectorNormalize (dir);
		VectorMA (tr.endpos, -2, dir, pos);
		if (gi.pointcontents (pos) & MASK_WATER)
			VectorCopy (pos, tr.endpos);
		else
			tr = gi.trace (pos, NULL, NULL, water_start, tr.ent, MASK_WATER);

		VectorAdd (water_start, tr.endpos, pos);
		VectorScale (pos, 0.5, pos);

		gi.WriteByte (svc_temp_entity);
		gi.WriteByte (TE_BUBBLETRAIL);
		gi.WritePosition (water_start);
		gi.WritePosition (tr.endpos);
		gi.multicast (pos, MULTICAST_PVS);
	}

	if ( g_antilag->integer)
		G_UndoTimeShiftFor( self );

}

void fire_plasma (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int kick)
{
	vec3_t		from;
	vec3_t		end;
	trace_t		tr;
	edict_t		*ignore, *bomb;
	int			mask;
	qboolean	water;

	if ( g_antilag->integer)
		G_DoTimeShiftFor( self );

	self->client->resp.weapon_shots[1]++;

	VectorMA (start, 8192, aimdir, end);
	VectorCopy (start, from);
	ignore = self;
	water = false;
	mask = MASK_SHOT|CONTENTS_SLIME|CONTENTS_LAVA;

	tr = gi.trace (from, NULL, NULL, end, ignore, mask);

	if (tr.contents & (CONTENTS_SLIME|CONTENTS_LAVA))
	{
		mask &= ~(CONTENTS_SLIME|CONTENTS_LAVA);
		water = true;
	}
	else
	{

		if ((tr.ent != self) && (tr.ent->takedamage)) {
			T_Damage (tr.ent, self, self, aimdir, tr.endpos, tr.plane.normal, damage, kick, 0, MOD_DISRUPTOR);
			self->client->resp.weapon_hits[1]++;
			gi.sound (self, CHAN_VOICE, gi.soundindex("misc/hit.wav"), 1, ATTN_STATIC, 0);
		}
	}
	VectorCopy (tr.endpos, from);

	VectorMA (start, 8192, aimdir, end);
	VectorCopy (start, from);

	// trace for end point of laser beam.      // the laser aim is perfect.
	// no random aim like the machinegun
	tr = gi.trace (from, NULL, NULL, end, self, MASK_SHOT);

	// send laser beam temp entity to clients
	VectorCopy (tr.endpos, from);

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_RAILTRAIL);
	gi.WritePosition (start);
	gi.WritePosition (tr.endpos);
	gi.multicast (self->s.origin, MULTICAST_PHS);

	//spawn a new ent at end and detonate - for trick jumping
	bomb = G_Spawn();
	VectorCopy (tr.endpos, bomb->s.origin);
	bomb->movetype = MOVETYPE_NONE;
	bomb->solid = SOLID_NOT;
	bomb->s.modelindex = 0;
	bomb->owner = self;
	bomb->think = G_FreeEdict;
	bomb->classname = "bomb";
	gi.linkentity (bomb);
	T_RadiusDamage(bomb, self, 95, NULL, 50, MOD_PLASMA_SPLASH, -1);
	G_FreeEdict (bomb);

	if (self->client)
		PlayerNoise(self, tr.endpos, PNOISE_IMPACT);

	if ( g_antilag->integer)
		G_UndoTimeShiftFor( self );
}

//vaporizer code
void fire_energy_field (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int kick)
{

	vec3_t		from;
	vec3_t		end;
	trace_t		tr;
	edict_t		*ignore;
	edict_t	*bomb;
	int			mask;
	qboolean	water;

	if ( g_antilag->integer)
		G_DoTimeShiftFor( self );

	self->client->resp.weapon_shots[7]++;

	VectorMA (start, 8192, aimdir, end);
	VectorCopy (start, from);
	ignore = self;
	water = false;
	mask = MASK_SHOT|CONTENTS_SLIME|CONTENTS_LAVA;

	tr = gi.trace (from, NULL, NULL, end, ignore, mask);

	if (tr.contents & (CONTENTS_SLIME|CONTENTS_LAVA))
	{
		mask &= ~(CONTENTS_SLIME|CONTENTS_LAVA);
		water = true;
	}
	else
	{
		if ((tr.ent != self) && (tr.ent->takedamage)) {
			T_Damage (tr.ent, self, self, aimdir, tr.endpos, tr.plane.normal, damage, kick, 0, MOD_VAPORIZER);
			self->client->resp.weapon_hits[7]++;
				gi.sound (self, CHAN_VOICE, gi.soundindex("misc/hit.wav"), 1, ATTN_STATIC, 0);

			T_RadiusDamage(tr.ent, self, damage, NULL, 120, MOD_VAPORIZER, -1);
		}
	}
	VectorCopy (tr.endpos, from);

	VectorMA (start, 8192, aimdir, end);
	VectorCopy (start, from);

	// trace for end point of laser beam.      // the laser aim is perfect.
	// no random aim like the machinegun
	tr = gi.trace (from, NULL, NULL, end, self, MASK_SHOT);

	// send laser beam temp entity to clients
	VectorCopy (tr.endpos, from);

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_VAPORBEAM);
	gi.WritePosition (start);
	gi.WritePosition (tr.endpos);
	gi.multicast (self->s.origin, MULTICAST_PHS);

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_BFG_BIGEXPLOSION);
	gi.WritePosition (tr.endpos);
	gi.multicast (tr.endpos, MULTICAST_PVS);

	//spawn a new ent at end of explosion - and detonate it
	bomb = G_Spawn();
	VectorCopy (tr.endpos, bomb->s.origin);
	bomb->movetype = MOVETYPE_NONE;
	bomb->solid = SOLID_NOT;
	bomb->s.modelindex = 0;
	bomb->owner = self;
	bomb->think = G_FreeEdict;
	bomb->classname = "bomb";
	gi.linkentity (bomb);
	T_RadiusDamage(bomb, self, 150, NULL, 150, MOD_VAPORIZER, 7); //ridiculously powerful!
	G_FreeEdict (bomb);

	if (self->client)
		PlayerNoise(self, tr.endpos, PNOISE_IMPACT);

	if ( g_antilag->integer)
		G_UndoTimeShiftFor( self );
}


void homing_think (edict_t *ent)
{
	edict_t *target = NULL;
	edict_t *blip = NULL;
	vec3_t  targetdir, blipdir;
	vec_t   speed;

	while ((blip = findradius(blip, ent->s.origin, 1000)) != NULL)
	{
		if (!(blip->svflags & SVF_MONSTER) && !blip->client)
			continue;
		if (blip == ent->owner)
			continue;
		if (!blip->takedamage)
			continue;
		if (blip->health <= 0)
			continue;
		if (!visible(ent, blip))
			continue;
		if (!infront(ent, blip))
			continue;
		VectorSubtract(blip->s.origin, ent->s.origin, blipdir);
		blipdir[2] += 16;
		if ((target == NULL) || (VectorLength(blipdir) < VectorLength(targetdir)))
		{
			target = blip;
			VectorCopy(blipdir, targetdir);
		}
	}

	if (target != NULL)
	{
		// target acquired, nudge our direction toward it
		VectorNormalize(targetdir);
		VectorScale(targetdir, 0.2, targetdir);
		VectorAdd(targetdir, ent->movedir, targetdir);
		VectorNormalize(targetdir);
		VectorCopy(targetdir, ent->movedir);
		vectoangles(targetdir, ent->s.angles);
		speed = VectorLength(ent->velocity);
		VectorScale(targetdir, speed, ent->velocity);
	}

	ent->nextthink = level.time + .1;
}

void fire_homingrocket (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, float damage_radius, int radius_damage)
{
	edict_t	*rocket;

	self->client->resp.weapon_shots[5]++;
	self->client->homing_shots++;

	rocket = G_Spawn();
	VectorCopy (start, rocket->s.origin);
	VectorCopy (dir, rocket->movedir);
	vectoangles (dir, rocket->s.angles);
	VectorScale (dir, speed, rocket->velocity);
	rocket->movetype = MOVETYPE_FLYMISSILE;
	rocket->clipmask = MASK_SHOT;
	rocket->solid = SOLID_BBOX;
	rocket->s.effects |= EF_ROCKET | EF_ROCKETEXHAUST ;
	VectorClear (rocket->mins);
	VectorClear (rocket->maxs);
	rocket->s.modelindex = gi.modelindex ("models/objects/rocket/tris.md2");
	rocket->owner = self;
	rocket->touch = rocket_touch;

	// CCH: if they have 5 cells, start homing, otherwise normal rocket think
	if (self->client->pers.inventory[ITEM_INDEX(FindItem("Cells"))] >= 5)
	{
		self->client->pers.inventory[ITEM_INDEX(FindItem("Cells"))] -= 5;
		rocket->nextthink = level.time + .1;
		rocket->think = homing_think;
	}
	else
	{
		safe_cprintf(self, PRINT_HIGH, "No cells for homing missile.\n");
		rocket->nextthink = level.time + 8000/speed;
		rocket->think = G_FreeEdict;
	}

    rocket->dmg = damage;
	rocket->radius_dmg = radius_damage;
	rocket->dmg_radius = damage_radius;
	rocket->s.sound = gi.soundindex ("weapons/rockfly.wav");
	rocket->classname = "rocket";

	gi.linkentity (rocket);
}

/*
=================
fire_floater
=================
*/

void floater_think (edict_t *self)
{
	edict_t	*ent;
	edict_t *target = NULL;
	edict_t	*ignore;
	vec3_t	point;
	vec3_t	dir;
	vec3_t	start;
	vec3_t	end;
	int		dmg;
	trace_t	tr;

	if (deathmatch->value) {
		if(excessive->value)
			dmg = 15;
		else
			dmg = 3;
	}
	else
		dmg = 7;

	ent = NULL;
	while ((ent = findradius(ent, self->s.origin, 256)) != NULL)
	{
		if (ent == self)
			continue;

		if (ent == self->owner)
			continue;

		if (!ent->takedamage)
			continue;

		if (!(ent->svflags & SVF_MONSTER) && (!ent->client) && (strcmp(ent->classname, "misc_explobox") != 0))
			continue;

		VectorMA (ent->absmin, 0.5, ent->size, point);

		VectorSubtract (point, self->s.origin, dir);
		VectorNormalize (dir);

		ignore = self;
		VectorCopy (self->s.origin, start);
		VectorMA (start, 2048, dir, end);

		tr = gi.trace (start, NULL, NULL, end, ignore, CONTENTS_SOLID|CONTENTS_MONSTER|CONTENTS_DEADMONSTER);


		// hurt it if we can
		if ((tr.ent->takedamage) && !(tr.ent->flags & FL_IMMUNE_LASER) && (tr.ent != self->owner)) {
			T_Damage (tr.ent, self, self->owner, dir, tr.endpos, vec3_origin, dmg, 1, DAMAGE_ENERGY, MOD_SMARTGUN);
			self->owner->client->resp.weapon_shots[2]++;
			self->owner->client->resp.weapon_hits[2]++;
			gi.sound (self->owner, CHAN_VOICE, gi.soundindex("misc/hit.wav"), 1, ATTN_STATIC, 0);
		}

		// if we hit something that's not a monster or player we're done
		if (!(tr.ent->svflags & SVF_MONSTER) && (!tr.ent->client))
		{
			gi.WriteByte (svc_temp_entity);
			gi.WriteByte (TE_LASER_SPARKS);
			gi.WriteByte (4);
			gi.WritePosition (tr.endpos);
			gi.WriteDir (tr.plane.normal);
			gi.WriteByte (self->s.skinnum);
			gi.multicast (tr.endpos, MULTICAST_PVS);
		}

		VectorCopy (tr.endpos, start);

		gi.WriteByte (svc_temp_entity);
		gi.WriteByte (TE_LIGHTNING);
		gi.WritePosition (self->s.origin);
		gi.WritePosition (tr.endpos);
		gi.multicast (self->s.origin, MULTICAST_PHS);

		target = ent;
	}

	self->s.frame = (self->s.frame + 1) % 23; //pulse grotesquely
	self->nextthink = level.time + FRAMETIME;
	floater_timer++;

	if(floater_timer > 10) { //explode
		T_RadiusDamage(self, self->owner, self->radius_dmg, self->enemy, self->dmg_radius, MOD_R_SPLASH, 2);

		gi.WriteByte (svc_temp_entity);
		gi.WriteByte (TE_BFG_BIGEXPLOSION);
		gi.WritePosition (self->s.origin);
		gi.multicast (self->s.origin, MULTICAST_PHS);

		G_FreeEdict (self);
	}

}
void prox_think (edict_t *self)
{
	edict_t	*ent;

	ent = NULL;
	while ((ent = findradius(ent, self->s.origin, 64)) != NULL)
	{
		if (ent == self)
			continue;

		if (ent == self->owner)
			continue;

		if (!ent->takedamage)
			continue;

		T_RadiusDamage(self, self->owner, self->radius_dmg, self->enemy, self->dmg_radius, MOD_R_SPLASH, -1);
		self->owner->client->resp.weapon_hits[2]++;

		gi.WriteByte (svc_temp_entity);
		gi.WriteByte (TE_BFG_BIGEXPLOSION);
		gi.WritePosition (self->s.origin);
		gi.multicast (self->s.origin, MULTICAST_PHS);

		G_FreeEdict (self);
		return;
	}

	self->s.frame = (self->s.frame + 1) % 23; //pulse grotesquely
	self->nextthink = level.time + FRAMETIME;
	prox_timer++;

	if(prox_timer > 300)
	{	//explode
		T_RadiusDamage(self, self->owner, self->radius_dmg, self->enemy, self->dmg_radius, MOD_R_SPLASH, 2);

		gi.WriteByte (svc_temp_entity);
		gi.WriteByte (TE_BFG_BIGEXPLOSION);
		gi.WritePosition (self->s.origin);
		gi.multicast (self->s.origin, MULTICAST_PHS);

		G_FreeEdict (self);
	}

}
void floater_touch (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (other == ent->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (ent);
		return;
	}

	//just bounce off of everything
	gi.sound (ent, CHAN_VOICE, gi.soundindex ("weapons/clank.wav"), 1, ATTN_NORM, 0);
	return;

}

void fire_floater (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int speed, float damage_radius, int radius_damage, float timer)
{
	edict_t	*floater;
    vec3_t	dir, forward, right, up;

	self->client->resp.weapon_shots[2]++;

	vectoangles(aimdir, dir);
	AngleVectors(dir, forward, right, up);

	floater = G_Spawn();
	VectorCopy (start, floater->s.origin);
	VectorScale (aimdir, speed, floater->velocity);
	VectorMA (floater->velocity, 200 + crandom() * 10.0, up, floater->velocity);
	VectorMA (floater->velocity, crandom() * 10.0, right, floater->velocity);
	VectorSet (floater->avelocity, 300, 300, 300);
	floater->movetype = MOVETYPE_BOUNCE;
	floater->clipmask = MASK_SHOT;
	floater->solid = SOLID_BBOX;
	floater->s.effects |= EF_COLOR_SHELL;
	floater->s.renderfx |= RF_SHELL_BLUE | RF_GLOW;
	VectorClear (floater->mins);
	VectorClear (floater->maxs);
	floater->s.modelindex = gi.modelindex ("models/objects/electroball/tris.md2");
	floater->owner = self;
	floater->touch = floater_touch;
	floater->nextthink = level.time + .1;
    floater->think = floater_think;
    floater->dmg = damage;
	floater->radius_dmg = radius_damage;
	floater->dmg_radius = damage_radius;
	floater->s.sound = gi.soundindex ("weapons/electroball.wav");
	floater->classname = "grenade";
	floater_timer = 0;


	gi.linkentity (floater);
}
void fire_prox (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int speed, float damage_radius, int radius_damage, float timer)
{
	edict_t	*prox;
    vec3_t	dir, forward, right, up;

	self->client->resp.weapon_shots[2]++;

	vectoangles(aimdir, dir);
	AngleVectors(dir, forward, right, up);

	prox = G_Spawn();
	VectorCopy (start, prox->s.origin);
	VectorScale (aimdir, speed, prox->velocity);
	VectorMA (prox->velocity, 20 + crandom() * 10.0, up, prox->velocity);
	VectorMA (prox->velocity, crandom() * 10.0, right, prox->velocity);
	VectorSet (prox->avelocity, 20, 20, 20);
	prox->movetype = MOVETYPE_BOUNCE;
	prox->clipmask = MASK_SHOT;
	prox->solid = SOLID_BBOX;
	VectorClear (prox->mins);
	VectorClear (prox->maxs);
	prox->s.modelindex = gi.modelindex ("models/objects/electroball/tris.md2");
	prox->owner = self;
	prox->touch = floater_touch;
	prox->nextthink = level.time + .1;
    prox->think = prox_think;
    prox->dmg = damage;
	prox->radius_dmg = radius_damage;
	prox->dmg_radius = damage_radius;
	prox->classname = "mine";
	prox_timer = 0;

	gi.linkentity (prox);
}

//vehicles
void bomb_think (edict_t *self)
{
	self->nextthink = level.time + FRAMETIME;
}
void bomb_blow (edict_t *self)
{
	self->nextthink = level.time + .02;
	self->s.frame++;
	G_FreeEdict (self);
}
void bomb_touch (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	vec3_t		origin;
	int			i;
	edict_t	*e;

	if (other == ent->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (ent);
		return;
	}

	if (ent->owner->client)
		PlayerNoise(ent->owner, ent->s.origin, PNOISE_IMPACT);

	// calculate position for the explosion entity
	VectorMA (ent->s.origin, -0.02, ent->velocity, origin);

	if (other->takedamage)
	{
		T_Damage (other, ent, ent->owner, ent->velocity, ent->s.origin, plane->normal, ent->dmg, 0, 0, MOD_ROCKET);
		ent->owner->client->resp.weapon_hits[7]++;
		gi.sound (ent->owner, CHAN_VOICE, gi.soundindex("misc/hit.wav"), 1, ATTN_STATIC, 0);
	}

	T_RadiusDamage(ent, ent->owner, ent->radius_dmg, other, ent->dmg_radius, MOD_R_SPLASH, 7);

	//advance to expoding frame
	ent->s.frame++;

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_BFG_BIGEXPLOSION);
	gi.WritePosition (origin);
	gi.multicast (ent->s.origin, MULTICAST_PHS);

	gi.sound (ent, CHAN_WEAPON, gi.soundindex("vehicles/explodebomb.wav"), 1, ATTN_NORM, 0);

	//shake the ground a bit(it's a big ass bomb, right?)
	for (i=1, e=g_edicts+i; i < globals.num_edicts; i++,e++)
	{
		if (!e->inuse)
			continue;
		if (!e->client)
			continue;
		if (!e->groundentity)
			continue;

		e->groundentity = NULL;
		e->velocity[0] += crandom()* 50;
		e->velocity[1] += crandom()* 50;
		e->velocity[2] += 175 + crandom()* 50;
	}
	ent->think = bomb_blow;
	ent->nextthink = level.time + .1;
	//G_FreeEdict (ent);
}
void fire_bomb (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int speed, float damage_radius, int radius_damage, float timer)
{
	edict_t	*bomb;
    vec3_t	dir, forward, right, up;

	vectoangles(aimdir, dir);
	AngleVectors(dir, forward, right, up);

	bomb = G_Spawn();
	VectorCopy (start, bomb->s.origin);
	VectorScale (aimdir, speed, bomb->velocity);
	VectorMA (bomb->velocity, 200 + crandom() * 10.0, up, bomb->velocity);
	VectorMA (bomb->velocity, crandom() * 10.0, right, bomb->velocity);
	VectorSet (bomb->avelocity, 100, 100, 100);
	bomb->movetype = MOVETYPE_BOUNCE;
	bomb->clipmask = MASK_SHOT;
	bomb->solid = SOLID_BBOX;
	bomb->s.effects |= EF_ROTATE;
	bomb->s.renderfx |= RF_GLOW;
	VectorClear (bomb->mins);
	VectorClear (bomb->maxs);
	bomb->s.modelindex = gi.modelindex ("vehicles/bomber/bomb.md2");
	bomb->owner = self;
	bomb->touch = bomb_touch;
	bomb->nextthink = level.time + .1;
    bomb->think = bomb_think;
    bomb->dmg = damage;
	bomb->radius_dmg = radius_damage;
	bomb->dmg_radius = damage_radius;
	bomb->s.sound = gi.soundindex ("vehicles/flybomb.wav"); //get a new sound
	bomb->classname = "grenade";

	gi.linkentity (bomb);
}

/*
=======================
Flamethrower
=======================
*/
void Fire_Think (edict_t *self)
{
	vec3_t dir;
	int damage;
	float	points;
	vec3_t	v;

	if (level.time > self->delay || self->owner->deadflag == DEAD_DEAD)
	{
		self->owner->Flames--;
		G_FreeEdict (self);
		return;
	}

	if (!self->owner)
	{
		G_FreeEdict (self);
		return;
	}
	if (self->owner->waterlevel)
	{
		self->owner->Flames--;
		G_FreeEdict (self);
		return;
	}
	damage = self->FlameDamage;
	VectorAdd (self->orb->mins, self->orb->maxs, v);
	VectorMA (self->orb->s.origin, 0.5, v, v);
	VectorSubtract (self->s.origin, v, v);
	points = damage - 0.5 * (VectorLength (v));
	VectorSubtract (self->owner->s.origin, self->s.origin, dir);

	if (self->FlameDelay < level.time)
	{
		T_Damage (self->owner, self, self->orb, dir, self->owner->s.origin,vec3_origin, damage, 0, DAMAGE_NO_KNOCKBACK,MOD_FLAME);
		self->FlameDelay = level.time + 0.8;
	}
	VectorCopy(self->owner->s.origin,self->s.origin);
	self->nextthink = level.time + .2;
}

void burn_person(edict_t *target, edict_t *owner, int damage)
{
	edict_t	*flame;

	if (target->Flames > 1)//This number sets the allowed amount of flames per person
		return;
	target->Flames++;
	flame = G_Spawn();
	flame->movetype = MOVETYPE_NOCLIP;
	flame->clipmask = MASK_SHOT;
	flame->solid = SOLID_NOT;
	flame->s.effects |= EF_SHIPEXHAUST;
	flame->s.renderfx = RF_TRANSLUCENT;
	flame->velocity[0] = target->velocity[0];
	flame->velocity[1] = target->velocity[1];
	flame->velocity[2] = target->velocity[2];

	VectorClear (flame->mins);
	VectorClear (flame->maxs);
	flame->s.modelindex = gi.modelindex ("models/objects/fireball/tris.md2");
	flame->owner = target;
	flame->orb = owner;
	flame->delay = level.time + 5;//8;//Jr Shorten it so it goes away faster
	flame->nextthink = level.time + .8;
	flame->FlameDelay = level.time + 0.8;
	flame->think = Fire_Think;
	flame->FlameDamage = damage+2;
	flame->classname = "fire";
	flame->s.sound = gi.soundindex ("weapons/grenlf1a.wav");
	gi.linkentity (flame);

	VectorCopy(target->s.origin,flame->s.origin);
}
void flame_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{

	if (other == self->owner)
		return;

	// clean up laser entities

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (self);
		return;
	}

	if (self->owner->client)
		PlayerNoise(self->owner, self->s.origin, PNOISE_IMPACT);
	T_Damage (other, self, self->owner, self->velocity, self->s.origin, plane->normal, 6, 0, 0,MOD_FLAME);

	// core explosion - prevents firing it into the wall/floor
	if (other->health)
	{
		burn_person(other, self->owner, self->FlameDamage);
		self->owner->client->resp.weapon_hits[4]++;
	}
	G_FreeEdict (self);


}

void fire_flamethrower(edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, float damage_radius)
{
	edict_t	*flame;

	self->client->resp.weapon_shots[4]++;

	flame = G_Spawn();
	VectorCopy (start, flame->s.origin);
	VectorCopy (dir, flame->movedir);
	vectoangles (dir, flame->s.angles);
	VectorScale (dir, speed, flame->velocity);
	flame->movetype = MOVETYPE_FLYMISSILE;
	flame->clipmask = MASK_SHOT;
	flame->solid = SOLID_BBOX;
	flame->s.effects |= EF_SHIPEXHAUST;
	flame->s.renderfx = RF_TRANSLUCENT;
	VectorClear (flame->mins);
	VectorClear (flame->maxs);
	flame->s.modelindex = gi.modelindex ("models/objects/fireball/tris.md2");
	flame->owner = self;
	flame->touch = flame_touch;
	flame->nextthink = level.time + 500/speed;
	flame->think = G_FreeEdict;
	flame->radius_dmg = damage;
	flame->FlameDamage = damage;
	flame->dmg_radius = damage_radius;
	flame->classname = "flame";
	flame->s.sound = gi.soundindex ("weapons/grenlf1a.wav");

	gi.linkentity (flame);
}
void fireball_touch (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{

	vec3_t		origin;

	if (other == ent->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (ent);
		return;
	}

	if (ent->owner->client)
		PlayerNoise(ent->owner, ent->s.origin, PNOISE_IMPACT);

	// calculate position for the explosion entity
	VectorMA (ent->s.origin, -0.02, ent->velocity, origin);

	if (other->takedamage)
	{
		T_Damage (other, ent, ent->owner, ent->velocity, ent->s.origin, plane->normal, ent->dmg, 0, 0, MOD_ROCKET);
		ent->owner->client->resp.weapon_hits[4]++;
		gi.sound (ent->owner, CHAN_VOICE, gi.soundindex("misc/hit.wav"), 1, ATTN_STATIC, 0);
	}

	T_RadiusDamage(ent, ent->owner, ent->radius_dmg, other, ent->dmg_radius, MOD_R_SPLASH, 4);

	gi.WriteByte (svc_temp_entity);
	if (ent->waterlevel)
		gi.WriteByte (TE_ROCKET_EXPLOSION_WATER);
	else
		gi.WriteByte (TE_ROCKET_EXPLOSION);
	gi.WritePosition (origin);
	gi.multicast (ent->s.origin, MULTICAST_PHS);

	if (other->health)
	{
		burn_person(other, ent->owner, ent->FlameDamage);
	}
	G_FreeEdict (ent);

}
void fire_fireball (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int speed, float damage_radius, int radius_damage)
{
	edict_t	*fireball;
    vec3_t	dir, forward, right, up;

	self->client->resp.weapon_shots[4]++;

	vectoangles(aimdir, dir);
	AngleVectors(dir, forward, right, up);

	fireball = G_Spawn();
	VectorCopy (start, fireball->s.origin);
	VectorScale (aimdir, speed, fireball->velocity);
	VectorMA (fireball->velocity, 200 + crandom() * 10.0, up, fireball->velocity);
	VectorMA (fireball->velocity, crandom() * 10.0, right, fireball->velocity);
	VectorSet (fireball->avelocity, 300, 300, 300);
	fireball->movetype = MOVETYPE_BOUNCE;
	fireball->clipmask = MASK_SHOT;
	fireball->solid = SOLID_BBOX;
	fireball->s.effects |= EF_SHIPEXHAUST;
	fireball->s.renderfx = RF_TRANSLUCENT;
	VectorClear (fireball->mins);
	VectorClear (fireball->maxs);
	fireball->s.modelindex = gi.modelindex ("models/objects/fireball/tris.md2");
	fireball->owner = self;
	fireball->touch = fireball_touch;
	fireball->nextthink = level.time + 1500/speed;
	fireball->think = G_FreeEdict;
    fireball->dmg = damage;
	fireball->radius_dmg = radius_damage;
	fireball->dmg_radius = damage_radius;
	fireball->FlameDamage = damage;
	fireball->s.sound = gi.soundindex ("weapons/grenlf1a.wav");
	fireball->classname = "flame";

	gi.linkentity (fireball);
}
//deathball
static void DeathballTouch(edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	//owner (who dropped us) can't touch for two secs
//	if (other == ent->owner &&
//		ent->nextthink - level.time > 2)
//		return;

	Touch_Item (ent, other, plane, surf);
}
static void DeathballThink(edict_t *ent)
{
	// auto return the ball

	ResetDeathball();
	safe_bprintf(PRINT_HIGH, "The ball has returned!\n");

}
void fire_deathball (edict_t *self, vec3_t start, vec3_t aimdir, int speed)
{
	edict_t	*ball;
    vec3_t	dir, forward, right, up;
	gitem_t *deathball_item;
	float		*v;

	deathball_item = FindItemByClassname("item_deathball");

	vectoangles(aimdir, dir);
	AngleVectors(dir, forward, right, up);

	ball = Drop_Item(self, deathball_item);
	VectorCopy (start, ball->s.origin);
	VectorScale (aimdir, speed, ball->velocity);
	VectorMA (ball->velocity, 200 + crandom() * 10.0, up, ball->velocity);
	VectorMA (ball->velocity, crandom() * 10.0, right, ball->velocity);
	VectorSet (ball->avelocity, 100, 100, 100);
	ball->movetype = MOVETYPE_BOUNCE;
	ball->clipmask = MASK_SHOT;
	ball->solid = SOLID_BBOX;
	v = tv(-24,-24,-24);
	VectorCopy (v, ball->mins);
	v = tv(24,24,24);
	VectorCopy (v, ball->maxs);
	ball->s.modelindex = gi.modelindex ("vehicles/deathball/deathball.md2");

	ball->owner = self; //important for scoring

	ball->classname = "item_deathball";
	ball->think = DeathballThink;
	ball->nextthink = level.time + 30;
	ball->touch = DeathballTouch;
	ball->s.frame = 229;

	gi.linkentity (ball);

	//remove deathball from players inventory
	//set everything back
	self->in_deathball = false;
	if ( instagib->integer || insta_rockets->integer )
	{
		self->client->newweapon = FindItem( "Alien Disruptor" );
	}
	else if ( rocket_arena->integer )
	{
		self->client->newweapon = FindItem( "Rocket Launcher" );
	}
	else
	{
		self->client->newweapon = FindItem( "blaster" );
	}
	assert( self->client->newweapon != NULL );

	self->client->pers.inventory[ITEM_INDEX(deathball_item)] = 0;
	self->s.modelindex4 = 0;
}

void fire_violator(edict_t *self, vec3_t start, vec3_t aimdir, int damage, int kick, int alt)
{

	vec3_t		from;
	vec3_t		end;
	trace_t		tr;
	edict_t		*ignore;

	if ( g_antilag->integer)
		G_DoTimeShiftFor( self );

	if(alt)
		VectorMA (start, 6.4, aimdir, end);
	else
		VectorMA (start, 6.4, aimdir, end);
	VectorCopy (start, from);
	ignore = self;

	tr = gi.trace (from, NULL, NULL, end, ignore, MASK_PLAYERSOLID);


	if ((tr.ent != self) && (tr.ent->takedamage)) {
		T_Damage (tr.ent, self, self, aimdir, tr.endpos, tr.plane.normal, damage, kick, 0, MOD_VIOLATOR);
		self->client->resp.weapon_hits[8]++;
		gi.sound (self, CHAN_VOICE, gi.soundindex("misc/hit.wav"), 1, ATTN_STATIC, 0);
		gi.WriteByte (svc_temp_entity);
		gi.WriteByte (TE_LASER_SPARKS);
		gi.WriteByte (4);
		gi.WritePosition (tr.endpos);
		gi.WriteDir (tr.plane.normal);
		// skinnum is sometimes larger, why?, but does it matter?
		gi.WriteByte( ((self->s.skinnum > 255) ? 0 : self->s.skinnum) );
		gi.multicast (tr.endpos, MULTICAST_PVS);

	}
	VectorCopy (tr.endpos, from);

	if ( g_antilag->integer)
		G_UndoTimeShiftFor( self );
}