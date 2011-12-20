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

#if defined WIN32_VARIANT
#  include <windows.h>
#endif

#include <stdio.h>

#include <GL/gl.h>
#include <math.h>
#include "glext.h"

#ifndef GL_COLOR_INDEX8_EXT
#define GL_COLOR_INDEX8_EXT GL_COLOR_INDEX
#endif

#include "client/ref.h"
#include "client/vid.h"

#include "qgl.h"
#include "r_math.h"

#if !defined min
#define min(a,b) (((a)<(b)) ? (a) : (b))
#endif

// up / down
#define	PITCH	0

// left / right
#define	YAW		1

// fall over
#define	ROLL	2

#define	DLIGHT_CUTOFF	64

extern	viddef_t	vid;

#include "r_image.h"

//===================================================================

typedef enum
{
	rserr_ok,

	rserr_invalid_fullscreen,
	rserr_invalid_mode,

	rserr_unknown
} rserr_t;

#include "r_model.h"

extern float	r_frametime;

void GL_BeginRendering (int *x, int *y, int *width, int *height);
void GL_EndRendering (void);

void GL_SetDefaultState( void );
void GL_UpdateSwapInterval( void );

extern	float	gldepthmin, gldepthmax;

#define	MAX_LBM_HEIGHT		512

#define BACKFACE_EPSILON	0.01

extern	entity_t	*currententity;
extern	model_t		*currentmodel;
extern	int			r_visframecount;
extern	int			r_framecount;
extern  int			r_shadowmapcount;	
extern	cplane_t	frustum[4];
extern	int			c_brush_polys, c_alias_polys;
extern 	int c_flares;
extern  int c_grasses;
extern	int c_beams;

extern	int			gl_filter_min, gl_filter_max;

//
// screen size info
//
extern	refdef_t	r_newrefdef;
extern	int		r_viewcluster, r_viewcluster2, r_oldviewcluster, r_oldviewcluster2;

extern	cvar_t	*r_norefresh;
extern	cvar_t	*r_lefthand;
extern	cvar_t	*r_drawentities;
extern	cvar_t	*r_drawworld;
extern	cvar_t	*r_speeds;
extern	cvar_t	*r_fullbright;
extern	cvar_t	*r_novis;
extern	cvar_t	*r_nocull;
extern	cvar_t	*r_lerpmodels;

extern	cvar_t	*r_lightlevel;	// FIXME: This is a HACK to get the client's light level
extern  cvar_t  *r_wave; //water waves

extern cvar_t	*gl_ext_swapinterval;
extern cvar_t	*gl_ext_pointparameters;
extern cvar_t	*gl_ext_compiled_vertex_array;

extern cvar_t	*gl_particle_min_size;
extern cvar_t	*gl_particle_max_size;
extern cvar_t	*gl_particle_size;
extern cvar_t	*gl_particle_att_a;
extern cvar_t	*gl_particle_att_b;
extern cvar_t	*gl_particle_att_c;

extern	cvar_t	*gl_bitdepth;
extern	cvar_t	*gl_mode;
extern	cvar_t	*gl_log;
extern	cvar_t	*gl_lightmap;
extern	cvar_t	*gl_shadows;
extern	cvar_t	*gl_dynamic;
extern	cvar_t	*gl_nobind;
extern	cvar_t	*gl_picmip;
extern	cvar_t	*gl_skymip;
extern	cvar_t	*gl_showtris;
extern	cvar_t	*gl_showpolys;
extern	cvar_t	*gl_finish;
extern	cvar_t	*gl_clear;
extern	cvar_t	*gl_cull;
extern	cvar_t	*gl_poly;
extern	cvar_t	*gl_polyblend;
extern	cvar_t	*gl_modulate;
extern	cvar_t	*gl_drawbuffer;
extern	cvar_t	*gl_driver;
extern	cvar_t	*gl_swapinterval;
extern	cvar_t	*gl_texturemode;
extern	cvar_t	*gl_texturealphamode;
extern	cvar_t	*gl_texturesolidmode;
extern	cvar_t	*gl_lockpvs;
extern	cvar_t	*gl_vlights;
extern  cvar_t	*gl_usevbo;

extern	cvar_t	*vid_fullscreen;
extern	cvar_t	*vid_gamma;
extern  cvar_t	*vid_contrast;

extern cvar_t *r_anisotropic;
extern cvar_t *r_ext_max_anisotropy;

extern cvar_t	*r_overbrightbits;

extern cvar_t	*gl_normalmaps;
extern cvar_t	*gl_shadowmaps;
extern cvar_t	*gl_glsl_postprocess;

extern	cvar_t	*r_shaders;
extern	cvar_t	*r_bloom;
extern	cvar_t	*r_lensflare;
extern	cvar_t	*r_lensflare_intens;
extern	cvar_t	*r_drawsun;

extern  cvar_t	*r_ragdolls;
extern  cvar_t  *r_ragdoll_debug;

extern	qboolean	map_fog;
extern	char		map_music[128];
extern  unsigned	r_weather;
extern unsigned		r_nosun;

extern  cvar_t		*r_minimap;
extern  cvar_t		*r_minimap_size;
extern  cvar_t		*r_minimap_zoom;
extern  cvar_t		*r_minimap_style;

extern	cvar_t	*gl_mirror;

extern	cvar_t	*gl_arb_fragment_program;
extern	cvar_t	*gl_glsl_shaders;

extern	cvar_t	*sys_affinity;
extern	cvar_t	*sys_priority;

extern	cvar_t	*gl_screenshot_type;
extern	cvar_t	*gl_screenshot_jpeg_quality;

extern  cvar_t  *r_test;

extern	int		gl_lightmap_format;
extern	int		gl_solid_format;
extern	int		gl_alpha_format;
extern	int		gl_tex_solid_format;
extern	int		gl_tex_alpha_format;

extern	int		c_visible_lightmaps;
extern	int		c_visible_textures;

extern	float	r_world_matrix[16];
extern	float	r_project_matrix[16];
extern	int		r_viewport[4];
extern	float	r_farclip, r_farclip_min, r_farclip_bias;

//Image
extern void R_InitImageSubsystem(void);
extern void GL_Bind (int texnum);
extern void GL_MBind( GLenum target, int texnum );
extern void GL_TexEnv( GLenum value );
extern void GL_EnableMultitexture( qboolean enable );
extern void GL_SelectTexture( GLenum );
extern void vectoangles (vec3_t value1, vec3_t angles);
extern void R_LightPoint (vec3_t p, vec3_t color, qboolean addDynamic);
extern void R_PushDlights (void);
extern void R_PushDlightsForBModel (entity_t *e);
extern void SetVertexOverbrights (qboolean toggle);
extern void RefreshFont (void);

//====================================================================
extern	model_t	*r_worldmodel;

extern	unsigned	d_8to24table[256];

extern	int		registration_sequence;

extern void V_AddBlend (float r, float g, float b, float a, float *v_blend);

//Renderer main loop
extern int	R_Init( void *hinstance, void *hWnd );
extern void R_Shutdown( void );

extern void R_RenderView (refdef_t *fd);
extern void GL_ScreenShot_f (void);
extern void R_DrawAliasModel (void);
extern void R_DrawBrushModel (void);
extern void R_DrawWorld (void);
extern void R_RenderDlights (void);
extern void R_DrawAlphaSurfaces (void);
extern void R_DrawRSSurfaces(void);
extern void R_InitParticleTexture (void);
extern void R_DrawParticles (void);
extern void R_DrawRadar(void);
extern void R_DrawVehicleHUD (void);
extern void R_ClearSkyBox (void);
extern void R_DrawSkyBox (void);
extern void Draw_InitLocal (void);

//Renderer utils
extern void R_SubdivideSurface (msurface_t *fa);
extern qboolean R_CullBox (vec3_t mins, vec3_t maxs);
extern qboolean R_CullOrigin(vec3_t origin);
extern qboolean R_CullSphere( const vec3_t centre, const float radius, const int clipflags );
extern void R_RotateForEntity (entity_t *e);
extern void R_MarkLeaves (void);
extern void R_AddSkySurface (msurface_t *fa);
extern void R_RenderWaterPolys (msurface_t *fa, int texnum, float scaleX, float scaleY);
extern void R_ReadFogScript(char config_file[128]);
extern void R_ReadMusicScript(char config_file[128]);

//Lights
extern dlight_t *dynLight;
extern vec3_t lightspot;
extern void R_MarkLights (dlight_t *light, int bit, mnode_t *node);
extern void  VLight_Init (void);
extern float VLight_GetLightValue ( vec3_t normal, vec3_t dir, float apitch, float ayaw );

//BSP 
extern GLuint normalisationCubeMap;
extern void BSP_DrawTexturelessPoly (msurface_t *fa);
extern void BSP_DrawTexturelessBrushModel (entity_t *e);
extern void BSP_RenderBrushPoly (msurface_t *fa);

//Postprocess
void R_GLSLPostProcess(void);
void R_FB_InitTextures(void);

//VBO
extern GLuint vboId;
extern int totalVBOsize;
extern int	vboPosition;
extern void R_LoadVBOSubsystem(void);
extern void R_VCShutdown(void);
extern void VB_VCInit(void);
extern void VB_BuildVBOBufferSize(msurface_t *surf);
extern void VB_BuildWorldVBO(void);
void GL_BindVBO(vertCache_t *cache);
void GL_BindIBO(vertCache_t *cache);
vertCache_t *R_VCFindCache(vertStoreMode_t store, entity_t *ent);
vertCache_t *R_VCLoadData(vertCacheMode_t mode, int size, void *buffer, vertStoreMode_t store, entity_t *ent);

//Light Bloom
extern void R_BloomBlend( refdef_t *fd );
extern void R_InitBloomTextures( void );

//Flares and Sun
int r_numflares;
extern int c_flares;
flare_t r_flares[MAX_FLARES];
extern void Mod_AddFlareSurface (msurface_t *surf, int type );
extern void R_RenderFlares (void);
extern void R_ClearFlares(void);

vec3_t sun_origin;
qboolean spacebox;
qboolean draw_sun;
float sun_x;
float sun_y;
float sun_size;
extern void R_InitSun();
extern void R_RenderSun();

//Vegetation
int r_numgrasses;
extern int c_grasses;
grass_t r_grasses[MAX_GRASSES];
qboolean r_hasleaves;
extern void Mod_AddVegetationSurface (msurface_t *surf, int texnum, vec3_t color, float size, char name[MAX_QPATH], int type);
extern void R_DrawVegetationSurface (void);
extern void R_ClearGrasses(void);

//Light beams/volumes
int r_numbeams;
extern int c_beams;
beam_t r_beams[MAX_BEAMS];
extern void Mod_AddBeamSurface (msurface_t *surf, int texnum, vec3_t color, float size, char name[MAX_QPATH], int type, float xang, float yang,
	qboolean rotating);
extern void R_DrawBeamSurface ( void );
extern void R_ClearBeams(void);

//Player icons
extern float	scr_playericonalpha;

//Team colors
int r_teamColor;

extern void	Draw_GetPicSize (int *w, int *h, char *name);
extern void	Draw_Pic (int x, int y, char *name);
extern void	Draw_ScaledPic (int x, int y, float scale, char *pic);
extern void	Draw_StretchPic (int x, int y, int w, int h, char *name);
extern void	Draw_Char (int x, int y, int c);
extern void	Draw_ColorChar (int x, int y, int num, vec4_t color);
extern void	Draw_ScaledChar(float x, float y, int num, float scale, int from_menu);
extern void	Draw_ScaledColorChar (float x, float y, int num, vec4_t color, float scale, int from_menu);
extern void	Draw_TileClear (int x, int y, int w, int h, char *name);
extern void	Draw_Fill (int x, int y, int w, int h, int c);
extern void	Draw_FadeScreen (void);

extern void	R_BeginFrame( float camera_separation );
extern void	R_SwapBuffers( int );
extern void	R_SetPalette ( const unsigned char *palette);
extern int	Draw_GetPalette (void);

extern void	GL_ResampleTexture (unsigned *in, int inwidth, int inheight, unsigned *out,  int outwidth, int outheight);

image_t *R_RegisterSkin (char *name);
image_t *R_RegisterParticlePic(const char *name);
image_t *R_RegisterParticleNormal(const char *name);
image_t *R_RegisterGfxPic(const char *name);

extern void	LoadPCX (char *filename, byte **pic, byte **palette, int *width, int *height);
extern image_t *GL_LoadPic (char *name, byte *pic, int width, int height, imagetype_t type, int bits);
extern image_t *GL_GetImage( const char * name );
extern image_t	*GL_FindImage (char *name, imagetype_t type);
extern void	GL_FreeImage( image_t * image );
extern void	GL_TextureMode( char *string );
extern void	GL_ImageList_f (void);

extern void	GL_InitImages (void);
extern void	GL_ShutdownImages (void);

extern void	GL_FreeUnusedImages (void);

extern void	GL_TextureAlphaMode( char *string );
extern void	GL_TextureSolidMode( char *string );

/*
** GL config stuff
*/
#define GL_RENDERER_VOODOO		0x00000001
#define GL_RENDERER_VOODOO2   	0x00000002
#define GL_RENDERER_VOODOO_RUSH	0x00000004
#define GL_RENDERER_BANSHEE		0x00000008
#define	GL_RENDERER_3DFX		0x0000000F

#define GL_RENDERER_PCX1		0x00000010
#define GL_RENDERER_PCX2		0x00000020
#define GL_RENDERER_PMX			0x00000040
#define	GL_RENDERER_POWERVR		0x00000070

#define GL_RENDERER_PERMEDIA2	0x00000100
#define GL_RENDERER_GLINT_MX	0x00000200
#define GL_RENDERER_GLINT_TX	0x00000400
#define GL_RENDERER_3DLABS_MISC	0x00000800
#define	GL_RENDERER_3DLABS		0x00000F00

#define GL_RENDERER_REALIZM		0x00001000
#define GL_RENDERER_REALIZM2	0x00002000
#define	GL_RENDERER_INTERGRAPH	0x00003000

#define GL_RENDERER_3DPRO		0x00004000
#define GL_RENDERER_REAL3D		0x00008000
#define GL_RENDERER_RIVA128		0x00010000
#define GL_RENDERER_DYPIC		0x00020000

#define GL_RENDERER_V1000		0x00040000
#define GL_RENDERER_V2100		0x00080000
#define GL_RENDERER_V2200		0x00100000
#define	GL_RENDERER_RENDITION	0x001C0000

#define GL_RENDERER_O2          0x00100000
#define GL_RENDERER_IMPACT      0x00200000
#define GL_RENDERER_RE			0x00400000
#define GL_RENDERER_IR			0x00800000
#define	GL_RENDERER_SGI			0x00F00000

#define GL_RENDERER_MCD			0x01000000
#define GL_RENDERER_OTHER		0x80000000

#define GLSTATE_DISABLE_ALPHATEST	if (gl_state.alpha_test) { qglDisable(GL_ALPHA_TEST); gl_state.alpha_test=false; }
#define GLSTATE_ENABLE_ALPHATEST	if (!gl_state.alpha_test) { qglEnable(GL_ALPHA_TEST); gl_state.alpha_test=true; }

#define GLSTATE_DISABLE_BLEND		if (gl_state.blend) { qglDisable(GL_BLEND); gl_state.blend=false; }
#define GLSTATE_ENABLE_BLEND		if (!gl_state.blend) { qglEnable(GL_BLEND); gl_state.blend=true; }

#define GLSTATE_DISABLE_TEXGEN		if (gl_state.texgen) { qglDisable(GL_TEXTURE_GEN_S); qglDisable(GL_TEXTURE_GEN_T); qglDisable(GL_TEXTURE_GEN_R); gl_state.texgen=false; }
#define GLSTATE_ENABLE_TEXGEN		if (!gl_state.texgen) { qglEnable(GL_TEXTURE_GEN_S); qglEnable(GL_TEXTURE_GEN_T); qglEnable(GL_TEXTURE_GEN_R); gl_state.texgen=true; }

#define GL_COMBINE                        0x8570
#define GL_DOT3_RGB                       0x86AE

typedef struct
{
	int         renderer;
	const char *renderer_string;
	const char *vendor_string;
	const char *version_string;
	const char *extensions_string;
	qboolean	allow_cds;
	qboolean 	mtexcombine;
} glconfig_t;

typedef struct
{
	float		inverse_intensity;
	qboolean	fullscreen;

	int			prev_mode;

	unsigned char *d_16to8table;

	int			lightmap_textures;

	int			currenttextures[3];
	int			currenttmu;

	float		camera_separation;
	qboolean	stereo_enabled;

	qboolean	alpha_test;
	qboolean	blend;
	qboolean	texgen;
	qboolean	fragment_program;
	qboolean	glsl_shaders;
	qboolean	separateStencil;
	qboolean	stencil_wrap;
	qboolean	vbo;
	qboolean	fbo;
	qboolean	hasFBOblit;

} glstate_t;

extern glconfig_t  gl_config;
extern glstate_t   gl_state;

// vertex arrays
extern int KillFlags;

#define MAX_ARRAY (MAX_PARTICLES*4)

#define VA_SetElem2(v,a,b)		((v)[0]=(a),(v)[1]=(b))
#define VA_SetElem3(v,a,b,c)	((v)[0]=(a),(v)[1]=(b),(v)[2]=(c))
#define VA_SetElem4(v,a,b,c,d)	((v)[0]=(a),(v)[1]=(b),(v)[2]=(c),(v)[3]=(d))

extern float	tex_array[MAX_ARRAY][2];
extern float	vert_array[MAX_ARRAY][3];
extern float	norm_array[MAX_ARRAY][3];
extern float	tan_array[MAX_ARRAY][4];
extern float	col_array[MAX_ARRAY][4];

#define MAX_VARRAY_VERTS (MAX_VERTS + 2)
#define MAX_VARRAY_VERTEX_SIZE 11

#define MAX_VERTICES 16384
#define MAX_INDICES	(MAX_VERTICES * 4)
#define MAX_SHADOW_VERTS 16384

extern float VArrayVerts[MAX_VARRAY_VERTS * MAX_VARRAY_VERTEX_SIZE];
extern int VertexSizes[];
extern float *VArray;
extern vec3_t ShadowArray[MAX_SHADOW_VERTS];

// define our vertex types
#define VERT_SINGLE_TEXTURED			0		// verts and st for 1 tmu
#define VERT_BUMPMAPPED					1		// verts and st for 1 tmu, but with 2 texcoord pointers
#define VERT_MULTI_TEXTURED				2		// verts and st for 2 tmus
#define VERT_COLOURED_UNTEXTURED		3		// verts and colour
#define VERT_COLOURED_TEXTURED			4		// verts, st for 1 tmu and colour
#define VERT_COLOURED_MULTI_TEXTURED	5		// verts, st for 2 tmus and colour
#define VERT_DUAL_TEXTURED				6		// verts, st for 2 tmus both with same st
#define VERT_NO_TEXTURE					7		// verts only, no textures
#define VERT_BUMPMAPPED_COLOURED		8		// verts and st for 1 tmu, 2 texoord pointers and colour
#define VERT_NORMAL_COLOURED_TEXTURED	9		// verts and st for 1tmu and color, with normals

#if 0
// vertex array kill flags
#define KILL_TMU0_POINTER	1
#define KILL_TMU1_POINTER	2
#define KILL_TMU2_POINTER	3
#define KILL_TMU3_POINTER	4
#define KILL_RGBA_POINTER	5
#define KILL_NORMAL_POINTER 6
#else
// looks like these should be bit flags (2010-08)
#define KILL_TMU0_POINTER	0x01
#define KILL_TMU1_POINTER	0x02
#define KILL_TMU2_POINTER	0x04
#define KILL_TMU3_POINTER	0x08
#define KILL_RGBA_POINTER	0x10
#define KILL_NORMAL_POINTER 0x20
#endif

// vertex array subsystem
extern void R_InitVArrays (int varraytype);
extern void R_KillVArrays (void);
extern void R_InitQuadVarrays(void);
extern void R_AddSurfToVArray (msurface_t *surf);
extern void R_AddTexturedSurfToVArray (msurface_t *surf, float scroll);
extern void R_AddLightMappedSurfToVArray (msurface_t *surf, float scroll);
extern void R_AddGLSLShadedWarpSurfToVArray (msurface_t *surf, float scroll);
extern void R_KillNormalTMUs(void);

//shadows
extern	cvar_t		*r_shadowmapratio;
extern  int			r_lightgroups;
extern  image_t		*r_depthtexture;
extern	image_t		*r_depthtexture2;
extern  image_t		*r_colorbuffer;
extern  image_t		*r_shadowbuffer;
extern	image_t		*r_shadowbufferBlur;
extern GLuint   fboId[3];
extern GLuint	rboId;
extern vec3_t	r_worldLightVec;
extern qboolean have_stencil;
typedef struct	LightGroup {
	vec3_t	group_origin;
	vec3_t	accum_origin;
	float	avg_intensity;
	float	dist;
} LightGroup_t;
extern			LightGroup_t LightGroups[MAX_LIGHTS];

extern void		R_GenerateShadowFBO(void);
extern void		R_InitShadowSubsystem(void);
extern void		MD2_DrawCaster (void);
extern void		IQM_DrawCaster (void);
extern void		IQM_DrawRagDollCaster (int);
extern void		R_DrawDynamicCaster(void);
extern void		R_DrawVegetationCaster(void);
extern void		R_CastShadow(void);
extern float	SHD_ShadowLight (vec3_t pos, vec3_t angles, vec3_t lightAdd, int type);
int				FB_texture_width, FB_texture_height;

//shader programs
extern void R_LoadARBPrograms(void);
extern void	R_LoadGLSLPrograms(void);

//arb fragment
extern unsigned int g_water_program_id;

//glsl
extern GLhandleARB	g_programObj;
extern GLhandleARB	g_waterprogramObj;
extern GLhandleARB	g_meshprogramObj;
extern GLhandleARB	g_fbprogramObj;
extern GLhandleARB	g_blurprogramObj;
extern GLhandleARB	g_rblurprogramObj;

extern GLhandleARB	g_vertexShader;
extern GLhandleARB	g_fragmentShader;

//standard bsp surfaces
extern GLuint		g_location_surfTexture;
extern GLuint		g_location_eyePos;
extern GLuint		g_tangentSpaceTransform;
extern GLuint		g_location_heightTexture;
extern GLuint		g_location_lmTexture;
extern GLuint		g_location_normalTexture;
extern GLuint		g_location_bspShadowmapTexture;
extern GLuint		g_location_bspShadowmapTexture2;
extern GLuint		g_location_bspShadowmapNum;
extern GLuint		g_location_fog;
extern GLuint		g_location_parallax;
extern GLuint		g_location_dynamic;
extern GLuint		g_location_shadowmap;
extern GLuint		g_Location_statshadow;
extern GLuint		g_location_lightPosition;
extern GLuint		g_location_staticLightPosition;
extern GLuint		g_location_lightColour;
extern GLuint		g_location_lightCutoffSquared;

//water
extern GLuint		g_location_baseTexture;
extern GLuint		g_location_normTexture;
extern GLuint		g_location_refTexture;
extern GLuint		g_location_tangent;
extern GLuint		g_location_time;
extern GLuint		g_location_lightPos;
extern GLuint		g_location_reflect;
extern GLuint		g_location_trans;
extern GLuint		g_location_fogamount;

//mesh
extern GLuint		g_location_meshlightPosition;
extern GLuint		g_location_baseTex;
extern GLuint		g_location_normTex;
extern GLuint		g_location_fxTex;
extern GLuint		g_location_color;
extern GLuint		g_location_minLight;
extern GLuint		g_location_meshNormal;
extern GLuint		g_location_meshTime;
extern GLuint		g_location_meshFog;
extern GLuint		g_location_useFX;
extern GLuint		g_location_useGlow;

//fullscreen distortion effects
extern GLuint		g_location_framebuffTex;
extern GLuint		g_location_distortTex;
extern GLuint		g_location_dParams;
extern GLuint		g_location_fxPos;

//gaussian blur
extern GLuint		g_location_scale;
extern GLuint		g_location_source;

//radial blur	
extern GLuint		g_location_rscale;
extern GLuint		g_location_rsource;
extern GLuint		g_location_rparams;

//MD2
extern void Mod_LoadMD2Model (model_t *mod, void *buffer);

//Shared mesh items
extern vec3_t	shadelight;
extern vec3_t	shadevector;
extern int		model_dlights_num;
extern m_dlight_t model_dlights[128];
extern image_t	*r_mirrortexture;
extern cvar_t	*cl_gun;
vec3_t lightPosition;
float  dynFactor;
extern void		R_GetLightVals(vec3_t origin, qboolean RagDoll, qboolean dynamic);
extern void R_ModelViewTransform(const vec3_t in, vec3_t out);
extern void GL_BlendFunction (GLenum sfactor, GLenum dfactor);

//iqm
#define pi 3.14159265
extern float modelpitch;
extern double degreeToRadian(double degree);
extern qboolean Mod_INTERQUAKEMODEL_Load(model_t *mod, void *buffer);
extern qboolean IQM_ReadSkinFile(char skin_file[MAX_OSPATH], char *skinpath);
extern void R_DrawINTERQUAKEMODEL(void);
extern void IQM_AnimateFrame(float curframe, int nextframe);
extern qboolean IQM_InAnimGroup(int frame, int oldframe);
extern int IQM_NextFrame(int frame);
extern void IQM_AnimateRagdoll(int RagDollID);
extern void IQM_DrawRagDollFrame(int RagDollID, int skinnum, float shellAlpha, int shellEffect);
extern void IQM_DrawShadow(vec3_t origin);

//Ragdoll
int r_DrawingRagDoll;
int r_SurfaceCount;

#define TURBSCALE2 (256.0 / (2 * M_PI))

// reduce runtime calcs
#define TURBSCALE 40.743665431525205956834243423364

extern float r_turbsin[];
/*
====================================================================

IMPLEMENTATION SPECIFIC FUNCTIONS

====================================================================
*/

extern void		GLimp_BeginFrame( float camera_separation );
extern void		GLimp_EndFrame( void );
extern qboolean	GLimp_Init( void *hinstance, void *hWnd );
extern void		GLimp_Shutdown( void );
extern rserr_t    	GLimp_SetMode( unsigned *pwidth, unsigned *pheight, int mode, qboolean fullscreen );
extern void		GLimp_AppActivate( qboolean active );
extern void		GLimp_EnableLogging( qboolean enable );
extern void		GLimp_LogNewFrame( void );

/*
====================================================================

IMPORTED FUNCTIONS

====================================================================
*/

#include "r_script.h"