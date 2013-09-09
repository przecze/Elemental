/*
   Copyright (c) 2009-2013, Jack Poulson
   All rights reserved.

   This file is part of Elemental and is under the BSD 2-Clause License, 
   which can be found in the LICENSE file in the root directory, or at 
   http://opensource.org/licenses/BSD-2-Clause
*/
// NOTE: It is possible to simply include "elemental.hpp" instead
#include "elemental-lite.hpp"
#include "elemental/blas-like/level1/MakeHermitian.hpp"
#include "elemental/lapack-like/HermitianFunction.hpp"
using namespace std;
using namespace elem;

// Create a typedef for convenience
typedef double Real;

// A functor for returning the exponential of a real number
class ExpFunctor {
public:
    Real operator()( Real alpha ) const { return std::exp(alpha); }
};

int
main( int argc, char* argv[] )
{
    Initialize( argc, argv );

    try 
    {
        const Int n = Input("--size","size of matrix",100);
        const bool print = Input("--print","print matrices?",false);
        ProcessInput();
        PrintInputReport();

        DistMatrix<Real> H( n, n );

        // We will fill entry (i,j) with the value i+j so that
        // the global matrix is symmetric. However, only one triangle of the 
        // matrix actually needs to be filled, the symmetry can be implicit.
        const Int colShift = H.ColShift(); // first row we own
        const Int rowShift = H.RowShift(); // first col we own
        const Int colStride = H.ColStride();
        const Int rowStride = H.RowStride();
        const Int localHeight = H.LocalHeight();
        const Int localWidth = H.LocalWidth();
        for( Int jLoc=0; jLoc<localWidth; ++jLoc )
        {
            // Our process owns the rows colShift:colStride:n,
            //           and the columns rowShift:rowStride:n
            const Int j = rowShift + jLoc*rowStride;
            for( Int iLoc=0; iLoc<localHeight; ++iLoc )
            {
                const Int i = colShift + iLoc*colStride;
                H.SetLocal( iLoc, jLoc, Real(i+j) );
            }
        }
        if( print )
            Print( H, "H" );

        // Reform the matrix with the exponentials of the original eigenvalues
        RealHermitianFunction( LOWER, H, ExpFunctor() );
        if( print )
        {
            MakeHermitian( LOWER, H );
            Print( H, "exp(H)" );
        }
    }
    catch( exception& e ) { ReportException(e); }

    Finalize();
    return 0;
}
