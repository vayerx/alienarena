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
// R_SURF.C: surface-related refresh code
#include <assert.h>

#include "r_local.h"
#include "r_refl.h"	// MPO

static vec3_t	modelorg;		// relative to viewpoint

msurface_t	*r_alpha_surfaces;
msurface_t	*r_special_surfaces;

#define DYNAMIC_LIGHT_WIDTH  128
#define DYNAMIC_LIGHT_HEIGHT 128

#define LIGHTMAP_BYTES 4

#define	BLOCK_WIDTH		128
#define	BLOCK_HEIGHT	128

#define	MAX_LIGHTMAPS	128

int		c_visible_lightmaps;
int		c_visible_textures;

#define GL_LIGHTMAP_FORMAT GL_RGBA

typedef struct
{
	int internal_format;
	int	current_lightmap_texture;

	msurface_t	*lightmap_surfaces[MAX_LIGHTMAPS];

	int			allocated[BLOCK_WIDTH];

	// the lightmap texture data needs to be kept in
	// main memory so texsubimage can update properly
	byte		lightmap_buffer[4*BLOCK_WIDTH*BLOCK_HEIGHT];
} gllightmapstate_t;

static gllightmapstate_t gl_lms;

static void		LM_InitBlock( void );
static void		LM_UploadBlock( qboolean dynamic );
static qboolean	LM_AllocBlock (int w, int h, int *x, int *y);

extern void R_SetCacheState( msurface_t *surf );
extern void R_BuildLightMap (msurface_t *surf, byte *dest, int stride);

// MH - detail textures begin
// this should most likely properly belong in a state management struct somewhere
static msurface_t *r_detailsurfaces;
// MH - detail textures end
static msurface_t *r_normalsurfaces;

/*
=============================================================

	BRUSH MODELS

=============================================================
*/

/*
===============
R_TextureAnimation

Returns the proper texture for a given time and base texture
===============
*/
image_t *R_TextureAnimation (mtexinfo_t *tex)
{
	int		c;

	if (!tex->next)
		return tex->image;

	c = currententity->frame % tex->numframes;
	while (c)
	{
		tex = tex->next;
		c--;
	}

	return tex->image;
}

/*
================
DrawGLPoly
================
*/
void DrawGLPoly (glpoly_t *p, int flags)
{
	int		i;
	float	*v = p->verts[0];
	float	scroll;

	scroll = 0;
	if (flags & SURF_FLOWING)
	{
		scroll = -64 * ( (r_newrefdef.time / 40.0) - (int)(r_newrefdef.time / 40.0) );
		if (scroll == 0.0)
			scroll = -64.0;
	}

	qglBegin (GL_POLYGON);
	for (i=0 ; i<p->numverts ; i++, v+= VERTEXSIZE)
	{
		qglTexCoord2f (v[3] + scroll, v[4]);
		qglVertex3fv (v);
	}
	qglEnd ();
}

/*
** R_DrawTriangleOutlines
*/
void R_DrawTriangleOutlines (void)
{
	int			i, j;
	glpoly_t	*p;

	if (!gl_showtris->value)
		return;

	qglDisable (GL_TEXTURE_2D);
	qglDisable (GL_DEPTH_TEST);
	qglColor4f (1,1,1,1);

	for (i=0 ; i<MAX_LIGHTMAPS ; i++)
	{
		msurface_t *surf;

		for ( surf = gl_lms.lightmap_surfaces[i]; surf != 0; surf = surf->lightmapchain )
		{
			p = surf->polys;
			for (j=2 ; j<p->numverts ; j++ )
			{
				qglBegin (GL_LINE_STRIP);
				qglVertex3fv (p->verts[0]);
				qglVertex3fv (p->verts[j-1]);
				qglVertex3fv (p->verts[j]);
				qglVertex3fv (p->verts[0]);
				qglEnd ();
			}
		}
	}

	qglEnable (GL_DEPTH_TEST);
	qglEnable (GL_TEXTURE_2D);
}

/*
** DrawGLPolyLightmap
*/
void DrawGLPolyLightmap( glpoly_t *p, float soffset, float toffset )
{
	float *v = p->verts[0];
	int j;
	
	qglBegin (GL_POLYGON);
	for (j=0 ; j<p->numverts ; j++, v+= VERTEXSIZE)
	{
		qglTexCoord2f (v[5] - soffset, v[6] - toffset );
		qglVertex3fv (v);
	}
	qglEnd ();
}

/*
** R_BlendLightMaps
**
** This routine takes all the given light mapped surfaces in the world and
** blends them into the framebuffer.
*/
void R_BlendLightmaps (void)
{
	int			i;
	msurface_t	*surf, *newdrawsurf = 0;

	// don't bother if we're set to fullbright
	if (r_fullbright->value)
		return;
	if (!r_worldmodel->lightdata)
		return;

	// don't bother writing Z
	qglDepthMask( 0 );

	/*
	** set the appropriate blending mode unless we're only looking at the
	** lightmaps.
	*/
	if (!gl_lightmap->value)
	{
		qglEnable (GL_BLEND);

		if ( gl_saturatelighting->value )
		{
			qglBlendFunc( GL_ONE, GL_ONE );
		}
		else
		{
			if ( gl_monolightmap->string[0] != '0' )
			{
				switch ( toupper( gl_monolightmap->string[0] ) )
				{
				case 'I':
					qglBlendFunc (GL_ZERO, GL_SRC_COLOR );
					break;
				case 'L':
					qglBlendFunc (GL_ZERO, GL_SRC_COLOR );
					break;
				case 'A':
				default:
					qglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
					break;
				}
			}
			else
			{
				qglBlendFunc (GL_ZERO, GL_SRC_COLOR );
			}
		}
	}

	if ( currentmodel == r_worldmodel )
		c_visible_lightmaps = 0;

	/*
	** render static lightmaps first
	*/
	for ( i = 1; i < MAX_LIGHTMAPS; i++ )
	{
		if ( gl_lms.lightmap_surfaces[i] )
		{
			if (currentmodel == r_worldmodel)
				c_visible_lightmaps++;
			GL_Bind( gl_state.lightmap_textures + i);

			for ( surf = gl_lms.lightmap_surfaces[i]; surf != 0; surf = surf->lightmapchain )
			{
				if ( surf->polys )
					DrawGLPolyLightmap( surf->polys, 0, 0 );
			}
		}
	}

	/*
	** render dynamic lightmaps
	*/
	if ( gl_dynamic->value )
	{
		LM_InitBlock();

		GL_Bind( gl_state.lightmap_textures+0 );

		if (currentmodel == r_worldmodel)
			c_visible_lightmaps++;

		newdrawsurf = gl_lms.lightmap_surfaces[0];

		for ( surf = gl_lms.lightmap_surfaces[0]; surf != 0; surf = surf->lightmapchain )
		{
			int		smax, tmax;
			byte	*base;

			smax = (surf->extents[0]>>4)+1;
			tmax = (surf->extents[1]>>4)+1;

			if ( LM_AllocBlock( smax, tmax, &surf->dlight_s, &surf->dlight_t ) )
			{
				base = gl_lms.lightmap_buffer;
				base += ( surf->dlight_t * BLOCK_WIDTH + surf->dlight_s ) * LIGHTMAP_BYTES;

				R_BuildLightMap (surf, base, BLOCK_WIDTH*LIGHTMAP_BYTES);
			}
			else
			{
				msurface_t *drawsurf;

				// upload what we have so far
				LM_UploadBlock( true );

				// draw all surfaces that use this lightmap
				for ( drawsurf = newdrawsurf; drawsurf != surf; drawsurf = drawsurf->lightmapchain )
				{
					if ( drawsurf->polys )
						DrawGLPolyLightmap( drawsurf->polys, 
							              ( drawsurf->light_s - drawsurf->dlight_s ) * ( 1.0 / 128.0 ), 
										( drawsurf->light_t - drawsurf->dlight_t ) * ( 1.0 / 128.0 ) );
				}

				newdrawsurf = drawsurf;

				// clear the block
				LM_InitBlock();

				// try uploading the block now
				if ( !LM_AllocBlock( smax, tmax, &surf->dlight_s, &surf->dlight_t ) )
				{
					Com_Error( ERR_FATAL, "Consecutive calls to LM_AllocBlock(%d,%d) failed (dynamic)\n", smax, tmax );
				}

				base = gl_lms.lightmap_buffer;
				base += ( surf->dlight_t * BLOCK_WIDTH + surf->dlight_s ) * LIGHTMAP_BYTES;

				R_BuildLightMap (surf, base, BLOCK_WIDTH*LIGHTMAP_BYTES);
			}
		}

		/*
		** draw remainder of dynamic lightmaps that haven't been uploaded yet
		*/
		if ( newdrawsurf )
			LM_UploadBlock( true );

		for ( surf = newdrawsurf; surf != 0; surf = surf->lightmapchain )
		{
			if ( surf->polys )
				DrawGLPolyLightmap( surf->polys, ( surf->light_s - surf->dlight_s ) * ( 1.0 / 128.0 ), ( surf->light_t - surf->dlight_t ) * ( 1.0 / 128.0 ) );
		}
	}

	/*
	** restore state
	*/
	qglDisable (GL_BLEND);
	qglBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	qglDepthMask( 1 );
}

/*
================
R_RenderBrushPoly
================
*/
void R_RenderBrushPoly (msurface_t *fa)
{
	int			maps;
	image_t		*image;
	qboolean is_dynamic = false;

	c_brush_polys++;

	image = R_TextureAnimation (fa->texinfo);

	if (fa->flags & SURF_DRAWTURB)
	{	
		GL_Bind( image->texnum );

		// warp texture, no lightmaps
		GL_TexEnv( GL_MODULATE );
		qglColor4f( gl_state.inverse_intensity, 
			        gl_state.inverse_intensity,
					gl_state.inverse_intensity,
					1.0F );
		if(!gl_reflection->value || (!Q_stricmp(fa->texinfo->image->name, "textures/arena2/lava.wal"))) //lava HACK!
			EmitWaterPolys_original(fa);
		else
			EmitWaterPolys (fa);
		GL_TexEnv( GL_REPLACE );

		return;
	}
	else
	{
		GL_Bind( image->texnum );

		GL_TexEnv( GL_REPLACE );
	}

	// MH - detail textures begin
	// accumulate the detail chain
	// wait until we get here so that we don't get any detail texturing on DRAWTURB polys
	fa->detailchain = r_detailsurfaces;
	r_detailsurfaces = fa;
	// MH - detail textures end
	fa->normalchain = r_normalsurfaces;
	r_normalsurfaces = fa;

	if (SurfaceIsAlphaBlended(fa))
		qglEnable( GL_ALPHA_TEST );
	
	DrawGLPoly (fa->polys, fa->texinfo->flags);

	if (SurfaceIsAlphaBlended(fa))
	{
		qglDisable( GL_ALPHA_TEST );
		return;
	}//for now until Vic fixes problems with colored lightmaps on these surfaces

	/*
	** check for lightmap modification
	*/
	for ( maps = 0; maps < MAXLIGHTMAPS && fa->styles[maps] != 255; maps++ )
	{
		if ( r_newrefdef.lightstyles[fa->styles[maps]].white != fa->cached_light[maps] )
			goto dynamic;
	}

	// dynamic this frame or dynamic previously
	if ( ( fa->dlightframe == r_framecount ) )
	{
dynamic:
		if ( gl_dynamic->value )
		{
			if ( !SurfaceHasNoLightmap( fa ) )
			{
				is_dynamic = true;
			}
		}
	}

	if ( is_dynamic )
	{
		if ( ( fa->styles[maps] >= 32 || fa->styles[maps] == 0 ) && ( fa->dlightframe != r_framecount ) )
		{
			unsigned	temp[34*34];
			int			smax, tmax;

			smax = (fa->extents[0]>>4)+1;
			tmax = (fa->extents[1]>>4)+1;

			R_BuildLightMap( fa, (void *)temp, smax*4 );
			R_SetCacheState( fa );

			GL_Bind( gl_state.lightmap_textures + fa->lightmaptexturenum );

			qglTexSubImage2D( GL_TEXTURE_2D, 0,
							  fa->light_s, fa->light_t, 
							  smax, tmax, 
							  GL_LIGHTMAP_FORMAT, 
							  GL_UNSIGNED_BYTE, temp );

			fa->lightmapchain = gl_lms.lightmap_surfaces[fa->lightmaptexturenum];
			gl_lms.lightmap_surfaces[fa->lightmaptexturenum] = fa;
		}
		else
		{
			fa->lightmapchain = gl_lms.lightmap_surfaces[0];
			gl_lms.lightmap_surfaces[0] = fa;
		}
	}
	else
	{
		fa->lightmapchain = gl_lms.lightmap_surfaces[fa->lightmaptexturenum];
		gl_lms.lightmap_surfaces[fa->lightmaptexturenum] = fa;
	}
}


/*
================
R_DrawAlphaSurfaces

Draw water surfaces and windows.
The BSP tree is waled front to back, so unwinding the chain
of alpha_surfaces will draw back to front, giving proper ordering.
================
*/
void R_DrawAlphaSurfaces (void)
{
	msurface_t	*s;
	float		intens;
	rscript_t *rs_shader;

	// the textures are prescaled up for a better lighting range,
	// so scale it back down
	intens = gl_state.inverse_intensity;

	for (s=r_alpha_surfaces ; s ; s=s->texturechain)
	{
		qglLoadMatrixf (r_world_matrix); //moving trans brushes

		qglDepthMask ( GL_FALSE );
		qglEnable (GL_BLEND);
		GL_TexEnv( GL_MODULATE );

		GL_Bind(s->texinfo->image->texnum);
		c_brush_polys++;

		if (s->texinfo->flags & SURF_TRANS33)
			qglColor4f (intens, intens, intens, 0.33);
		else if (s->texinfo->flags & SURF_TRANS66)
			qglColor4f (intens, intens, intens, 0.66);
		else
			qglColor4f (intens, intens, intens, 1);

		//moving trans brushes - spaz
		if (s->entity)
		{
			s->entity->angles[0] = -s->entity->angles[0];	// stupid quake bug
			s->entity->angles[2] = -s->entity->angles[2];	// stupid quake bug
				R_RotateForEntity (s->entity);
			s->entity->angles[0] = -s->entity->angles[0];	// stupid quake bug
			s->entity->angles[2] = -s->entity->angles[2];	// stupid quake bug
		}

		if (s->flags & SURF_DRAWTURB) { 
			if(!gl_reflection->value || (!Q_stricmp(s->texinfo->image->name, "textures/arena2/lava.wal"))) //lava HACK!
				EmitWaterPolys_original (s);
			else
				EmitWaterPolys (s);
		}
		else {
			if(r_shaders->value) {
				rs_shader = (rscript_t *)s->texinfo->image->script;
				if(rs_shader) {

					qglDepthMask(false);
					qglShadeModel (GL_SMOOTH);
					qglEnable(GL_POLYGON_OFFSET_FILL); 
					qglPolygonOffset(-3, -2); 
					
					RS_SpecialSurface(s);

					qglDisable(GL_POLYGON_OFFSET_FILL); 
					GLSTATE_DISABLE_BLEND
					GLSTATE_DISABLE_ALPHATEST

					qglDepthMask(true);
				}
				else
					DrawGLPoly (s->polys, s->texinfo->flags);
			}
			else
				DrawGLPoly (s->polys, s->texinfo->flags);
		}

	}

	GL_TexEnv( GL_REPLACE );
	qglColor4f (1,1,1,1);
	qglDisable (GL_BLEND);
	qglDepthMask ( GL_TRUE );

	r_alpha_surfaces = NULL;
}

void R_DrawSpecialSurfaces (void)
{
	msurface_t	*s;
	
	if (!r_shaders->value)
	{
		r_special_surfaces = NULL;
		return;
	}

	qglDepthMask(false);
	qglShadeModel (GL_SMOOTH);

	qglEnable(GL_POLYGON_OFFSET_FILL); 
	qglPolygonOffset(-3, -2); 

	for (s=r_special_surfaces ; s ; s=s->specialchain)
		RS_SpecialSurface(s);
	
	qglDisable(GL_POLYGON_OFFSET_FILL); 

	GLSTATE_DISABLE_BLEND
	GLSTATE_DISABLE_ALPHATEST

	qglDepthMask(true);

	r_special_surfaces = NULL;
}
/*
================
DrawTextureChains
================
*/
void DrawTextureChains (void)
{
	int		i;
	msurface_t	*s;
	image_t		*image;

	c_visible_textures = 0;

	if ( !qglSelectTextureSGIS && !qglActiveTextureARB )
	{
		for ( i = 0, image=gltextures ; i<numgltextures ; i++,image++)
		{
			if (!image->registration_sequence)
				continue;
			s = image->texturechain;
			if (!s)
				continue;
			c_visible_textures++;

			for ( ; s ; s=s->texturechain)
				R_RenderBrushPoly (s);

			image->texturechain = NULL;
		}
	}
	else
	{
		for ( i = 0, image=gltextures ; i<numgltextures ; i++,image++)
		{
			if (!image->registration_sequence)
				continue;
			if (!image->texturechain)
				continue;
			c_visible_textures++;

			for ( s = image->texturechain; s ; s=s->texturechain)
			{
				if ( !( s->flags & SURF_DRAWTURB ) )
					R_RenderBrushPoly (s);
			}
		}

		GL_EnableMultitexture( false );
		for ( i = 0, image=gltextures ; i<numgltextures ; i++,image++)
		{
			if (!image->registration_sequence)
				continue;
			s = image->texturechain;
			if (!s)
				continue;

			for ( ; s ; s=s->texturechain)
			{
				if ( s->flags & SURF_DRAWTURB )
					R_RenderBrushPoly (s);
			}

			image->texturechain = NULL;
		}
	}

	GL_TexEnv( GL_REPLACE );
}


static void GL_RenderLightmappedPoly( msurface_t *surf )
{
	int		i, nv = surf->polys->numverts;
	int		map;
	float	scroll;
	float	*v;
	image_t *image = R_TextureAnimation( surf->texinfo );
	qboolean is_dynamic = false;
	unsigned lmtex = surf->lightmaptexturenum;

	// MH - detail textures begin
	// accumulate the detail texture chain
	surf->detailchain = r_detailsurfaces;
	r_detailsurfaces = surf;
	// MH - detail textures end
	surf->normalchain = r_normalsurfaces;
	r_normalsurfaces = surf;

	for ( map = 0; map < MAXLIGHTMAPS && surf->styles[map] != 255; map++ )
	{
		if ( r_newrefdef.lightstyles[surf->styles[map]].white != surf->cached_light[map] )
			goto dynamic;
	}

	// dynamic this frame or dynamic previously
	if ( ( surf->dlightframe == r_framecount ) )
	{
dynamic:
		if ( gl_dynamic->value )
		{
			if ( !SurfaceHasNoLightmap(surf) )
				is_dynamic = true;
		}
	}

	c_brush_polys++;

	if ( is_dynamic )
	{
		unsigned	temp[128*128];
		int			smax, tmax;

		smax = (surf->extents[0]>>4)+1;
		tmax = (surf->extents[1]>>4)+1;
		
		R_BuildLightMap( surf, (void *)temp, smax*4 );
		
		if ( ( surf->styles[map] >= 32 || surf->styles[map] == 0 ) && ( surf->dlightframe != r_framecount ) ) {
			R_SetCacheState( surf );
		} else {
			lmtex = 0;
		}

		GL_MBind( GL_TEXTURE1, gl_state.lightmap_textures + lmtex );
		qglTexSubImage2D( GL_TEXTURE_2D, 0,
						  surf->light_s, surf->light_t, 
						  smax, tmax, 
						  GL_LIGHTMAP_FORMAT, 
						  GL_UNSIGNED_BYTE, temp );
	}

	if (SurfaceIsAlphaBlended(surf))
		qglEnable( GL_ALPHA_TEST );

	GL_MBind( GL_TEXTURE0, image->texnum );
	GL_MBind( GL_TEXTURE1, gl_state.lightmap_textures + lmtex );

	scroll = 0;
	if (surf->texinfo->flags & SURF_FLOWING)
	{
		scroll = -64 * ( (r_newrefdef.time / 40.0) - (int)(r_newrefdef.time / 40.0) );
		if (scroll == 0.0)
			scroll = -64.0;
	}

	v = surf->polys->verts[0];
	qglBegin (GL_POLYGON);
	for (i=0 ; i< nv; i++, v+= VERTEXSIZE)
	{
		qglMTexCoord2fSGIS( GL_TEXTURE0, v[3]+scroll, v[4]);
		qglMTexCoord2fSGIS( GL_TEXTURE1, v[5], v[6]);
		qglVertex3fv (v);
	}
	qglEnd ();

	if (SurfaceIsAlphaBlended(surf))
		qglDisable( GL_ALPHA_TEST);
}

//normal maps
static void R_DrawNormalMaps (void)
{
	msurface_t *surf = r_normalsurfaces;
	int		i;
	float	*v;
	glpoly_t *p;

	if(!gl_normalmaps->value)
		return;

	// nothing to draw!
	if (!surf)
		return;

	qglDepthMask (GL_FALSE); 
	qglEnable (GL_BLEND); 

	// set the correct blending mode for normal maps 
	qglBlendFunc (GL_ZERO, GL_SRC_COLOR); 

	// and the texenv 
	qglTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE); 
	qglTexEnvi (GL_TEXTURE_ENV, GL_COMBINE_RGB_EXT, GL_DOT3_RGB); 

	for (; surf; surf = surf->normalchain) 
	{ 
	   
	   // don't draw if there isn't a normalmap 
	   if (!surf->texinfo->normalMap->texnum) continue; 

	   if (surf->flags & SURF_DRAWTURB) continue; 

	   qglBindTexture (GL_TEXTURE_2D, surf->texinfo->normalMap->texnum); 

	   for (p = surf->polys; p; p = p->chain)
	   {
		   qglBegin (GL_TRIANGLE_FAN);

		   for (v = p->verts[0], i = 0 ; i < p->numverts; i++, v += VERTEXSIZE)
		   {
				// take the 128 * 128 version of the s and t co-ords(fix me, obviously)
				qglTexCoord2f (v[3], v[4]);
				qglVertex3fv (v);
		   }

		   qglEnd ();
	   }
	} 

	// back to replace mode 
	qglTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE); 

	// restore the original blend mode 
	qglBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); 

	// switch off blending 
	qglDisable (GL_BLEND); 
	qglDepthMask (GL_TRUE); 

}

// MH - detail textures begin
// here we draw the detail textures!
// we could do this as a multitextured render along with the other stuff, but my own tests
// have indicated that for something like this there's no real benefit to be had from doing
// so.  the difference is in the order of 1 or 2 FPS - the real hit from detail texturing
// comes from scrunching large images into small spaces
static void R_DrawDetailSurfaces (void)
{
	msurface_t *surf = r_detailsurfaces;
	int		i;
	float	*v;
	glpoly_t *p;

	// nothing to draw!
	if (!surf)
		return;

	// we do this rather than checking for 0 (checking for 0 is always bad with floats)
	// standard 6 decimal place precision
	if (gl_detailtextures->value < 0.00001)
		return;

	// we *could* use the standard glBindTexture here as we don't need to do any bind checking
	// but again we'll maintain consistency
	GL_Bind (r_detailtexture->texnum);

	// set the correct blending mode
	qglTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
	qglBlendFunc (GL_DST_COLOR, GL_SRC_COLOR);
	qglEnable (GL_BLEND);

	// here we get clever and do our texture scaling in the texture matrix.  this saves
	// us from more runtime calculations in software
	qglMatrixMode (GL_TEXTURE);
	qglLoadIdentity ();

	// we only need to scale the X and Y co-ords, correcponding to texture s and t.
	// everyone will have their own favourite scaling amount, so i did a cvar
	qglScalef (gl_detailtextures->value, gl_detailtextures->value, 1.0);

	// we can safely leave the texture matrix as the active mode while we're doing the draw
	for (; surf; surf = surf->detailchain)
	{
		if (SurfaceIsAlphaBlended(surf))
			continue;
		for (p = surf->polys; p; p = p->chain)
		{
			// using GL_TRIANGLE_FAN is theoretically more efficient than GL_POLYGON as
			// the GL implementation can know something about what we are going to draw
			// in advance, and set things up accordingly.  the visual result is identical.
			qglBegin (GL_TRIANGLE_FAN);

			for (v = p->verts[0], i = 0 ; i < p->numverts; i++, v += VERTEXSIZE)
			{
				// take the 128 * 128 version of the s and t co-ords
				qglTexCoord2f (v[7], v[8]);
				qglVertex3fv (v);
			}

			qglEnd ();
		}
	}

	// restore original texture matrix
	qglLoadIdentity ();
	qglMatrixMode (GL_MODELVIEW);

	// restore original blend
	qglTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	qglBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	qglDisable (GL_BLEND);
}
// MH - detail textures end
/*
=================
R_DrawInlineBModel
=================
*/
void R_DrawInlineBModel (entity_t *e)
{
	int			i;
	cplane_t	*pplane;
	float		dot;
	msurface_t	*psurf;

	R_PushDlightsForBModel ( currententity );

	if ( currententity->flags & RF_TRANSLUCENT )
	{
		qglEnable (GL_BLEND);
		qglColor4f (1,1,1,0.25);
		GL_TexEnv( GL_MODULATE );
	}

	// MH - detail textures begin
	// null the detail chain again
	r_detailsurfaces = NULL;
	// MH - detail textures end
	r_normalsurfaces = NULL;

	//
	// draw texture
	//
	psurf = &currentmodel->surfaces[currentmodel->firstmodelsurface];
	for (i=0 ; i<currentmodel->nummodelsurfaces ; i++, psurf++)
	{
	// find which side of the plane we are on
		pplane = psurf->plane;
		dot = DotProduct (modelorg, pplane->normal) - pplane->dist;

	// draw the polygon
		if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
			(!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
		{
			if (SurfaceIsTranslucent(psurf) && !SurfaceIsAlphaBlended(psurf))
			{	// add to the translucent chain
				psurf->texturechain = r_alpha_surfaces;
				r_alpha_surfaces = psurf;
				psurf->entity = e;
			}
			else if ( qglMTexCoord2fSGIS && !( psurf->flags & SURF_DRAWTURB ) )
			{
				GL_RenderLightmappedPoly( psurf );
			}
			else
			{
				GL_EnableMultitexture( false );
				R_RenderBrushPoly( psurf );
				GL_EnableMultitexture( true );
			}

			psurf->visframe = r_framecount;
		}
	}

	if ( !(currententity->flags & RF_TRANSLUCENT) )
	{
		if ( !qglMTexCoord2fSGIS )
			R_BlendLightmaps ();

		// MH - detail textures begin
		// shut down multitexturing (we can't do this in R_DrawDetailSurfaces cos then it
		// messes things up for the world)
		GL_EnableMultitexture (false);

		// don't put them on translucent surfs
		R_DrawDetailSurfaces ();
		R_DrawNormalMaps ();

		// bring multitexturing back up
		GL_EnableMultitexture (true);
		// MH - detail textures end
	}
	else
	{
		qglDisable (GL_BLEND);
		qglColor4f (1,1,1,1);
		GL_TexEnv( GL_REPLACE );
	}
}

/*
=================
R_DrawBrushModel
=================
*/
void R_DrawBrushModel (entity_t *e)
{
	vec3_t		mins, maxs;
	int			i;
	qboolean	rotated;

	if (currentmodel->nummodelsurfaces == 0)
		return;

	currententity = e;
	gl_state.currenttextures[0] = gl_state.currenttextures[1] = -1;

	if (e->angles[0] || e->angles[1] || e->angles[2])
	{
		rotated = true;
		for (i=0 ; i<3 ; i++)
		{
			mins[i] = e->origin[i] - currentmodel->radius;
			maxs[i] = e->origin[i] + currentmodel->radius;
		}
	}
	else
	{
		rotated = false;
		VectorAdd (e->origin, currentmodel->mins, mins);
		VectorAdd (e->origin, currentmodel->maxs, maxs);
	}

	R_PushStainsForBModel ( e );

	if (R_CullBox (mins, maxs)) {
		return;
	}

	qglColor3f (1,1,1);
	memset (gl_lms.lightmap_surfaces, 0, sizeof(gl_lms.lightmap_surfaces));

	//VectorSubtract (r_newrefdef.vieworg, e->origin, modelorg);

	// start MPO
	// if this is a reflection we're drawing we need to flip the vertical position across the water
	if (g_drawing_refl)
	{
		/*
		vec3_t tmp;
		VectorCopy(r_newrefdef.vieworg, tmp);
		tmp[2] = (2*g_refl_Z[g_active_refl]) - tmp[2];	// flip
		VectorSubtract(tmp, e->origin, modelorg);
		*/

		// equivalent, faster code
        modelorg[0] = r_newrefdef.vieworg[0] - e->origin[0];
        modelorg[1] = r_newrefdef.vieworg[1] - e->origin[1];
        modelorg[2] = ((2*g_refl_Z[g_active_refl]) - r_newrefdef.vieworg[2])
        	- e->origin[2];
	}
	else
	{
		VectorSubtract (r_newrefdef.vieworg, e->origin, modelorg);
	}
	// stop MPO

	if (rotated)
	{
		vec3_t	temp;
		vec3_t	forward, right, up;

		VectorCopy (modelorg, temp);
		AngleVectors (e->angles, forward, right, up);
		modelorg[0] = DotProduct (temp, forward);
		modelorg[1] = -DotProduct (temp, right);
		modelorg[2] = DotProduct (temp, up);
	}

    qglPushMatrix ();
	e->angles[0] = -e->angles[0];	// stupid quake bug
	e->angles[2] = -e->angles[2];	// stupid quake bug
	R_RotateForEntity (e);
	e->angles[0] = -e->angles[0];	// stupid quake bug
	e->angles[2] = -e->angles[2];	// stupid quake bug

	GL_EnableMultitexture( true );
	GL_SelectTexture( GL_TEXTURE0);
	// Vic - begin

	if ( !gl_config.mtexcombine ) {

		GL_TexEnv( GL_REPLACE );

		GL_SelectTexture( GL_TEXTURE1);



		if ( gl_lightmap->value )

			GL_TexEnv( GL_REPLACE );

		else 

			GL_TexEnv( GL_MODULATE );

	} else {

		GL_TexEnv ( GL_COMBINE_EXT );

		qglTexEnvi ( GL_TEXTURE_ENV, GL_COMBINE_RGB_EXT, GL_REPLACE );

		qglTexEnvi ( GL_TEXTURE_ENV, GL_SOURCE0_RGB_EXT, GL_TEXTURE );

		qglTexEnvi ( GL_TEXTURE_ENV, GL_COMBINE_ALPHA_EXT, GL_REPLACE );

		qglTexEnvi ( GL_TEXTURE_ENV, GL_SOURCE0_ALPHA_EXT, GL_TEXTURE );

		GL_SelectTexture( GL_TEXTURE1 );

		GL_TexEnv ( GL_COMBINE_EXT );



		if ( gl_lightmap->value ) {

			qglTexEnvi ( GL_TEXTURE_ENV, GL_COMBINE_RGB_EXT, GL_REPLACE );

			qglTexEnvi ( GL_TEXTURE_ENV, GL_SOURCE0_RGB_EXT, GL_TEXTURE );

			qglTexEnvi ( GL_TEXTURE_ENV, GL_COMBINE_ALPHA_EXT, GL_REPLACE );

			qglTexEnvi ( GL_TEXTURE_ENV, GL_SOURCE0_ALPHA_EXT, GL_TEXTURE );

		} else {

			qglTexEnvi ( GL_TEXTURE_ENV, GL_COMBINE_RGB_EXT, GL_MODULATE );

			qglTexEnvi ( GL_TEXTURE_ENV, GL_SOURCE0_RGB_EXT, GL_TEXTURE );

			qglTexEnvi ( GL_TEXTURE_ENV, GL_SOURCE1_RGB_EXT, GL_PREVIOUS_EXT );

			qglTexEnvi ( GL_TEXTURE_ENV, GL_COMBINE_ALPHA_EXT, GL_MODULATE );

			qglTexEnvi ( GL_TEXTURE_ENV, GL_SOURCE0_ALPHA_EXT, GL_TEXTURE );

			qglTexEnvi ( GL_TEXTURE_ENV, GL_SOURCE1_ALPHA_EXT, GL_PREVIOUS_EXT );

		}

		if ( r_overbrightbits->value ) {

			qglTexEnvi ( GL_TEXTURE_ENV, GL_RGB_SCALE_EXT, r_overbrightbits->value );

		}

	}

	// Vic - end

	R_DrawInlineBModel (e);
	GL_EnableMultitexture( false );

	qglPopMatrix ();
}

/*
=============================================================

	WORLD MODEL

=============================================================
*/

/*
================
R_RecursiveWorldNode
================
*/
void R_RecursiveWorldNode (mnode_t *node, int clipflags)
{
	int			c, side, sidebit;
	cplane_t	*plane;
	msurface_t	*surf, **mark;
	mleaf_t		*pleaf;
	float		dot;
	image_t		*image;

	if (node->contents == CONTENTS_SOLID)
		return;		// solid
	if (node->visframe != r_visframecount)
		return;
	if (!r_nocull->value)
	{
		int i, clipped;
		cplane_t *clipplane;

		for (i=0,clipplane=frustum ; i<4 ; i++,clipplane++)
		{
			clipped = BoxOnPlaneSide (node->minmaxs, node->minmaxs+3, clipplane);
			
			if (clipped == 1)
				clipflags &= ~(1<<i);	// node is entirely on screen
			else if (clipped == 2)
				return;					// fully clipped
		}
	}
	
// if a leaf node, draw stuff
	if (node->contents != -1)
	{
		pleaf = (mleaf_t *)node;

		// check for door connected areas
		if (r_newrefdef.areabits)
		{
			if (! (r_newrefdef.areabits[pleaf->area>>3] & (1<<(pleaf->area&7)) ) )
				return;		// not visible
		}

		mark = pleaf->firstmarksurface;
		if (! (c = pleaf->nummarksurfaces) )
			return;

		do
		{
			(*mark++)->visframe = r_framecount;
		} while (--c);

		return;
	}

// node is just a decision point, so go down the apropriate sides

// find which side of the node we are on
	plane = node->plane;

	switch (plane->type)
	{
	case PLANE_X:
		dot = modelorg[0] - plane->dist;
		break;
	case PLANE_Y:
		dot = modelorg[1] - plane->dist;
		break;
	case PLANE_Z:
		dot = modelorg[2] - plane->dist;
		break;
	default:
		dot = DotProduct (modelorg, plane->normal) - plane->dist;
		break;
	}

	if (dot >= 0)
	{
		side = 0;
		sidebit = 0;
	}
	else
	{
		side = 1;
		sidebit = SURF_PLANEBACK;
	}

// recurse down the children, front side first
	R_RecursiveWorldNode (node->children[side], clipflags);

	// draw stuff
	for ( c = node->numsurfaces, surf = r_worldmodel->surfaces + node->firstsurface; c ; c--, surf++)
	{
		if (surf->visframe != r_framecount)
			continue;

		if ( (surf->flags & SURF_PLANEBACK) != sidebit )
			continue;		// wrong side
		if (surf->texinfo->flags & SURF_SKY)
		{	// just adds to visible sky bounds
			R_AddSkySurface (surf);
		}
		else if (SurfaceIsTranslucent(surf) && !SurfaceIsAlphaBlended(surf))
		{	// add to the translucent chain
			surf->texturechain = r_alpha_surfaces;
			r_alpha_surfaces = surf;
			surf->entity = NULL;
		}
		else
		{
			if ( qglMTexCoord2fSGIS && !( surf->flags & SURF_DRAWTURB ) )
			{
				GL_RenderLightmappedPoly( surf );

				surf->specialchain = r_special_surfaces;
				r_special_surfaces = surf;
			}
			else
			{
				// the polygon is visible, so add it to the texture
				// sorted chain
				// FIXME: this is a hack for animation
				image = R_TextureAnimation (surf->texinfo);
				surf->texturechain = image->texturechain;
				image->texturechain = surf;

				surf->specialchain = r_special_surfaces;
				r_special_surfaces = surf;
			}
		}
	}

	// recurse down the back side
	R_RecursiveWorldNode (node->children[!side], clipflags);
}


/*
=============
R_DrawWorld
=============
*/
void R_DrawWorld (void)
{
	entity_t	ent;

	if (!r_drawworld->value)
		return;

	if ( r_newrefdef.rdflags & RDF_NOWORLDMODEL )
		return;

	currentmodel = r_worldmodel;

	VectorCopy (r_newrefdef.vieworg, modelorg);

	if (g_drawing_refl) // jitwater / MPO
		modelorg[2] = (2.0f * g_refl_Z[g_active_refl]) - modelorg[2]; // flip

	// auto cycle the world frame for texture animation
	memset (&ent, 0, sizeof(ent));
	ent.frame = (int)(r_newrefdef.time*2);
	currententity = &ent;

	gl_state.currenttextures[0] = gl_state.currenttextures[1] = -1;

	qglColor3f (1,1,1);
	memset (gl_lms.lightmap_surfaces, 0, sizeof(gl_lms.lightmap_surfaces));

	R_ClearSkyBox ();

	// MH - detail textures begin
	r_detailsurfaces = NULL;
	// MH - detail textures end
	r_normalsurfaces = NULL;

	if ( qglMTexCoord2fSGIS )
	{
		GL_EnableMultitexture( true );

		GL_SelectTexture( GL_TEXTURE0);
		// Vic - begin

		if ( !gl_config.mtexcombine ) {

			GL_TexEnv( GL_REPLACE );

			GL_SelectTexture( GL_TEXTURE1);



			if ( gl_lightmap->value )

				GL_TexEnv( GL_REPLACE );

			else 

				GL_TexEnv( GL_MODULATE );

		} else {

			GL_TexEnv ( GL_COMBINE_EXT );

			qglTexEnvi ( GL_TEXTURE_ENV, GL_COMBINE_RGB_EXT, GL_REPLACE );

			qglTexEnvi ( GL_TEXTURE_ENV, GL_SOURCE0_RGB_EXT, GL_TEXTURE );

			qglTexEnvi ( GL_TEXTURE_ENV, GL_COMBINE_ALPHA_EXT, GL_REPLACE );

			qglTexEnvi ( GL_TEXTURE_ENV, GL_SOURCE0_ALPHA_EXT, GL_TEXTURE );

			GL_SelectTexture( GL_TEXTURE1 );

			GL_TexEnv ( GL_COMBINE_EXT );



			if ( gl_lightmap->value ) {

				qglTexEnvi ( GL_TEXTURE_ENV, GL_COMBINE_RGB_EXT, GL_REPLACE );

				qglTexEnvi ( GL_TEXTURE_ENV, GL_SOURCE0_RGB_EXT, GL_TEXTURE );

				qglTexEnvi ( GL_TEXTURE_ENV, GL_COMBINE_ALPHA_EXT, GL_REPLACE );

				qglTexEnvi ( GL_TEXTURE_ENV, GL_SOURCE0_ALPHA_EXT, GL_TEXTURE );

			} else {

				qglTexEnvi ( GL_TEXTURE_ENV, GL_COMBINE_RGB_EXT, GL_MODULATE );

				qglTexEnvi ( GL_TEXTURE_ENV, GL_SOURCE0_RGB_EXT, GL_TEXTURE );

				qglTexEnvi ( GL_TEXTURE_ENV, GL_SOURCE1_RGB_EXT, GL_PREVIOUS_EXT );

				qglTexEnvi ( GL_TEXTURE_ENV, GL_COMBINE_ALPHA_EXT, GL_MODULATE );

				qglTexEnvi ( GL_TEXTURE_ENV, GL_SOURCE0_ALPHA_EXT, GL_TEXTURE );

				qglTexEnvi ( GL_TEXTURE_ENV, GL_SOURCE1_ALPHA_EXT, GL_PREVIOUS_EXT );

			}



			if ( r_overbrightbits->value ) {

				qglTexEnvi ( GL_TEXTURE_ENV, GL_RGB_SCALE_EXT, r_overbrightbits->value );

			}

		}

		// Vic - end

		R_RecursiveWorldNode (r_worldmodel->nodes, 15);

		GL_EnableMultitexture( false );
	}
	else
	{
		R_RecursiveWorldNode (r_worldmodel->nodes, 15);
	}

	/*
	** theoretically nothing should happen in the next two functions
	** if multitexture is enabled
	*/
	DrawTextureChains ();
	R_BlendLightmaps ();

	// MH - detail textures begin
	R_DrawDetailSurfaces ();
	// MH - detail textures end
	R_DrawNormalMaps ();
	
	R_DrawSkyBox ();

	R_DrawTriangleOutlines ();
}


/*
===============
R_MarkLeaves

Mark the leaves and nodes that are in the PVS for the current
cluster
===============
*/
void R_MarkLeaves (void)
{
	byte	*vis;
	byte	fatvis[MAX_MAP_LEAFS/8];
	mnode_t	*node;
	int		i, c;
	mleaf_t	*leaf;
	int		cluster;

	if (r_oldviewcluster == r_viewcluster && r_oldviewcluster2 == r_viewcluster2 && !r_novis->value && r_viewcluster != -1)
		return;

	// development aid to let you run around and see exactly where
	// the pvs ends
	if (gl_lockpvs->value)
		return;

	r_visframecount++;
	r_oldviewcluster = r_viewcluster;
	r_oldviewcluster2 = r_viewcluster2;

	if (r_novis->value || r_viewcluster == -1 || !r_worldmodel->vis)
	{
		// mark everything
		for (i=0 ; i<r_worldmodel->numleafs ; i++)
			r_worldmodel->leafs[i].visframe = r_visframecount;
		for (i=0 ; i<r_worldmodel->numnodes ; i++)
			r_worldmodel->nodes[i].visframe = r_visframecount;
		return;
	}

	vis = Mod_ClusterPVS (r_viewcluster, r_worldmodel);
	// may have to combine two clusters because of solid water boundaries
	if (r_viewcluster2 != r_viewcluster)
	{
		memcpy (fatvis, vis, (r_worldmodel->numleafs+7)/8);
		vis = Mod_ClusterPVS (r_viewcluster2, r_worldmodel);
		c = (r_worldmodel->numleafs+31)/32;
		for (i=0 ; i<c ; i++)
			((int *)fatvis)[i] |= ((int *)vis)[i];
		vis = fatvis;
	}
	
	for (i=0,leaf=r_worldmodel->leafs ; i<r_worldmodel->numleafs ; i++, leaf++)
	{
		cluster = leaf->cluster;
		if (cluster == -1)
			continue;
		if (vis[cluster>>3] & (1<<(cluster&7)))
		{
			node = (mnode_t *)leaf;
			do
			{
				if (node->visframe == r_visframecount)
					break;
				node->visframe = r_visframecount;
				node = node->parent;
			} while (node);
		}
	}
}



/*
=============================================================================

  LIGHTMAP ALLOCATION

=============================================================================
*/

static void LM_InitBlock( void )
{
	memset( gl_lms.allocated, 0, sizeof( gl_lms.allocated ) );
}

static void LM_UploadBlock( qboolean dynamic )
{
	int texture;
	int height = 0;

	if ( dynamic )
	{
		texture = 0;
	}
	else
	{
		texture = gl_lms.current_lightmap_texture;
	}

	GL_Bind( gl_state.lightmap_textures + texture );
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	if ( dynamic )
	{
		int i;

		for ( i = 0; i < BLOCK_WIDTH; i++ )
		{
			if ( gl_lms.allocated[i] > height )
				height = gl_lms.allocated[i];
		}

		qglTexSubImage2D( GL_TEXTURE_2D, 
						  0,
						  0, 0,
						  BLOCK_WIDTH, height,
						  GL_LIGHTMAP_FORMAT,
						  GL_UNSIGNED_BYTE,
						  gl_lms.lightmap_buffer );
	}
	else
	{
		qglTexImage2D( GL_TEXTURE_2D, 
					   0, 
					   gl_lms.internal_format,
					   BLOCK_WIDTH, BLOCK_HEIGHT, 
					   0, 
					   GL_LIGHTMAP_FORMAT, 
					   GL_UNSIGNED_BYTE, 
					   gl_lms.lightmap_buffer );
		if ( ++gl_lms.current_lightmap_texture == MAX_LIGHTMAPS )
			Com_Error( ERR_DROP, "LM_UploadBlock() - MAX_LIGHTMAPS exceeded\n" );
	}
}

// returns a texture number and the position inside it
static qboolean LM_AllocBlock (int w, int h, int *x, int *y)
{
	int		i, j;
	int		best, best2;

	best = BLOCK_HEIGHT;

	for (i=0 ; i<BLOCK_WIDTH-w ; i++)
	{
		best2 = 0;

		for (j=0 ; j<w ; j++)
		{
			if (gl_lms.allocated[i+j] >= best)
				break;
			if (gl_lms.allocated[i+j] > best2)
				best2 = gl_lms.allocated[i+j];
		}
		if (j == w)
		{	// this is a valid spot
			*x = i;
			*y = best = best2;
		}
	}

	if (best + h > BLOCK_HEIGHT)
		return false;

	for (i=0 ; i<w ; i++)
		gl_lms.allocated[*x + i] = best + h;

	return true;
}

/*
================
GL_BuildPolygonFromSurface
================
*/
void GL_BuildPolygonFromSurface(msurface_t *fa)
{
	int			i, lindex, lnumverts;
	medge_t		*r_pedge;
	float		*vec;
	float		s, t;
	glpoly_t	*poly;

// reconstruct the polygon
	lnumverts = fa->numedges;

	//
	// draw texture
	//
	poly = Hunk_Alloc (sizeof(glpoly_t) + (lnumverts-4) * VERTEXSIZE*sizeof(float));
	poly->next = fa->polys;
	poly->numverts = lnumverts;
	fa->polys = poly;

	for (i=0 ; i<lnumverts ; i++)
	{
		lindex = currentmodel->surfedges[fa->firstedge + i];

		if (lindex > 0)
		{
			r_pedge = &currentmodel->edges[lindex];
			vec = currentmodel->vertexes[r_pedge->v[0]].position;
		}
		else
		{
			r_pedge = &currentmodel->edges[-lindex];
			vec = currentmodel->vertexes[r_pedge->v[1]].position;
		}

		s = DotProduct (vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		s /= fa->texinfo->image->width;

		t = DotProduct (vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		t /= fa->texinfo->image->height;

		VectorCopy (vec, poly->verts[i]);
		poly->verts[i][3] = s;
		poly->verts[i][4] = t;

		//
		// lightmap texture coordinates
		//
		s = DotProduct (vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		s -= fa->texturemins[0];
		s += fa->light_s*16;
		s += 8;
		s /= BLOCK_WIDTH*16; //fa->texinfo->texture->width;

		t = DotProduct (vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		t -= fa->texturemins[1];
		t += fa->light_t*16;
		t += 8;
		t /= BLOCK_HEIGHT*16; //fa->texinfo->texture->height;

		poly->verts[i][5] = s;
		poly->verts[i][6] = t;

		// MH - detail textures begin
		// here we build a set of texture s and t co-ords suitable for a 128 * 128 version
		// of the texture.  this is necessary because using the original texture s and t
		// will cause the detail texture (and anything else we may ever consider adding)
		// to rescale itself to the same aspect ratio as the original texture, which looks
		// bad.  NOTE - to enable this we *must* increase the define of VERTEXSIZE to 9.
		s = DotProduct (vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		s /= 128;

		t = DotProduct (vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		t /= 128;

		// and load them in
		poly->verts[i][7] = s;
		poly->verts[i][8] = t;
		// MH - detail textures end
	}
}

/*
========================
GL_CreateSurfaceLightmap
========================
*/
void GL_CreateSurfaceLightmap (msurface_t *surf)
{
	int		smax, tmax;
	byte	*base;

	if (!surf->samples)
		return;

	if (surf->texinfo->flags & (SURF_SKY|SURF_WARP))
		return; //may not need this?

	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;

	if ( !LM_AllocBlock( smax, tmax, &surf->light_s, &surf->light_t ) )
	{
		LM_UploadBlock( false );
		LM_InitBlock();
		if ( !LM_AllocBlock( smax, tmax, &surf->light_s, &surf->light_t ) )
		{
			Com_Error( ERR_FATAL, "Consecutive calls to LM_AllocBlock(%d,%d) failed\n", smax, tmax );
		}
	}

	surf->lightmaptexturenum = gl_lms.current_lightmap_texture;

	base = gl_lms.lightmap_buffer;
	base += (surf->light_t * BLOCK_WIDTH + surf->light_s) * LIGHTMAP_BYTES;

	R_SetCacheState( surf );
	R_BuildLightMap (surf, base, BLOCK_WIDTH*LIGHTMAP_BYTES);
}

/*
========================
GL_CreateSurfaceStainmap
========================
*/
void GL_CreateSurfaceStainmap (msurface_t *surf)
{
	int		smax, tmax;

	if (!surf->samples)
		return;

	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;

	surf->stainsamples = Hunk_Alloc (smax*tmax*3);
	memset (surf->stainsamples, 255, smax*tmax*3);
}

/*
==================
GL_BeginBuildingLightmaps

==================
*/
void GL_BeginBuildingLightmaps (model_t *m)
{
	static lightstyle_t	lightstyles[MAX_LIGHTSTYLES];
	int				i;
	unsigned		dummy[128*128];

	memset( gl_lms.allocated, 0, sizeof(gl_lms.allocated) );

	r_framecount = 1;		// no dlightcache

	GL_EnableMultitexture( true );
	GL_SelectTexture( GL_TEXTURE1);

	/*
	** setup the base lightstyles so the lightmaps won't have to be regenerated
	** the first time they're seen
	*/
	for (i=0 ; i<MAX_LIGHTSTYLES ; i++)
	{
		lightstyles[i].rgb[0] = 1;
		lightstyles[i].rgb[1] = 1;
		lightstyles[i].rgb[2] = 1;
		lightstyles[i].white = 3;
	}
	r_newrefdef.lightstyles = lightstyles;

	if (!gl_state.lightmap_textures)
		gl_state.lightmap_textures	= TEXNUM_LIGHTMAPS;

	gl_lms.current_lightmap_texture = 1;

	/*
	** if mono lightmaps are enabled and we want to use alpha
	** blending (a,1-a) then we're likely running on a 3DLabs
	** Permedia2.  In a perfect world we'd use a GL_ALPHA lightmap
	** in order to conserve space and maximize bandwidth, however 
	** this isn't a perfect world.
	**
	** So we have to use alpha lightmaps, but stored in GL_RGBA format,
	** which means we only get 1/16th the color resolution we should when
	** using alpha lightmaps.  If we find another board that supports
	** only alpha lightmaps but that can at least support the GL_ALPHA
	** format then we should change this code to use real alpha maps.
	*/
	if ( toupper( gl_monolightmap->string[0] ) == 'A' )
	{
		gl_lms.internal_format = gl_tex_alpha_format;
	}
	/*
	** try to do hacked colored lighting with a blended texture
	*/
	else if ( toupper( gl_monolightmap->string[0] ) == 'C' )
	{
		gl_lms.internal_format = gl_tex_alpha_format;
	}
	else if ( toupper( gl_monolightmap->string[0] ) == 'I' )
	{
		gl_lms.internal_format = GL_INTENSITY8;
	}
	else if ( toupper( gl_monolightmap->string[0] ) == 'L' ) 
	{
		gl_lms.internal_format = GL_LUMINANCE8;
	}
	else
	{
		gl_lms.internal_format = gl_tex_solid_format;
	}

	/*
	** initialize the dynamic lightmap texture
	*/
	GL_Bind( gl_state.lightmap_textures + 0 );
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	qglTexImage2D( GL_TEXTURE_2D, 
				   0, 
				   gl_lms.internal_format,
				   BLOCK_WIDTH, BLOCK_HEIGHT, 
				   0, 
				   GL_LIGHTMAP_FORMAT, 
				   GL_UNSIGNED_BYTE, 
				   dummy );
}

/*
=======================
GL_EndBuildingLightmaps
=======================
*/
void GL_EndBuildingLightmaps (void)
{
	LM_UploadBlock( false );
	GL_EnableMultitexture( false );
}

/*
========================
GLOOM MINI MAP !!!
draw with vertex array 
and ower speedup changes
========================
*/


//sul's minimap thing
void R_RecursiveRadarNode (mnode_t *node)
{
	int			c, side, sidebit;
	cplane_t	*plane;
	msurface_t	*surf, **mark;
	mleaf_t		*pleaf;
	float		dot,distance;
	glpoly_t	*p;
	float		*v;
	int			i;

	if (node->contents == CONTENTS_SOLID)	return;		// solid

	if(r_minimap_zoom->value>=0.1) {
		distance = 1024.0/r_minimap_zoom->value;
	} else {
		distance = 1024.0;
	}

	if ( r_origin[0]+distance < node->minmaxs[0] ||
		 r_origin[0]-distance > node->minmaxs[3] ||
		 r_origin[1]+distance < node->minmaxs[1] ||
		 r_origin[1]-distance > node->minmaxs[4] ||
		 r_origin[2]+256 < node->minmaxs[2] ||
		 r_origin[2]-256 > node->minmaxs[5]) return;
  
	// if a leaf node, draw stuff
	if (node->contents != -1) {
		pleaf = (mleaf_t *)node;
		// check for door connected areas
		if (r_newrefdef.areabits) {
			// not visible
			if (! (r_newrefdef.areabits[pleaf->area>>3] & (1<<(pleaf->area&7)) ) ) return;
		}
		mark = pleaf->firstmarksurface;
		c = pleaf->nummarksurfaces;

		if (c) {
			do {
				(*mark)->visframe = r_framecount;
				mark++;
			} while (--c);
		}
		return;
	}

// node is just a decision point, so go down the apropriate sides
// find which side of the node we are on
	plane = node->plane;  

	switch (plane->type) {
	case PLANE_X:
		dot = modelorg[0] - plane->dist;
		break;
	case PLANE_Y:
		dot = modelorg[1] - plane->dist;
		break;
	case PLANE_Z:
		dot = modelorg[2] - plane->dist;
		break;
	default:
		dot = DotProduct (modelorg, plane->normal) - plane->dist;
		break;
	}

	if (dot >= 0) {
		side = 0;
		sidebit = 0;
	} else {
		side = 1;
		sidebit = SURF_PLANEBACK;
	}

// recurse down the children, front side first
	R_RecursiveRadarNode (node->children[side]);

  if(plane->normal[2]) {      
		// draw stuff    
		if(plane->normal[2]>0) for ( c = node->numsurfaces, surf = r_worldmodel->surfaces + node->firstsurface; c ; c--, surf++)
		{
			if (surf->texinfo->flags & SURF_SKY){
				continue;
			}
		

		}
	} else {
			qglDisable(GL_TEXTURE_2D);
		for ( c = node->numsurfaces, surf = r_worldmodel->surfaces + node->firstsurface; c ; c--, surf++) {
			float sColor,C[4];
			if (surf->texinfo->flags & SURF_SKY) continue;
			
			if (surf->texinfo->flags & (SURF_WARP|SURF_FLOWING|SURF_TRANS33|SURF_TRANS66)) {
				sColor=0.5;
			} else {
				sColor=0;
			}
		      
			for ( p = surf->polys; p; p = p->chain ) {
				v = p->verts[0];      
				qglBegin(GL_LINE_STRIP);
				for (i=0 ; i< p->numverts; i++, v+= VERTEXSIZE) {
					C[3]= (v[2]-r_origin[2])/512.0;        
					if (C[3]>0) {     
							
						C[0]=0.5;
						C[1]=0.5+sColor;
						C[2]=0.5;
						C[3]=1-C[3]; 
                      
					}
					   else 
					{  
						C[0]=0.5;
						C[1]=sColor;
						C[2]=0;
						C[3]+=1;
						
					}

					if(C[3]<0) {
						C[3]=0;  
						
					}
					qglColor4fv(C);
					qglVertex3fv (v);	
				}
								
				qglEnd();
			}   	      
		}
		qglEnable(GL_TEXTURE_2D);
	
  }
	// recurse down the back side
	R_RecursiveRadarNode (node->children[!side]);

	
}



int			numRadarEnts=0;
RadarEnt_t	RadarEnts[MAX_RADAR_ENTS];

void GL_DrawRadar(void)
{  
	int		i;
	float	fS[4]={0,0,-1.0/512.0,0};  
    
	if ( r_newrefdef.rdflags & RDF_NOWORLDMODEL ) return;
	if(!r_minimap->value) return;

	qglViewport (vid.width-r_minimap_size->value,0, r_minimap_size->value, r_minimap_size->value);  
	
	qglDisable(GL_DEPTH_TEST);
  	qglMatrixMode(GL_PROJECTION);
	qglPushMatrix();
	qglLoadIdentity ();	    

	
	if (r_minimap_style->value) {
		qglOrtho(-1024,1024,-1024,1024,-256,256);
	} else {
		qglOrtho(-1024,1024,-512,1536,-256,256);
	}

	qglMatrixMode(GL_MODELVIEW);  
	qglPushMatrix();  
	qglLoadIdentity ();

    	
		{
		qglStencilMask(255);
		qglClear(GL_STENCIL_BUFFER_BIT);
		qglEnable(GL_STENCIL_TEST);
		qglStencilFunc(GL_ALWAYS,4,4);
		qglStencilOp(GL_KEEP,GL_KEEP,GL_REPLACE);

	  
		GLSTATE_ENABLE_ALPHATEST;
		qglAlphaFunc(GL_LESS,0.1);
		qglColorMask(0,0,0,0);
	    
		qglColor4f(1,1,1,1);
		if(r_around)
			GL_Bind(r_around->texnum);
		qglBegin(GL_TRIANGLE_FAN);
		if (r_minimap_style->value){
			qglTexCoord2f(0,1); qglVertex3f(1024,-1024,1);
			qglTexCoord2f(1,1); qglVertex3f(-1024,-1024,1);
			qglTexCoord2f(1,0); qglVertex3f(-1024,1024,1);
			qglTexCoord2f(0,0); qglVertex3f(1024,1024,1);
		} else {
			qglTexCoord2f(0,1); qglVertex3f(1024,-512,1);
			qglTexCoord2f(1,1); qglVertex3f(-1024,-512,1);
			qglTexCoord2f(1,0); qglVertex3f(-1024,1536,1);
			qglTexCoord2f(0,0); qglVertex3f(1024,1536,1);
		}
		qglEnd();    

		qglColorMask(1,1,1,1);
		GLSTATE_DISABLE_ALPHATEST;
		qglAlphaFunc(GL_GREATER, 0.5);
		qglStencilOp(GL_KEEP,GL_KEEP,GL_REPLACE);
		qglStencilFunc(GL_NOTEQUAL,4,4);

	}

	if(r_minimap_zoom->value>=0.1) {
		qglScalef(r_minimap_zoom->value,r_minimap_zoom->value,r_minimap_zoom->value); 
	}

	if (r_minimap_style->value) {
		qglPushMatrix();  
		qglRotatef (90-r_newrefdef.viewangles[1],  0, 0, -1);        
		
		qglDisable(GL_TEXTURE_2D);
		qglBegin(GL_TRIANGLES);  
		qglColor4f(1,1,1,0.5);
		qglVertex3f(0,32,0);
		qglColor4f(1,1,0,0.5);
		qglVertex3f(24,-32,0);
		qglVertex3f(-24,-32,0);
		qglEnd();
		
		qglPopMatrix();  
	} else {
		qglDisable(GL_TEXTURE_2D);
		qglBegin(GL_TRIANGLES);  
		qglColor4f(1,1,1,0.5);
		qglVertex3f(0,32,0);
		qglColor4f(1,1,0,0.5);
		qglVertex3f(24,-32,0);
		qglVertex3f(-24,-32,0);
		qglEnd();

		qglRotatef (90-r_newrefdef.viewangles[1],  0, 0, 1);        
	}
	qglTranslatef (-r_newrefdef.vieworg[0],  -r_newrefdef.vieworg[1],  -r_newrefdef.vieworg[2]);  

/*	if(!deathmatch->value)
	{
	qglBegin(GL_QUADS);
	for(i=0;i<numRadarEnts;i++){
	float x=RadarEnts[i].org[0];
	float y=RadarEnts[i].org[1];
	float z=RadarEnts[i].org[2];
	qglColor4fv(RadarEnts[i].color);    
	
    qglVertex3f(x+9, y+9, z);
    qglVertex3f(x+9, y-9, z);
    qglVertex3f(x-9, y-9, z);
    qglVertex3f(x-9, y+9, z);
	}  
	qglEnd();
	}*/

	qglEnable(GL_TEXTURE_2D);
	  
	if(r_radarmap)
		GL_Bind(r_radarmap->texnum);
	qglBlendFunc(GL_SRC_ALPHA, GL_ONE);
	GLSTATE_ENABLE_BLEND;
	qglColor3f(1,1,1);
	  
	fS[3]=0.5+ r_newrefdef.vieworg[2]/512.0;
	qglTexGenf(GL_S,GL_TEXTURE_GEN_MODE,GL_OBJECT_LINEAR);
		
	GLSTATE_ENABLE_TEXGEN;
	qglTexGenfv(GL_S,GL_OBJECT_PLANE,fS);

	// draw the stuff
	R_RecursiveRadarNode (r_worldmodel->nodes);  

	qglBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
	GLSTATE_DISABLE_TEXGEN;

	qglPopMatrix();
    
	qglViewport(0,0,vid.width,vid.height);
	  
	qglMatrixMode(GL_PROJECTION);
	qglPopMatrix();
	qglMatrixMode(GL_MODELVIEW);
	qglDisable(GL_STENCIL_TEST);
	GL_TexEnv( GL_REPLACE );
	GLSTATE_DISABLE_BLEND;
	qglEnable(GL_DEPTH_TEST);
    qglColor4f(1,1,1,1);

}

