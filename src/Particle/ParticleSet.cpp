////////////////////////////////////////////////////////////////////////////////
// This file is distributed under the University of Illinois/NCSA Open Source
// License.  See LICENSE file in top directory for details.
//
// Copyright (c) 2016 Jeongnim Kim and QMCPACK developers.
//
// File developed by:
// Ken Esler, kpesler@gmail.com,
//    University of Illinois at Urbana-Champaign
// Luke Shulenburger, lshulen@sandia.gov,
//    Sandia National Laboratories
// Jeremy McMinnis, jmcminis@gmail.com,
//    University of Illinois at Urbana-Champaign
// Jeongnim Kim, jeongnim.kim@gmail.com,
//    University of Illinois at Urbana-Champaign
// Jaron T. Krogel, krogeljt@ornl.gov,
//    Oak Ridge National Laboratory
// Ye Luo, yeluo@anl.gov,
//    Argonne National Laboratory
// Mark A. Berrill, berrillma@ornl.gov,
//    Oak Ridge National Laboratory
// Mark Dewing, markdewing@gmail.com,
//    University of Illinois at Urbana-Champaign
//
// File created by:
// Jeongnim Kim, jeongnim.kim@gmail.com,
//    University of Illinois at Urbana-Champaign
////////////////////////////////////////////////////////////////////////////////

#include <numeric>
#include <iomanip>
#include "Particle/ParticleSet.h"
#include "Particle/DistanceTableData.h"
#include "Particle/DistanceTable.h"
#include "Utilities/RandomGenerator.h"

/** @file ParticleSet.cpp
 * @brief Particle positions and related data
 */

//#define PACK_DISTANCETABLES

namespace qmcplusplus
{
enum DistanceTimers
{
  Timer_makeMove,
  Timer_setActive,
  Timer_acceptMove
};

TimerNameList_t<DistanceTimers> DistanceTimerNames{
    {Timer_makeMove, "Make move"},
    {Timer_setActive, "Set active"},
    {Timer_acceptMove, "Accept move"},
};

ParticleSet::ParticleSet()
    : UseBoundBox(true), IsGrouped(true), myName("none"), SameMass(true), myTwist(0.0), activePtcl(-1)
{
  setup_timers(timers, DistanceTimerNames, timer_level_coarse);
}

ParticleSet::ParticleSet(const ParticleSet& p)
    : UseBoundBox(p.UseBoundBox),
      IsGrouped(p.IsGrouped),
      mySpecies(p.getSpeciesSet()),
      SameMass(true),
      myTwist(0.0),
      activePtcl(-1)
{
  //distance_timer = TimerManager.createTimer("Distance Tables", timer_level_coarse);
  setup_timers(timers, DistanceTimerNames, timer_level_coarse);
  // initBase();
  assign(p); // only the base is copied, assumes that other properties are not
             // assignable
  // need explicit copy:
  Mass = p.Mass;
  Z    = p.Z;
  this->setName(p.getName());
  app_log() << "  Copying a particle set " << p.getName() << " to " << this->getName()
            << " groups=" << groups() << std::endl;
  // construct the distance tables with the same order
  if (p.DistTables.size())
  {
    app_log() << "  Cloning distance tables. It has " << p.DistTables.size() << std::endl;
    addTable(*this,
             p.DistTables[0]->DTType); // first is always for this-this pair
    for (int i = 1; i < p.DistTables.size(); ++i)
      addTable(p.DistTables[i]->origin(), p.DistTables[i]->DTType);
  }

  for (int i = 0; i < p.DistTables.size(); ++i)
  {
    DistTables[i]->Need_full_table_loadWalker = p.DistTables[i]->Need_full_table_loadWalker;
  }
  myTwist = p.myTwist;

  RSoA.resize(TotalNum);
}

ParticleSet::~ParticleSet() { clearDistanceTables(); }

void ParticleSet::pushDataToParticleSetKokkos() {
  const int numElec = TotalNum;
  const int numIons = DistTables[1]->centers();

  psk.ID           = Kokkos::View<int*>("ID", numElec);
  psk.IndirectID   = Kokkos::View<int*>("IndirectID", numElec);
  psk.GroupID      = Kokkos::View<int*>("GroupID", numElec);
  psk.SubPtcl      = Kokkos::View<int*>("SubPtcl", SubPtcl.size());
  psk.R            = Kokkos::View<RealType*[DIM],Kokkos::LayoutRight>("R", numElec);
  psk.RSoA         = Kokkos::View<RealType*[DIM],Kokkos::LayoutLeft>("RSoA", numElec);
  psk.G            = Kokkos::View<ValueType*[DIM],Kokkos::LayoutLeft>("G", numElec);
  psk.L            = Kokkos::View<ValueType*>("L", numElec);
  psk.UseBoundBox  = Kokkos::View<bool[1]>("UseBoundBox");
  psk.IsGrouped    = Kokkos::View<bool[1]>("IsGrouped");
  psk.activePtcl   = Kokkos::View<int[1]>("activePtcl");
  psk.activePos    = Kokkos::View<RealType[DIM]>("activePos");

  // distance table related stuff
  psk.DT_G                  = Kokkos::View<RealType[DIM][DIM]>("DT_G"); //reciprocal lattice vectors
  psk.DT_R                  = Kokkos::View<RealType[DIM][DIM]>("DT_R"); //real lattice vectors (from Lattice.a)
  psk.BoxBConds             = Kokkos::View<int[DIM]>("BoxBConds");
  psk.corners               = Kokkos::View<RealType[8][DIM],Kokkos::LayoutLeft>("corners");
  psk.LikeDTDistances       = Kokkos::View<RealType**>("LikeDTDistances", numElec, numElec);
  psk.LikeDTDisplacements   = Kokkos::View<RealType**[DIM]>("LikeDTDisplacements", numElec, numElec);
  psk.LikeDTTemp_r          = Kokkos::View<RealType*>("LikeDTTemp_r", numElec);
  psk.LikeDTTemp_dr         = Kokkos::View<RealType*[DIM],Kokkos::LayoutLeft>("LikeDTTemp_dr", numElec);
  psk.UnlikeDTDistances     = Kokkos::View<RealType**>("UnlikeDTDistances", numElec, numIons);
  psk.UnlikeDTDisplacements = Kokkos::View<RealType**[DIM]>("UnlikeDTDisplacements", numElec, numIons);
  psk.UnlikeDTTemp_r        = Kokkos::View<RealType*>("UnlikeDTTemp_r", numIons);
  psk.UnlikeDTTemp_dr       = Kokkos::View<RealType*[DIM],Kokkos::LayoutLeft>("UnlikeDTTemp_dr", numElec);
  psk.originR               = Kokkos::View<RealType*[DIM],Kokkos::LayoutLeft>("OriginR", numIons);
  psk.numIonGroups          = Kokkos::View<int[1]>("numIonGroups");
  psk.ionGroupID            = Kokkos::View<int*>("ionGroupID", numIons);
  psk.ionSubPtcl            = Kokkos::View<int*>("ionSubPtcl", DistTables[1]->Origin->SubPtcl.size());
  
  auto IDMirror                    = Kokkos::create_mirror_view(psk.ID);
  auto IndirectIDMirror            = Kokkos::create_mirror_view(psk.IndirectID); 
  auto GroupIDMirror               = Kokkos::create_mirror_view(psk.GroupID);
  auto SubPtclMirror               = Kokkos::create_mirror_view(psk.SubPtcl);
  auto RMirror                     = Kokkos::create_mirror_view(psk.R);
  auto RSoAMirror                  = Kokkos::create_mirror_view(psk.RSoA);
  auto GMirror                     = Kokkos::create_mirror_view(psk.G);
  auto LMirror                     = Kokkos::create_mirror_view(psk.L);
  auto UseBoundBoxMirror           = Kokkos::create_mirror_view(psk.UseBoundBox);
  auto IsGroupedMirror             = Kokkos::create_mirror_view(psk.IsGrouped);
  auto activePtclMirror            = Kokkos::create_mirror_view(psk.activePtcl);
  auto activePosMirror             = Kokkos::create_mirror_view(psk.activePos);
  auto DT_GMirror                  = Kokkos::create_mirror_view(psk.DT_G);
  auto DT_RMirror                  = Kokkos::create_mirror_view(psk.DT_R);
  auto BoxBCondsMirror             = Kokkos::create_mirror_view(psk.BoxBConds);
  auto cornersMirror               = Kokkos::create_mirror_view(psk.corners);
  auto LikeDTDistancesMirror       = Kokkos::create_mirror_view(psk.LikeDTDistances);
  auto LikeDTDisplacementsMirror   = Kokkos::create_mirror_view(psk.LikeDTDisplacements);
  auto LikeDTTemp_rMirror          = Kokkos::create_mirror_view(psk.LikeDTTemp_r);
  auto LikeDTTemp_drMirror         = Kokkos::create_mirror_view(psk.LikeDTTemp_dr);
  auto UnlikeDTDistancesMirror     = Kokkos::create_mirror_view(psk.UnlikeDTDistances);
  auto UnlikeDTDisplacementsMirror = Kokkos::create_mirror_view(psk.UnlikeDTDisplacements);
  auto UnlikeDTTemp_rMirror        = Kokkos::create_mirror_view(psk.UnlikeDTTemp_r);
  auto UnlikeDTTemp_drMirror       = Kokkos::create_mirror_view(psk.UnlikeDTTemp_dr);
  auto originRMirror               = Kokkos::create_mirror_view(psk.originR);
  auto numIonGroupsMirror          = Kokkos::create_mirror_view(psk.numIonGroups);
  auto ionGroupIDMirror            = Kokkos::create_mirror_view(psk.ionGroupID);
  auto ionSubPtclMirror            = Kokkos::create_mirror_view(psk.ionSubPtcl);

  
  UseBoundBoxMirror(0) = UseBoundBox;
  IsGroupedMirror(0)   = IsGrouped;
  activePtclMirror(0)  = activePtcl;
  activePosMirror(0)   = activePos[0];
  activePosMirror(1)   = activePos[1];
  activePosMirror(2)   = activePos[2];

  for (int i = 0; i < SubPtcl.size(); i++) {
    SubPtclMirror(i) = SubPtcl[i];
  }
  
  for (int i = 0; i < numElec; i++) {
    IDMirror(i)            = ID[i];
    IndirectIDMirror(i)    = IndirectID[i];
    GroupIDMirror(i)       = GroupID[i];
    LMirror(i) = L[i];

    for (int j = 0; j < DIM; j++) {
      RMirror(i,j) = R[i][j];
      RSoAMirror(i,j) = R[i][j];
      GMirror(i,j) = G[i][j];
    }   
  }

  for (int d1 = 0; d1 < DIM; d1++) {
    for (int d2 = 0; d2 < DIM; d2++) {
      DT_RMirror(d1, d2) = Lattice.R(d2, d1);
      DT_GMirror(d1, d2) = Lattice.Gt(d1, d2); // possible this should be Lattice.G instead of Gt
    }
  }

  for (int d = 0; d < DIM; d++) {
    //    BoxBCondsMirror(d) = BoxBConds[d];
    BoxBCondsMirror(d) = 1;
  }

  for (int d = 0; d < DIM; d++) {
    cornersMirror(0,d) = 0.0;
    cornersMirror(1,d) = -1.0 * Lattice.R(0,d);
    cornersMirror(2,d) = -1.0 * Lattice.R(1,d);
    cornersMirror(3,d) = -1.0 * Lattice.R(2,d);
    cornersMirror(4,d) = -1.0 * (Lattice.R(0,d)+Lattice.R(1,d));
    cornersMirror(5,d) = -1.0 * (Lattice.R(0,d)+Lattice.R(2,d));
    cornersMirror(6,d) = -1.0 * (Lattice.R(1,d)+Lattice.R(2,d));
    cornersMirror(7,d) = -1.0 * (Lattice.R(0,d)+Lattice.R(1,d)+Lattice.R(2,d));
  }


  for (int i = 0; i < numElec; i++) {
    for (int j = 0; j < numElec; j++) {
      LikeDTDistancesMirror(i,j) = DistTables[0]->Distances[i][j];
      for (int d = 0; d < DIM; d++) { 
	LikeDTDisplacementsMirror(i,j,d) = DistTables[0]->Displacements[i][j][d];
      }
    }
    LikeDTTemp_rMirror(i) = DistTables[0]->Temp_r[i];
    for (int d = 0; d < DIM; d++) {
      LikeDTTemp_drMirror(i,d) = DistTables[0]->Temp_dr[i][d];
    }
  }

  for (int j = 0; j < numIons; j++) {
    for (int i = 0; i < numElec; i++) {
      UnlikeDTDistancesMirror(i,j) = DistTables[1]->Distances[i][j];
      for (int d = 0; d < DIM; d++) { 
	UnlikeDTDisplacementsMirror(i,j,d) = DistTables[1]->Displacements[i][j][d];
      }
    }
    UnlikeDTTemp_rMirror(j) = DistTables[1]->Temp_r[j];
    for (int d = 0; d < DIM; d++) {
      UnlikeDTTemp_drMirror(j,d) = DistTables[1]->Temp_dr[j][d];
    }
  }

  for (int i = 0; i < numIons; i++) {
    ionGroupIDMirror(i) = DistTables[1]->Origin->GroupID[i];
    for (int d = 0; d < DIM; d++) {
      originRMirror(i,d) = DistTables[1]->Origin->RSoA[i][d];
    }
  }

  numIonGroupsMirror(0) = DistTables[1]->Origin->SubPtcl.size()-1;
  for (int i = 0; i < DistTables[1]->Origin->SubPtcl.size(); i++) {
    ionSubPtclMirror(i) = DistTables[1]->Origin->SubPtcl[i];
  }
  
  Kokkos::deep_copy(psk.ID, IDMirror);
  Kokkos::deep_copy(psk.IndirectID, IndirectIDMirror);
  Kokkos::deep_copy(psk.GroupID, GroupIDMirror);
  Kokkos::deep_copy(psk.SubPtcl, SubPtclMirror);
  Kokkos::deep_copy(psk.R, RMirror);
  Kokkos::deep_copy(psk.RSoA, RSoAMirror);
  Kokkos::deep_copy(psk.G, GMirror);
  Kokkos::deep_copy(psk.L, LMirror);
  Kokkos::deep_copy(psk.UseBoundBox, UseBoundBoxMirror);
  Kokkos::deep_copy(psk.IsGrouped, IsGroupedMirror);
  Kokkos::deep_copy(psk.activePtcl, activePtclMirror);
  Kokkos::deep_copy(psk.activePos, activePosMirror);
  Kokkos::deep_copy(psk.DT_G, DT_GMirror);
  Kokkos::deep_copy(psk.DT_R, DT_RMirror);
  Kokkos::deep_copy(psk.BoxBConds, BoxBCondsMirror);
  Kokkos::deep_copy(psk.corners, cornersMirror);
  Kokkos::deep_copy(psk.LikeDTDistances, LikeDTDistancesMirror);
  Kokkos::deep_copy(psk.LikeDTDisplacements, LikeDTDisplacementsMirror);
  Kokkos::deep_copy(psk.LikeDTTemp_r, LikeDTTemp_rMirror);
  Kokkos::deep_copy(psk.LikeDTTemp_dr, LikeDTTemp_drMirror);
  Kokkos::deep_copy(psk.UnlikeDTDistances, UnlikeDTDistancesMirror);
  Kokkos::deep_copy(psk.UnlikeDTDisplacements, UnlikeDTDisplacementsMirror);
  Kokkos::deep_copy(psk.UnlikeDTTemp_r, UnlikeDTTemp_rMirror);
  Kokkos::deep_copy(psk.UnlikeDTTemp_dr, UnlikeDTTemp_drMirror);
  Kokkos::deep_copy(psk.originR, originRMirror);
  Kokkos::deep_copy(psk.numIonGroups, numIonGroupsMirror);
  Kokkos::deep_copy(psk.ionGroupID, ionGroupIDMirror);
  Kokkos::deep_copy(psk.ionSubPtcl, ionSubPtclMirror);
}

  
void ParticleSet::create(int numPtcl)
{
  resize(numPtcl);
  GroupID = 0;
  R       = RealType(0);
}

void ParticleSet::create(const std::vector<int>& agroup)
{
  SubPtcl.resize(agroup.size() + 1);
  SubPtcl[0] = 0;
  for (int is = 0; is < agroup.size(); is++)
    SubPtcl[is + 1] = SubPtcl[is] + agroup[is];
  size_t nsum = SubPtcl[agroup.size()];
  resize(nsum);
  TotalNum = nsum;
  int loc  = 0;
  for (int i = 0; i < agroup.size(); i++)
  {
    for (int j = 0; j < agroup[i]; j++, loc++)
      GroupID[loc] = i;
  }
}

void ParticleSet::resetGroups()
{
  int nspecies = mySpecies.getTotalNum();
  if (nspecies == 0)
  {
    APP_ABORT("ParticleSet::resetGroups() Failed. No species exisits");
  }
  int natt = mySpecies.numAttributes();
  int qind = mySpecies.addAttribute("charge");
  if (natt == qind)
  {
    app_log() << " Missing charge attribute of the SpeciesSet " << myName << " particleset"
              << std::endl;
    app_log() << " Assume neutral particles Z=0.0 " << std::endl;
    for (int ig = 0; ig < nspecies; ig++)
      mySpecies(qind, ig) = 0.0;
  }
  for (int iat = 0; iat < Z.size(); iat++)
    Z[iat] = mySpecies(qind, GroupID[iat]);
  natt        = mySpecies.numAttributes();
  int massind = mySpecies.addAttribute("mass");
  if (massind == natt)
  {
    for (int ig = 0; ig < nspecies; ig++)
      mySpecies(massind, ig) = 1.0;
  }
  SameMass  = true;
  double m0 = mySpecies(massind, 0);
  for (int ig = 1; ig < nspecies; ig++)
    SameMass &= (mySpecies(massind, ig) == m0);
  if (SameMass)
    app_log() << "  All the species have the same mass " << m0 << std::endl;
  else
    app_log() << "  Distinctive masses for each species " << std::endl;
  for (int iat = 0; iat < Mass.size(); iat++)
    Mass[iat] = mySpecies(massind, GroupID[iat]);
  std::vector<int> ng(nspecies, 0);
  for (int iat = 0; iat < GroupID.size(); iat++)
  {
    if (GroupID[iat] < nspecies)
      ng[GroupID[iat]]++;
    else
      APP_ABORT("ParticleSet::resetGroups() Failed. GroupID is out of bound.");
  }
  SubPtcl.resize(nspecies + 1);
  SubPtcl[0] = 0;
  for (int i = 0; i < nspecies; ++i)
    SubPtcl[i + 1] = SubPtcl[i] + ng[i];
  int membersize = mySpecies.addAttribute("membersize");
  for (int ig = 0; ig < nspecies; ++ig)
    mySpecies(membersize, ig) = ng[ig];
  // orgID=ID;
  // orgGroupID=GroupID;
  int new_id = 0;
  for (int i = 0; i < nspecies; ++i)
    for (int iat = 0; iat < GroupID.size(); ++iat)
      if (GroupID[iat] == i)
        IndirectID[new_id++] = ID[iat];
  IsGrouped = true;
  for (int iat = 0; iat < ID.size(); ++iat)
    IsGrouped &= (IndirectID[iat] == ID[iat]);
  if (IsGrouped)
    app_log() << "Particles are grouped. Safe to use groups " << std::endl;
  else
    app_log() << "ID is not grouped. Need to use IndirectID for "
                 "species-dependent operations "
              << std::endl;
}

/// write to a std::ostream
bool ParticleSet::get(std::ostream& os) const
{
  os << "  ParticleSet " << getName() << " : ";
  for (int i = 0; i < SubPtcl.size(); i++)
    os << SubPtcl[i] << " ";
  os << "\n\n    " << TotalNum << "\n\n";
  const int maxParticlesToPrint = 10;
  int numToPrint                = std::min(TotalNum, maxParticlesToPrint);

  for (int i = 0; i < numToPrint; i++)
  {
    os << "    " << mySpecies.speciesName[GroupID[i]] << R[i] << std::endl;
  }

  if (numToPrint < TotalNum)
  {
    os << "    (... and " << (TotalNum - numToPrint) << " more particle positions ...)" << std::endl;
  }

  return true;
}

/// read from std::istream
bool ParticleSet::put(std::istream& is) { return true; }

/// reset member data
void ParticleSet::reset() { app_log() << "<<<< going to set properties >>>> " << std::endl; }

void ParticleSet::setBoundBox(bool yes) { UseBoundBox = yes; }

int ParticleSet::addTable(const ParticleSet& psrc, int dt_type)
{
  if (myName == "none")
    APP_ABORT("ParticleSet::addTable needs a proper name for this particle set.");
  if (DistTables.empty())
  {
    DistTables.reserve(4);
    DistTables.push_back(createDistanceTable(*this, dt_type));
    // add  this-this pair
    myDistTableMap.clear();
    myDistTableMap[myName] = 0;
    app_log() << "  ... ParticleSet::addTable Create Table #0 " << DistTables[0]->Name << std::endl;
    if (psrc.getName() == myName)
      return 0;
  }
  if (psrc.getName() == myName)
  {
    app_log() << "  ... ParticleSet::addTable Reuse Table #" << 0 << " " << DistTables[0]->Name
              << std::endl;
    // if(!DistTables[0]->is_same_type(dt_type))
    //{//itself is special, cannot mix them: some of the users do not check the
    // index
    //  APP_ABORT("ParticleSet::addTable for itself Cannot mix AoS and SoA
    //  distance tables.\n");
    //}
    return 0;
  }
  int tid;
  std::map<std::string, int>::iterator tit(myDistTableMap.find(psrc.getName()));
  if (tit == myDistTableMap.end())
  {
    tid = DistTables.size();
    DistTables.push_back(createDistanceTable(psrc, *this, dt_type));
    myDistTableMap[psrc.getName()] = tid;
    app_log() << "  ... ParticleSet::addTable Create Table #" << tid << " " << DistTables[tid]->Name
              << std::endl;
  }
  else
  {
    tid = (*tit).second;
    if (dt_type == DT_SOA_PREFERRED || DistTables[tid]->is_same_type(dt_type)) // good to reuse
    {
      app_log() << "  ... ParticleSet::addTable Reuse Table #" << tid << " "
                << DistTables[tid]->Name << std::endl;
    }
    else
    {
      APP_ABORT("ParticleSet::addTable Cannot mix AoS and SoA distance tables.\n");
    }
    // if(dt_type == DT_SOA || dt_type == DT_AOS) //not compatible
    //{
    //}
    // for DT_SOA_PREFERRED or DT_AOS_PREFERRED, return the existing table
  }
  app_log().flush();
  return tid;
}

void ParticleSet::update(bool skipSK)
{
  RSoA.copyIn(R);
  for (int i = 0; i < DistTables.size(); i++)
    DistTables[i]->evaluate(*this);
  activePtcl = -1;
}

void ParticleSet::setActive(int iat)
{
  ScopedTimer local_timer(timers[Timer_setActive]);

  for (size_t i = 0, n = DistTables.size(); i < n; i++)
    DistTables[i]->evaluate(*this, iat);
}

void ParticleSet::multi_setActiveKokkos(std::vector<ParticleSet*>& P_list, int iel)
{
  ScopedTimer local_timer(timers[Timer_setActive]);
  Kokkos::View<ParticleSet::pskType*> allParticleSetData("apsd", P_list.size());
  auto apsdMirror = Kokkos::create_mirror_view(allParticleSetData);
  for (int i = 0; i < P_list.size(); i++) {
    apsdMirror(i) = P_list[i]->psk;
  }
  int locIel = iel;
  Kokkos::deep_copy(allParticleSetData, apsdMirror);

  using BarePolicy = Kokkos::TeamPolicy<>;
  BarePolicy pol(P_list.size(), Kokkos::AUTO, 32);
  Kokkos::parallel_for("ps-setActive", pol,
		       KOKKOS_LAMBDA(BarePolicy::member_type member) {
			 const int i = member.league_rank();
			 allParticleSetData(i).setActivePtcl(member, locIel);
		       });
}


/** move a particle iat
 * @param iat the index of the particle to be moved
 * @param displ the displacement of the iath-particle position
 * @return the proposed position
 *
 * Update activePtcl index and activePos position for the proposed move.
 * Evaluate the related distance table data DistanceTableData::Temp.
 */
bool ParticleSet::makeMoveAndCheck(Index_t iat, const SingleParticlePos_t& displ)
{
  ScopedTimer local_timer(timers[Timer_makeMove]);

  activePtcl = iat;
  activePos  = R[iat] + displ;
  if (UseBoundBox)
  {
    if (Lattice.outOfBound(Lattice.toUnit(displ)))
    {
      activePtcl = -1;
      return false;
    }
    newRedPos = Lattice.toUnit(activePos);
    if (Lattice.isValid(newRedPos))
    {
      for (int i = 0; i < DistTables.size(); ++i)
        DistTables[i]->move(*this, activePos);
      return true;
    }
    // out of bound
    activePtcl = -1;
    return false;
  }
  else
  {
    for (int i = 0; i < DistTables.size(); ++i)
      DistTables[i]->move(*this, activePos);
    return true;
  }
}

void ParticleSet::multi_makeMoveAndCheckKokkos(Kokkos::View<ParticleSet::pskType*>& allParticleSetData, 
					       Kokkos::View<RealType*[3]>& dr,
					       int iel, Kokkos::View<int*>& isValidList)
{
  ScopedTimer local_timer(timers[Timer_makeMove]);
  int locIel = iel;
  auto& locDr = dr;

  using BarePolicy = Kokkos::TeamPolicy<>;
  BarePolicy pol(allParticleSetData.extent(0), Kokkos::AUTO, 32);
  Kokkos::parallel_for("ps-makeMoveAndCheck", pol,
		       KOKKOS_LAMBDA(BarePolicy::member_type member) {
			 const int i = member.league_rank();
			 auto& pset = allParticleSetData(i);
			 pset.activePtcl(0) = locIel;
			 for (int d = 0; d < 3; d++) { 
			   pset.activePos(d) = pset.R(i,d) + locDr(i,d);
			 }
			 if (pset.UseBoundBox(0)) {
			   RealType x;
			   RealType y;
			   RealType z;
			   pset.toUnit(locDr(i,0), locDr(i,1), locDr(i,2), x, y, z);
			   if (pset.outOfBound(x,y,z)) {
			     pset.activePtcl(0) = -1;
			     isValidList(i) = 0;
			   } else {
			     pset.toUnit(pset.activePos(0), pset.activePos(1), pset.activePos(2), x, y, z);
			     if (pset.isValid(x,y,z)) {
			       pset.LikeMove(member, pset.activePos(0), pset.activePos(1), pset.activePos(2));
			       pset.UnlikeMove(member, pset.activePos(0), pset.activePos(1), pset.activePos(2));
			       isValidList(i) = 1;
			     } else {
			       isValidList(i) = 0;
			     }
			   }
			 } else {
			   pset.LikeMove(member, pset.activePos(0), pset.activePos(1), pset.activePos(2));
			   pset.UnlikeMove(member, pset.activePos(0), pset.activePos(1), pset.activePos(2));
			   isValidList(i) = 1;
			 }
		       });
}

/** move the iat-th particle by displ
 *
 * @param iat the particle that is moved on a sphere
 * @param displ displacement from the current position
 */
void ParticleSet::makeMoveOnSphere(Index_t iat, const SingleParticlePos_t& displ)
{
  ScopedTimer local_timer(timers[Timer_makeMove]);

  activePtcl = iat;
  activePos  = R[iat] + displ;
  for (int i = 0; i < DistTables.size(); ++i)
    DistTables[i]->moveOnSphere(*this, activePos);
}

/** update the particle attribute by the proposed move
 *@param iat the particle index
 *
 *When the activePtcl is equal to iat, overwrite the position and update the
 *content of the distance tables.
 */
void ParticleSet::acceptMove(Index_t iat)
{
  ScopedTimer local_timer(timers[Timer_acceptMove]);

  if (iat == activePtcl)
  {
    // Update position + distance-table
    for (int i = 0, n = DistTables.size(); i < n; i++)
      DistTables[i]->update(iat);

    R[iat]     = activePos;
    RSoA(iat)  = activePos;
    activePtcl = -1;
  }
  else
  {
    std::ostringstream o;
    o << "  Illegal acceptMove " << iat << " != " << activePtcl;
    APP_ABORT(o.str());
  }
}

void ParticleSet::rejectMove(Index_t iat) { activePtcl = -1; }

void ParticleSet::multi_acceptRejectMoveKokkos(Kokkos::View<ParticleSet::pskType*>& psk,
					       Kokkos::View<int*> isAcceptedMap,
					       int numAccepted, int iel) {
  int locIel = iel;
  using BarePolicy = Kokkos::TeamPolicy<>;
  BarePolicy pol(numAccepted, 32, 32);
  Kokkos::parallel_for("ps-multi_acceptRejectMove", pol,
		       KOKKOS_LAMBDA(const BarePolicy::member_type& member) {
			 const int idx = member.league_rank();
			 const int i = isAcceptedMap(idx);
			 auto& psd = psk(i);
			 psd.LikeUpdate(member,locIel);
			 psd.UnlikeUpdate(member,locIel);
			 //std::cout << "finished psd.UnlikeUpdate" << std::endl;
			 for (int dim = 0; dim < 3; dim++) {
			   psd.R(locIel,dim) = psd.activePos(dim);
			   psd.RSoA(locIel,dim) = psd.activePos(dim);
			 }
			 //std::cout << "at end of loop" << std::endl;
		       });
  Kokkos::parallel_for(",ps-multi_acceptRejectMove2",
		       psk.extent(0),
		       KOKKOS_LAMBDA(const int& i) {
			 psk(i).activePtcl(0) = -1;
		       });

  
}



void ParticleSet::multi_acceptRejectMoveKokkos(std::vector<ParticleSet*>& psets, 
					       std::vector<bool>& isAccepted, int iel) {
  //std::cout << "psets.size() = " << psets.size() << std::endl;
  Kokkos::View<ParticleSet::pskType*> allParticleSetData("apsd", psets.size());
  auto apsdMirror = Kokkos::create_mirror_view(allParticleSetData);
  for (int i = 0; i < psets.size(); i++) {
    apsdMirror(i) = psets[i]->psk;
  }
  Kokkos::deep_copy(allParticleSetData, apsdMirror);
  Kokkos::View<bool*> deviceIsAccepted("devIsAccepted", isAccepted.size());
  auto devIsAcceptedMirror = Kokkos::create_mirror_view(deviceIsAccepted);
  for (int i = 0; i < isAccepted.size(); i++) {
    devIsAcceptedMirror(i) = isAccepted[i];
  }
  Kokkos::deep_copy(deviceIsAccepted, devIsAcceptedMirror);

  int locIel = iel;
  //std::cout << " about to start parallel_for" << std::endl;

  using BarePolicy = Kokkos::TeamPolicy<>;
  BarePolicy pol(psets.size(), 1, 1);
  Kokkos::parallel_for("ps-multi_acceptRejectMove", pol,
		       KOKKOS_LAMBDA(BarePolicy::member_type member) {
			 const int i = member.league_rank();
			 auto& psd = allParticleSetData(i);
			 if (deviceIsAccepted(i)) {
			   psd.LikeUpdate(locIel);
			   psd.UnlikeUpdate(locIel);
			   //std::cout << "finished psd.UnlikeUpdate" << std::endl;
			   for (int dim = 0; dim < 3; dim++) {
			     psd.R(locIel,dim) = psd.activePos(dim);
			     psd.RSoA(locIel,dim) = psd.activePos(dim);
			   }
			 }
			 psd.activePtcl(0) = -1;
			 //std::cout << "at end of loop" << std::endl;
		       });
  
}


void ParticleSet::donePbyP(bool skipSK) { activePtcl = -1; }

void ParticleSet::multi_donePbyP(std::vector<ParticleSet*>& psets, bool skipSK) {
  Kokkos::View<ParticleSet::pskType*> allParticleSetData("apsd", psets.size());
  auto apsdMirror = Kokkos::create_mirror_view(allParticleSetData);
  for (int i = 0; i < psets.size(); i++) {
    apsdMirror(i) = psets[i]->psk;
  }
  Kokkos::deep_copy(allParticleSetData, apsdMirror);
  Kokkos::parallel_for("ps-multi_donePbyP", psets.size(),
		       KOKKOS_LAMBDA(const int& i) {
			 allParticleSetData(i).activePtcl(0) = -1;
		       });
}

void ParticleSet::loadWalker(Walker_t& awalker, bool pbyp)
{
  R = awalker.R;
  RSoA.copyIn(R);
  if (pbyp)
  {
    // in certain cases, full tables must be ready
    for (int i = 0; i < DistTables.size(); i++)
      if (DistTables[i]->Need_full_table_loadWalker)
        DistTables[i]->evaluate(*this);
  }
}

void ParticleSet::saveWalker(Walker_t& awalker) { awalker.R = R; }

void ParticleSet::clearDistanceTables()
{
  // Physically remove the tables
  for (auto iter = DistTables.begin(); iter != DistTables.end(); iter++)
    delete *iter;
  DistTables.clear();
}

const std::vector<ParticleSet::ParticleGradient_t*>
    extract_G_list(const std::vector<ParticleSet*>& P_list)
{
  std::vector<ParticleSet::ParticleGradient_t*> G_list;
  for (auto it = P_list.begin(); it != P_list.end(); it++)
    G_list.push_back(&(*it)->G);
  return G_list;
}

const std::vector<ParticleSet::ParticleLaplacian_t*>
    extract_L_list(const std::vector<ParticleSet*>& P_list)
{
  std::vector<ParticleSet::ParticleLaplacian_t*> L_list;
  for (auto it = P_list.begin(); it != P_list.end(); it++)
    L_list.push_back(&(*it)->L);
  return L_list;
}

} // namespace qmcplusplus
