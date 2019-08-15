/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2002-2007 Victor Luchits

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
#pragma once

#include "qcommon/types.h"
#include "r_mesh.h"
#include "r_shader.h"
#include "r_surface.h"

/*

d*_t structures are on-disk representations
m*_t structures are in-memory

*/

/*
==============================================================================

BRUSH MODELS

==============================================================================
*/

#define CLUSTER_UNKNOWN -2
#define CLUSTER_INVALID -1

//
// in memory representation
//
typedef struct {
	float radius;

	unsigned int firstModelSurface;
	unsigned int numModelSurfaces;

	unsigned int firstModelDrawSurface;
	unsigned int numModelDrawSurfaces;

	vec3_t mins, maxs;
} mmodel_t;

typedef struct mshaderref_s {
	char name[MAX_QPATH];
	int flags;
	int contents;
	shader_t *shaders[NUM_SHADER_TYPES_BSP];
} mshaderref_t;

typedef struct msurface_s {
	unsigned int facetype, flags;

	unsigned int firstDrawSurfVert, firstDrawSurfElem;

	unsigned int numInstances;

	unsigned int drawSurf;

	int fragmentframe;                  // for multi-check avoidance

	vec4_t plane;

	union {
		float origin[3];
		float mins[3];
	};
	union {
		float maxs[3];
		float color[3];
	};

	mesh_t mesh;

	instancePoint_t *instances;

	shader_t *shader;

	int superLightStyle;
} msurface_t;

typedef struct mnode_s {
	// common with leaf
	cplane_t        *plane;

	// node specific
	struct mnode_s  *children[2];
} mnode_t;

typedef struct mleaf_s {
	// common with node
	cplane_t        *plane;

	// leaf specific
	int cluster, area;

	float mins[3];
	float maxs[3];                      // for bounding box culling

	unsigned numVisSurfaces;
	unsigned *visSurfaces;

	unsigned numFragmentSurfaces;
	unsigned *fragmentSurfaces;
} mleaf_t;

typedef struct mbrushmodel_s {
	const bspFormatDesc_t *format;

	dvis_t          *pvs;

	unsigned int numsubmodels;
	mmodel_t        *submodels;
	struct model_s  *inlines;

	unsigned int numModelSurfaces;
	unsigned int firstModelSurface;

	unsigned int numModelDrawSurfaces;
	unsigned int firstModelDrawSurface;

	msurface_t      *modelSurfaces;

	unsigned int numplanes;
	cplane_t        *planes;

	unsigned int numleafs;              // number of visible leafs, not counting 0
	mleaf_t         *leafs;

	unsigned int numnodes;
	mnode_t         *nodes;

	unsigned int numsurfaces;
	msurface_t      *surfaces;

	/*unsigned*/ int numareas;

	unsigned int numDrawSurfaces;
	drawSurfaceBSP_t *drawSurfaces;

	float fogStrength;
} mbrushmodel_t;

/*
==============================================================================

ALIAS MODELS

==============================================================================
*/

//
// in memory representation
//
typedef struct {
	short point[3];
	uint8_t latlong[2];                     // use bytes to keep 8-byte alignment
} maliasvertex_t;

typedef struct {
	vec3_t mins, maxs;
	vec3_t scale;
	vec3_t translate;
	float radius;
} maliasframe_t;

typedef struct {
	char name[MD3_MAX_PATH];
	quat_t quat;
	vec3_t origin;
} maliastag_t;

typedef struct {
	char name[MD3_MAX_PATH];
	StringHash shader;
} maliasskin_t;

typedef struct maliasmesh_s {
	char name[MD3_MAX_PATH];

	int numverts;
	maliasvertex_t *vertexes;
	vec2_t          *stArray;

	vec4_t          *xyzArray;
	vec4_t          *normalsArray;
	vec4_t          *sVectorsArray;

	int numtris;
	elem_t          *elems;

	int numskins;
	maliasskin_t    *skins;

	struct mesh_vbo_s *vbo;
} maliasmesh_t;

typedef struct maliasmodel_s {
	int numframes;
	maliasframe_t   *frames;

	int numtags;
	maliastag_t     *tags;

	int nummeshes;
	maliasmesh_t    *meshes;
	drawSurfaceAlias_t *drawSurfs;

	int numskins;
	maliasskin_t    *skins;

	int numverts;             // sum of numverts for all meshes
	int numtris;             // sum of numtris for all meshes
} maliasmodel_t;

//
// Whole model
//

enum ModelType {
	mod_bad = -1,
	mod_free,
	mod_brush,
	mod_alias,
	ModelType_GLTF,
};

typedef void ( *mod_touch_t )( struct model_s *model );

typedef struct model_s {
	char *name;
	int registrationSequence;
	mod_touch_t touch;          // touching a model updates registration sequence, images and VBO's

	ModelType type;

	mat4_t transform;

	//
	// volume occupied by the model graphics
	//
	vec3_t mins, maxs;
	float radius;

	//
	// memory representation pointer
	//
	void *extradata;

	mempool_t *mempool;
} model_t;

//============================================================================

extern model_t *r_prevworldmodel;

void        R_InitModels( void );
void        R_ShutdownModels( void );
void        R_FreeUnusedModels( void );

void        R_RegisterWorldModel( const char *model );
struct model_s *R_RegisterModel( const char *name );

void		R_GetTransformBufferForMesh( mesh_t *mesh, bool positions, bool normals, bool sVectors );

model_t     *Mod_ForName( const char *name, bool crash );
mleaf_t     *Mod_PointInLeaf( const vec3_t p, mbrushmodel_t *bmodel );
uint8_t     *Mod_ClusterPVS( int cluster, mbrushmodel_t *bmodel );

unsigned int Mod_Handle( const model_t *mod );
model_t     *Mod_ForHandle( unsigned int elem );

// force 16-bytes alignment for all memory chunks allocated for model data
#define     Mod_Malloc( mod, size ) Mem_AllocExt( ( mod )->mempool, size, 1 )
#define     Mod_Realloc( data, size ) Mem_Realloc( data, size )
#define     Mod_MemFree( data ) Mem_Free( data )

void        Mod_Modellist_f( void );
