/*
   Copyright (c) 2009-2013, Jack Poulson
   All rights reserved.

   This file is part of Elemental and is under the BSD 2-Clause License, 
   which can be found in the LICENSE file in the root directory, or at 
   http://opensource.org/licenses/BSD-2-Clause
*/
#pragma once
#ifndef ELEM_LAPACK_APPLYPACKEDREFLECTORS_LLHF_HPP
#define ELEM_LAPACK_APPLYPACKEDREFLECTORS_LLHF_HPP

#include "elemental/blas-like/level1/MakeTrapezoidal.hpp"
#include "elemental/blas-like/level1/SetDiagonal.hpp"
#include "elemental/blas-like/level3/Gemm.hpp"
#include "elemental/blas-like/level3/Herk.hpp"
#include "elemental/blas-like/level3/Syrk.hpp"
#include "elemental/blas-like/level3/Trsm.hpp"
#include "elemental/matrices/Zeros.hpp"

namespace elem {
namespace apply_packed_reflectors {

//
// Since applying Householder transforms from vectors stored top-to-bottom
// implies that we will be forming a generalization of
//
//   (I - tau_1 v_1^H v_1) (I - tau_0 v_0^H v_0) = 
//   I - tau_0 v_0^H v_0 - tau_1 v_1^H v_1 + (tau_0 tau_1 v_1 v_0^H) v_1^H v_0 =
//   I - [ v_0^H, v_1^H ] [  tau_0,                 0     ] [ v_0 ]
//                        [ -tau_0 tau_1 v_1 v_0^H, tau_1 ] [ v_1 ],
//
// which has a lower-triangular center matrix, say S, we will form S as 
// the inverse of a matrix T, which can easily be formed as
// 
//   tril(T) = tril( V V^H ),  diag(T) = 1/t or 1/conj(t),
//
// where V is the matrix of Householder vectors and t is the vector of scalars.
//

template<typename F> 
inline void
LLHF
( Conjugation conjugation, Int offset, 
  const Matrix<F>& H, const Matrix<F>& t, Matrix<F>& A )
{
#ifndef RELEASE
    CallStackEntry cse("apply_packed_reflectors::LLHF");
    if( H.Width() != A.Height() )
        LogicError("H's width must match A's height");
#endif
    const Int nA = A.Width();
    const Int diagLength = H.DiagonalLength(offset);
#ifndef RELEASE
    if( t.Height() != diagLength )
        LogicError("t must be the same length as H's offset diag");
#endif
    Matrix<F> HPanCopy, SInv, Z;

    const Int iOff = ( offset>=0 ? 0      : -offset );
    const Int jOff = ( offset>=0 ? offset : 0       );

    const Int bsize = Blocksize();
    for( Int k=0; k<diagLength; k+=bsize )
    {
        const Int nb = Min(bsize,diagLength-k);
        const Int ki = k+iOff;
        const Int kj = k+jOff;

        auto HPan = LockedViewRange( H, ki, 0, ki+nb, kj+nb );
        auto ATop = ViewRange( A, 0, 0, k+nb, nA );
        auto t1 = LockedView( t, k, 0, nb, 1 );

        HPanCopy = HPan;
        MakeTrapezoidal( LOWER, HPanCopy, 0, RIGHT );
        SetDiagonal( HPanCopy, F(1), 0, RIGHT );
        Herk( LOWER, NORMAL, F(1), HPanCopy, SInv );
        FixDiagonal( conjugation, t1, SInv );

        Gemm( NORMAL, NORMAL, F(1), HPanCopy, ATop, Z );
        Trsm( LEFT, LOWER, NORMAL, NON_UNIT, F(1), SInv, Z );
        Gemm( ADJOINT, NORMAL, F(-1), HPanCopy, Z, F(1), ATop );
    }
}

template<typename F> 
inline void
LLHF
( Conjugation conjugation, Int offset, 
  const DistMatrix<F>& H, const DistMatrix<F,MD,STAR>& t, DistMatrix<F>& A )
{
#ifndef RELEASE
    CallStackEntry cse("apply_packed_reflectors::LLHF");
    if( H.Width() != A.Height() )
        LogicError("H's width must match A's height");
    if( H.Grid() != t.Grid() || t.Grid() != A.Grid() )
        LogicError("{H,t,A} must be distributed over same grid");
#endif
    const Int nA = A.Width();
    const Int diagLength = H.DiagonalLength(offset);
#ifndef RELEASE
    if( t.Height() != diagLength )
        LogicError("t must be the same length as H's offset diag");
    if( !t.AlignedWithDiagonal( H, offset ) )
        LogicError("t must be aligned with H's 'offset' diagonal");
#endif
    const Grid& g = H.Grid();
    DistMatrix<F> HPanCopy(g);
    DistMatrix<F,STAR,VR  > HPan_STAR_VR(g); 
    DistMatrix<F,STAR,MC  > HPan_STAR_MC(g);
    DistMatrix<F,STAR,STAR> t1_STAR_STAR(g);
    DistMatrix<F,STAR,STAR> SInv_STAR_STAR(g);
    DistMatrix<F,STAR,MR  > Z_STAR_MR(g);
    DistMatrix<F,STAR,VR  > Z_STAR_VR(g);

    const Int iOff = ( offset>=0 ? 0      : -offset );
    const Int jOff = ( offset>=0 ? offset : 0       );

    const Int bsize = Blocksize();
    for( Int k=0; k<diagLength; k+=bsize )
    {
        const Int nb = Min(bsize,diagLength-k);
        const Int ki = k+iOff;
        const Int kj = k+jOff;

        auto HPan = LockedViewRange( H, ki, 0, ki+nb, kj+nb );
        auto ATop = ViewRange( A, 0, 0, k+nb, nA );
        auto t1 = LockedView( t, k, 0, nb, 1 );

        HPanCopy = HPan;
        MakeTrapezoidal( LOWER, HPanCopy, 0, RIGHT );
        SetDiagonal( HPanCopy, F(1), 0, RIGHT );
        HPan_STAR_VR = HPanCopy;
        Zeros( SInv_STAR_STAR, nb, nb );
        Herk
        ( LOWER, NORMAL,
          F(1), HPan_STAR_VR.LockedMatrix(),
          F(0), SInv_STAR_STAR.Matrix() );
        SInv_STAR_STAR.SumOverGrid();
        t1_STAR_STAR = t1;
        FixDiagonal( conjugation, t1_STAR_STAR, SInv_STAR_STAR );

        HPan_STAR_MC.AlignWith( ATop );
        HPan_STAR_MC = HPan_STAR_VR;
        Z_STAR_MR.AlignWith( ATop );
        LocalGemm( NORMAL, NORMAL, F(1), HPan_STAR_MC, ATop, Z_STAR_MR );
        Z_STAR_VR.AlignWith( ATop );
        Z_STAR_VR.SumScatterFrom( Z_STAR_MR );

        LocalTrsm
        ( LEFT, LOWER, NORMAL, NON_UNIT, F(1), SInv_STAR_STAR, Z_STAR_VR );

        Z_STAR_MR = Z_STAR_VR;
        LocalGemm
        ( ADJOINT, NORMAL, F(-1), HPan_STAR_MC, Z_STAR_MR, F(1), ATop );
    }
}

} // namespace apply_packed_reflectors
} // namespace elem

#endif // ifndef ELEM_LAPACK_APPLYPACKEDREFLECTORS_LLHF_HPP
