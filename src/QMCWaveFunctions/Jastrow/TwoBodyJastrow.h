//////////////////////////////////////////////////////////////////////////////////////
// This file is distributed under the University of Illinois/NCSA Open Source
// License. See LICENSE file in top directory for details.
//
// Copyright (c) 2016 Jeongnim Kim and QMCPACK developers.
//
// File developed by: Jeongnim Kim, jeongnim.kim@intel.com, Intel Corp.
//                    Amrita Mathuriya, amrita.mathuriya@intel.com, Intel Corp.
//                    Ye Luo, yeluo@anl.gov, Argonne National Laboratory
//
// File created by: Jeongnim Kim, jeongnim.kim@intel.com, Intel Corp.
//////////////////////////////////////////////////////////////////////////////////////
// -*- C++ -*-
#ifndef QMCPLUSPLUS_TWOBODYJASTROW_H
#define QMCPLUSPLUS_TWOBODYJASTROW_H
#include "Utilities/Configuration.h"
#include "QMCWaveFunctions/WaveFunctionKokkos.h"
#include "QMCWaveFunctions/WaveFunctionComponent.h"
#include "Particle/DistanceTableData.h"
#include "QMCWaveFunctions/Jastrow/TwoBodyJastrowKokkos.h"
#include <Utilities/SIMD/allocator.hpp>
#include <Utilities/SIMD/algorithm.hpp>
#include <numeric>
#include <Kokkos_Core.hpp>
/*!
 * @file TwoBodyJastrow.h
 */

namespace qmcplusplus
{
/** @ingroup WaveFunctionComponent
 *  @brief Specialization for two-body Jastrow function using multiple functors
 *
 * Each pair-type can have distinct function \f$u(r_{ij})\f$.
 * For electrons, distinct pair correlation functions are used
 * for spins up-up/down-down and up-down/down-up.
 *
 * Based on TwoBodyJastrow.h with these considerations
 * - DistanceTableData using SoA containers
 * - support mixed precision: FT::real_type != OHMMS_PRECISION
 * - loops over the groups: elminated PairID
 * - support simd function
 * - RealType the loop counts
 * - Memory use is O(N).
 */

// this is something I could work on...
template<typename atbjdType, typename apsdType>
void doTwoBodyJastrowMultiEvaluateGL(atbjdType atbjd, apsdType apsd, int numEl, bool fromscratch) {
  const int numWalkers = atbjd.extent(0);
  using BarePolicy = Kokkos::TeamPolicy<>;
  BarePolicy pol(numWalkers*numEl, 1, 32);
  Kokkos::parallel_for("tbj-evalGL-waker-loop", pol,
		       KOKKOS_LAMBDA(BarePolicy::member_type member) {
			 int walkerNum = member.league_rank()/numEl; 
			 atbjd(walkerNum).evaluateGL(member, apsd(walkerNum), fromscratch);
		       });
}

// note.  this is the one that is currently being used
template<typename atbjdType, typename apsdType>
void doTwoBodyJastrowMultiAcceptRestoreMove(atbjdType atbjd, apsdType apsd, 
					    Kokkos::View<int*>& isAcceptedMap,
					    int numAccepted, int iat, int numElectrons,
					    int numIons) {
  const int numWalkers = numAccepted;
  Kokkos::Profiling::pushRegion("doTwoBodyJastrowMultiAcceptRestoreMove");
  if (numWalkers>2048) { // not really wanting to go here, could make this configurable though
    using BarePolicy = Kokkos::TeamPolicy<>;
    BarePolicy pol(numWalkers, 16, 32);
    Kokkos::parallel_for("tbj-acceptRestoreMove-waker-loop", pol,
                         KOKKOS_LAMBDA(BarePolicy::member_type member) {
			   int walkerIdx = member.league_rank();
			   const int walkerNum = isAcceptedMap(walkerIdx);
			   atbjd(walkerNum).acceptMove(member, apsd(walkerNum), iat);
			 });
  } else {
    int numWalkers = numAccepted;
    Kokkos::parallel_for("tbj-acceptRestore-first-part",
			 Kokkos::RangePolicy<>(0,numWalkers*numElectrons),
			 KOKKOS_LAMBDA(const int& idx) {
			   const int walkerIdx = idx / numElectrons;
			   const int walkerNum = isAcceptedMap(walkerIdx);
			   const int workingElNum = idx % numElectrons;
			   atbjd(walkerNum).acceptMove_part1(apsd(walkerNum), iat, workingElNum);
			 });
    Kokkos::parallel_for("tbj-acceptRestore-second-part",
			 Kokkos::RangePolicy<>(0,numWalkers*numElectrons),
			 KOKKOS_LAMBDA(const int& idx) {
			   const int walkerIdx = idx / numElectrons;
			   const int walkerNum = isAcceptedMap(walkerIdx);
			   const int workingElNum = idx % numElectrons;
			   atbjd(walkerNum).acceptMove_part2(apsd(walkerNum), iat, workingElNum);
			 });
    Kokkos::parallel_for("tbj-acceptRestore-third-part",
			 Kokkos::RangePolicy<>(0,numWalkers),
			 KOKKOS_LAMBDA(const int& idx) {
			   const int walkerNum = isAcceptedMap(idx);
			   atbjd(walkerNum).acceptMove_part3(iat);
			 });
			   
    /*
    for(int i = 0; i<numWalkers; i++) {
      // this is the entry point for the next issue
      // I'm finding this annoying as it is requiring UVM to access memory 
      // that I want to just stay on the GPU
      int walkerNum = isAcceptedMap(i);
      typename atbjdType::value_type thing(atbjd(walkerNum));
      typename apsdType::value_type otherThing(apsd(walkerNum));
      thing.acceptMove(Kokkos::DefaultExecutionSpace(), otherThing, iat);
    }
    */
  }
  Kokkos::Profiling::popRegion();
}

template<typename atbjdType, typename apsdType>
void doTwoBodyJastrowMultiAcceptRestoreMove(atbjdType atbjd, apsdType apsd, 
					    Kokkos::View<int*>& isAcceptedMap,
					    int numAccepted, int iat, int numElectrons,
					    int numIons, const typename Kokkos::HostSpace&) {
  const int numWalkers = atbjd.extent(0);
  using BarePolicy = Kokkos::TeamPolicy<>;
  BarePolicy pol(numWalkers, Kokkos::AUTO, 32);
  Kokkos::parallel_for("tbj-acceptRestoreMove-waker-loop", pol,
		       KOKKOS_LAMBDA(BarePolicy::member_type member) {
			 int walkerIdx = member.league_rank();
			 const int walkerNum = isAcceptedMap(walkerIdx);
			 atbjd(walkerNum).acceptMove(member, apsd(walkerNum), iat);
		       });
}

#ifdef KOKKOS_ENABLE_CUDA
template<typename atbjdType, typename apsdType>
void doTwoBodyJastrowMultiAcceptRestoreMove(atbjdType atbjd, apsdType apsd, 
					    Kokkos::View<int*>& isAcceptedMap,
					    int numAccepted, int iat, int numElectrons,
					    int numIons, const Kokkos::CudaSpace& ms) {
  doTwoBodyJastrowMultiAcceptRestoreMove(atbjd, apsd, isAcceptedMap, numAccepted,
					 iat, numElectrons, numIons);
}

template<typename atbjdType, typename apsdType>
void doTwoBodyJastrowMultiAcceptRestoreMove(atbjdType atbjd, apsdType apsd, 
					    Kokkos::View<int*>& isAcceptedMap,
					    int numAccepted, int iat, int numElectrons,
					    int numIons, const Kokkos::CudaUVMSpace& ms) {
  doTwoBodyJastrowMultiAcceptRestoreMove(atbjd, apsd, isAcceptedMap, numAccepted,
					 iat, numElectrons, numIons);
}
#endif
//////////////////////////////////////
 
/* 
template<typename atbjdType, typename apsdType>
  void doTwoBodyJastrowMultiAcceptRestoreMove(atbjdType atbjd, apsdType apsd, int iat) {
  const int numWalkers = atbjd.extent(0);
  if(numWalkers>16) {
  using BarePolicy = Kokkos::TeamPolicy<>;
  BarePolicy pol(numWalkers, 16, 32);
  Kokkos::parallel_for("tbj-acceptRestoreMove-waker-loop", pol,
		       KOKKOS_LAMBDA(BarePolicy::member_type member) {
			 int walkerNum = member.league_rank(); 
			 atbjd(walkerNum).acceptMove(member, apsd(walkerNum), iat);
		       });
  } else {
    for(int walkerNum = 0; walkerNum<numWalkers; walkerNum++) {
      typename atbjdType::value_type thing(atbjd(walkerNum));
      typename apsdType::value_type otherThing(apsd(walkerNum));
      thing.acceptMove(Kokkos::DefaultExecutionSpace(), otherThing, iat);
    }
  }
}
*/
 
// note, this is the one I am currently using
template<typename atbjdType, typename apsdType, typename valT>
void doTwoBodyJastrowMultiRatioGrad(atbjdType& atbjd, apsdType& apsd, Kokkos::View<int*>& isValidMap,
				int numValid, int iel, Kokkos::View<valT**> gradNowView,
				    Kokkos::View<valT*> ratiosView) {
  const int numWalkers = numValid;
  const int numElectrons = atbjd(0).Nelec(0); // note this is bad, relies on UVM, kill it
  
  Kokkos::Profiling::pushRegion("tbj-evalRatioGrad");
  Kokkos::parallel_for("tbj-evalRatioGrad-part1",
		       Kokkos::RangePolicy<>(0,numWalkers*numElectrons),
		       KOKKOS_LAMBDA(const int &idx) {
			 const int walkerIdx = idx / numElectrons;
			 const int walkerNum = isValidMap(walkerIdx);
			 const int workingElNum = idx % numElectrons;
			 atbjd(walkerNum).ratioGrad_part1(apsd(walkerNum), iel, workingElNum);
		       });
  // might see if recoding this as a loop over walkers where the reduction happens directly
  // rather than with atomics would work
  Kokkos::parallel_for("tbj-evalRatioGrad-part2",
		       Kokkos::RangePolicy<>(0,numWalkers*numElectrons),
		       KOKKOS_LAMBDA(const int &idx) {
			 const int walkerIdx = idx / numElectrons;
			 const int walkerNum = isValidMap(walkerIdx);
			 const int workingElNum = idx % numElectrons;
			 atbjd(walkerNum).ratioGrad_part2(apsd(walkerNum), iel, workingElNum);
		       });
  Kokkos::parallel_for("tbj-evalRatioGrad-part3",
		       Kokkos::RangePolicy<>(0,numWalkers),
		       KOKKOS_LAMBDA(const int &idx) {
			 const int walkerIdx = idx;
			 const int walkerNum = isValidMap(walkerIdx);
			 auto gv = Kokkos::subview(gradNowView,walkerIdx,Kokkos::ALL());
			 ratiosView(walkerIdx) = atbjd(walkerNum).ratioGrad_part3(iel, gv);
		       });
			 
  Kokkos::Profiling::popRegion();
  /*
  using BarePolicy = Kokkos::TeamPolicy<>;
  BarePolicy pol(numWalkers, 1, 32);
  Kokkos::parallel_for("tbj-evalRatioGrad-walker-loop", pol,
		       KOKKOS_LAMBDA(BarePolicy::member_type member) {
			 int walkerIdx = member.league_rank();
			 int walkerNum = isValidMap(walkerIdx);
			 auto gv = Kokkos::subview(gradNowView,walkerIdx,Kokkos::ALL());
			 ratiosView(walkerIdx) = atbjd(walkerNum).ratioGrad(member, apsd(walkerNum), iel, gv);
		       });
  */
}

template<typename atbjdType, typename apsdType, typename valT>
void doTwoBodyJastrowMultiRatioGrad(atbjdType& atbjd, apsdType& apsd, Kokkos::View<int*>& isValidMap,
				    int numValid, int iel, Kokkos::View<valT**> gradNowView,
				    Kokkos::View<valT*> ratiosView, const Kokkos::HostSpace& ) {
  const int numWalkers = atbjd.extent(0);
  using BarePolicy = Kokkos::TeamPolicy<>;
  BarePolicy pol(numWalkers, Kokkos::AUTO, 32);
  Kokkos::parallel_for("tbj-evalRatioGrad-walker-loop", pol,
		       KOKKOS_LAMBDA(BarePolicy::member_type member) {
			 int walkerIdx = member.league_rank();
			 int walkerNum = isValidMap(walkerIdx);
			 auto gv = Kokkos::subview(gradNowView,walkerIdx,Kokkos::ALL());
			 ratiosView(walkerIdx) = atbjd(walkerNum).ratioGrad(member, apsd(walkerNum), iel, gv);
		       });
}

#ifdef KOKKOS_ENABLE_CUDA
template<typename atbjdType, typename apsdType, typename valT>
void doTwoBodyJastrowMultiRatioGrad(atbjdType& atbjd, apsdType& apsd, Kokkos::View<int*>& isValidMap,
				    int numValid, int iel, Kokkos::View<valT**> gradNowView,
				    Kokkos::View<valT*> ratiosView, const Kokkos::CudaSpace& ms) {
  doTwoBodyJastrowMultiRatioGrad(atbjd, apsd, isValidMap, numValid, iel, gradNowView, ratiosView);
}

template<typename atbjdType, typename apsdType, typename valT>
void doTwoBodyJastrowMultiRatioGrad(atbjdType& atbjd, apsdType& apsd, Kokkos::View<int*>& isValidMap,
				    int numValid, int iel, Kokkos::View<valT**> gradNowView,
				    Kokkos::View<valT*> ratiosView, const Kokkos::CudaUVMSpace& ms) {
  doTwoBodyJastrowMultiRatioGrad(atbjd, apsd, isValidMap, numValid, iel, gradNowView, ratiosView);
}
#endif
 
//////////////////////////////////
 
/* 
template<typename atbjdType, typename apsdType, typename valT>
void doTwoBodyJastrowMultiRatioGrad(atbjdType atbjd, apsdType apsd, int iat, 
				    Kokkos::View<valT**> gradNowView,
				    Kokkos::View<valT*> ratiosView) {
  const int numWalkers = atbjd.extent(0);
  using BarePolicy = Kokkos::TeamPolicy<>;
  BarePolicy pol(numWalkers, 1, 32);
  Kokkos::parallel_for("tbj-evalRatioGrad-walker-loop", pol,
		       KOKKOS_LAMBDA(BarePolicy::member_type member) {
			 int walkerNum = member.league_rank();
			 auto gv = Kokkos::subview(gradNowView,walkerNum,Kokkos::ALL());
			 ratiosView(walkerNum) = atbjd(walkerNum).ratioGrad(member, apsd(walkerNum), iat, gv);
		       });
}
*/			 
template<typename atbjdType, typename valT>
void doTwoBodyJastrowMultiEvalGrad(atbjdType atbjd, int iat, Kokkos::View<valT**> gradNowView) {
  int numWalkers = atbjd.extent(0);
  using BarePolicy = Kokkos::TeamPolicy<>;
  BarePolicy pol(numWalkers, 1, 32);
  Kokkos::parallel_for("tbj-evalGrad-walker-loop", pol,
		       KOKKOS_LAMBDA(BarePolicy::member_type member) {
			 int walkerNum = member.league_rank();
			 for (int idim = 0; idim < gradNowView.extent(1); idim++) {
			   gradNowView(walkerNum,idim) = atbjd(walkerNum).dUat(iat,idim);
			 }
		       });
 }

// note, this is the one we are using
template<typename eiListType, typename apskType, typename atbjdType, typename tempRType,
         typename devRatioType, typename activeMapType>
void doTwoBodyJastrowMultiEvalRatio(int pairNum, eiListType& eiList, apskType& apsk,
				    atbjdType& allTwoBodyJastrowData,
				    tempRType& likeTempR, devRatioType& devRatios,
				    activeMapType& activeMap, int numActive) {
  int numWalkers = numActive;
  int numKnots = likeTempR.extent(1);
  const int numElectrons = allTwoBodyJastrowData(0).Nelec(0); // note this is bad, relies on UVM, kill it

  Kokkos::parallel_for("tbj-multi-ratio", Kokkos::RangePolicy<>(0,numWalkers*numKnots*numElectrons),
		       KOKKOS_LAMBDA(const int& idx) {
			 const int workingElecNum = idx / numWalkers / numKnots;
			 const int knotNum = (idx - workingElecNum * numWalkers * numKnots) / numWalkers;
			 const int walkerIdx = (idx - workingElecNum * numWalkers * numKnots - knotNum * numWalkers);
			 
			 const int walkerNum = activeMap(walkerIdx);
			 auto& psk = apsk(walkerNum);
			 int iel = eiList(walkerNum, pairNum, 0);

			 auto singleDists = Kokkos::subview(likeTempR, walkerNum, knotNum, Kokkos::ALL);
			 allTwoBodyJastrowData(walkerIdx).computeU(psk, iel, singleDists, workingElecNum, devRatios, walkerIdx, knotNum);
		       });
  Kokkos::parallel_for("tbj-multi-ratio-cleanup", Kokkos::RangePolicy<>(0,numWalkers*numKnots),
		       KOKKOS_LAMBDA(const int& idx) {
			 const int walkerIdx = idx / numKnots;
			 const int knotNum = idx % numKnots;
			 const int walkerNum = activeMap(walkerIdx);
			 if (knotNum == 0) {
			   allTwoBodyJastrowData(walkerIdx).updateMode(0) = 0;
			 }
			 int iel = eiList(walkerNum, pairNum, 0);
			 auto val = devRatios(walkerIdx, knotNum);
			 devRatios(walkerIdx,knotNum) = std::exp(allTwoBodyJastrowData(walkerIdx).Uat(iel) - val);
		       });
}			 

 
template<typename eiListType, typename apskType, typename atbjdType, typename tempRType,
         typename devRatioType, typename activeMapType>
void doTwoBodyJastrowMultiEvalRatio(int pairNum, eiListType& eiList, apskType& apsk,
				    atbjdType& allTwoBodyJastrowData,
				    tempRType& likeTempR, devRatioType& devRatios,
				    activeMapType& activeMap, int numActive, const Kokkos::HostSpace&) {
  const int numWalkers = apsk.extent(0);
  const int numKnots = devRatios.extent(1);
  using BarePolicy = Kokkos::TeamPolicy<>;
  BarePolicy pol(numWalkers, Kokkos::AUTO, 32);
  
  Kokkos::parallel_for("tbj-multi-ratio", pol,
		       KOKKOS_LAMBDA(BarePolicy::member_type member) {
			 int walkerIndex = member.league_rank();
			 int walkerNum = activeMap(walkerIndex);
			 auto& psk = apsk(walkerNum);
			 //auto& jd = allTwoBodyJastrowData(walkerIndex);
			 allTwoBodyJastrowData(walkerIndex).updateMode(0) = 0;
			 
			 Kokkos::parallel_for(Kokkos::TeamThreadRange(member, numKnots),
					      [=](const int& knotNum) {
						auto singleDists = Kokkos::subview(likeTempR, walkerNum, knotNum, Kokkos::ALL);
						int iel = eiList(walkerNum, pairNum, 0);
						auto val = allTwoBodyJastrowData(walkerIndex).computeU(member, psk, iel, singleDists);
						devRatios(walkerIndex, numKnots) = std::exp(allTwoBodyJastrowData(walkerIndex).Uat(iel) - val);
					      });
		       });
}

#ifdef KOKKOS_ENABLE_CUDA
template<typename eiListType, typename apskType, typename atbjdType, typename tempRType,
         typename devRatioType, typename activeMapType>
void doTwoBodyJastrowMultiEvalRatio(int pairNum, eiListType& eiList, apskType& apsk,
				    atbjdType& allTwoBodyJastrowData,
				    tempRType& likeTempR, devRatioType& devRatios,
				    activeMapType& activeMap, int numActive, const Kokkos::CudaSpace& ms) {
  doTwoBodyJastrowMultiEvalRatio(pairNum, eiList, apsk, allTwoBodyJastrowData,
				 likeTempR, devRatios, activeMap, numActive);
}

template<typename eiListType, typename apskType, typename atbjdType, typename tempRType,
         typename devRatioType, typename activeMapType>
void doTwoBodyJastrowMultiEvalRatio(int pairNum, eiListType& eiList, apskType& apsk,
				    atbjdType& allTwoBodyJastrowData,
				    tempRType& likeTempR, devRatioType& devRatios,
				    activeMapType& activeMap, int numActive, const Kokkos::CudaUVMSpace& ms) {
  doTwoBodyJastrowMultiEvalRatio(pairNum, eiList, apsk, allTwoBodyJastrowData,
				 likeTempR, devRatios, activeMap, numActive);
}
#endif
//////////////////////////////////
 
// could potentially restore this for OpenMP, but note that
// on the back end would need to restore the previous backside implementation
// that does not expect a single electron per member
template<typename atbjdType, typename apsdType, typename valT>
void doTwoBodyJastrowMultiEvaluateLog(atbjdType atbjd, apsdType apsd, Kokkos::View<valT*> values) {
  Kokkos::Profiling::pushRegion("2BJ-multiEvalLog");
  const int numWalkers = atbjd.extent(0);
  using BarePolicy = Kokkos::TeamPolicy<>;
  const int numElectrons = atbjd(0).Nelec(0);


  BarePolicy pol(numWalkers*numElectrons, 8, 32);
  Kokkos::parallel_for("tbj-evalLog-waker-loop", pol,
		       KOKKOS_LAMBDA(BarePolicy::member_type member) {
			 int walkerNum = member.league_rank()/numElectrons;
			 values(walkerNum) = atbjd(walkerNum).evaluateLog(member, apsd(walkerNum));
		       });
  Kokkos::Profiling::popRegion();
}








template<class FT>
struct TwoBodyJastrow : public WaveFunctionComponent
{
  using jasDataType = TwoBodyJastrowKokkos<RealType, ValueType, OHMMS_DIM>;
  jasDataType jasData;
  bool splCoefsNotAllocated;
  
  /// alias FuncType
  using FuncType = FT;
  /// type of each component U, dU, d2U;
  using valT = typename FT::real_type;
  /// element position type
  using posT = TinyVector<valT, OHMMS_DIM>;
  /// use the same container
  using RowContainer = DistanceTableData::RowContainer;

  /// number of particles
  size_t N;
  /// number of groups of the target particleset
  size_t NumGroups;
  /// Used to compute correction
  bool FirstTime;
  // 
  int first[2];
  int last[2];

  /// diff value
  RealType DiffVal;
  Kokkos::View<valT*> cur_u, cur_du, cur_d2u;
  Kokkos::View<valT*> old_u, old_du, old_d2u;
  Kokkos::View<valT*> DistCompressed;
  Kokkos::View<int*> DistIndice;
  /// Container for \f$F[ig*NumGroups+jg]\f$

  TwoBodyJastrow(ParticleSet& p);
  TwoBodyJastrow(const TwoBodyJastrow& rhs) = default;
  ~TwoBodyJastrow();

  /* initialize storage */
  void init(ParticleSet& p);
  void initializeJastrowKokkos();
  
  /** add functor for (ia,ib) pair */
  void addFunc(int ia, int ib, FT* j);

  /////////// Helpers to populate collective data structures
  template<typename atbjType, typename apsdType, typename vectorType, typename vectorType2>
  void populateCollectiveViews(atbjType atbjd, apsdType apsd, 
			       vectorType& WFC_list, vectorType2& P_list) {
    auto atbjdMirror = Kokkos::create_mirror_view(atbjd);
    auto apsdMirror = Kokkos::create_mirror_view(apsd);
    
    for (int i = 0; i < WFC_list.size(); i++) {
      atbjdMirror(i) = static_cast<TwoBodyJastrow*>(WFC_list[i])->jasData;
      apsdMirror(i) = P_list[i]->psk;
    }
    Kokkos::deep_copy(atbjd, atbjdMirror);
    Kokkos::deep_copy(apsd, apsdMirror);
  }

  template<typename atbjType, typename apsdType, typename vectorType, typename vectorType2>
  void populateCollectiveViews(atbjType atbjd, apsdType apsd, vectorType& WFC_list, 
			       vectorType2& P_list, const std::vector<bool>& isAccepted) {
    auto atbjdMirror = Kokkos::create_mirror_view(atbjd);
    auto apsdMirror = Kokkos::create_mirror_view(apsd);
    
    int idx = 0;
    for (int i = 0; i < WFC_list.size(); i++) {
      if (isAccepted[i]) {
	atbjdMirror(idx) = static_cast<TwoBodyJastrow*>(WFC_list[i])->jasData;
	apsdMirror(idx) = P_list[i]->psk;
	idx++;
      }
    }
    Kokkos::deep_copy(atbjd, atbjdMirror);
    Kokkos::deep_copy(apsd, apsdMirror);
  }

  //////////////////multi_evaluate functions
  // note that G_list and L_list feel redundant, they are just elements of P_list
  // if we wanted to be really kind though, we could copy them back out so this would
  // be where the host could see the results of the calcualtion easily
  /*
  virtual void multi_evaluateLog(const std::vector<WaveFunctionComponent*>& WFC_list,
                                 const std::vector<ParticleSet*>& P_list,
                                 const std::vector<ParticleSet::ParticleGradient_t*>& G_list,
                                 const std::vector<ParticleSet::ParticleLaplacian_t*>& L_list,
                                 ParticleSet::ParticleValue_t& values);
  */

  virtual void multi_evaluateLog(const std::vector<WaveFunctionComponent*>& WFC_list,
				 WaveFunctionKokkos& wfc,
				 Kokkos::View<ParticleSet::pskType*>& psk,
				 ParticleSet::ParticleValue_t& values);

  /*
  virtual void multi_evalGrad(const std::vector<WaveFunctionComponent*>& WFC_list,
			      const std::vector<ParticleSet*>& P_list,
                              int iat, std::vector<posT>& grad_now);
  */

  virtual void multi_evalGrad(const std::vector<WaveFunctionComponent*>& WFC_list,
			      WaveFunctionKokkos& wfc,
			      Kokkos::View<ParticleSet::pskType*>& psk,
                              int iat, std::vector<posT>& grad_now);

  virtual void multi_ratioGrad(const std::vector<WaveFunctionComponent*>& WFC_list,
                               WaveFunctionKokkos& wfc,
                               Kokkos::View<ParticleSet::pskType*> psk,
                               int iel,
                               Kokkos::View<int*>& isValidMap, int numValid,
                               std::vector<ValueType>& ratios,
                               std::vector<PosType>& grad_new); 

  /*
  virtual void multi_ratioGrad(const std::vector<WaveFunctionComponent*>& WFC_list,
			       const std::vector<ParticleSet*>& P_list,
			       int iat, std::vector<valT>& ratios,
			       std::vector<posT>& grad_new);
  */

  /*
  virtual void multi_acceptRestoreMove(const std::vector<WaveFunctionComponent*>& WFC_list,
				       const std::vector<ParticleSet*>& P_list,
				       const std::vector<bool>& isAccepted,
				       int iat);
  */

  virtual void multi_acceptrestoreMove(const std::vector<WaveFunctionComponent*>& WFC_list,
                                       WaveFunctionKokkos& wfc,
                                       Kokkos::View<ParticleSet::pskType*> psk,
                                       Kokkos::View<int*>& isAcceptedMap, int numAccepted, int iel);


  virtual void multi_evalRatio(int pairNum, Kokkos::View<int***>& eiList,
			       WaveFunctionKokkos& wfc,
			       Kokkos::View<ParticleSetKokkos<RealType, ValueType, 3>*>& apsk,
			       Kokkos::View<RealType***>& likeTempR,
			       Kokkos::View<RealType***>& unlikeTempR,
			       std::vector<ValueType>& ratios, int numActive);
  
  virtual void multi_evaluateGL(const std::vector<WaveFunctionComponent*>& WFC_list,
				const std::vector<ParticleSet*>& P_list,
				const std::vector<ParticleSet::ParticleGradient_t*>& G_list,
				const std::vector<ParticleSet::ParticleLaplacian_t*>& L_list,
				bool fromscratch = false);


};

template<typename FT>
TwoBodyJastrow<FT>::TwoBodyJastrow(ParticleSet& p)
{
  init(p);
  FirstTime                 = true;
  WaveFunctionComponentName = "TwoBodyJastrow";
}

template<typename FT>
TwoBodyJastrow<FT>::~TwoBodyJastrow()
{
//  auto it = J2Unique.begin();
//  while (it != J2Unique.end())
//  {
//    delete ((*it).second);
//    ++it;
 // }
} // need to clean up J2Unique

template<typename FT>
void TwoBodyJastrow<FT>::init(ParticleSet& p)
{
  N         = p.getTotalNum();
  NumGroups = p.groups();
  
  for (int i = 0; i < 2; i++) {
    first[i] = p.first(i);
    last[i] = p.last(i);
  }

  //And now the Kokkos vectors
  cur_u   = Kokkos::View<valT*>("cur_u",N);
  cur_du  = Kokkos::View<valT*>("cur_du",N);
  cur_d2u = Kokkos::View<valT*>("cur_d2u",N);
  old_u   = Kokkos::View<valT*>("old_u",N);
  old_du  = Kokkos::View<valT*>("old_du",N);
  old_d2u = Kokkos::View<valT*>("old_d2u",N);
  DistIndice=Kokkos::View<int*>("DistIndice",N);
  DistCompressed=Kokkos::View<valT*>("DistCompressed",N);

  initializeJastrowKokkos();
}

template<typename FT>
void TwoBodyJastrow<FT>::initializeJastrowKokkos() {
  jasData.LogValue       = Kokkos::View<valT[1]>("LogValue");

  jasData.Nelec          = Kokkos::View<int[1]>("Nelec");
  auto NelecMirror       = Kokkos::create_mirror_view(jasData.Nelec);
  NelecMirror(0)         = N;
  Kokkos::deep_copy(jasData.Nelec, NelecMirror);

  jasData.NumGroups      = Kokkos::View<int[1]>("NumGroups");
  auto NumGroupsMirror   = Kokkos::create_mirror_view(jasData.NumGroups);
  NumGroupsMirror(0)     = NumGroups;
  Kokkos::deep_copy(jasData.NumGroups, NumGroupsMirror);

  jasData.first          = Kokkos::View<int[2]>("first");
  auto firstMirror       = Kokkos::create_mirror_view(jasData.first);
  firstMirror(0)         = first[0];
  firstMirror(1)         = first[1];
  Kokkos::deep_copy(jasData.first, firstMirror);

  jasData.last           = Kokkos::View<int[2]>("last");
  auto lastMirror        = Kokkos::create_mirror_view(jasData.last);
  lastMirror(0)          = last[0];
  lastMirror(1)          = last[1];
  Kokkos::deep_copy(jasData.last, lastMirror);

  jasData.updateMode      = Kokkos::View<int[1]>("updateMode");
  auto updateModeMirror   = Kokkos::create_mirror_view(jasData.updateMode);
  updateModeMirror(0)     = 3;
  Kokkos::deep_copy(jasData.updateMode, updateModeMirror);

  jasData.temporaryScratch = Kokkos::View<valT[1]>("temporaryScratch");
  jasData.temporaryScratchDim = Kokkos::View<valT[OHMMS_DIM]>("temporaryScratchDim");

  jasData.cur_Uat         = Kokkos::View<valT[1]>("cur_Uat");
  jasData.Uat             = Kokkos::View<valT*>("Uat", N);
  jasData.dUat            = Kokkos::View<valT*[OHMMS_DIM], Kokkos::LayoutLeft>("dUat", N);
  jasData.d2Uat           = Kokkos::View<valT*>("d2Uat", N);

  // these things are already views, so just do operator=
  jasData.cur_u = cur_u;
  jasData.old_u = old_u;
  jasData.cur_du = cur_du;
  jasData.old_du = old_du;
  jasData.cur_d2u = cur_d2u;
  jasData.old_d2u = old_d2u;
  jasData.DistCompressed = DistCompressed;
  jasData.DistIndices = DistIndice;

  // need to put in the data for A, dA and d2A    
  TinyVector<valT, 16> A(-1.0/6.0,  3.0/6.0, -3.0/6.0, 1.0/6.0,
  			  3.0/6.0, -6.0/6.0,  0.0/6.0, 4.0/6.0,
			 -3.0/6.0,  3.0/6.0,  3.0/6.0, 1.0/6.0,
			  1.0/6.0,  0.0/6.0,  0.0/6.0, 0.0/6.0);
  TinyVector<valT,16>  dA(0.0, -0.5,  1.0, -0.5,
			  0.0,  1.5, -2.0,  0.0,
			  0.0, -1.5,  1.0,  0.5,
			  0.0,  0.5,  0.0,  0.0);
  TinyVector<valT,16>  d2A(0.0, 0.0, -1.0,  1.0,
			   0.0, 0.0,  3.0, -2.0,
			   0.0, 0.0, -3.0,  1.0,
			   0.0, 0.0,  1.0,  0.0);
  
  jasData.A              = Kokkos::View<valT[16]>("A");
  auto Amirror           = Kokkos::create_mirror_view(jasData.A);
  jasData.dA             = Kokkos::View<valT[16]>("dA");
  auto dAmirror          = Kokkos::create_mirror_view(jasData.dA);
  jasData.d2A            = Kokkos::View<valT[16]>("d2A");
  auto d2Amirror         = Kokkos::create_mirror_view(jasData.d2A);

  for (int i = 0; i < 16; i++) {
    Amirror(i) = A[i];
    dAmirror(i) = dA[i];
    d2Amirror(i) = d2A[i];
  }

  Kokkos::deep_copy(jasData.A, Amirror);
  Kokkos::deep_copy(jasData.dA, dAmirror);
  Kokkos::deep_copy(jasData.d2A, d2Amirror);

  // also set up and allocate memory for cutoff_radius, DeltaRInv
  jasData.cutoff_radius   = Kokkos::View<valT*>("Cutoff_Radii", NumGroups*NumGroups);
  jasData.DeltaRInv       = Kokkos::View<valT*>("DeltaRInv", NumGroups*NumGroups);
  
  // unfortunately have to defer setting up SplineCoefs because we don't yet know
  // how many elements are in SplineCoefs on the cpu
  splCoefsNotAllocated   = true;
}
  

template<typename FT>
void TwoBodyJastrow<FT>::addFunc(int ia, int ib, FT* j)
{
  if (splCoefsNotAllocated) {
    splCoefsNotAllocated = false;
    jasData.SplineCoefs   = Kokkos::View<valT**>("SplineCoefficients", NumGroups*NumGroups, j->SplineCoefs.extent(0));
  }

  if (ia == ib)
  {
    if (ia == 0) // first time, assign everything
    {
      int ij = 0;
      for (int ig = 0; ig < NumGroups; ++ig)
        for (int jg = 0; jg < NumGroups; ++jg, ++ij) {
	  auto crMirror = Kokkos::create_mirror_view(jasData.cutoff_radius);
	  auto drinvMirror = Kokkos::create_mirror_view(jasData.cutoff_radius);
	  Kokkos::deep_copy(crMirror, jasData.cutoff_radius);
	  Kokkos::deep_copy(drinvMirror, jasData.DeltaRInv);
	  crMirror(ij) = j->cutoff_radius;
	  drinvMirror(ij) = j->DeltaRInv;
	  Kokkos::deep_copy(jasData.cutoff_radius, crMirror);
	  Kokkos::deep_copy(jasData.DeltaRInv, drinvMirror);
	  
	  auto bigScMirror   = Kokkos::create_mirror_view(jasData.SplineCoefs);
	  auto smallScMirror = Kokkos::create_mirror_view(j->SplineCoefs);
	  Kokkos::deep_copy(smallScMirror, j->SplineCoefs);
	  Kokkos::deep_copy(bigScMirror,   jasData.SplineCoefs);
	  for (int i = 0; i < j->SplineCoefs.extent(0); i++) {
	    bigScMirror(ij, i) = smallScMirror(i);
	  }
	  Kokkos::deep_copy(jasData.SplineCoefs, bigScMirror);
	}
    }
    else {
      int groupIndex = ia*NumGroups + ib;
      auto crMirror = Kokkos::create_mirror_view(jasData.cutoff_radius);
      auto drinvMirror = Kokkos::create_mirror_view(jasData.cutoff_radius);
      Kokkos::deep_copy(crMirror, jasData.cutoff_radius);
      Kokkos::deep_copy(drinvMirror, jasData.DeltaRInv);
      crMirror(groupIndex) = j->cutoff_radius;
      drinvMirror(groupIndex) = j->DeltaRInv;
      Kokkos::deep_copy(jasData.cutoff_radius, crMirror);
      Kokkos::deep_copy(jasData.DeltaRInv, drinvMirror);
      
      auto bigScMirror   = Kokkos::create_mirror_view(jasData.SplineCoefs);
      auto smallScMirror = Kokkos::create_mirror_view(j->SplineCoefs);
      Kokkos::deep_copy(smallScMirror, j->SplineCoefs);
      Kokkos::deep_copy(bigScMirror,   jasData.SplineCoefs);
      for (int i = 0; i < j->SplineCoefs.extent(0); i++) {
	bigScMirror(groupIndex, i) = smallScMirror(i);
      }
      Kokkos::deep_copy(jasData.SplineCoefs, bigScMirror);
    }
    
  }
  else
  {
    if (N == 2)
    {
      // a very special case, 1 up + 1 down
      // uu/dd was prevented by the builder
      for (int ig = 0; ig < NumGroups; ++ig)
        for (int jg = 0; jg < NumGroups; ++jg) {
	  int groupIndex = ig*NumGroups + jg;
	  auto crMirror = Kokkos::create_mirror_view(jasData.cutoff_radius);
	  auto drinvMirror = Kokkos::create_mirror_view(jasData.cutoff_radius);
	  Kokkos::deep_copy(crMirror, jasData.cutoff_radius);
	  Kokkos::deep_copy(drinvMirror, jasData.DeltaRInv);
	  crMirror(groupIndex) = j->cutoff_radius;
	  drinvMirror(groupIndex) = j->DeltaRInv;
	  Kokkos::deep_copy(jasData.cutoff_radius, crMirror);
	  Kokkos::deep_copy(jasData.DeltaRInv, drinvMirror);
	  
	  auto bigScMirror   = Kokkos::create_mirror_view(jasData.SplineCoefs);
	  auto smallScMirror = Kokkos::create_mirror_view(j->SplineCoefs);
	  Kokkos::deep_copy(smallScMirror, j->SplineCoefs);
	  Kokkos::deep_copy(bigScMirror,   jasData.SplineCoefs);
	  for (int i = 0; i < j->SplineCoefs.extent(0); i++) {
	    bigScMirror(groupIndex, i) = smallScMirror(i);
	  }
	  Kokkos::deep_copy(jasData.SplineCoefs, bigScMirror);
	}
    }
    else
    {
      // generic case
      int groupIndex = ia*NumGroups + ib;
      auto crMirror = Kokkos::create_mirror_view(jasData.cutoff_radius);
      auto drinvMirror = Kokkos::create_mirror_view(jasData.DeltaRInv);
      Kokkos::deep_copy(crMirror, jasData.cutoff_radius);
      Kokkos::deep_copy(drinvMirror, jasData.DeltaRInv);
      crMirror(groupIndex) = j->cutoff_radius;
      drinvMirror(groupIndex) = j->DeltaRInv;
      Kokkos::deep_copy(jasData.cutoff_radius, crMirror);
      Kokkos::deep_copy(jasData.DeltaRInv, drinvMirror);
      
      auto bigScMirror   = Kokkos::create_mirror_view(jasData.SplineCoefs);
      auto smallScMirror = Kokkos::create_mirror_view(j->SplineCoefs);
      Kokkos::deep_copy(smallScMirror, j->SplineCoefs);
      Kokkos::deep_copy(bigScMirror,   jasData.SplineCoefs);
      for (int i = 0; i < j->SplineCoefs.extent(0); i++) {
	bigScMirror(groupIndex, i) = smallScMirror(i);
      }
      Kokkos::deep_copy(jasData.SplineCoefs, bigScMirror);
    }
  }
  std::stringstream aname;
  aname << ia << ib;
//  J2Unique[aname.str()] = *j;
  FirstTime             = false;
}


/*
template<typename FT>
void TwoBodyJastrow<FT>::multi_evaluateLog(const std::vector<WaveFunctionComponent*>& WFC_list,
					   const std::vector<ParticleSet*>& P_list,
					   const std::vector<ParticleSet::ParticleGradient_t*>& G_list,
					   const std::vector<ParticleSet::ParticleLaplacian_t*>& L_list,
					   ParticleSet::ParticleValue_t& values) {
  // make a view of all of the TwoBodyJastrowData and relevantParticleSetData
  Kokkos::View<jasDataType*> allTwoBodyJastrowData("atbjd", WFC_list.size()); 
  Kokkos::View<ParticleSet::pskType*> allParticleSetData("apsd", P_list.size());
  populateCollectiveViews(allTwoBodyJastrowData, allParticleSetData, WFC_list, P_list);
  
  // need to make a view to hold all of the output LogValues
  Kokkos::View<ValueType*> tempValues("tempValues", P_list.size());
  
  // need to write this function
  doTwoBodyJastrowMultiEvaluateLog(allTwoBodyJastrowData, allParticleSetData, tempValues);
  
  // copy the results out to values
  auto tempValMirror = Kokkos::create_mirror_view(tempValues);
  Kokkos::deep_copy(tempValMirror, tempValues);
  
  for (int i = 0; i < P_list.size(); i++) {
    values[i] = tempValMirror(i);
  }
}
*/

template<typename FT>
void TwoBodyJastrow<FT>::multi_evaluateLog(const std::vector<WaveFunctionComponent*>& WFC_list,
					   WaveFunctionKokkos& wfc,
					   Kokkos::View<ParticleSet::pskType*>& psk,
					   ParticleSet::ParticleValue_t& values) {
  
  // need to write this function
  doTwoBodyJastrowMultiEvaluateLog(wfc.twoBodyJastrows, psk, wfc.ratios_view);

  Kokkos::deep_copy(wfc.ratios_view_mirror, wfc.ratios_view);
  
  for (int i = 0; i < WFC_list.size(); i++) {
    values[i] = wfc.ratios_view_mirror(i);
  }
}

/*
template<typename FT>
void TwoBodyJastrow<FT>::multi_evalGrad(const std::vector<WaveFunctionComponent*>& WFC_list,
					const std::vector<ParticleSet*>& P_list,
					int iat, std::vector<posT>& grad_now) {
  // make a view of all of the TwoBodyJastrowData and relevantParticleSetData
  Kokkos::View<jasDataType*> allTwoBodyJastrowData("atbjd", WFC_list.size()); 
  Kokkos::View<ParticleSet::pskType*> allParticleSetData("apsd", P_list.size());
  populateCollectiveViews(allTwoBodyJastrowData, allParticleSetData, WFC_list, P_list);
  
  // need to make a view to hold all of the output LogValues
  Kokkos::View<RealType**> grad_now_view("tempValues", P_list.size(), OHMMS_DIM);
  
  // need to write this function
  doTwoBodyJastrowMultiEvalGrad(allTwoBodyJastrowData, iat, grad_now_view);
  
  // copy the results out to values
  auto grad_now_view_mirror = Kokkos::create_mirror_view(grad_now_view);
  Kokkos::deep_copy(grad_now_view_mirror, grad_now_view);
  
  for (int i = 0; i < P_list.size(); i++) {
    for (int j = 0; j < OHMMS_DIM; j++) {
      grad_now[i][j] = grad_now_view_mirror(i,j);
    }
  }
}
*/

template<typename FT>
void TwoBodyJastrow<FT>::multi_evalGrad(const std::vector<WaveFunctionComponent*>& WFC_list,
					WaveFunctionKokkos& wfc,
					Kokkos::View<ParticleSet::pskType*>& psk,
					int iat,
					std::vector<posT>& grad_now) {
  const int numItems = WFC_list.size();

  doTwoBodyJastrowMultiEvalGrad(wfc.twoBodyJastrows, iat, wfc.grad_view);
  // copy the results out to values
  Kokkos::deep_copy(wfc.grad_view_mirror, wfc.grad_view);

  for (int i = 0; i < numItems; i++) {
    for (int j = 0; j < OHMMS_DIM; j++) {
      grad_now[i][j] = wfc.grad_view_mirror(i,j);
    }
  }
}

template<typename FT>
void TwoBodyJastrow<FT>::multi_ratioGrad(const std::vector<WaveFunctionComponent*>& WFC_list,
					 WaveFunctionKokkos& wfc,
					 Kokkos::View<ParticleSet::pskType*> psk,
					 int iel,
					 Kokkos::View<int*>& isValidMap, int numValid,
					 std::vector<ValueType>& ratios,
					 std::vector<PosType>& grad_new) {
  if (numValid > 0) {

    doTwoBodyJastrowMultiRatioGrad(wfc.twoBodyJastrows, psk, isValidMap, numValid, iel,
				   wfc.grad_view, wfc.ratios_view, typename Kokkos::View<int*>::memory_space());
    Kokkos::fence();

    // copy the results out to values                                                                                                          
    Kokkos::deep_copy(wfc.grad_view_mirror, wfc.grad_view);
    Kokkos::deep_copy(wfc.ratios_view_mirror, wfc.ratios_view);
    //std::cout << "       finished copying grad and ratios out" << std::endl;                                                                 

    for (int i = 0; i < numValid; i++) {
      ratios[i] = wfc.ratios_view_mirror(i);
      for (int j = 0; j < OHMMS_DIM; j++) {
	grad_new[i][j] += wfc.grad_view_mirror(i,j);
      }
    }
  }
  //std::cout << "      finishing J1 multi_ratioGrad" << std::endl;                                                                            
 }


/*
template<typename FT>
void TwoBodyJastrow<FT>::multi_ratioGrad(const std::vector<WaveFunctionComponent*>& WFC_list,
					 const std::vector<ParticleSet*>& P_list,
					 int iat, std::vector<valT>& ratios,
					 std::vector<posT>& grad_new) {
  // make a view of all of the TwoBodyJastrowData and relevantParticleSetData
  Kokkos::View<jasDataType*> allTwoBodyJastrowData("atbjd", WFC_list.size()); 
  Kokkos::View<ParticleSet::pskType*> allParticleSetData("apsd", P_list.size());
  populateCollectiveViews(allTwoBodyJastrowData, allParticleSetData, WFC_list, P_list);
  
  // need to make a view to hold all of the output LogValues
  Kokkos::View<RealType**> grad_new_view("tempValues", P_list.size(), OHMMS_DIM);
  Kokkos::View<RealType*> ratios_view("ratios", P_list.size());
    
  // need to write this function
  doTwoBodyJastrowMultiRatioGrad(allTwoBodyJastrowData, allParticleSetData, iat, grad_new_view, ratios_view);

  // copy the results out to values
  auto grad_new_view_mirror = Kokkos::create_mirror_view(grad_new_view);
  Kokkos::deep_copy(grad_new_view_mirror, grad_new_view);
  auto ratios_view_mirror = Kokkos::create_mirror_view(ratios_view);
  Kokkos::deep_copy(ratios_view_mirror, ratios_view);
  
  for (int i = 0; i < P_list.size(); i++) {
    ratios[i] = ratios_view_mirror(i);
    for (int j = 0; j < OHMMS_DIM; j++) {
      grad_new[i][j] += grad_new_view_mirror(i,j);
    }
  }
}
*/

// still need to fix this one
template<typename FT>
void TwoBodyJastrow<FT>::multi_evalRatio(int pairNum, Kokkos::View<int***>& eiList,
					 WaveFunctionKokkos& wfc,
					 Kokkos::View<ParticleSetKokkos<RealType, ValueType, 3>*>& apsk,
					 Kokkos::View<RealType***>& likeTempR,
					 Kokkos::View<RealType***>& unlikeTempR,
					 std::vector<ValueType>& ratios, int numActive) {
  Kokkos::Profiling::pushRegion("tbj-multi_eval_ratio");
  const int numKnots = likeTempR.extent(1);

  Kokkos::Profiling::pushRegion("tbj-multi_eval_ratio-meat");
  doTwoBodyJastrowMultiEvalRatio(pairNum, eiList, apsk, wfc.twoBodyJastrows, likeTempR, 
				 wfc.knots_ratios_view, wfc.activeMap, numActive);
  Kokkos::Profiling::popRegion();  

  Kokkos::Profiling::pushRegion("tbj-multi_eval_ratio-postlude");

  Kokkos::deep_copy(wfc.knots_ratios_view_mirror, wfc.knots_ratios_view);
  for (int i = 0; i < numActive; i++) {
    const int walkerNum = wfc.activeMapMirror(i);
    for (int j = 0; j < wfc.knots_ratios_view_mirror.extent(1); j++) {
      ratios[walkerNum*numKnots+j] = wfc.knots_ratios_view_mirror(i,j);
    }
  }
  Kokkos::Profiling::popRegion();
  Kokkos::Profiling::popRegion();
}
template<typename FT>
void TwoBodyJastrow<FT>::multi_acceptrestoreMove(const std::vector<WaveFunctionComponent*>& WFC_list,
						 WaveFunctionKokkos& wfc,
						 Kokkos::View<ParticleSet::pskType*> psk,
						 Kokkos::View<int*>& isAcceptedMap, int numAccepted, int iel) {
  doTwoBodyJastrowMultiAcceptRestoreMove(wfc.twoBodyJastrows, psk, isAcceptedMap, numAccepted, 
					 iel, wfc.numElectrons, wfc.numIons, typename Kokkos::DefaultExecutionSpace::memory_space());
}

/*
template<typename FT>
void TwoBodyJastrow<FT>::multi_acceptRestoreMove(const std::vector<WaveFunctionComponent*>& WFC_list,
						 const std::vector<ParticleSet*>& P_list,
						 const std::vector<bool>& isAccepted, int iat) {
  int numAccepted = 0;
  for (int i = 0; i < isAccepted.size(); i++) {
    if (isAccepted[i]) {
      numAccepted++;
    }
  }
  
  // make a view of all of the TwoBodyJastrowData and relevantParticleSetData
  Kokkos::View<jasDataType*> allTwoBodyJastrowData("atbjd", numAccepted); 
  Kokkos::View<ParticleSet::pskType*> allParticleSetData("apsd", numAccepted);
  populateCollectiveViews(allTwoBodyJastrowData, allParticleSetData, WFC_list, P_list, isAccepted);
  
  // need to write this function
  doTwoBodyJastrowMultiAcceptRestoreMove(allTwoBodyJastrowData, allParticleSetData, iat);
  
  // be careful on this one, looks like it is being done for side effects.  Should see what needs to go back!!!
}
*/

template<typename FT>
void TwoBodyJastrow<FT>::multi_evaluateGL(const std::vector<WaveFunctionComponent*>& WFC_list,
					  const std::vector<ParticleSet*>& P_list,
					  const std::vector<ParticleSet::ParticleGradient_t*>& G_list,
					  const std::vector<ParticleSet::ParticleLaplacian_t*>& L_list,
					  bool fromscratch) {
  // make a view of all of the TwoBodyJastrowData and relevantParticleSetData
  Kokkos::View<jasDataType*> allTwoBodyJastrowData("atbjd", WFC_list.size()); 
  Kokkos::View<ParticleSet::pskType*> allParticleSetData("apsd", P_list.size());
  populateCollectiveViews(allTwoBodyJastrowData, allParticleSetData, WFC_list, P_list);
  
  // need to write this function
  doTwoBodyJastrowMultiEvaluateGL(allTwoBodyJastrowData, allParticleSetData, N, fromscratch);
  
  // know that we will need LogValue to up updated after this, possibly other things in ParticleSet!!!
  for (int i = 0; i < WFC_list.size(); i++) {
    auto LogValueMirror = Kokkos::create_mirror_view(static_cast<TwoBodyJastrow*>(WFC_list[i])->jasData.LogValue);
    Kokkos::deep_copy(LogValueMirror, static_cast<TwoBodyJastrow*>(WFC_list[i])->jasData.LogValue);
    LogValue = LogValueMirror(0);
  }
}

} // namespace qmcplusplus
#endif
