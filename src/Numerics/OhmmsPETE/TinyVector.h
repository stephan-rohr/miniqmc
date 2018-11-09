////////////////////////////////////////////////////////////////////////////////
// This file is distributed under the University of Illinois/NCSA Open Source
// License.  See LICENSE file in top directory for details.
//
// Copyright (c) 2016 Jeongnim Kim and QMCPACK developers.
//
// File developed by:
// Jeremy McMinnis, jmcminis@gmail.com,
//    University of Illinois at Urbana-Champaign
//
// File created by:
// Jeongnim Kim, jeongnim.kim@gmail.com,
//    University of Illinois at Urbana-Champaign
////////////////////////////////////////////////////////////////////////////////

#ifndef OHMMS_TINYVECTOR_H
#define OHMMS_TINYVECTOR_H

/***************************************************************************
 *
 * \class TinyVector
 * \brief Pooma/AppyTypes/Vecktor is modified to work with PETE.
 *
 * The POOMA Framework
 *
 * This program was prepared by the Regents of the University of
 * California at Los Alamos National Laboratory (the University) under
 * Contract No.  W-7405-ENG-36 with the U.S. Department of Energy (DOE).
 * The University has certain rights in the program pursuant to the
 * contract and the program should not be copied or distributed outside
 * your organization.  All rights in the program are reserved by the DOE
 * and the University.  Neither the U.S.  Government nor the University
 * makes any warranty, express or implied, or assumes any liability or
 * responsibility for the use of this software
 *
 * Visit http://www.acl.lanl.gov/POOMA for more details
 *
 ***************************************************************************/

// include files
#include <iomanip>
#include "Numerics/PETE/PETE.h"
#include "Numerics/OhmmsPETE/OhmmsTinyMeta.h"
#include <Kokkos_Core.hpp>

namespace qmcplusplus
{
/** Fixed-size array. candidate for array<T,D>
 */
template<class T, unsigned D>
struct TinyVector
{
  typedef T Type_t;
  enum
  {
    Size = D
  };
  T X[Size];

  // Default Constructor initializes to zero.
  KOKKOS_INLINE_FUNCTION TinyVector()
  {
    OTAssign<TinyVector<T, D>, T, OpAssign>::apply(*this, T(0), OpAssign());
  }

  // A noninitializing ctor.
  class DontInitialize
  {};
  KOKKOS_INLINE_FUNCTION TinyVector(DontInitialize) {}

  // Copy Constructor
  KOKKOS_INLINE_FUNCTION TinyVector(const TinyVector& rhs)
  {
    OTAssign<TinyVector<T, D>, TinyVector<T, D>, OpAssign>::apply(*this, rhs, OpAssign());
  }

  // Templated TinyVector constructor.
  template<class T1, unsigned D1>
  KOKKOS_INLINE_FUNCTION TinyVector(const TinyVector<T1, D1>& rhs)
  {
    for (unsigned d = 0; d < D; ++d)
      X[d] = (d < D1) ? rhs[d] : T1(0);
  }

  // Constructor from a single T
  KOKKOS_INLINE_FUNCTION TinyVector(const T& x00)
  {
    OTAssign<TinyVector<T, D>, T, OpAssign>::apply(*this, x00, OpAssign());
  }

  // Constructors for fixed dimension
  KOKKOS_INLINE_FUNCTION TinyVector(const T& x00, const T& x01)
  {
    X[0] = x00;
    X[1] = x01;
  }
  KOKKOS_INLINE_FUNCTION TinyVector(const T& x00, const T& x01, const T& x02)
  {
    X[0] = x00;
    X[1] = x01;
    X[2] = x02;
  }
  KOKKOS_INLINE_FUNCTION TinyVector(const T& x00, const T& x01, const T& x02, const T& x03)
  {
    X[0] = x00;
    X[1] = x01;
    X[2] = x02;
    X[3] = x03;
  }

  KOKKOS_INLINE_FUNCTION TinyVector(const T& x00,
                                    const T& x01,
                                    const T& x02,
                                    const T& x03,
                                    const T& x10,
                                    const T& x11,
                                    const T& x12,
                                    const T& x13,
                                    const T& x20,
                                    const T& x21,
                                    const T& x22,
                                    const T& x23,
                                    const T& x30,
                                    const T& x31,
                                    const T& x32,
                                    const T& x33)
  {
    X[0]  = x00;
    X[1]  = x01;
    X[2]  = x02;
    X[3]  = x03;
    X[4]  = x10;
    X[5]  = x11;
    X[6]  = x12;
    X[7]  = x13;
    X[8]  = x20;
    X[9]  = x21;
    X[10] = x22;
    X[11] = x23;
    X[12] = x30;
    X[13] = x31;
    X[14] = x32;
    X[15] = x33;
  }

  KOKKOS_INLINE_FUNCTION TinyVector(const T* restrict base, int offset)
  {
#pragma unroll
    for (int i = 0; i < D; ++i)
      X[i] = base[i * offset];
  }

  // Destructor
  KOKKOS_INLINE_FUNCTION ~TinyVector() {}

  KOKKOS_INLINE_FUNCTION int size() const { return D; }

  KOKKOS_INLINE_FUNCTION int byteSize() const { return D * sizeof(T); }

  KOKKOS_INLINE_FUNCTION TinyVector& operator=(const TinyVector& rhs)
  {
    OTAssign<TinyVector<T, D>, TinyVector<T, D>, OpAssign>::apply(*this, rhs, OpAssign());
    return *this;
  }

  template<class T1>
  KOKKOS_INLINE_FUNCTION TinyVector<T, D>& operator=(const TinyVector<T1, D>& rhs)
  {
    OTAssign<TinyVector<T, D>, TinyVector<T1, D>, OpAssign>::apply(*this, rhs, OpAssign());
    return *this;
  }

  KOKKOS_INLINE_FUNCTION TinyVector<T, D>& operator=(const T& rhs)
  {
    OTAssign<TinyVector<T, D>, T, OpAssign>::apply(*this, rhs, OpAssign());
    return *this;
  }

  // Get and Set Operations
  KOKKOS_INLINE_FUNCTION Type_t& operator[](unsigned int i) { return X[i]; }
  KOKKOS_INLINE_FUNCTION Type_t operator[](unsigned int i) const { return X[i]; }
  KOKKOS_INLINE_FUNCTION Type_t& operator()(unsigned int i) { return X[i]; }
  KOKKOS_INLINE_FUNCTION Type_t operator()(unsigned int i) const { return X[i]; }

  KOKKOS_INLINE_FUNCTION Type_t* data() { return X; }
  KOKKOS_INLINE_FUNCTION const Type_t* data() const { return X; }
  KOKKOS_INLINE_FUNCTION Type_t* begin() { return X; }
  KOKKOS_INLINE_FUNCTION const Type_t* begin() const { return X; }
  KOKKOS_INLINE_FUNCTION Type_t* end() { return X + D; }
  KOKKOS_INLINE_FUNCTION const Type_t* end() const { return X + D; }

  template<class Msg>
  KOKKOS_INLINE_FUNCTION Msg& putMessage(Msg& m)
  {
    m.Pack(X, Size);
    return m;
  }

  template<class Msg>
  KOKKOS_INLINE_FUNCTION Msg& getMessage(Msg& m)
  {
    m.Unpack(X, Size);
    return m;
  }
};

// Adding binary operators using macro defined in OhmmsTinyMeta.h
OHMMS_META_ACCUM_OPERATORS(TinyVector, operator+=, OpAddAssign)
OHMMS_META_ACCUM_OPERATORS(TinyVector, operator-=, OpSubtractAssign)
OHMMS_META_ACCUM_OPERATORS(TinyVector, operator*=, OpMultiplyAssign)
OHMMS_META_ACCUM_OPERATORS(TinyVector, operator/=, OpDivideAssign)

OHMMS_META_BINARY_OPERATORS(TinyVector, operator+, OpAdd)
OHMMS_META_BINARY_OPERATORS(TinyVector, operator-, OpSubtract)
OHMMS_META_BINARY_OPERATORS(TinyVector, operator*, OpMultiply)
OHMMS_META_BINARY_OPERATORS(TinyVector, operator/, OpDivide)

//----------------------------------------------------------------------
// dot product
//----------------------------------------------------------------------
template<class T1, class T2, unsigned D>
KOKKOS_INLINE_FUNCTION typename BinaryReturn<T1, T2, OpMultiply>::Type_t
    dot(const TinyVector<T1, D>& lhs, const TinyVector<T2, D>& rhs)
{
  return OTDot<TinyVector<T1, D>, TinyVector<T2, D>>::apply(lhs, rhs);
}

//----------------------------------------------------------------------
// cross product
//----------------------------------------------------------------------

template<class T1, class T2, unsigned D>
KOKKOS_INLINE_FUNCTION TinyVector<typename BinaryReturn<T1, T2, OpMultiply>::Type_t, D>
    cross(const TinyVector<T1, D>& lhs, const TinyVector<T2, D>& rhs)
{
  return OTCross<TinyVector<T1, D>, TinyVector<T2, D>>::apply(lhs, rhs);
}

//----------------------------------------------------------------------
// cross product
//----------------------------------------------------------------------

template<class T1, class T2, unsigned D>
KOKKOS_INLINE_FUNCTION Tensor<typename BinaryReturn<T1, T2, OpMultiply>::Type_t, D>
    outerProduct(const TinyVector<T1, D>& lhs, const TinyVector<T2, D>& rhs)
{
  return OuterProduct<TinyVector<T1, D>, TinyVector<T2, D>>::apply(lhs, rhs);
}

template<class T1, unsigned D>
KOKKOS_INLINE_FUNCTION TinyVector<Tensor<T1, D>, D>
    outerdot(const TinyVector<T1, D>& lhs, const TinyVector<T1, D>& mhs, const TinyVector<T1, D>& rhs)
{
  TinyVector<Tensor<T1, D>, D> ret;
  Tensor<T1, D> tmp = OuterProduct<TinyVector<T1, D>, TinyVector<T1, D>>::apply(lhs, mhs);
  for (unsigned i(0); i < D; i++)
    ret[i] = rhs[i] * tmp;
  return ret;
}

template<class T1, class T2, class T3, unsigned D>
KOKKOS_INLINE_FUNCTION TinyVector<Tensor<typename BinaryReturn<T1, T2, OpMultiply>::Type_t, D>, D>
    symouterdot(const TinyVector<T1, D>& lhs, const TinyVector<T2, D>& mhs, const TinyVector<T3, D>& rhs)
{
  TinyVector<Tensor<typename BinaryReturn<T1, T2, OpMultiply>::Type_t, D>, D> ret;
  Tensor<typename BinaryReturn<T1, T2, OpMultiply>::Type_t, D> tmp =
      OuterProduct<TinyVector<T1, D>, TinyVector<T2, D>>::apply(lhs, mhs);
  for (unsigned i(0); i < D; i++)
    ret[i] = rhs[i] * tmp;
  tmp = OuterProduct<TinyVector<T2, D>, TinyVector<T3, D>>::apply(mhs, rhs);
  for (unsigned i(0); i < D; i++)
    ret[i] += lhs[i] * tmp;
  tmp = OuterProduct<TinyVector<T1, D>, TinyVector<T3, D>>::apply(lhs, rhs);
  for (unsigned i(0); i < D; i++)
    ret[i] += mhs[i] * tmp;
  return ret;
}

//----------------------------------------------------------------------
// I/O
template<class T>
struct printTinyVector
{};

// specialized for Vector<TinyVector<T,D> >
template<class T, unsigned D>
struct printTinyVector<TinyVector<T, D>>
{
  KOKKOS_INLINE_FUNCTION static void print(std::ostream& os, const TinyVector<T, D>& r)
  {
    for (int d = 0; d < D; d++)
      os << std::setw(18) << std::setprecision(10) << r[d];
  }
};

// specialized for Vector<TinyVector<T,2> >
template<class T>
struct printTinyVector<TinyVector<T, 2>>
{
  KOKKOS_INLINE_FUNCTION static void print(std::ostream& os, const TinyVector<T, 2>& r)
  {
    os << std::setw(18) << std::setprecision(10) << r[0] << std::setw(18) << std::setprecision(10)
       << r[1];
  }
};

// specialized for Vector<TinyVector<T,3> >
template<class T>
struct printTinyVector<TinyVector<T, 3>>
{
  KOKKOS_INLINE_FUNCTION static void print(std::ostream& os, const TinyVector<T, 3>& r)
  {
    os << std::setw(18) << std::setprecision(10) << r[0] << std::setw(18) << std::setprecision(10)
       << r[1] << std::setw(18) << std::setprecision(10) << r[2];
  }
};

template<class T, unsigned D>
std::ostream& operator<<(std::ostream& out, const TinyVector<T, D>& rhs)
{
  printTinyVector<TinyVector<T, D>>::print(out, rhs);
  return out;
}

template<class T, unsigned D>
std::istream& operator>>(std::istream& is, TinyVector<T, D>& rhs)
{
  // printTinyVector<TinyVector<T,D> >::print(out,rhs);
  for (int i = 0; i < D; i++)
    is >> rhs[i];
  return is;
}
} // namespace qmcplusplus

#endif // VEKTOR_H
