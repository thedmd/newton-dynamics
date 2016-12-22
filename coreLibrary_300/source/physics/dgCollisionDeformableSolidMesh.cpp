/* Copyright (c) <2003-2016> <Julio Jerez, Newton Game Dynamics>
* 
* This software is provided 'as-is', without any express or implied
* warranty. In no event will the authors be held liable for any damages
* arising from the use of this software.
* 
* Permission is granted to anyone to use this software for any purpose,
* including commercial applications, and to alter it and redistribute it
* freely, subject to the following restrictions:
* 
* 1. The origin of this software must not be misrepresented; you must not
* claim that you wrote the original software. If you use this software
* in a product, an acknowledgment in the product documentation would be
* appreciated but is not required.
* 
* 2. Altered source versions must be plainly marked as such, and must not be
* misrepresented as being the original software.
* 
* 3. This notice may not be removed or altered from any source distribution.
*/

#include "dgPhysicsStdafx.h"
#include "dgBody.h"
#include "dgWorld.h"
#include "dgContact.h"
#include "dgMeshEffect.h"
#include "dgDynamicBody.h"
#include "dgCollisionBVH.h"
#include "dgCollisionConvexPolygon.h"
#include "dgCollisionDeformableSolidMesh.h"



dgCollisionDeformableSolidMesh::dgCollisionDeformableSolidMesh(dgWorld* const world, dgMeshEffect* const mesh)
	:dgCollisionDeformableMesh(world, m_deformableSolidMesh)
	,m_finiteElements(world->GetAllocator())
	,m_finiteElementsCount(0)
{
	m_rtti |= dgCollisionDeformableSolidMesh_RTTI;

	dgInt32 count = mesh->GetVertexCount();
	dgVector* const points = dgAlloca (dgVector, count);

	dgAssert (mesh->HasLayers());
	for (dgInt32 i = 0; i < count; i++) {
		dgBigVector p(mesh->GetVertex(i));
		points[i] = p;
		m_finiteElementsCount = dgMax (mesh->GetVertexLayer(i), m_finiteElementsCount);
	}
	m_finiteElementsCount ++;

	m_indexToVertexMap.Resize(count);
	dgInt32* const indexToVertexMap = &m_indexToVertexMap[0];
	m_particlesCount = dgVertexListToIndexList (&points[0].m_x, sizeof (dgVector), 3 * sizeof (dgFloat32), 0, count, indexToVertexMap, dgFloat32 (1.0e-5f));
	for (dgInt32 i = 0; i < m_particlesCount; i++) {
		m_posit[i] = points[i];
	}

	dgInt32 edgeCount = 0;
	dgSoftLink* const links = dgAlloca (dgSoftLink, mesh->GetCount() / 2);
	for (void* edgePtr = mesh->GetFirstEdge(); edgePtr; edgePtr = mesh->GetNextEdge(edgePtr)) {
		dgInt32 v0;
		dgInt32 v1;
		mesh->GetEdgeIndex(edgePtr, v0, v1);
		v0 = indexToVertexMap[v0];
		v1 = indexToVertexMap[v1];
		links[edgeCount].m_m0 = dgInt16(dgMin (v0, v1));
		links[edgeCount].m_m1 = dgInt16(dgMax (v0, v1));
		edgeCount ++;
	}
	dgAssert(edgeCount == mesh->GetCount() / 2);
	dgSort(links, edgeCount, CompareEdges);

	dgInt32 uniqueEdgeCount = 0;
	for (dgInt32 i = 1; i < edgeCount; i ++) {
		if (CompareEdges (&links[i], &links[uniqueEdgeCount], NULL) > 0) {
			uniqueEdgeCount++;
			links[uniqueEdgeCount] = links[i];
		}
	}
	uniqueEdgeCount++;
	m_linksCount = uniqueEdgeCount;
	m_linkList.Resize(m_linksCount);
	for (dgInt32 i = 0; i < m_linksCount; i ++) {
		m_linkList[i] = links[i];
	}
	FinalizeBuild();

	m_finiteElements.Resize(m_finiteElementsCount);

	dgInt32 elementIndex = 0;
	dgInt32 mark = mesh->IncLRU();	
	dgMeshEffect::Iterator iter(*mesh);
	for (iter.Begin (); iter; iter ++) {
		dgEdge* const edge = &(*iter);
		dgAssert (edge->m_incidentFace > 0);
		if ((edge->m_mark < mark) && (edge->m_incidentFace > 0)) {
			dgEdge* edgeStack[32];
			dgFiniteElementCell& fem = m_finiteElements[elementIndex];
			dgAssert (elementIndex < m_finiteElementsCount);
			elementIndex ++;

			dgInt32 tetraVertex = 0;
			dgInt32 stackIndex = 1;
			edgeStack[0] = edge; 
			while (stackIndex){
				stackIndex --;
				dgEdge* const pointEdge = edgeStack[stackIndex];
				dgAssert (pointEdge->m_incidentFace > 0);
				if ((pointEdge->m_mark < mark) && (pointEdge->m_incidentFace > 0)) {
					fem.m_index[tetraVertex] = dgInt16 (m_indexToVertexMap[pointEdge->m_incidentVertex]);
					dgAssert (tetraVertex < 4);
					tetraVertex ++;
					dgEdge* edgePtr = pointEdge;
					do {
						dgAssert (mesh->GetVertexLayer(edge->m_incidentVertex) == mesh->GetVertexLayer(edgePtr->m_incidentVertex));
						if (edgePtr->m_mark != mark) {
							edgePtr->m_mark = mark;
							edgeStack[stackIndex] = edgePtr->m_twin; 
							stackIndex ++;
						}
						edgePtr = edgePtr->m_twin->m_next;
					} while (edgePtr != pointEdge);
				}
			}
		}
	}
	dgAssert (elementIndex == m_finiteElementsCount);

	for (dgInt32 i = 0; i < m_finiteElementsCount; i ++) {
		dgFiniteElementCell& fem = m_finiteElements[i];
		const dgVector& p0 = points[fem.m_index[0]];
		const dgVector& p1 = points[fem.m_index[1]];
		const dgVector& p2 = points[fem.m_index[2]];
		const dgVector& p3 = points[fem.m_index[3]];

		dgVector p01 (p0 - p1);
		dgVector p12 (p1 - p2);
		dgVector p23 (p2 - p3);
		dgFloat32 volume = (p01.DotProduct4(p12.CrossProduct3(p23))).GetScalar();
		if (volume < dgFloat32 (0.0f)) {
			volume = -volume;
			dgSwap(fem.m_index[0], fem.m_index[1]);
		}
		fem.m_restVolume = volume;
	}
}

dgCollisionDeformableSolidMesh::dgCollisionDeformableSolidMesh(const dgCollisionDeformableSolidMesh& source)
	:dgCollisionDeformableMesh(source)
	,m_finiteElements(source.m_finiteElements, source.m_finiteElementsCount)
	,m_finiteElementsCount(source.m_finiteElementsCount)
{
	m_rtti = source.m_rtti;
}

dgCollisionDeformableSolidMesh::dgCollisionDeformableSolidMesh(dgWorld* const world, dgDeserialize deserialization, void* const userData, dgInt32 revisionNumber)
	:dgCollisionDeformableMesh(world, deserialization, userData, revisionNumber)
	,m_finiteElements(world->GetAllocator())
	,m_finiteElementsCount(0)
{
	dgAssert (0);
}


dgCollisionDeformableSolidMesh::~dgCollisionDeformableSolidMesh(void)
{
}


dgInt32 dgCollisionDeformableSolidMesh::CompareEdges (const dgSoftLink* const A, const dgSoftLink* const B, void* const context)
{
	dgInt64 m0 = (dgInt64(A->m_m0)<<32) + A->m_m1;
	dgInt64 m1 = (dgInt64(B->m_m0)<<32) + B->m_m1;
	if (m0 > m1) {
		return 1;
	} else if (m0 < m1) {
		return -1;
	} 
	return 0;
}

void dgCollisionDeformableSolidMesh::DebugCollision(const dgMatrix& matrix, dgCollision::OnDebugCollisionMeshCallback callback, void* const userData) const
{
	const dgVector* const posit = &m_posit[0];
	const dgSoftLink* const links = &m_linkList[0];
	for (dgInt32 i = 0; i < m_linksCount; i++) {
		const dgInt32 j0 = links[i].m_m0;
		const dgInt32 j1 = links[i].m_m1;
		dgVector p0 (matrix.TransformVector(posit[j0]));
		dgVector p1 (matrix.TransformVector(posit[j1]));
		dgTriplex points[2];
		points[0].m_x = p0.m_x;
		points[0].m_y = p0.m_y;
		points[0].m_z = p0.m_z;
		points[1].m_x = p1.m_x;
		points[1].m_y = p1.m_y;
		points[1].m_z = p1.m_z;
		callback(userData, 2, &points[0].m_x, 0);
	}
}


void dgCollisionDeformableSolidMesh::CalculateAcceleration(dgFloat32 timestep)
{
// Ks is in [sec^-2] a spring constant unit acceleration, not a spring force acceleration. 
// Kc is in [sec^-1] a damper constant unit velocity, not a damper force acceleration. 

// for now make a share value for all springs. later this is a per material feature.
dgFloat32 kSpring = dgFloat32(1000.0f);
dgFloat32 kDamper = dgFloat32(30.0f);
dgFloat32 kVolumetricStiffness = dgFloat32(200000.0f);
kVolumetricStiffness = 0.0f;

	dgInt32 iter = 4;
	dgVector* const accel = &m_accel[0];
	dgVector* const veloc = &m_veloc[0];
	dgVector* const posit = &m_posit[0];
	const dgFloat32* const restLenght = &m_restlength[0];
	const dgFiniteElementCell* const finiteElements = &m_finiteElements[0];

	dgFloat32* const spring_A01 = dgAlloca(dgFloat32, m_linksCount);
	dgFloat32* const spring_B01 = dgAlloca(dgFloat32, m_linksCount);
//	dgFloat32* const damper_A01 = dgAlloca(dgFloat32, m_linksCount);
//	dgFloat32* const damper_B01 = dgAlloca(dgFloat32, m_linksCount);
//	dgFloat32* const damper_C01 = dgAlloca(dgFloat32, m_linksCount);
	dgVector* const dx = dgAlloca(dgVector, m_linksCount);
	dgVector* const dv = dgAlloca(dgVector, m_linksCount);

	dgVector* const collisionDir0 = dgAlloca(dgVector, m_particlesCount);
	dgVector* const collisionDir1 = dgAlloca(dgVector, m_particlesCount);
	dgVector* const collisionDir2 = dgAlloca(dgVector, m_particlesCount);
	dgVector* const collidingAccel = dgAlloca(dgVector, m_particlesCount);
	dgVector* const tmp0 = dgAlloca(dgVector, m_particlesCount);
	dgVector* const tmp1 = dgAlloca(dgVector, m_particlesCount);
	dgVector* const tmp2 = dgAlloca(dgVector, m_particlesCount);
	dgVector* const dpdv = dgAlloca(dgVector, m_linksCount);
//	dgVector* const diag = dgAlloca(dgVector, m_particlesCount);
//	dgVector* const offDiag = dgAlloca(dgVector, m_particlesCount);
	dgVector* const veloc0 = dgAlloca(dgVector, m_particlesCount);

	dgVector unitAccel(m_body->m_externalForce.CompProduct4(m_body->m_invMass.m_w));
	dgVector deltaOmega(m_body->m_invWorldInertiaMatrix.RotateVector (m_body->m_externalTorque.Scale4 (timestep)));

	m_body->m_alpha = dgVector::m_zero;
	m_body->m_omega = dgVector::m_zero;
	m_body->m_externalForce = dgVector::m_zero;
	m_body->m_externalTorque = dgVector::m_zero;

	// here I need to add all other external acceleration like wind and pressure, friction and collision.
	for (dgInt32 i = 0; i < m_particlesCount; i++) {
		veloc[i] += deltaOmega.CrossProduct3(m_posit[i]);
		veloc0[i] = veloc[i];
	}

	const dgSoftLink* const links = &m_linkList[0];
	dgFloat32 ks_dt = - timestep * kSpring;
//	dgFloat32 kd_dt0 = -timestep * kDamper;
//	dgFloat32 kd_dt1 = -timestep * kDamper * dgFloat32(2.0f);

static int xxx;
xxx ++;

	dgVector dtRK4 (timestep / iter);
	dgVector volumetricStiffness (-kVolumetricStiffness);
	HandleCollision (timestep, collisionDir0, collisionDir1, collisionDir2, collidingAccel);
	for (dgInt32 k = 0; k < iter; k ++) {

		for (dgInt32 i = 0; i < m_particlesCount; i++) {
			accel[i] = unitAccel;
			tmp0[i] = dgVector::m_zero;
		}

		for (dgInt32 i = 0; i < m_finiteElementsCount; i++) {
			const dgFiniteElementCell& fem = finiteElements[i];
			const dgInt32 i0 = fem.m_index[0];
			const dgInt32 i1 = fem.m_index[1];
			const dgInt32 i2 = fem.m_index[2];
			const dgInt32 i3 = fem.m_index[3];
			const dgVector& p0 = posit[i0];
			const dgVector& p1 = posit[i1];
			const dgVector& p2 = posit[i2];
			const dgVector& p3 = posit[i3];

			const dgVector p01(p0 - p1);
			const dgVector p12(p1 - p2);
			const dgVector p23(p2 - p3);

			dgVector area123(p12.CrossProduct3(p23));
			dgVector area0123(p23.CrossProduct3(p01));
			dgVector area012(p12.CrossProduct3(p01));

			dgFloat32 volume = (p01.DotProduct4(area123)).GetScalar();
			dgVector deltaVolume(volume - fem.m_restVolume);
			dgVector a0(deltaVolume.CompProduct4(area123));
			dgVector a1(deltaVolume.CompProduct4(area0123));
			dgVector a3(deltaVolume.CompProduct4(area012));

			tmp0[i0] += a0;
			tmp0[i1] += a1 - a0;
			tmp0[i2] -= a1 + a3;
			tmp0[i3] += a3;
		}

		for (dgInt32 i = 0; i < m_linksCount; i++) {
			const dgInt32 j0 = links[i].m_m0;
			const dgInt32 j1 = links[i].m_m1;
			dv[i] = veloc[j0] - veloc[j1];
			dx[i] = posit[j0] - posit[j1];

			const dgVector p0p1 (dx[i]);
			const dgVector v0v1 (dv[i]);
			const dgVector length2(p0p1.DotProduct4(p0p1));
			const dgVector mask(length2 > m_smallestLenght2);

			const dgVector lenght2 ((length2 & mask) | length2.AndNot(mask));
			const dgFloat32 length = (lenght2.Sqrt()).GetScalar();
			const dgFloat32 den = dgFloat32 (1.0f) / length;
			const dgFloat32 lenghtRatio = restLenght[i] * den;
			const dgFloat32 compression = dgFloat32(1.0f) - lenghtRatio;
			const dgVector fs(p0p1.Scale4(kSpring * compression));
			const dgVector fd(p0p1.Scale4(kDamper * den * den * (v0v1.DotProduct4(p0p1)).GetScalar()));

			dpdv[i] = p0p1.CompProduct4(v0v1);

			accel[j0] -= (fs + fd);
			accel[j1] += (fs + fd);

			spring_A01[i] = ks_dt * compression;
			spring_B01[i] = ks_dt * lenghtRatio * den * den;
		}

		for (dgInt32 i = 0; i < m_linksCount; i++) {
			const dgVector dv0 (dv[i]);
			const dgVector A01(spring_A01[i]);
			const dgVector B01(spring_B01[i]);
			const dgVector dfdx (A01.CompProduct4(dv0) + B01.CompProduct4(dx[i].CompProduct4(dpdv[i])));

			const dgInt32 j0 = links[i].m_m0;
			const dgInt32 j1 = links[i].m_m1;
			accel[j0] += dfdx;
			accel[j1] -= dfdx;
		}

		for (dgInt32 i = 0; i < m_particlesCount; i++) {
			accel[i] += volumetricStiffness.CompProduct4(tmp0[i]);
			tmp0[i] = accel[i].CompProduct4(collisionDir0[i]);
			tmp1[i] = accel[i].CompProduct4(collisionDir1[i]);
			tmp2[i] = accel[i].CompProduct4(collisionDir2[i]);
		}

		for (dgInt32 i = 0; i < m_particlesCount; i++) {
			tmp0[i] = accel[i] + collidingAccel[i] - collisionDir0[i].CompProduct4(tmp0[i]) - collisionDir1[i].CompProduct4(tmp1[i]) - collisionDir2[i].CompProduct4(tmp2[i]);
		}

		for (dgInt32 i = 0; i < m_particlesCount; i++) {
			veloc[i] += tmp0[i].CompProduct4(dtRK4);
			posit[i] += veloc[i].CompProduct4(dtRK4);
		}
	}
}
