/*
Copyright (C) 2010 COR Entertainment, LLC.

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

#include "r_local.h"
#include "r_ragdoll.h"

//This file will handle all of the ragdoll calculations, etc.

//Notes:

//There are alot of tutorials in which to glean this information.  As best I can tell we will have to do the following

//We will need to get a velocity vector.  This could be done by either calculating the distance from entity origin's last frame, or
//by some other means such as a nearby effect like a rocket explosion to get the vector.  We also may need to figure out which part
//of the ragdoll to apply this velocity to - ie, say an explosion hits their feet rather than their torso.  For starting out, let's 
//concentrate on the torso.

//Once the frames are at the death stage, the ragdoll takes over.

//From what I best understand from reading the tuts, we have to build a "world" in which things collide.  These would be done 
//by the existing bsp drawing routines, and simply putting some calls before the vertex arrays are built.  This will use the 
//ODE trimesh functions.  These are not completely straightforward, as there appears to be a need for the verts to be done with 
//indices so that normals are correctly built.

//The ragdoll will need to either be hardcoded in, or read in at load time.  To begin, we will
//hardcode one in.  Since our playermodels use roughly the same skeleton, this shouldn't be too bad for initial testing.

//So how does the ragoll then animate the mesh?  We need to get rotation and location vectors from the
//ragdoll, and apply them to the models skeleton.  We would need to use the same tech we use for player pitch spine bending to 
//manipulate the skeleton.  We get rotation and location of body parts, and that is it.

//This data will need to be individually linked to each entity.  

//This tutorial is straightforward and useful, and though it's in python, it's easily translated into C code.

//There are several examples at http://opende.sourceforge.net/wiki/index.php/HOWTO_rag-doll

//Stage 1 - math funcs needed

signed int sign(signed int x)
{
	if( x > 0.0 )
		return 1.0;
	else
		return -1.0;
}

float len3(vec3_t v)
{
	return VectorLength(v);
}

vec_t* neg3(vec3_t v)
{
	vec_t *rv = NULL;

	rv[0] = -v[0];
	rv[1] = -v[1];
	rv[2] = -v[2];

	return rv;
}

vec_t* add3(vec3_t a, vec3_t b)
{
	vec_t *rv = NULL;

	rv[0] = a[0] + b[0];
	rv[1] = a[1] + b[1];
	rv[2] = a[2] + b[2];
	
	return rv;
}

vec_t* sub3(vec3_t a, vec3_t b)
{
	vec_t *rv = NULL;

	rv[0] = a[0] - b[0];
	rv[1] = a[1] - b[1];
	rv[2] = a[2] - b[2];

	return rv;
}

vec_t* mul3(vec3_t v, float s)
{
	vec_t *rv = NULL;

	rv[0] = v[0] * s;
	rv[1] = v[1] * s;
	rv[2] = v[2] * s;

	return rv;
}

vec_t* div3(vec3_t v, float s)
{
	vec_t *rv = NULL;

	rv[0] = v[0] / s;
	rv[1] = v[1] / s;
	rv[2] = v[2] / s;

	return rv;
}

float dist3(vec3_t a, vec3_t b)
{
	return VectorLength(sub3(a, b));
}

vec_t* norm3(vec3_t v)
{
	vec_t *rv = NULL;
	float l = len3(v);

	if(l > 0.0)
	{
		rv[0] = v[0] / l;
		rv[1] = v[1] / l;
		rv[2] = v[2] / l;

		return rv;
	}
	else
	{
		VectorClear(rv);
		return rv;
	}
}

float dot3(vec3_t a, vec3_t b)
{
	return(DotProduct(a, b));
}

vec_t* cross(vec3_t a, vec3_t b)
{
	vec_t *crossP = NULL;
	
	CrossProduct(a, b, crossP);
	return crossP;
}

vec_t* project3(vec3_t v, vec3_t d)
{
	return mul3(v, dot3(norm3(v), d));
}

double acosdot3(vec3_t a, vec3_t b)
{
	double x;

	x = dot3(a, b);
	if(x < -1.0)
		return pi;
	else if(x > 1.0)
		return 0.0;
	else
		return acos(x);
}

vec_t* rotate3(matrix3x3_t m, vec3_t v)
{
	vec_t *rv = NULL;

	rv[0] = v[0] * m.a[0] + v[1] * m.a[1] + v[2] * m.a[2];
	rv[1] = v[0] * m.b[0] + v[1] * m.b[1] + v[2] * m.b[2];
	rv[2] = v[0] * m.c[0] + v[1] * m.c[1] + v[2] * m.c[2];

	return rv;
}

matrix3x3_t* invert3x3(matrix3x3_t m)
{
	matrix3x3_t *rm = NULL;

	rm->a[0] = m.a[0];
	rm->a[1] = m.b[0];
	rm->a[2] = m.c[0];
	rm->b[0] = m.a[1];
	rm->b[1] = m.b[1];
	rm->b[2] = m.c[1];
	rm->c[0] = m.a[2];
	rm->c[1] = m.b[2];
	rm->c[2] = m.c[2];

	return rm;
}

vec_t* zaxis(matrix3x3_t m)
{
	vec_t *rm = NULL;

	rm[0] = m.a[2];
	rm[1] = m.b[2];
	rm[2] = m.c[2];

	return rm;
}

matrix3x3_t* calcRotMatrix(vec3_t axis, float angle)
{
	float cosTheta = cosf(angle);
	float sinTheta = sinf(angle);
	float t = 1.0 - cosTheta;

	matrix3x3_t *rm = NULL;

	rm->a[0] = t * pow(axis[0],2) + cosTheta;
	rm->a[1] = t * axis[0] * axis[1] - sinTheta * axis[2];
	rm->a[2] = t * axis[0] * axis[2] + sinTheta * axis[1];
	rm->b[0] = t * axis[0] * axis[1] + sinTheta * axis[2];
	rm->b[1] = t * pow(axis[1], 2) + cosTheta;
	rm->b[2] = t * axis[1] * axis[2] - sinTheta * axis[0];
	rm->c[0] = t * axis[0] * axis[2] - sinTheta * axis[1];
	rm->c[1] = t * axis[1] * axis[2] + sinTheta * axis[0];
	rm->c[2] = t * pow(axis[2], 2) + cosTheta;

	return rm;
}

matrix4x4_t* makeOpenGLMatrix(matrix3x3_t r, vec3_t p)
{
	matrix4x4_t *rm = NULL;

	rm->a[0] = r.a[0];
	rm->a[1] = r.b[0];
	rm->a[2] = r.c[0];
	rm->a[3] = 0.0;
	rm->b[0] = r.a[1];
	rm->b[1] = r.b[1];
	rm->b[2] = r.c[1];
	rm->b[3] = 0.0;
	rm->c[0] = r.a[2];
	rm->c[1] = r.b[2];
	rm->c[2] = r.c[2];
	rm->c[3] = 0.0;
	rm->d[0] = p[0];
	rm->d[1] = p[1];
	rm->d[2] = p[2];
	rm->d[3] = 1.0;

	return rm;
}

//routine to create ragdoll body parts between two joints
void R_addBody(char *name, int objectID, vec3_t p1, vec3_t p2, float radius, float density)
{
	//Adds a capsule body between joint positions p1 and p2 and with given
	//radius to the ragdoll.
	float length;
	vec3_t xa, ya, za, temp;
	dMatrix3 rot;

	p1 = add3(p1, currententity->origin);
	p2 = add3(p2, currententity->origin);

	//cylinder length not including endcaps, make capsules overlap by half
	//radius at joints
	length = dist3(p1, p2) - radius;

	//create body id
	RagDollObject[objectID].body = dBodyCreate(RagDollWorld);

	//set it's mass
	dMassSetCapsule (&RagDollObject[objectID].mass, density, 3, radius, length); 
	dBodySetMass(RagDollObject[objectID].body, &RagDollObject[objectID].mass);

	//creat the geometry and give it a name
	RagDollObject[objectID].geom = dCreateCapsule (RagDollSpace, radius, length);
	dGeomSetData(RagDollObject[objectID].geom, name);
	dGeomSetBody(RagDollObject[objectID].geom, RagDollObject[objectID].body);

	//define body rotation automatically from body axis
	VectorCopy(norm3(sub3(p2, p1)), za);
		
	VectorSet(temp, 1.0, 0.0, 0.0);
	if (abs(dot3(za, temp)) < 0.7)
		VectorSet(xa, 1.0, 0.0, 0.0);
	else
		VectorSet(xa, 0.0, 1.0, 0.0);
		
	VectorCopy(cross(za, xa), ya);

	VectorCopy(norm3(cross(ya, za)), xa);
	VectorCopy(cross(za, xa), ya);	

	rot[0] = xa[0];
	rot[1] = ya[0];
	rot[2] = za[0];
	rot[3] = xa[1];
	rot[4] = ya[1];
	rot[5] = za[1];
	rot[6] = xa[2];
	rot[7] = ya[2];
	rot[8] = za[2];

	VectorCopy(mul3(add3(p1, p2), 0.5), temp);
	dGeomSetPosition(RagDollObject[objectID].geom, temp[0], temp[1], temp[2]);
	dGeomSetRotation(RagDollObject[objectID].geom, rot);
}

R_CreateWorldObject( void )
{
	// Initialize the world
	RagDollWorld = dWorldCreate();
	dWorldSetGravity(RagDollWorld, 0.0, -4.0, 0.0);

	RagDollSpace = dSimpleSpaceCreate(0);

	contactGroup = dJointGroupCreate(0);
}

R_DestroyWorldObject( void )
{
	dWorldDestroy(RagDollWorld);
}

//set initial position of ragdoll - note this will need to get some information from the skeleton on the first death frame
void R_RagdollBody_Init( void )
{
	vec3_t temp;

	//set upper body
	VectorSet(R_SHOULDER_POS, -SHOULDER_W * 0.5, SHOULDER_H, 0.0);
	VectorSet(L_SHOULDER_POS, SHOULDER_W * 0.5, SHOULDER_H, 0.0);

	VectorSet(temp, UPPER_ARM_LEN, 0.0, 0.0);
	VectorCopy(sub3(R_SHOULDER_POS, temp), R_ELBOW_POS);
	VectorCopy(add3(L_SHOULDER_POS, temp), L_ELBOW_POS);
	VectorSet(temp, FORE_ARM_LEN, 0.0, 0.0);
	VectorCopy(sub3(R_ELBOW_POS, temp), R_WRIST_POS);
	VectorCopy(add3(L_ELBOW_POS, temp), L_WRIST_POS);
	VectorSet(temp, HAND_LEN, 0.0, 0.0);
	VectorCopy(sub3(R_WRIST_POS, temp), R_FINGERS_POS);
	VectorCopy(add3(L_WRIST_POS, temp), L_FINGERS_POS);

	//set lower body
	VectorSet(R_HIP_POS, -LEG_W * 0.5, HIP_H, 0.0);
	VectorSet(L_HIP_POS, LEG_W * 0.5, HIP_H, 0.0);
	VectorSet(R_KNEE_POS, -LEG_W * 0.5, KNEE_H, 0.0);
	VectorSet(L_KNEE_POS, LEG_W * 0.5, KNEE_H, 0.0);
	VectorSet(R_ANKLE_POS, -LEG_W * 0.5, ANKLE_H, 0.0);
	VectorSet(L_ANKLE_POS, LEG_W * 0.5, ANKLE_H, 0.0);

	VectorSet(temp, 0.0, 0.0, HEEL_LEN);
	VectorCopy(sub3(R_ANKLE_POS, temp), R_HEEL_POS);
	VectorCopy(sub3(L_ANKLE_POS, temp), L_HEEL_POS);
	VectorSet(temp, 0.0, 0.0, FOOT_LEN);
	VectorCopy(add3(R_ANKLE_POS, temp), R_TOES_POS);
	VectorCopy(add3(L_ANKLE_POS, temp), L_TOES_POS);
}

/*
	Callback function for the collide() method.

	This function checks if the given geoms do collide and creates contact
	joints if they do.
*/
void near_callback(dGeomID geom1, dGeomID geom2)
{
	dContact contact[MAX_CONTACTS];
	int i;
	dJointID j;
	dBodyID body1 = dGeomGetBody(geom1);
	dBodyID body2 = dGeomGetBody(geom2);

	if (dAreConnected(body1, body2))
		return;

	// create contact joints
	for (i = 0; i < MAX_CONTACTS; i++) {
		contact[i].surface.mode = dContactBounce; // Bouncy surface
		contact[i].surface.bounce = 0.2;
		contact[i].surface.mu = 500.0; // Friction

		j = dJointCreateContact(RagDollWorld, contactGroup, &contact[i]);
		dJointAttach(j, body1, body2);
	}
}