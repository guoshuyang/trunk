/*************************************************************************
 Copyright (C) 2008 by Bruno Chareyre		                         *
*  bruno.chareyre@hmg.inpg.fr      					 *
*                                                                        *
*  This program is free software; it is licensed under the terms of the  *
*  GNU General Public License v2 or later. See file LICENSE for details. *
*************************************************************************/

#include"NewtonsDampedLaw.hpp"
#include<yade/core/MetaBody.hpp>
#include<yade/pkg-dem/Clump.hpp>
#include<yade/pkg-common/VelocityBins.hpp>
#include<yade/lib-base/yadeWm3Extra.hpp>

YADE_PLUGIN((NewtonsDampedLaw));
CREATE_LOGGER(NewtonsDampedLaw);
void NewtonsDampedLaw::cundallDamp(const Real& dt, const Vector3r& N, const Vector3r& V, Vector3r& A){
	for(int i=0; i<3; i++) A[i]*= 1 - damping*Mathr::Sign ( N[i]*(V[i] + (Real) 0.5 *dt*A[i]) );
}
void NewtonsDampedLaw::blockTranslateDOFs(unsigned blockedDOFs, Vector3r& v) {
	if(blockedDOFs==State::DOF_NONE) return;
	if(blockedDOFs==State::DOF_ALL)  v = Vector3r::ZERO;
	if((blockedDOFs & State::DOF_X)!=0) v[0]=0;
	if((blockedDOFs & State::DOF_Y)!=0) v[1]=0;
	if((blockedDOFs & State::DOF_Z)!=0) v[2]=0;
}
void NewtonsDampedLaw::blockRotateDOFs(unsigned blockedDOFs, Vector3r& v) {
	if(blockedDOFs==State::DOF_NONE) return;
	if(blockedDOFs==State::DOF_ALL)  v = Vector3r::ZERO;
	if((blockedDOFs & State::DOF_RX)!=0) v[0]=0;
	if((blockedDOFs & State::DOF_RY)!=0) v[1]=0;
	if((blockedDOFs & State::DOF_RZ)!=0) v[2]=0;
}
void NewtonsDampedLaw::handleClumpMember(MetaBody* ncb, const body_id_t memberId, State* clumpState){
	const shared_ptr<Body>& b=Body::byId(memberId,ncb);
	assert(b->isClumpMember());
	State* state=b->state.get();
	const Vector3r& m=ncb->bex.getTorque(memberId); const Vector3r& f=ncb->bex.getForce(memberId);
	Vector3r diffClumpAccel=f/clumpState->mass;
	// angular acceleration from: normal torque + torque generated by the force WRT particle centroid on the clump centroid
	Vector3r diffClumpAngularAccel=diagDiv(m,clumpState->inertia)+diagDiv((state->pos-clumpState->pos).Cross(f),clumpState->inertia); 
	// damp increment of accels on the clump, using velocities of the clump MEMBER
	//cundallDamp(ncb->dt,f,state->vel,diffClumpAccel,m,state->angVel,diffClumpAngularAccel);
	cundallDamp(ncb->dt,f,state->vel,diffClumpAccel);
	cundallDamp(ncb->dt,m,state->angVel,diffClumpAngularAccel);
	// clumpState->{acceleration,angularAcceleration} are reset byt Clump::moveMembers, it is ok to just increment here
	clumpState->accel+=diffClumpAccel;
	clumpState->angAccel+=diffClumpAngularAccel;
	if(haveBins) velocityBins->binVelSqUse(memberId,VelocityBins::getBodyVelSq(state));
	#ifdef YADE_OPENMP
		Real& thrMaxVSq=threadMaxVelocitySq[omp_get_thread_num()]; thrMaxVSq=max(thrMaxVSq,state->vel.SquaredLength());
	#else
		maxVelocitySq=max(maxVelocitySq,state->vel.SquaredLength());
	#endif
}

void NewtonsDampedLaw::action(MetaBody * ncb)
{
	ncb->bex.sync();
	Real dt=Omega::instance().getTimeStep();
	maxVelocitySq=-1;
	haveBins=(bool)velocityBins;
	if(haveBins) velocityBins->binVelSqInitialize();

	#ifdef YADE_OPENMP
		FOREACH(Real& thrMaxVSq, threadMaxVelocitySq) { thrMaxVSq=0; }
		const BodyContainer& bodies=*(ncb->bodies.get());
		const long size=(long)bodies.size();
		#pragma omp parallel for schedule(static)
		for(long _id=0; _id<size; _id++){
			const shared_ptr<Body>& b(bodies[_id]);
	#else
		FOREACH(const shared_ptr<Body>& b, *ncb->bodies){
	#endif
			if(!b) continue;
			State* state=b->state.get();
			const body_id_t& id=b->getId();
			// clump members are non-dynamic; we only get their velocities here
			if (!b->isDynamic || b->isClumpMember()){
				// FIXME: duplicated code from below; awaits https://bugs.launchpad.net/yade/+bug/398089 to be solved
				if(haveBins) {velocityBins->binVelSqUse(id,VelocityBins::getBodyVelSq(state));}
				#ifdef YADE_OPENMP
					Real& thrMaxVSq=threadMaxVelocitySq[omp_get_thread_num()]; thrMaxVSq=max(thrMaxVSq,state->vel.SquaredLength());
				#else
					maxVelocitySq=max(maxVelocitySq,state->vel.SquaredLength());
				#endif
				continue;
			}

			if (b->isStandalone()){
				// translate equation
				const Vector3r& f=ncb->bex.getForce(id); 
				state->accel=f/state->mass; 
				cundallDamp(dt,f,state->vel,state->accel); 
				lfTranslate(ncb,state,id,dt);
				// rotate equation
				if (/*b->isSpheral || */ !accRigidBodyRot){ // spheral body or accRigidBodyRot disabled
					const Vector3r& m=ncb->bex.getTorque(id); 
					state->angAccel=diagDiv(m,state->inertia);
					cundallDamp(dt,m,state->angVel,state->angAccel);
					lfSpheralRotate(ncb,state,id,dt);
				} else { // non spheral body and accRigidBodyRot enabled
					const Vector3r& m=ncb->bex.getTorque(id); 
					lfRigidBodyRotate(ncb,state,id,dt,m);
				}
			} else if (b->isClump()){
				state->accel=state->angAccel=Vector3r::ZERO; // to make sure; should be reset in Clump::moveMembers
				if (accRigidBodyRot){
					// forces applied to clump proper, if there are such
					const Vector3r& f=ncb->bex.getForce(id);
					Vector3r dLinAccel=f/state->mass;
					cundallDamp(dt,f,state->vel,dLinAccel);
					state->accel+=dLinAccel;
					const Vector3r& m=ncb->bex.getTorque(id);
					Vector3r M(m);
					// sum forces on clump members
					FOREACH(Clump::memberMap::value_type mm, static_cast<Clump*>(b.get())->members){
						const body_id_t memberId=mm.first;
						const shared_ptr<Body>& member=Body::byId(memberId,ncb);
						assert(member->isClumpMember());
						State* memberState=member->state.get();
						// Linear acceleration
						const Vector3r& f=ncb->bex.getForce(memberId); 
						Vector3r diffClumpAccel=f/state->mass;
						// damp increment of accel on the clump, using velocities of the clump MEMBER
						cundallDamp(dt,f,memberState->vel,diffClumpAccel);
						state->accel+=diffClumpAccel;
						// Momentum
						const Vector3r& m=ncb->bex.getTorque(memberId);
						M+=(memberState->pos-state->pos).Cross(f)+m;
						if(haveBins) velocityBins->binVelSqUse(memberId,VelocityBins::getBodyVelSq(memberState));
						#ifdef YADE_OPENMP
							Real& thrMaxVSq=threadMaxVelocitySq[omp_get_thread_num()]; thrMaxVSq=max(thrMaxVSq,memberState->vel.SquaredLength());
						#else
							maxVelocitySq=max(maxVelocitySq,memberState->vel.SquaredLength());
						#endif
					}
					// motion
					lfTranslate(ncb,state,id,dt);
					lfRigidBodyRotate(ncb,state,id,dt,M);
				} else { // accRigidBodyRot disabled
					// sum force on clump memebrs, add them to the clump itself
					FOREACH(Clump::memberMap::value_type mm, static_cast<Clump*>(b.get())->members){
						handleClumpMember(ncb,mm.first,state);
					}
					// forces applied to clump proper, if there are such
					const Vector3r& m=ncb->bex.getTorque(id); const Vector3r& f=ncb->bex.getForce(id);
					Vector3r dLinAccel=f/state->mass, dAngAccel=diagDiv(m,state->inertia);
					cundallDamp(dt,f,state->vel,dLinAccel); cundallDamp(dt,m,state->angVel,dAngAccel);
					state->accel+=dLinAccel; state->angAccel+=dAngAccel;
					// motion
					lfTranslate(ncb,state,id,dt);
					lfSpheralRotate(ncb,state,id,dt);
				}
				static_cast<Clump*>(b.get())->moveMembers();
			}

			// save maxima velocity
				if(haveBins) {velocityBins->binVelSqUse(id,VelocityBins::getBodyVelSq(state));}
				#ifdef YADE_OPENMP
					Real& thrMaxVSq=threadMaxVelocitySq[omp_get_thread_num()]; thrMaxVSq=max(thrMaxVSq,state->vel.SquaredLength());
				#else
					maxVelocitySq=max(maxVelocitySq,state->vel.SquaredLength());
				#endif
	}
	#ifdef YADE_OPENMP
		FOREACH(const Real& thrMaxVSq, threadMaxVelocitySq) { maxVelocitySq=max(maxVelocitySq,thrMaxVSq); }
	#endif
	if(haveBins) velocityBins->binVelSqFinalize();
}

inline void NewtonsDampedLaw::lfTranslate(MetaBody* ncb, State* state, const body_id_t& id, const Real& dt )
{
	blockTranslateDOFs(state->blockedDOFs, state->accel);
	state->vel+=dt*state->accel;
	state->pos += state->vel*dt + ncb->bex.getMove(id);
}
inline void NewtonsDampedLaw::lfSpheralRotate(MetaBody* ncb, State* state, const body_id_t& id, const Real& dt )
{
	blockRotateDOFs(state->blockedDOFs, state->angAccel);
	state->angVel+=dt*state->angAccel;
	Vector3r axis = state->angVel; Real angle = axis.Normalize();
	Quaternionr q; q.FromAxisAngle ( axis,angle*dt );
	state->ori = q*state->ori;
	if(ncb->bex.getMoveRotUsed() && ncb->bex.getRot(id)!=Vector3r::ZERO){ Vector3r r(ncb->bex.getRot(id)); Real norm=r.Normalize(); Quaternionr q; q.FromAxisAngle(r,norm); state->ori=q*state->ori; }
	state->ori.Normalize();
}
void NewtonsDampedLaw::lfRigidBodyRotate(MetaBody* ncb, State* state, const body_id_t& id, const Real& dt, const Vector3r& M){
	Matrix3r A; state->ori.Conjugate().ToRotationMatrix(A); // rotation matrix from global to local r.f.
	const Vector3r l_n = state->angMom + dt/2 * M; // global angular momentum at time n
	const Vector3r l_b_n = A*l_n; // local angular momentum at time n
	const Vector3r angVel_b_n = diagDiv(l_b_n,state->inertia); // local angular velocity at time n
	const Quaternionr dotQ_n=DotQ(angVel_b_n,state->ori); // dQ/dt at time n
	const Quaternionr Q_half = state->ori + dt/2 * dotQ_n; // Q at time n+1/2
	state->angMom+=dt*M; // global angular momentum at time n+1/2
	const Vector3r l_b_half = A*state->angMom; // local angular momentum at time n+1/2
	Vector3r angVel_b_half = diagDiv(l_b_half,state->inertia); // local angular velocity at time n+1/2
	blockRotateDOFs( state->blockedDOFs, angVel_b_half );
	const Quaternionr dotQ_half=DotQ(angVel_b_half,Q_half); // dQ/dt at time n+1/2
	state->ori+=dt*dotQ_half; // Q at time n+1
	state->angVel=state->ori.Rotate(angVel_b_half); // global angular velocity at time n+1/2
	if(ncb->bex.getMoveRotUsed() && ncb->bex.getRot(id)!=Vector3r::ZERO){ Vector3r r(ncb->bex.getRot(id)); Real norm=r.Normalize(); Quaternionr q; q.FromAxisAngle(r,norm); state->ori=q*state->ori; }
	state->ori.Normalize(); 

	LOG_TRACE( "\ntorque: " << M
		<< "\nglobal angular momentum at time n: " << l_n
		<< "\nlocal angular momentum at time n:  " << l_b_n
		<< "\nlocal angular velocity at time n:  " << angVel_b_n
		<< "\ndQ/dt at time n:                   " << dotQ_n
		<< "\nQ at time n+1/2:                   " << Q_half
		<< "\nglobal angular momentum at time n+1/2: " << state->angMom
		<< "\nlocal angular momentum at time n+1/2:  " << l_b_half
		<< "\nlocal angular velocity at time n+1/2:  " << angVel_b_half
		<< "\ndQ/dt at time n+1/2:                   " << dotQ_half
		<< "\nQ at time n+1:                         " << state->ori
		<< "\nglobal angular velocity at time n+1/2: " << state->angVel
	);
}
	
Quaternionr NewtonsDampedLaw::DotQ(const Vector3r& angVel, const Quaternionr& Q){
	Quaternionr dotQ(Quaternionr::ZERO);
	dotQ[0] = (-Q[1]*angVel[0]-Q[2]*angVel[1]-Q[3]*angVel[2])/2;
	dotQ[1] = ( Q[0]*angVel[0]-Q[3]*angVel[1]+Q[2]*angVel[2])/2;
	dotQ[2] = ( Q[3]*angVel[0]+Q[0]*angVel[1]-Q[1]*angVel[2])/2;
	dotQ[3] = (-Q[2]*angVel[0]+Q[1]*angVel[1]+Q[0]*angVel[2])/2;
	return dotQ;
}
