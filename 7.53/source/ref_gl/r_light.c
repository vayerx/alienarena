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
// r_light.c

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "r_local.h"

int	r_dlightframecount;

/*
=============================================================================

DYNAMIC LIGHTS

=============================================================================
*/

/*
=============
R_MarkLights
=============
*/
void R_MarkLights (dlight_t *light, int bit, mnode_t *node)
{
          cplane_t  *splitplane;
          float              dist;
          msurface_t         *surf;
          int                i, sidebit;

          if (node->contents != -1)
                   return;

          splitplane = node->plane;
          dist = DotProduct (light->origin, splitplane->normal) - splitplane->dist;

          if (dist > light->intensity-DLIGHT_CUTOFF) {
                   R_MarkLights (light, bit, node->children[0]);
                   return;
          }
          if (dist < -light->intensity+DLIGHT_CUTOFF) {
                   R_MarkLights (light, bit, node->children[1]);
                   return;
          }

// mark the polygons
          surf = r_worldmodel->surfaces + node->firstsurface;
          for (i=0 ; i<node->numsurfaces ; i++, surf++)
          {
                   dist = DotProduct (light->origin, surf->plane->normal) - surf->plane->dist;         //Discoloda
                   if (dist >= 0)                                                                              //Discoloda
                             sidebit = 0;                                                                      //Discoloda
                   else                                                                                        //Discoloda
                             sidebit = SURF_PLANEBACK;                                                //Discoloda

                   if ( (surf->flags & SURF_PLANEBACK) != sidebit )                                   //Discoloda
                             continue;                                                                //Discoloda

                   if (surf->dlightframe != r_dlightframecount)
                   {
                             surf->dlightbits = bit;
                             surf->dlightframe = r_dlightframecount;
                   } else
                             surf->dlightbits |= bit;
          }

          R_MarkLights (light, bit, node->children[0]);
          R_MarkLights (light, bit, node->children[1]);
}


/*
=============
R_PushDlights
=============
*/
void R_PushDlights (void)
{
	int		i;
	dlight_t	*l;

	r_dlightframecount = r_framecount + 1;	// because the count hasn't
											//  advanced yet for this frame
	l = r_newrefdef.dlights;
	for (i=0 ; i<r_newrefdef.num_dlights ; i++, l++)
		R_MarkLights ( l, 1<<i, r_worldmodel->nodes );
}

/*
=============
R_PushDlightsForBModel
=============
*/
void R_PushDlightsForBModel (entity_t *e)
{
	int			k;
	dlight_t	*lt;

	lt = r_newrefdef.dlights;

	if (e->angles[0] || e->angles[1] || e->angles[2])
	{
		vec3_t	temp;
		vec3_t	forward, right, up;

		AngleVectors (e->angles, forward, right, up);

		for (k=0 ; k<r_newrefdef.num_dlights ; k++, lt++)
		{
			VectorSubtract (lt->origin, e->origin, temp);
			lt->origin[0] = DotProduct (temp, forward);
			lt->origin[1] = -DotProduct (temp, right);
			lt->origin[2] = DotProduct (temp, up);
			R_MarkLights (lt, 1<<k, e->model->nodes + e->model->firstnode);
			VectorAdd (temp, e->origin, lt->origin);
		}
	}
	else
	{
		for (k=0 ; k<r_newrefdef.num_dlights ; k++, lt++)
		{
			VectorSubtract (lt->origin, e->origin, lt->origin);
			R_MarkLights (lt, 1<<k, e->model->nodes + e->model->firstnode);
			VectorAdd (lt->origin, e->origin, lt->origin);
		}
	}
}

/*
=============================================================================

LIGHT SAMPLING

=============================================================================
*/

vec3_t			pointcolor;
cplane_t		*lightplane;		// used as shadow plane
vec3_t			lightspot;

int RecursiveLightPoint (mnode_t *node, vec3_t start, vec3_t end)
{
	float		front, back, frac;
	int			side;
	cplane_t	*plane;
	vec3_t		mid;
	msurface_t	*surf;
	int			s, t, ds, dt;
	int			i;
	mtexinfo_t	*tex;
	byte		*lightmap;
	int			maps;
	int			r;
	vec3_t		scale;

	if (node->contents != -1)
		return -1;		// didn't hit anything

// calculate mid point

// FIXME: optimize for axial
	plane = node->plane;
	front = DotProduct (start, plane->normal) - plane->dist;
	back = DotProduct (end, plane->normal) - plane->dist;
	side = front < 0;

	if ( (back < 0) == side)
		return RecursiveLightPoint (node->children[side], start, end);

	frac = front / (front-back);
	mid[0] = start[0] + (end[0] - start[0])*frac;
	mid[1] = start[1] + (end[1] - start[1])*frac;
	mid[2] = start[2] + (end[2] - start[2])*frac;

// go down front side
	r = RecursiveLightPoint (node->children[side], start, mid);
	if (r >= 0)
		return r;		// hit something

	if ( (back < 0) == side )
		return -1;		// didn't hit anuthing

// check for impact on this node
	VectorCopy (mid, lightspot);
	lightplane = plane;

	surf = r_worldmodel->surfaces + node->firstsurface;
	for (i=0 ; i<node->numsurfaces ; i++, surf++)
	{
		tex = surf->texinfo;

		if (tex->flags & (SURF_WARP|SURF_SKY))
			continue;	// no lightmaps

		s = DotProduct (mid, tex->vecs[0]) + tex->vecs[0][3];
		if (s < surf->texturemins[0])
			continue;

		ds = s - surf->texturemins[0];
		if (ds > surf->extents[0])
			continue;

		t = DotProduct (mid, tex->vecs[1]) + tex->vecs[1][3];
		if (t < surf->texturemins[1])
			continue;

		dt = t - surf->texturemins[1];
		if (dt > surf->extents[1])
			continue;

		lightmap = surf->samples;
		if (!surf->samples)
			return 0;

		ds >>= 4;
		dt >>= 4;

		VectorClear (pointcolor);

		lightmap += 3*(dt * ((surf->extents[0]>>4)+1) + ds);

		for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ;	maps++)
		{
			for (i=0 ; i<3 ; i++)
				scale[i] = gl_modulate->value*r_newrefdef.lightstyles[surf->styles[maps]].rgb[i];

			pointcolor[0] += lightmap[0] * scale[0] * (1.0/255);
			pointcolor[1] += lightmap[1] * scale[1] * (1.0/255);
			pointcolor[2] += lightmap[2] * scale[2] * (1.0/255);
			lightmap += 3*((surf->extents[0]>>4)+1) * ((surf->extents[1]>>4)+1);
		}

		return 1;
	}

// go down back side
	return RecursiveLightPoint (node->children[!side], mid, end);
}

/*
===============
R_LightPoint
===============
*/
void R_LightPoint (vec3_t p, vec3_t color, qboolean addDynamic)
{
	vec3_t		end;
	float		r;
	int			lnum;
	dlight_t	*dl;
	float		light;
	vec3_t		dist, dlightcolor;
	float		add;

	if (!r_worldmodel->lightdata)
	{
		color[0] = color[1] = color[2] = 1.0;
		return;
	}

	end[0] = p[0];
	end[1] = p[1];
	end[2] = p[2] - 2048;

	r = RecursiveLightPoint (r_worldmodel->nodes, p, end);

	if (r == -1)
	{
		VectorCopy (vec3_origin, color);
	}
	else
	{
		VectorCopy (pointcolor, color);
	}

	if (!addDynamic)
		return;

	//
	// add dynamic lights
	//
	light = 0;
	dl = r_newrefdef.dlights;
	VectorClear ( dlightcolor );
	for (lnum=0 ; lnum<r_newrefdef.num_dlights ; lnum++, dl++)
	{
		VectorSubtract (p,
						dl->origin,
						dist);
		add = dl->intensity - VectorLength(dist);
		if (add > 0)
		{
			add *= (1.0/256);
			VectorMA (dlightcolor, add, dl->color, dlightcolor);
		}
	}

	VectorMA (color, gl_modulate->value, dlightcolor, color);
}

//===================================================================

static float s_blocklights[34*34*3];

/*
** R_SetCacheState
*/
void R_SetCacheState( msurface_t *surf )
{
	int maps;

	for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ;
		 maps++)
	{
		surf->cached_light[maps] = r_newrefdef.lightstyles[surf->styles[maps]].white;
	}
}

/*
===============
R_BuildLightMap

Combine and scale multiple lightmaps into the floating format in blocklights
===============
*/
void R_BuildLightMap (msurface_t *surf, byte *dest, int stride)
{
	int			smax, tmax;
	int			r, g, b, a, max;
	int			i, j, size;
	byte		*lightmap;
	float		scale[4];
	int			nummaps;
	float		*bl;
	lightstyle_t	*style;

	if ( SurfaceHasNoLightmap( surf ) )
		Com_Error (ERR_DROP, "R_BuildLightMap called for non-lit surface");

	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;
	size = smax*tmax;
	if (size > (sizeof(s_blocklights)>>4) )
		Com_Error (ERR_DROP, "Bad s_blocklights size");

// set to full bright if no light data
	if (!surf->samples)
	{
		int maps;

		for (i=0 ; i<size*3 ; i++)
			s_blocklights[i] = 255;
		for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ;
			 maps++)
		{
			style = &r_newrefdef.lightstyles[surf->styles[maps]];
		}
		goto store;
	}

	// count the # of maps
	for ( nummaps = 0 ; nummaps < MAXLIGHTMAPS && surf->styles[nummaps] != 255 ;
		 nummaps++)
		;

	lightmap = surf->samples;

	// add all the lightmaps
	if ( nummaps == 1 )
	{
		int maps;

		for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ;
			 maps++)
		{
			bl = s_blocklights;

			for (i=0 ; i<3 ; i++)
				scale[i] = gl_modulate->value*r_newrefdef.lightstyles[surf->styles[maps]].rgb[i];

			if ( scale[0] == 1.0F &&
				 scale[1] == 1.0F &&
				 scale[2] == 1.0F )
			{
				for (i=0 ; i<size ; i++, bl+=3)
				{
					bl[0] = lightmap[i*3+0];
					bl[1] = lightmap[i*3+1];
					bl[2] = lightmap[i*3+2];
				}
			}
			else
			{
				for (i=0 ; i<size ; i++, bl+=3)
				{
					bl[0] = lightmap[i*3+0] * scale[0];
					bl[1] = lightmap[i*3+1] * scale[1];
					bl[2] = lightmap[i*3+2] * scale[2];
				}
			}
			lightmap += size*3;		// skip to next lightmap
		}
	}
	else
	{
		int maps;

		memset( s_blocklights, 0, sizeof( s_blocklights[0] ) * size * 3 );

		for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ;
			 maps++)
		{
			bl = s_blocklights;

			for (i=0 ; i<3 ; i++)
				scale[i] = gl_modulate->value*r_newrefdef.lightstyles[surf->styles[maps]].rgb[i];

			if ( scale[0] == 1.0F &&
				 scale[1] == 1.0F &&
				 scale[2] == 1.0F )
			{
				for (i=0 ; i<size ; i++, bl+=3 )
				{
					bl[0] += lightmap[i*3+0];
					bl[1] += lightmap[i*3+1];
					bl[2] += lightmap[i*3+2];
				}
			}
			else
			{
				for (i=0 ; i<size ; i++, bl+=3)
				{
					bl[0] += lightmap[i*3+0] * scale[0];
					bl[1] += lightmap[i*3+1] * scale[1];
					bl[2] += lightmap[i*3+2] * scale[2];
				}
			}
			lightmap += size*3;		// skip to next lightmap
		}
	}

// put into texture format
store:
	stride -= (smax<<2);
	bl = s_blocklights;

	for (i=0 ; i<tmax ; i++, dest += stride)
	{
		for (j=0 ; j<smax ; j++)
		{
			r = Q_ftol( bl[0] );
			g = Q_ftol( bl[1] );
			b = Q_ftol( bl[2] );

			// catch negative lights
			if (r < 0)
				r = 0;
			if (g < 0)
				g = 0;
			if (b < 0)
				b = 0;
			/*
			** determine the brightest of the three color components
			*/
			if (r > g)
				max = r;
			else
				max = g;
			if (b > max)
				max = b;

			//255 is best for alpha testing, so textures don't "disapear" in the dark
			a = 255;

			/*
			** rescale all the color components if the intensity of the greatest
			** channel exceeds 1.0
			*/
			if (max > 255)
			{
				float t = 255.0F / max;
				r = r*t;
				g = g*t;
				b = b*t;
				a = a*t;
			}

			dest[0] = r;
			dest[1] = g;
			dest[2] = b;
			dest[3] = a;

			bl += 3;
			dest += 4;
		}
	}
}