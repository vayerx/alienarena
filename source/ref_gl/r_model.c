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
// r_model.c -- model loading and caching

#include "r_local.h"
 
model_t	*loadmodel;
int		modfilelen;

void Mod_LoadBrushModel (model_t *mod, void *buffer);
void Mod_LoadAliasModel (model_t *mod, void *buffer);
model_t *Mod_LoadModel (model_t *mod, qboolean crash);

byte	mod_novis[MAX_MAP_LEAFS/8];

#define	MAX_MOD_KNOWN	512
model_t	mod_known[MAX_MOD_KNOWN];
int		mod_numknown;

// the inline * models from the current map are kept seperate
model_t	mod_inline[MAX_MOD_KNOWN];

int		registration_sequence;

#ifdef _WINDOWS
extern void R_ReadFogScript(char config_file[128]);
extern void R_ReadMusicScript(char config_file[128]);
#endif

char map_music[128];
extern unsigned r_weather;

char		map_entitystring[MAX_MAP_ENTSTRING];
int			numentitychars;
byte	*mod_base;

/*
=================
Mod_LoadEntityString
=================
*/
void Mod_LoadEntityStrn (lump_t *l)
{
	numentitychars = l->filelen;
	if (l->filelen > MAX_MAP_ENTSTRING)
		Sys_Error ("Map has too large entity lump");

	memcpy (map_entitystring, mod_base + l->fileofs, l->filelen);
}

extern char	*CM_EntityString (void);

/*
================
R_ParseLightEntities - BERSERK

parses light entity string
================
*/

static void R_ParseLightEntities (void)
{

	int			i;
	char		*entString;
	char		*buf, *tok;
	char		block[2048], *bl;
	vec3_t		origin;
	float		intensity;

	//DONT LOAD MAP ETS
//	return;

	entString = map_entitystring;

	buf = CM_EntityString();
	while (1){
		tok = Com_ParseExt(&buf, true);
		if (!tok[0])
			break;			// End of data

		if (Q_stricmp(tok, "{"))
			continue;		// Should never happen!

		// Parse the text inside brackets
		block[0] = 0;
		do {
			tok = Com_ParseExt(&buf, false);
			if (!Q_stricmp(tok, "}"))
				break;		// Done

			if (!tok[0])	// Newline
				Q_strcat(block, "\n", sizeof(block));
			else {			// Token
				Q_strcat(block, " ", sizeof(block));
				Q_strcat(block, tok, sizeof(block));
			}
		} while (buf);

		// Now look for "classname"
		tok = strstr(block, "classname");
		if (!tok)
			continue;		// Not found

		// Skip over "classname" and whitespace
		tok += strlen("classname");
		while (*tok && *tok == ' ')
			tok++;

		// Next token must be "light"
		if (Q_strnicmp(tok, "light", 5))
			continue;		// Not "light"

		// Finally parse the light entity
		VectorClear(origin);
		intensity = 0;

		bl = block;
		while (1){
			tok = Com_ParseExt(&bl, true);
			if (!tok[0])
				break;		// End of data

			if (!Q_stricmp("origin", tok)){
				for (i = 0; i < 3; i++){
					tok = Com_ParseExt(&bl, false);
					origin[i] = atof(tok);
				}
			}
			else if (!Q_stricmp("light", tok) || !Q_stricmp("_light", tok)){
				tok = Com_ParseExt(&bl, false);
				intensity = atof(tok);
			}
			else
				Com_SkipRestOfLine(&bl);
		}

		if (!intensity)
			intensity = 150;

		// Add it to the list
		if (r_numWorldLights == MAX_LIGHTS)
			break;

		VectorCopy(origin, r_worldLights[r_numWorldLights].origin);
		r_worldLights[r_numWorldLights].intensity = intensity/2;
		r_worldLights[r_numWorldLights].surf = NULL;
		r_numWorldLights++;
	}
}

/*
===============
Mod_PointInLeaf
===============
*/
mleaf_t *Mod_PointInLeaf (vec3_t p, model_t *model)
{
	mnode_t		*node;
	float		d;
	cplane_t	*plane;
	
	if (!model || !model->nodes)
		Com_Printf ("Mod_PointInLeaf: bad model");

	node = model->nodes;
	while (1)
	{
		if (node->contents != -1)
			return (mleaf_t *)node;
		plane = node->plane;
		d = DotProduct (p,plane->normal) - plane->dist;
		if (d > 0)
			node = node->children[0];
		else
			node = node->children[1];
	}
	
	return NULL;	// never reached
}


/*
===================
Mod_DecompressVis
===================
*/
byte *Mod_DecompressVis (byte *in, model_t *model)
{
	static byte	decompressed[MAX_MAP_LEAFS/8];
	int		c;
	byte	*out;
	int		row;

	row = (model->vis->numclusters+7)>>3;	
	out = decompressed;

	if (!in)
	{	// no vis info, so make all visible
		while (row)
		{
			*out++ = 0xff;
			row--;
		}
		return decompressed;		
	}

	do
	{
		if (*in)
		{
			*out++ = *in++;
			continue;
		}
	
		c = in[1];
		in += 2;
		while (c)
		{
			*out++ = 0;
			c--;
		}
	} while (out - decompressed < row);
	
	return decompressed;
}

/*
==============
Mod_ClusterPVS
==============
*/
byte *Mod_ClusterPVS (int cluster, model_t *model)
{
	if (cluster == -1 || !model->vis)
		return mod_novis;
	return Mod_DecompressVis ( (byte *)model->vis + model->vis->bitofs[cluster][DVIS_PVS],
		model);
}


//===============================================================================

/*
================
Mod_Modellist_f
================
*/
void Mod_Modellist_f (void)
{
	int		i;
	model_t	*mod;
	int		total;

	total = 0;
	Com_Printf ("Loaded models:\n");
	for (i=0, mod=mod_known ; i < mod_numknown ; i++, mod++)
	{
		if (!mod->name[0])
			continue;
		Com_Printf ("%8i : %s\n",mod->extradatasize, mod->name);
		total += mod->extradatasize;
	}
	Com_Printf ("Total resident: %i\n", total);
}

/*
===============
Mod_Init
===============
*/
void Mod_Init (void)
{
	memset (mod_novis, 0xff, sizeof(mod_novis));
}



/*
==================
Mod_ForName

Loads in a model for the given name
==================
*/
model_t *Mod_ForName (char *name, qboolean crash)
{
	model_t	*mod;
	unsigned *buf;
	int		i;
	
	if (!name[0])
		Com_Error (ERR_DROP, "Mod_ForName: NULL name");
		
	//
	// inline models are grabbed only from worldmodel
	//
	if (name[0] == '*')
	{
		i = atoi(name+1);
		if (i < 1 || !r_worldmodel || i >= r_worldmodel->numsubmodels)
			Com_Error (ERR_DROP, "bad inline model number");
		return &mod_inline[i];
	}

	//
	// search the currently loaded models
	//
	for (i=0 , mod=mod_known ; i<mod_numknown ; i++, mod++)
	{
		if (!mod->name[0])
			continue;
		if (!strcmp (mod->name, name) ) {

			if (mod->type == mod_alias) {
				// Make sure models scripts are definately reloaded between maps - MrG
				char rs[MAX_OSPATH];
				int i = 0;
				image_t *img;
				img=mod->skins[i];
			
				while (img != NULL) {
					strcpy(rs,mod->skins[i]->name);
					rs[strlen(rs)-4]=0;

#ifdef _WINDOWS
					(struct rscript_s *)mod->script[i] = RS_FindScript(rs);
#else
					mod->script[i] = RS_FindScript(rs); //make it gcc 4.1.1 compatible
#endif
					if (mod->script[i])
						RS_ReadyScript((rscript_t *)mod->script[i]);
					i++;
					img=mod->skins[i];
				}
			}

			return mod;
		}
	}
	
	//
	// find a free model slot spot
	//
	for (i=0 , mod=mod_known ; i<mod_numknown ; i++, mod++)
	{
		if (!mod->name[0])
			break;	// free spot
	}
	if (i == mod_numknown)
	{
		if (mod_numknown == MAX_MOD_KNOWN)
			Com_Error (ERR_DROP, "mod_numknown == MAX_MOD_KNOWN");
		mod_numknown++;
	}
	strcpy (mod->name, name);
	
	//
	// load the file
	//
	modfilelen = FS_LoadFile (mod->name, &buf);
	if (!buf)
	{
		if (crash)
			Com_Error (ERR_DROP, "Mod_NumForName: %s not found", mod->name);
		memset (mod->name, 0, sizeof(mod->name));
		return NULL;
	}
	
	loadmodel = mod;

	//
	// fill it in
	//


	// call the apropriate loader
	
	switch (LittleLong(*(unsigned *)buf))
	{
	case IDALIASHEADER:
		loadmodel->extradata = Hunk_Begin (0x300000);
		Mod_LoadAliasModel (mod, buf);
		break;
		
	case IDBSPHEADER:
		loadmodel->extradata = Hunk_Begin (0x1500000);
		Mod_LoadBrushModel (mod, buf);
		break;

	default:
		Com_Error (ERR_DROP,"Mod_NumForName: unknown fileid for %s", mod->name);
		break;
	}

	loadmodel->extradatasize = Hunk_End ();

	FS_FreeFile (buf);

	return mod;
}

/*
===============================================================================

					BRUSHMODEL LOADING

===============================================================================
*/

byte	*mod_base;


/*
=================
Mod_LoadLighting
=================
*/
void Mod_LoadLighting (lump_t *l)
{
	if (!l->filelen)
	{
		loadmodel->lightdata = NULL;
		return;
	}
	loadmodel->lightdata = Hunk_Alloc ( l->filelen);	
	memcpy (loadmodel->lightdata, mod_base + l->fileofs, l->filelen);
}


/*
=================
Mod_LoadVisibility
=================
*/
void Mod_LoadVisibility (lump_t *l)
{
	int		i;

	if (!l->filelen)
	{
		loadmodel->vis = NULL;
		return;
	}
	loadmodel->vis = Hunk_Alloc ( l->filelen);	
	memcpy (loadmodel->vis, mod_base + l->fileofs, l->filelen);

	loadmodel->vis->numclusters = LittleLong (loadmodel->vis->numclusters);
	for (i=0 ; i<loadmodel->vis->numclusters ; i++)
	{
		loadmodel->vis->bitofs[i][0] = LittleLong (loadmodel->vis->bitofs[i][0]);
		loadmodel->vis->bitofs[i][1] = LittleLong (loadmodel->vis->bitofs[i][1]);
	}
}


/*
=================
Mod_LoadVertexes
=================
*/
void Mod_LoadVertexes (lump_t *l)
{
	dvertex_t	*in;
	mvertex_t	*out;
	int			i, count;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Com_Error (ERR_DROP, "MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->vertexes = out;
	loadmodel->numvertexes = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		out->position[0] = LittleFloat (in->point[0]);
		out->position[1] = LittleFloat (in->point[1]);
		out->position[2] = LittleFloat (in->point[2]);
	}
}

/*
=================
RadiusFromBounds
=================
*/
float RadiusFromBounds (vec3_t mins, vec3_t maxs)
{
	int		i;
	vec3_t	corner;

	for (i=0 ; i<3 ; i++)
	{
		corner[i] = fabs(mins[i]) > fabs(maxs[i]) ? fabs(mins[i]) : fabs(maxs[i]);
	}

	return VectorLength (corner);
}


/*
=================
Mod_LoadSubmodels
=================
*/
void Mod_LoadSubmodels (lump_t *l)
{
	dmodel_t	*in;
	mmodel_t	*out;
	int			i, j, count;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Com_Error (ERR_DROP, "MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->submodels = out;
	loadmodel->numsubmodels = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{	// spread the mins / maxs by a pixel
			out->mins[j] = LittleFloat (in->mins[j]) - 1;
			out->maxs[j] = LittleFloat (in->maxs[j]) + 1;
		}
		out->radius = RadiusFromBounds (out->mins, out->maxs);
		out->headnode = LittleLong (in->headnode);
		out->firstface = LittleLong (in->firstface);
		out->numfaces = LittleLong (in->numfaces);
	}
}

/*
=================
Mod_LoadEdges
=================
*/
void Mod_LoadEdges (lump_t *l)
{
	dedge_t *in;
	medge_t *out;
	int 	i, count;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Com_Error (ERR_DROP, "MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc ( (count + 1) * sizeof(*out));	

	loadmodel->edges = out;
	loadmodel->numedges = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		out->v[0] = (unsigned short)LittleShort(in->v[0]);
		out->v[1] = (unsigned short)LittleShort(in->v[1]);
	}
}

/*
=================
Mod_LoadTexinfo
=================
*/
void Mod_LoadTexinfo (lump_t *l)
{
	texinfo_t *in;
	mtexinfo_t *out, *step;
	int 	i, j, count;
	char	name[MAX_QPATH];
	char	sv_name[MAX_QPATH];
	int		next;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Com_Error (ERR_DROP, "MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->texinfo = out;
	loadmodel->numtexinfo = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<8 ; j++)
			out->vecs[0][j] = LittleFloat (in->vecs[0][j]);
		out->value = LittleLong (in->value);
		out->flags = LittleLong (in->flags);
		next = LittleLong (in->nexttexinfo);
		if (next > 0)
			out->next = loadmodel->texinfo + next;
		else
		    out->next = NULL;

		Com_sprintf (name, sizeof(name), "textures/%s.wal", in->texture);
		out->image = GL_FindImage (name, it_wall);

		Com_sprintf (name, sizeof(name), "textures/%s", in->texture);
		out->image->script = RS_FindScript(name);
		if (out->image->script)
			RS_ReadyScript(out->image->script);

		//check for normal, height and specular maps
		strcpy(sv_name, name);
		if( ( strlen( name ) + 8 ) <= MAX_QPATH )
		{
			strcat( name, "_nm.tga" );
			out->normalMap = GL_FindImage( name, it_bump );
			if( out->normalMap == NULL ) 
				out->normalMap = out->image; 
		
		}
		else
			out->normalMap = out->image; 

		strcpy(name, sv_name);
		if( ( strlen( name ) + 8 ) <= MAX_QPATH )
		{
			strcat( name, "_hm.tga" );
			out->heightMap = GL_FindImage( name, it_bump );
			if( out->heightMap == NULL ) 
				out->heightMap = out->image; 
		
		}
		else
			out->heightMap = out->image; 
	}

	// count animation frames
	for (i=0 ; i<count ; i++)
	{
		out = &loadmodel->texinfo[i];
		out->numframes = 1;
		for (step = out->next ; step && step != out ; step=step->next)
			out->numframes++;
	}
}

/*
================
CalcSurfaceExtents

Fills in s->texturemins[] and s->extents[]
================
*/
void CalcSurfaceExtents (msurface_t *s)
{
	float	mins[2], maxs[2], val;
	int		i,j, e;
	mvertex_t	*v;
	mtexinfo_t	*tex;
	int		bmins[2], bmaxs[2];

	mins[0] = mins[1] = 999999;
	maxs[0] = maxs[1] = -99999;

	tex = s->texinfo;
	
	for (i=0 ; i<s->numedges ; i++)
	{
		e = loadmodel->surfedges[s->firstedge+i];
		if (e >= 0)
			v = &loadmodel->vertexes[loadmodel->edges[e].v[0]];
		else
			v = &loadmodel->vertexes[loadmodel->edges[-e].v[1]];
		
		for (j=0 ; j<2 ; j++)
		{
			val = v->position[0] * tex->vecs[j][0] + 
				v->position[1] * tex->vecs[j][1] +
				v->position[2] * tex->vecs[j][2] +
				tex->vecs[j][3];
			if (val < mins[j])
				mins[j] = val;
			if (val > maxs[j])
				maxs[j] = val;
		}
	}

	for (i=0 ; i<2 ; i++)
	{	
		bmins[i] = floor(mins[i]/16);
		bmaxs[i] = ceil(maxs[i]/16);

		s->texturemins[i] = bmins[i] * 16;
		s->extents[i] = (bmaxs[i] - bmins[i]) * 16;
	}
}

//lens flares
int r_numflares;
flare_t r_flares[MAX_FLARES];

static void R_ClearFlares(void)
{
	memset(r_flares, 0, sizeof(r_flares));
	r_numflares = 0;
}

void GL_AddFlareSurface (msurface_t *surf, int type )
{
     int i, width, height, intens;
     glpoly_t *poly;
     flare_t  *light; 
     byte     *buffer;
	 byte     *p;
     float    *v, surf_bound;
	 vec3_t origin = {0,0,0}, color = {1,1,1}, tmp, rgbSum;
     vec3_t poly_center, mins, maxs, tmp1;
     
     if (surf->texinfo->flags & (SURF_SKY|SURF_TRANS33|SURF_TRANS66|SURF_FLOWING|SURF_DRAWTURB|SURF_WARP))
          return;

     if (!(surf->texinfo->flags & (SURF_LIGHT)))
          return;
  
	 if (r_numflares >= MAX_FLARES)
			return;

    light = &r_flares[r_numflares++];
    intens = surf->texinfo->value;

     
     //if (intens < 100) //if not a lighted surf, don't do a flare, right?
	//	 return;
    /*
	===================
	find poligon centre
	===================
	*/
	VectorSet(mins, 999999, 999999, 999999);
	VectorSet(maxs, -999999, -999999, -999999);

    for ( poly = surf->polys; poly; poly = poly->chain ) { 
         for (i=0, v=poly->verts[0] ; i< poly->numverts; i++, v+= VERTEXSIZE) { 
                
               if(v[0] > maxs[0])   maxs[0] = v[0]; 
               if(v[1] > maxs[1])   maxs[1] = v[1]; 
               if(v[2] > maxs[2])   maxs[2] = v[2]; 

               if(v[0] < mins[0])   mins[0] = v[0]; 
               if(v[1] < mins[1])   mins[1] = v[1]; 
               if(v[2] < mins[2])   mins[2] = v[2]; 
            } 
      } 

     poly_center[0] = (mins[0] + maxs[0]) /2; 
     poly_center[1] = (mins[1] + maxs[1]) /2; 
     poly_center[2] = (mins[2] + maxs[2]) /2;
	 VectorCopy(poly_center, origin);
     
     /*
	=====================================
	calc light surf bounds and flare size
	=====================================
	*/


	 VectorSubtract(maxs, mins, tmp1);
     surf_bound = VectorLength(tmp1);
	 if (surf_bound <=25) light->size = 10;
	 else if (surf_bound <=50)  light->size = 15;
     else if (surf_bound <=100) light->size = 20;
	 else if (surf_bound <=150) light->size = 25;
	 else if (surf_bound <=200) light->size = 30;
	 else if (surf_bound <=250) light->size = 35;


	/*
	===================
	calc texture color
	===================
	*/

     GL_Bind( surf->texinfo->image->texnum );
     width = surf->texinfo->image->upload_width;
	 height = surf->texinfo->image->upload_height;
     
     buffer = malloc(width * height * 3);
     qglGetTexImage (GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, buffer);
     VectorClear(rgbSum);

     for (i=0, p=buffer; i<width*height; i++, p += 3) 
      {
        rgbSum[0] += (float)p[0]  * (1.0 / 255);
        rgbSum[1] += (float)p[1]  * (1.0 / 255);
        rgbSum[2] += (float)p[2]  * (1.0 / 255);
      }
     
      VectorScale(rgbSum, r_lensflare_intens->value / (width *height), color);
     
      for (i=0; i<3; i++) {
          if (color[i] < 0.5)
               color[i] = color[i] * 0.5;
          else
               color[i] = color[i] * 0.5 + 0.5;
          }
      VectorCopy(color, light->color);
      
	/*
	==================================
	move flare origin in to map bounds
	==================================
	*/

     if (surf->flags & SURF_PLANEBACK)
		VectorNegate(surf->plane->normal, tmp);
     else
		VectorCopy(surf->plane->normal, tmp);
     
     VectorMA(origin, 2, tmp, origin);
     VectorCopy(origin, light->origin);
    
     light->style = type;

     free (buffer); 
}

//vegetation
int r_numgrasses;
grass_t r_grasses[MAX_GRASSES];

static void R_ClearGrasses(void)
{
	memset(r_grasses, 0, sizeof(r_grasses));
	r_numgrasses = 0;
}

void GL_AddVegetationSurface (msurface_t *surf, int texnum, vec3_t color, float size, char name[MAX_QPATH], int type)
{
    glpoly_t *poly;
    grass_t  *grass; 
	image_t *gl;
	vec3_t origin = {0,0,0}, binormal, tangent, tmp;

	if (r_numgrasses >= MAX_GRASSES)
			return;

	if(size == 0.0)
		size = 1.0f;
 	  
	poly = surf->polys;
         
	VectorCopy(poly->verts[0], origin);
   
	AngleVectors(surf->plane->normal, NULL, tangent, binormal);
	VectorNormalize(tangent);
	VectorNormalize(binormal);
	
	VectorMA(origin, -32*frand(), tangent, origin);

	if (surf->flags & SURF_PLANEBACK)
		VectorNegate(surf->plane->normal, tmp);
	else
		VectorCopy(surf->plane->normal, tmp);
     
	VectorMA(origin, 2, tmp, origin);
		
	grass = &r_grasses[r_numgrasses++];
	VectorCopy(origin, grass->origin);
	
	gl = R_RegisterGfxPic (name);
	if (gl)
		grass->texsize = gl->height;
	else
		grass->texsize = 64; //sane default
	
	grass->texnum = texnum;
	VectorCopy(color, grass->color);
	grass->size = size;
	strcpy(grass->name, name);
	grass->type = type;
}

int r_numbeams;
beam_t r_beams[MAX_BEAMS];

static void R_ClearBeams(void)
{
	memset(r_beams, 0, sizeof(r_beams));
	r_numbeams = 0;
}

void GL_AddBeamSurface (msurface_t *surf, int texnum, vec3_t color, float size, char name[MAX_QPATH], int type)
{
    glpoly_t *poly;
    beam_t  *beam; 
	image_t *gl;
	vec3_t poly_center, mins, maxs;
	float *v;
	int i;
	vec3_t origin = {0,0,0}, binormal, tangent, tmp;

	if (r_numbeams >= MAX_BEAMS)
			return;

	if(size == 0.0)
		size = 1.0f;
 	  
	poly = surf->polys;
         
	 /*
	===================
	find poligon centre
	===================
	*/
	VectorSet(mins, 999999, 999999, 999999);
	VectorSet(maxs, -999999, -999999, -999999);

    for ( poly = surf->polys; poly; poly = poly->chain ) { 
         for (i=0, v=poly->verts[0] ; i< poly->numverts; i++, v+= VERTEXSIZE) { 
                
               if(v[0] > maxs[0])   maxs[0] = v[0]; 
               if(v[1] > maxs[1])   maxs[1] = v[1]; 
               if(v[2] > maxs[2])   maxs[2] = v[2]; 

               if(v[0] < mins[0])   mins[0] = v[0]; 
               if(v[1] < mins[1])   mins[1] = v[1]; 
               if(v[2] < mins[2])   mins[2] = v[2]; 
            } 
      } 

     poly_center[0] = (mins[0] + maxs[0]) /2; 
     poly_center[1] = (mins[1] + maxs[1]) /2; 
     poly_center[2] = (mins[2] + maxs[2]) /2;
	 VectorCopy(poly_center, origin);
   
	AngleVectors(surf->plane->normal, NULL, tangent, binormal);
	VectorNormalize(tangent);
	VectorNormalize(binormal);

	if (surf->flags & SURF_PLANEBACK)
		VectorNegate(surf->plane->normal, tmp);
	else
		VectorCopy(surf->plane->normal, tmp);
     
	VectorMA(origin, 2, tmp, origin);
		
	beam = &r_beams[r_numbeams++];
	VectorCopy(origin, beam->origin);
	
	gl = R_RegisterGfxPic (name);
	if (gl)
		beam->texsize = gl->height;
	else
		beam->texsize = 64; //sane default
	
	beam->texnum = texnum;
	VectorCopy(color, beam->color);
	beam->size = size;
	strcpy(beam->name, name);
	beam->type = type;

}

GL_CalcSurfaceNormals(msurface_t *surf) {

	glpoly_t *p = surf->polys;
	float	*v;	
	int		i;

	for (; p; p = p->chain)
	{
		vec3_t	v01, v02, temp1, temp2, temp3;
		vec3_t	normal, binormal, tangent;
		float	s = 0;
		float	*vec;

		_VectorSubtract( p->verts[ 1 ], p->verts[0], v01 );
		vec = p->verts[0];
		_VectorCopy(surf->plane->normal, normal); 

		for (v = p->verts[0], i = 0 ; i < p->numverts; i++, v += VERTEXSIZE)
		{

			float currentLength;
			vec3_t currentNormal;

			//do calculations for normal, tangent and binormal
			if( i > 1) {
				_VectorSubtract( p->verts[ i ], p->verts[0], temp1 );

				CrossProduct( temp1, v01, currentNormal );
				currentLength = VectorLength( currentNormal );

				if( currentLength > s )
				{
					s = currentLength;
					_VectorCopy( currentNormal, normal );

					vec = p->verts[i];
					_VectorCopy( temp1, v02 );

				}
			}
		}

		VectorNormalize( normal ); //we have the largest normal

		surf->tangentSpaceTransform[ 0 ][ 2 ] = normal[ 0 ];
		surf->tangentSpaceTransform[ 1 ][ 2 ] = normal[ 1 ];
		surf->tangentSpaceTransform[ 2 ][ 2 ] = normal[ 2 ];

		//now get the tangent
		s = ( p->verts[ 1 ][ 3 ] - p->verts[ 0 ][ 3 ] )
			* ( vec[ 4 ] - p->verts[ 0 ][ 4 ] );
		s -= ( vec[ 3 ] - p->verts[ 0 ][ 3 ] )
			 * ( p->verts[ 1 ][ 4 ] - p->verts[ 0 ][ 4 ] );
		s = 1.0f / s;

		VectorScale( v01, vec[ 4 ] - p->verts[ 0 ][ 4 ], temp1 );
		VectorScale( v02, p->verts[ 1 ][ 4 ] - p->verts[ 0 ][ 4 ], temp2 );
		_VectorSubtract( temp1, temp2, temp3 );
		VectorScale( temp3, s, tangent );
		VectorNormalize( tangent ); 

		surf->tangentSpaceTransform[ 0 ][ 0 ] = tangent[ 0 ];
		surf->tangentSpaceTransform[ 1 ][ 0 ] = tangent[ 1 ];
		surf->tangentSpaceTransform[ 2 ][ 0 ] = tangent[ 2 ];

		//now get the binormal
		VectorScale( v02, p->verts[ 1 ][ 3 ] - p->verts[ 0 ][ 3 ], temp1 );
		VectorScale( v01, vec[ 3 ] - p->verts[ 0 ][ 3 ], temp2 );
		_VectorSubtract( temp1, temp2, temp3 );
		VectorScale( temp3, s, binormal );
		VectorNormalize( binormal ); 

		surf->tangentSpaceTransform[ 0 ][ 1 ] = binormal[ 0 ];
		surf->tangentSpaceTransform[ 1 ][ 1 ] = binormal[ 1 ];
		surf->tangentSpaceTransform[ 2 ][ 1 ] = binormal[ 2 ];
	}
}





void GL_BuildPolygonFromSurface(msurface_t *fa);
void GL_CreateSurfaceLightmap (msurface_t *surf);
void GL_EndBuildingLightmaps (void);
void GL_BeginBuildingLightmaps (model_t *m);

/*
=================
Mod_LoadFaces
=================
*/
void Mod_LoadFaces (lump_t *l)
{
	dface_t		*in;
	msurface_t 	*out;
	int			i, count, surfnum;
	int			planenum, side;
	int			ti;
	rscript_t	*rs;
	vec3_t		color;
	qboolean	fod;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Com_Error (ERR_DROP, "MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->surfaces = out;
	loadmodel->numsurfaces = count;

	currentmodel = loadmodel;

	GL_BeginBuildingLightmaps (loadmodel);

	for ( surfnum=0 ; surfnum<count ; surfnum++, in++, out++)
	{
		out->firstedge = LittleLong(in->firstedge);
		out->numedges = LittleShort(in->numedges);		
		out->flags = 0;
		out->polys = NULL;

		planenum = LittleShort(in->planenum);
		side = LittleShort(in->side);
		if (side)
			out->flags |= SURF_PLANEBACK;			

		out->plane = loadmodel->planes + planenum;

		ti = LittleShort (in->texinfo);
		if (ti < 0 || ti >= loadmodel->numtexinfo)
			Com_Error (ERR_DROP, "MOD_LoadBmodel: bad texinfo number");
		out->texinfo = loadmodel->texinfo + ti;

		CalcSurfaceExtents (out);

	// lighting info

		for (i=0 ; i<MAXLIGHTMAPS ; i++)
			out->styles[i] = in->styles[i];
		i = LittleLong(in->lightofs);

		if (i == -1)
			out->samples = NULL;
		else
			out->samples = loadmodel->lightdata + i;

		//special case - note one day we should find a way to check for contents such as mist to do this.
		if(!Q_stricmp(out->texinfo->image->name, "textures/arena6/fodblue.wal") || !Q_stricmp(out->texinfo->image->name, "textures/arena5/fod.wal"))
			fod = true;
		else
			fod = false;

		// set the drawing flags
		if (out->texinfo->flags & SURF_WARP)
		{
			out->flags |= SURF_DRAWTURB;
			for (i=0 ; i<2 ; i++)
			{
				out->extents[i] = 16384;
				out->texturemins[i] = -8192;
			}
			if(!(gl_state.glsl_shaders && gl_glsl_shaders->value) || fod)
				GL_SubdivideSurface (out);	// cut up polygon for warps
		}

		// create lightmaps and polygons
		if ( !SurfaceHasNoLightmap(out) ) 
		{
			GL_CreateSurfaceLightmap (out);
		}

		if ( (! (out->texinfo->flags & SURF_WARP)) || (gl_state.glsl_shaders && gl_glsl_shaders->value && !fod)) 
			GL_BuildPolygonFromSurface(out);
		
		rs = (rscript_t *)out->texinfo->image->script;
	
		if(rs)	{
#ifdef __unix__
			rs_stage_t	*stage;
			stage = rs->stage;
#else
			rs_stage_t	*stage = rs->stage;
#endif
			do {
				if (stage->lensflare) {
					if(r_lensflare->value) 	
						GL_AddFlareSurface(out, stage->flaretype);
				}
				if (stage->grass && stage->texture) {
					if(stage->colormap.enabled) {
						color[0] = stage->colormap.red;
						color[1] = stage->colormap.green;
						color[2] = stage->colormap.blue;
					}
					GL_AddVegetationSurface(out, stage->texture->texnum, color, stage->scale.scaleX, stage->texture->bare_name, stage->grasstype);
				}
				if (stage->beam && stage->texture) {
					if(stage->colormap.enabled) {
						color[0] = stage->colormap.red;
						color[1] = stage->colormap.green;
						color[2] = stage->colormap.blue;
					}
					GL_AddBeamSurface(out, stage->texture->texnum, color, stage->scale.scaleX, stage->texture->bare_name, stage->beamtype);
				}
			} while (stage = stage->next);
		}
		GL_CalcSurfaceNormals(out);	
	}
	GL_EndBuildingLightmaps ();
}


/*
=================
Mod_SetParent
=================
*/
void Mod_SetParent (mnode_t *node, mnode_t *parent)
{
	node->parent = parent;
	if (node->contents != -1)
		return;
	Mod_SetParent (node->children[0], node);
	Mod_SetParent (node->children[1], node);
}

/*
=================
Mod_LoadNodes
=================
*/
void Mod_LoadNodes (lump_t *l)
{
	int			i, j, count, p;
	dnode_t		*in;
	mnode_t 	*out;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Com_Error (ERR_DROP, "MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->nodes = out;
	loadmodel->numnodes = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = LittleShort (in->mins[j]);
			out->minmaxs[3+j] = LittleShort (in->maxs[j]);
		}
	
		p = LittleLong(in->planenum);
		out->plane = loadmodel->planes + p;

		out->firstsurface = LittleShort (in->firstface);
		out->numsurfaces = LittleShort (in->numfaces);
		out->contents = -1;	// differentiate from leafs

		for (j=0 ; j<2 ; j++)
		{
			p = LittleLong (in->children[j]);
			if (p >= 0)
				out->children[j] = loadmodel->nodes + p;
			else
				out->children[j] = (mnode_t *)(loadmodel->leafs + (-1 - p));
		}

	}
	
	Mod_SetParent (loadmodel->nodes, NULL);	// sets nodes and leafs
}

/*
=================
Mod_LoadLeafs
=================
*/
void Mod_LoadLeafs (lump_t *l)
{
	dleaf_t 	*in;
	mleaf_t 	*out;
	int			i, j, count, p;
	glpoly_t	*poly;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Com_Error (ERR_DROP, "MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->leafs = out;
	loadmodel->numleafs = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = LittleShort (in->mins[j]);
			out->minmaxs[3+j] = LittleShort (in->maxs[j]);
		}

		p = LittleLong(in->contents);
		out->contents = p;

		out->cluster = LittleShort(in->cluster);
		out->area = LittleShort(in->area);

		out->firstmarksurface = loadmodel->marksurfaces +
			LittleShort(in->firstleafface);
		out->nummarksurfaces = LittleShort(in->numleaffaces);

		// gl underwater warp
		if (out->contents & MASK_WATER )
		{
			for (j=0 ; j<out->nummarksurfaces ; j++)
			{
				out->firstmarksurface[j]->flags |= SURF_UNDERWATER;
				for (poly = out->firstmarksurface[j]->polys ; poly ; poly=poly->next)
					poly->flags |= SURF_UNDERWATER;
			}
		}
	}	
}

/*
=================
Mod_LoadMarksurfaces
=================
*/
void Mod_LoadMarksurfaces (lump_t *l)
{	
	int		i, j, count;
	short		*in;
	msurface_t **out;
	
	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Com_Error (ERR_DROP, "MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->marksurfaces = out;
	loadmodel->nummarksurfaces = count;

	for ( i=0 ; i<count ; i++)
	{
		j = LittleShort(in[i]);
		if (j < 0 ||  j >= loadmodel->numsurfaces)
			Com_Error (ERR_DROP, "Mod_ParseMarksurfaces: bad surface number");
		out[i] = loadmodel->surfaces + j;
	}
}

/*
=================
Mod_LoadSurfedges
=================
*/
void Mod_LoadSurfedges (lump_t *l)
{	
	int		i, count;
	int		*in, *out;
	
	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Com_Error (ERR_DROP, "MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	if (count < 1 || count >= MAX_MAP_SURFEDGES)
		Com_Error (ERR_DROP, "MOD_LoadBmodel: bad surfedges count in %s: %i",
		loadmodel->name, count);

	out = Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->surfedges = out;
	loadmodel->numsurfedges = count;

	for ( i=0 ; i<count ; i++)
		out[i] = LittleLong (in[i]);
}


/*
=================
Mod_LoadPlanes
=================
*/
void Mod_LoadPlanes (lump_t *l)
{
	int			i, j;
	cplane_t	*out;
	dplane_t 	*in;
	int			count;
	int			bits;
	
	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Com_Error (ERR_DROP, "MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc ( count*sizeof(*out));	
	
	loadmodel->planes = out;
	loadmodel->numplanes = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		bits = 0;
		for (j=0 ; j<3 ; j++)
		{
			out->normal[j] = LittleFloat (in->normal[j]);
			if (out->normal[j] < 0)
				bits |= 1<<j;
		}

		out->dist = LittleFloat (in->dist);
		out->type = LittleLong (in->type);
		out->signbits = bits;
	}
}

/*
=================
Mod_LoadBrushModel
=================
*/
extern cvar_t *scriptsloaded;
void Mod_LoadBrushModel (model_t *mod, void *buffer)
{
	int			i;
	dheader_t	*header;
	mmodel_t 	*bm;
	char		rs_name[MAX_OSPATH], tmp[MAX_QPATH];		// rscript - MrG

	if(r_lensflare->value)
		R_ClearFlares();
	R_ClearGrasses();
	R_ClearBeams();
	// rscript - MrG
	RS_FreeUnmarked();
	strcpy(tmp,loadmodel->name+5);
	tmp[strlen(tmp)-4]=0;
	Com_sprintf(rs_name,MAX_OSPATH,"scripts/maps/%s.rscript",tmp);
	RS_ScanPathForScripts();		// load all found scripts
	RS_LoadScript(rs_name);
	RS_ReloadImageScriptLinks();
	RS_LoadSpecialScripts();
	Cvar_SetValue("scriptsloaded", 1);
	// end
	r_numWorldLights = 0;

	loadmodel->type = mod_brush;
	if (loadmodel != mod_known)
		Com_Error (ERR_DROP, "Loaded a brush model after the world");

	header = (dheader_t *)buffer;

	i = LittleLong (header->version);
	if (i != BSPVERSION)
		Com_Error (ERR_DROP, "Mod_LoadBrushModel: %s has wrong version number (%i should be %i)", mod->name, i, BSPVERSION);

// swap all the lumps
	mod_base = (byte *)header;

	for (i=0 ; i<sizeof(dheader_t)/sizeof(int) ; i++)
		((int *)header)[i] = LittleLong ( ((int *)header)[i]);

// load into heap
	Mod_LoadEntityStrn (&header->lumps[LUMP_ENTITIES]);
	Mod_LoadVertexes (&header->lumps[LUMP_VERTEXES]);
	Mod_LoadEdges (&header->lumps[LUMP_EDGES]);
	Mod_LoadSurfedges (&header->lumps[LUMP_SURFEDGES]);
	Mod_LoadLighting (&header->lumps[LUMP_LIGHTING]);
	Mod_LoadPlanes (&header->lumps[LUMP_PLANES]);
	Mod_LoadTexinfo (&header->lumps[LUMP_TEXINFO]);
	Mod_LoadFaces (&header->lumps[LUMP_FACES]);
	Mod_LoadMarksurfaces (&header->lumps[LUMP_LEAFFACES]);
	Mod_LoadVisibility (&header->lumps[LUMP_VISIBILITY]);
	Mod_LoadLeafs (&header->lumps[LUMP_LEAFS]);
	Mod_LoadNodes (&header->lumps[LUMP_NODES]);
	Mod_LoadSubmodels (&header->lumps[LUMP_MODELS]);
	mod->numframes = 2;		// regular and alternate animation
	
//
// set up the submodels
//
	for (i=0 ; i<mod->numsubmodels ; i++)
	{
		model_t	*starmod;

		bm = &mod->submodels[i];
		starmod = &mod_inline[i];

		*starmod = *loadmodel;
		
		starmod->firstmodelsurface = bm->firstface;
		starmod->nummodelsurfaces = bm->numfaces;
		starmod->firstnode = bm->headnode;
		if (starmod->firstnode >= loadmodel->numnodes)
			Com_Error (ERR_DROP, "Inline model %i has bad firstnode", i);

		VectorCopy (bm->maxs, starmod->maxs);
		VectorCopy (bm->mins, starmod->mins);
		starmod->radius = bm->radius;
	
		if (i == 0)
			*loadmodel = *starmod;

		starmod->numleafs = bm->visleafs;
	}

	R_ParseLightEntities();
}

/*
==============================================================================

ALIAS MODELS

==============================================================================
*/

/*
========================
Mod_FindTriangleWithEdge
Shadow volumes stuff
========================
*/
static int Mod_FindTriangleWithEdge(neighbors_t * neighbors, dtriangle_t * tris, int numtris, int triIndex, int edgeIndex){


	int i, j, found = -1, foundj = 0;
	dtriangle_t *current = &tris[triIndex];
	qboolean dup = false;

	for (i = 0; i < numtris; i++) {
		if (i == triIndex)
			continue;

		for (j = 0; j < 3; j++) {
			if (((current->index_xyz[edgeIndex] == tris[i].index_xyz[j]) &&
				 (current->index_xyz[(edgeIndex + 1) % 3] ==
				  tris[i].index_xyz[(j + 1) % 3]))
				||
				((current->index_xyz[edgeIndex] ==
				  tris[i].index_xyz[(j + 1) % 3])
				 && (current->index_xyz[(edgeIndex + 1) % 3] ==
					 tris[i].index_xyz[j]))) {
				// no edge for this model found yet?
				if (found == -1) {
					found = i;
					foundj = j;
				} else
					dup = true;	// the three edges story
			}
		}
	}

	// normal edge, setup neighbour pointers
	if (!dup && found != -1) {	// / FIXED by BERSERKER: � Tenebrae ��
								// ����������� ������, ����� found == -1
								// -> ������ ������ ������!
		neighbors[found].n[foundj] = triIndex;
		return found;
	}
	// naughty egde let no-one have the neighbour
	return -1;
}

/*
===============
Mod_BuildTriangleNeighbors

===============
*/
static void Mod_BuildTriangleNeighbors(neighbors_t * neighbors,
									   dtriangle_t * tris, int numtris)
{
	int i, j;

	// set neighbours to -1
	for (i = 0; i < numtris; i++) {
		for (j = 0; j < 3; j++)
			neighbors[i].n[j] = -1;
	}

	// generate edges information (for shadow volumes)
	// NOTE: We do this with the original vertices not the reordered onces 
	// since reordering them
	// duplicates vertices and we only compare indices
	for (i = 0; i < numtris; i++) {
		for (j = 0; j < 3; j++) {
			if (neighbors[i].n[j] == -1)
				neighbors[i].n[j] =
					Mod_FindTriangleWithEdge(neighbors, tris, numtris, i,
											 j);
		}
	}
}

/*
R_LoadMd2VertexArrays
*/
extern 
void R_LoadMd2VertexArrays(model_t *md2model){

	dmdl_t *md2;
	daliasframe_t *md2frame;
	dtrivertx_t	*md2v;
	int i;

	if(md2model->num_frames > 1)
		return;

	md2 = (dmdl_t *)md2model->extradata;
	
	md2frame = (daliasframe_t *)((byte *)md2 + md2->ofs_frames);

	if(md2->num_xyz > MAX_VERTS)
		return;

	for(i = 0, md2v = md2frame->verts; i < md2->num_xyz; i++, md2v++){  // reconstruct the verts
		VectorSet(md2model->r_mesh_verts[i],
					md2v->v[0] * md2frame->scale[0] + md2frame->translate[0],
					md2v->v[1] * md2frame->scale[1] + md2frame->translate[1],
					md2v->v[2] * md2frame->scale[2] + md2frame->translate[2]);
	}
	
}

/*
=================
Mod_LoadAliasModel
=================
*/
void Mod_LoadAliasModel (model_t *mod, void *buffer)
{
	int					i, j, l;
	dmdl_t				*pinmodel, *pheader, *paliashdr;
	dstvert_t			*pinst, *poutst;
	dtriangle_t			*pintri, *pouttri, *tris;
	daliasframe_t		*pinframe, *poutframe, *pframe;
	int					*pincmd, *poutcmd;
	int					version;
	int					cx;
	float				s, t;
	float				iw, ih;
	fstvert_t			*st;
	pinmodel = (dmdl_t *)buffer;

	version = LittleLong (pinmodel->version);
	if (version != ALIAS_VERSION)
		Com_Printf("%s has wrong version number (%i should be %i)",
				 mod->name, version, ALIAS_VERSION);

	pheader = Hunk_Alloc (LittleLong(pinmodel->ofs_end));
	
	// byte swap the header fields and sanity check
	for (i=0 ; i<sizeof(dmdl_t)/sizeof(int) ; i++)
		((int *)pheader)[i] = LittleLong (((int *)buffer)[i]);

	if (pheader->skinheight > MAX_LBM_HEIGHT)
		Com_Printf("model %s has a skin taller than %d", mod->name,
				   MAX_LBM_HEIGHT);

	if (pheader->num_xyz <= 0)
		Com_Printf("model %s has no vertices", mod->name);

	if (pheader->num_xyz > MAX_VERTS)
		Com_Printf("model %s has too many vertices", mod->name);

	if (pheader->num_st <= 0)
		Com_Printf("model %s has no st vertices", mod->name);

	if (pheader->num_tris <= 0)
		Com_Printf("model %s has no triangles", mod->name);

	if (pheader->num_frames <= 0)
		Com_Printf("model %s has no frames", mod->name);

//
// load base s and t vertices (not used in gl version)
//
	pinst = (dstvert_t *) ((byte *)pinmodel + pheader->ofs_st);
	poutst = (dstvert_t *) ((byte *)pheader + pheader->ofs_st);

	for (i=0 ; i<pheader->num_st ; i++)
	{
		poutst[i].s = LittleShort (pinst[i].s);
		poutst[i].t = LittleShort (pinst[i].t);
	}

//
// load triangle lists
//
	pintri = (dtriangle_t *) ((byte *)pinmodel + pheader->ofs_tris);
	pouttri = (dtriangle_t *) ((byte *)pheader + pheader->ofs_tris);

	for (i=0 ; i<pheader->num_tris ; i++)
	{
		for (j=0 ; j<3 ; j++)
		{
			pouttri[i].index_xyz[j] = LittleShort (pintri[i].index_xyz[j]);
			pouttri[i].index_st[j] = LittleShort (pintri[i].index_st[j]);
		}
	}

//
// find neighbours
//
	mod->neighbors = malloc(pheader->num_tris * sizeof(neighbors_t));
	Mod_BuildTriangleNeighbors(mod->neighbors, pouttri, pheader->num_tris);

//
// load the frames
//
	for (i=0 ; i<pheader->num_frames ; i++)
	{
		pinframe = (daliasframe_t *) ((byte *)pinmodel 
			+ pheader->ofs_frames + i * pheader->framesize);
		poutframe = (daliasframe_t *) ((byte *)pheader 
			+ pheader->ofs_frames + i * pheader->framesize);

		memcpy (poutframe->name, pinframe->name, sizeof(poutframe->name));
		for (j=0 ; j<3 ; j++)
		{
			poutframe->scale[j] = LittleFloat (pinframe->scale[j]);
			poutframe->translate[j] = LittleFloat (pinframe->translate[j]);
		}
		// verts are all 8 bit, so no swapping needed
		memcpy (poutframe->verts, pinframe->verts, 
			pheader->num_xyz*sizeof(dtrivertx_t));

	}

	mod->type = mod_alias;
	mod->num_frames = pheader->num_frames;

	//
	// load the glcmds
	//
	pincmd = (int *) ((byte *)pinmodel + pheader->ofs_glcmds);
	poutcmd = (int *) ((byte *)pheader + pheader->ofs_glcmds);
	for (i=0 ; i<pheader->num_glcmds ; i++)
		poutcmd[i] = LittleLong (pincmd[i]);


	// register all skins
	memcpy ((char *)pheader + pheader->ofs_skins, (char *)pinmodel + pheader->ofs_skins,
		pheader->num_skins*MAX_SKINNAME);
	for (i=0 ; i<pheader->num_skins ; i++)
	{
		char rs[MAX_OSPATH];

		mod->skins[i] = GL_FindImage ((char *)pheader + pheader->ofs_skins + i*MAX_SKINNAME
			, it_skin);

		strcpy(rs,(char *)pinmodel + LittleLong(pinmodel->ofs_skins) + i*MAX_SKINNAME);

		rs[strlen(rs)-4]=0;

#ifdef _WINDOWS
		(struct rscript_s *)mod->script[i] = RS_FindScript(rs);
#else
		mod->script[i] = RS_FindScript(rs); //make it gcc 4.1.1 compatible
#endif
		
		if (mod->script[i])
			RS_ReadyScript((rscript_t *)mod->script[i]);
	}

	cx = pheader->num_st * sizeof(fstvert_t);
     mod->st = st = (fstvert_t*)Hunk_Alloc (cx);
		
	// Calculate texcoords for triangles
    iw = 1.0 / pheader->skinwidth;
    ih = 1.0 / pheader->skinheight;
    for (i=0; i<pheader->num_st ; i++)
    {
        s = poutst[i].s;
        t = poutst[i].t;
        st[i].s = (s + 1.0) * iw; //tweak by one pixel
        st[i].t = (t + 1.0) * ih;
    }

	R_LoadMd2VertexArrays(mod);

	paliashdr = (dmdl_t *)mod->extradata;

	//just use frame zero - we never scale frames in Alien Arena
	pframe = ( daliasframe_t * ) ( ( byte * ) paliashdr +
		                              paliashdr->ofs_frames);

	/*
	** compute axially aligned mins and maxs
	*/
	for ( i = 0; i < 3; i++ )
	{
		mod->mins[i] = pframe->translate[i];
		mod->maxs[i] = mod->mins[i] + pframe->scale[i]*255;
	}

	/*
	** compute a full bounding box
	*/
	for ( i = 0; i < 8; i++ )
	{
		vec3_t   tmp;

		if ( i & 1 )
			tmp[0] = mod->mins[0];
		else
			tmp[0] = mod->maxs[0];

		if ( i & 2 )
			tmp[1] = mod->mins[1];
		else
			tmp[1] = mod->maxs[1];

		if ( i & 4 )
			tmp[2] = mod->mins[2];
		else
			tmp[2] = mod->maxs[2];

		VectorCopy( tmp, mod->bbox[i] );
	}
	
	if (gl_state.vbo)
	{
		int		index_st;
		float	*map;

		mod->vbo_st = NULL;
		mod->vbo_xyz = NULL;

		tris = (dtriangle_t *) ((byte *)pheader + pheader->ofs_tris);
		map = (float*) vbo_shadow;	

		for (l=0, i=0; i<pheader->num_tris; i++)
		{
			for (j=0; j<3; j++)
			{
				index_st = tris[i].index_st[j];
				map[l++] = mod->st[index_st].s;
				map[l++] = mod->st[index_st].t;
			}
		}

		if (l>3*MAX_VBO_XYZs)
			Com_Error(ERR_FATAL, "Temporary buffer overflow\n");


		mod->vbo_st = R_VCLoadData(VBO_STATIC, l*sizeof(float), &vbo_shadow, VBO_STORE_ANY, NULL);
		//mod->vbo_xyz = R_VCLoadData(VBO_STATIC, paliashdr->num_xyz*sizeof(vec3_t), &mod->r_mesh_verts, VBO_STORE_ANY, NULL);
		GL_BindVBO(NULL);
	}

}

//=============================================================================

/*
@@@@@@@@@@@@@@@@@@@@@
R_BeginRegistration

Specifies the model that will be used as the world
@@@@@@@@@@@@@@@@@@@@@
*/
int R_FindFile (char *filename, FILE **file)
{
			
	*file = fopen (filename, "rb");
	if (!*file) {
		*file = NULL;
		return -1;
	}
	else
		return 1;

}

void R_BeginRegistration (char *model)
{
	char	fullname[MAX_OSPATH];
 	char    *path;
	cvar_t	*flushmap;
	FILE	*file;
	int		i;

	registration_sequence++;
	r_oldviewcluster = -1;		// force markleafs

	r_weather = 0; //default is 0

	// check for fog file, using file system search path
	path = NULL;
	map_fog = false;
	for(;;)
	{
		path = FS_NextPath( path );
		if( !path )
		{
			break;
		}
		Com_sprintf(fullname, sizeof(fullname), "%s/maps/scripts/%s.fog", path, model);
		i = 0;
		do
			fullname[i] = tolower(fullname[i]);
		while (fullname[i++]);
		R_FindFile( fullname, &file ); //does a fog file exist?
		if(file) {
			//read the file, get fog information
			fclose(file);
			R_ReadFogScript(fullname);
			map_fog = true;
			break;
		}
	}

	// check for background music file, , using file system search path
	strcpy(map_music, "music/none.wav");
	path = NULL;
	for(;;)
	{
		path = FS_NextPath( path );
		if( !path )
		{
			break;
		}
		Com_sprintf(fullname, sizeof(fullname), "%s/maps/scripts/%s.mus", path, model);
		i = 0;
		do
			fullname[i] = tolower(fullname[i]);
		while (fullname[i++]);
		R_FindFile( fullname, &file ); //does a music file exist?
		if(file) {
			//read the file, get music information
			fclose( file );
			R_ReadMusicScript( fullname );
			break;
		}
	}

	Com_sprintf (fullname, sizeof(fullname), "maps/%s.bsp", model);

	// explicitly free the old map if different
	// this guarantees that mod_known[0] is the world map
	flushmap = Cvar_Get ("flushmap", "0", 0);

	if ( strcmp(mod_known[0].name, fullname) || flushmap->value)
		Mod_Free (&mod_known[0]);
	else
		Mod_Free (&mod_known[0]);	//do it every time to fix shader bugs in AA

	r_worldmodel = Mod_ForName(fullname, true);

	r_viewcluster = -1;

}

/*
@@@@@@@@@@@@@@@@@@@@@
R_RegisterModel

@@@@@@@@@@@@@@@@@@@@@
*/
struct model_s *R_RegisterModel (char *name)
{
	model_t	*mod;
	int		i;
	dmdl_t		*pheader;
	
	mod = Mod_ForName (name, false);
	if (mod)
	{
		mod->registration_sequence = registration_sequence;

		// register any images used by the models
		if (mod->type == mod_alias)
		{
			pheader = (dmdl_t *)mod->extradata;
			for (i=0 ; i<pheader->num_skins ; i++)
				mod->skins[i] = GL_FindImage ((char *)pheader + pheader->ofs_skins + i*MAX_SKINNAME, it_skin);
//PGM
			mod->numframes = pheader->num_frames;
//PGM
		}
		else if (mod->type == mod_brush)
		{
			for (i=0 ; i<mod->numtexinfo ; i++) {
				mod->texinfo[i].image->registration_sequence = registration_sequence;
				mod->texinfo[i].normalMap->registration_sequence = registration_sequence;
			}
		}
	}
	return mod;
}


/*
@@@@@@@@@@@@@@@@@@@@@
R_EndRegistration

@@@@@@@@@@@@@@@@@@@@@
*/
void R_EndRegistration (void)
{
	int		i;
	model_t	*mod;

	for (i=0, mod=mod_known ; i<mod_numknown ; i++, mod++)
	{
		if (!mod->name[0])
			continue;
		if (mod->registration_sequence != registration_sequence)
		{	// don't need this model
			Mod_Free (mod);
		}
	}

	GL_FreeUnusedImages ();
}


//=============================================================================


/*
================
Mod_Free
================
*/
void Mod_Free (model_t *mod)
{
	Hunk_Free (mod->extradata);
	memset (mod, 0, sizeof(*mod));
}

/*
================
Mod_FreeAll
================
*/
void Mod_FreeAll (void)
{
	int		i;

	for (i=0 ; i<mod_numknown ; i++)
	{
		if (mod_known[i].extradatasize)
			Mod_Free (&mod_known[i]);
	}
}



