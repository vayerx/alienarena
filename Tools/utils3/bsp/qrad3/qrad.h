
#include "cmdlib.h"
#include "mathlib.h"
#include "bspfile.h"
#include "polylib.h"
#include "threads.h"
#include "lbmlib.h"

#ifdef WIN32
#include <windows.h>
#endif

#ifdef USE_SETRLIMIT
#include <sys/resource.h>
#endif

#if defined WIN32 || (!defined USE_PTHREADS && !defined USE_SETRLIMIT)
#define STACK_CONSTRAINED
#endif

typedef enum
{
	emit_surface,
	emit_point,
	emit_spotlight,
    emit_sky
} emittype_t;

typedef struct directlight_s
{
	struct directlight_s *next;
	emittype_t	type;

	float		intensity;
	int			style;
    float       wait;
    float       adjangle;
	vec3_t		origin;
	vec3_t		color;
	vec3_t		normal;		// for surfaces and spotlights
	float		stopdot;		// for spotlights
    dplane_t    *plane;
    dleaf_t     *leaf;
    int			nodenum;

} directlight_t;


// the sum of all tranfer->transfer values for a given patch
// should equal exactly 0x10000, showing that all radiance
// reaches other patches
typedef struct
{
	unsigned short	patch;
	unsigned short	transfer;
} transfer_t;


#define	MAX_PATCHES	65000			// larger will cause 32 bit overflows

typedef struct patch_s
{
	winding_t	*winding;
	struct patch_s		*next;		// next in face
	int			numtransfers;
	transfer_t	*transfers;
    byte *trace_hit;
    
    int			nodenum;

	int			cluster;			// for pvs checking
	vec3_t		origin;
	dplane_t	*plane;

	qboolean	sky;

	vec3_t		totallight;			// accumulated by radiosity
									// does NOT include light
									// accounted for by direct lighting
	float		area;

	// illuminance * reflectivity = radiosity
	vec3_t		reflectivity;
	vec3_t		baselight;			// emissivity only

	// each style 0 lightmap sample in the patch will be
	// added up to get the average illuminance of the entire patch
	vec3_t		samplelight;
	int			samples;		// for averaging direct light
} patch_t;

extern	patch_t		*face_patches[MAX_MAP_FACES];
extern	entity_t	*face_entity[MAX_MAP_FACES];
extern	vec3_t		face_offset[MAX_MAP_FACES];		// for rotating bmodels
extern	patch_t		patches[MAX_PATCHES];
extern	unsigned	num_patches;

extern	int		leafparents[MAX_MAP_LEAFS];
extern	int		nodeparents[MAX_MAP_NODES];

extern	float	lightscale;


void MakeShadowSplits (void);

float			*texture_data[MAX_MAP_TEXINFO];
int				texture_sizes[MAX_MAP_TEXINFO][2];


qboolean		doing_texcheck;
qboolean		doing_blur;

//==============================================


void BuildVisMatrix (void);
qboolean CheckVisBit (unsigned p1, unsigned p2);

//==============================================

extern	float ambient, maxlight;

void LinkPlaneFaces (void);

// extern qboolean    nocolor;
extern float grayscale;
extern float desaturate;
extern	qboolean	extrasamples;
extern int numbounce;
extern int noblock;

extern	directlight_t	*directlights[MAX_MAP_LEAFS];

extern	byte	nodehit[MAX_MAP_NODES];

void BuildLightmaps (void);

void BuildFacelights (int facenum);

void FinalLightFace (int facenum);
void BlurFace (int facenum);
void DetectUniformColor (int facenum);

qboolean PvsForOrigin (vec3_t org, byte *pvs);

int	PointInNodenum (vec3_t point);
int TestLine (vec3_t start, vec3_t stop);
int TestLine_color (int node, vec3_t start, vec3_t stop, vec3_t occluded);
int TestLine_r (int node, vec3_t start, vec3_t stop);

void CreateDirectLights (void);

dleaf_t		*PointInLeaf (vec3_t point);


extern	dplane_t	backplanes[MAX_MAP_PLANES];
extern	int			fakeplanes;					// created planes for origin offset

extern	float	subdiv;

extern	float	direct_scale;
extern	float	entity_scale;

extern qboolean sun;
extern qboolean sun_alt_color;
extern vec3_t sun_pos;
extern float sun_main;
extern float sun_ambient;
extern vec3_t sun_color;

int	refine_amt, refine_setting;

int	PointInLeafnum (vec3_t point);
void MakeTnodes (dmodel_t *bm);
void MakePatches (void);
void SubdividePatches (void);
void PairEdges (void);
void CalcTextureReflectivity (void);

byte	*dlightdata_ptr; 
byte	dlightdata_raw[MAX_OVERRIDE_LIGHTING];
