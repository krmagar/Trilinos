// @HEADER
// ***********************************************************************
//
//                 Anasazi: Block Eigensolvers Package
//                 Copyright (2004) Sandia Corporation
//
// Under terms of Contract DE-AC04-94AL85000, there is a non-exclusive
// license for use of this work by or on behalf of the U.S. Government.
//
// This library is free software; you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation; either version 2.1 of the
// License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
// USA
// Questions? Contact Michael A. Heroux (maherou@sandia.gov)
//
// ***********************************************************************
// @HEADER

/*! \file AnasaziMatOrthoManager.hpp
  \brief  Templated virtual class for providing orthogonalization/orthonormalization methods with matrix-based 
          inner products.
*/

// FINISH: update documentation

#ifndef ANASAZI_MATORTHOMANAGER_HPP
#define ANASAZI_MATORTHOMANAGER_HPP

/*! \class Anasazi::MatOrthoManager
  
  \brief Anasazi's templated virtual class for providing routines for orthogonalization and 
  orthonormzalition of multivectors. 
  
  A concrete implementation of this class is necessary. The user can create
  their own implementation if those supplied are not suitable for their needs.
  
  \author Chris Baker, Ulrich Hetmaniuk, Rich Lehoucq, and Heidi Thornquist
*/

#include "AnasaziTypes.hpp"
#include "AnasaziOrthoManager.hpp"
#include "AnasaziMultiVecTraits.hpp"
#include "AnasaziOperatorTraits.hpp"

namespace Anasazi {

  template <class ScalarType, class MV, class OP>
  class MatOrthoManager : public OrthoManager<ScalarType,MV> {
  protected:
    Teuchos::RefCountPtr<const OP> _Op;
    bool _hasOp;

  public:
    //@{ \name Constructor/Destructor.
    //! Default constructor.
    MatOrthoManager(Teuchos::RefCountPtr<const OP> Op = Teuchos::null) : _Op(Op), _hasOp(Op!=Teuchos::null) {};

    //! Destructor.
    virtual ~MatOrthoManager() {};
    //@}

    //@{ \name Accessor routines

    //! Set operator.
    void setOp( Teuchos::RefCountPtr<const OP> Op ) { 
      _Op = Op; 
      _hasOp = (_Op != Teuchos::null);
    };

    //! Get operator.
    Teuchos::RefCountPtr<const OP> getOp() const { return _Op; } 

    //@}


    //@{ \name Orthogonalization methods.

    /*! \brief Provides the inner product defining the orthogonality concepts, using the provided operator.
     */
    virtual ReturnType innerProd( const MV& X, const MV& Y, Teuchos::SerialDenseMatrix<int,ScalarType>& Z ) const {
      typedef Teuchos::ScalarTraits<ScalarType> SCT;
      typedef MultiVecTraits<ScalarType,MV>     MVT;
      typedef OperatorTraits<ScalarType,MV,OP>  OPT;

      Teuchos::RefCountPtr<const MV> P,Q;
      Teuchos::RefCountPtr<MV> R;
      ReturnType ret;

      if (_hasOp) {
        // attempt to minimize the amount of work in applying 
        if ( MVT::GetNumberVecs(X) < MVT::GetNumberVecs(Y) ) {
          R = MVT::Clone(X,MVT::GetNumberVecs(X));
          ret = OPT::Apply(*_Op,X,*R);
          P = R;
          Q = Teuchos::rcp( &Y, false );
        }
        else {
          P = Teuchos::rcp( &X, false );
          R = MVT::Clone(Y,MVT::GetNumberVecs(Y));
          ret = OPT::Apply(*_Op,Y,*R);
          Q = R;
        }
      }
      else {
        P = Teuchos::rcp( &X, false );
        Q = Teuchos::rcp( &Y, false );
        ret = Ok;
      }
      
      MVT::MvTransMv(SCT::one(),*P,*Q,Z);
      return ret;
    }

    /*! \brief Provides the inner product defining the orthogonality concepts, using the provided operator.
     *  The method has the option of
     *  exploiting a caller-provided \c MX, and returning updated information to the caller.
     */
    virtual ReturnType innerProd( const MV& X, const MV& Y, Teuchos::RefCountPtr<const MV> MY, 
                                  Teuchos::SerialDenseMatrix<int,ScalarType>& Z ) const {
      typedef Teuchos::ScalarTraits<ScalarType> SCT;
      typedef MultiVecTraits<ScalarType,MV>     MVT;
      typedef OperatorTraits<ScalarType,MV,OP>  OPT;

      Teuchos::RefCountPtr<MV> P,Q;
      ReturnType ret;

      if ( MY == Teuchos::null ) {
        return innerProd(X,Y,Z);
      }

      // the user has done the matrix vector for us
      MVT::MvTransMv(SCT::one(),X,*MY,Z);
      return Ok;
    }


    /*! \brief This method takes a multivector and projects it onto the space orthogonal to 
     *  another given multivector, in a specified inner product.
     *  
     @param X [in/out] The multivector to the modified. 
       On output, \c X will be orthogonal to \c Q with respect to \c innerProd().
      
     @param Q [in] A multivector specifying the space to be orthogonalized against. 
      
     @return Code specifying failure of the routine, as defined by the implementation.
    */
    virtual ReturnType project ( MV &X, const MV &Q ) const {
      return project(X,Teuchos::null,Q);
    }


    /*! \brief This method takes a multivector and projects it onto the space orthogonal to 
     *  another given multivector, in a specified inner product. The method has the option of
     *  exploiting a caller-provided \c MX, and returning updated information to the caller.
     *  
     @param X [in/out] The multivector to the modified. 
       On output, \c X will be orthogonal to \c Q with respect to \c innerProd().
      
     @param MX [in/out] The image of \c X under the operator \c M. 
       If <tt>MX != 0</tt>: On input, this is expected to be consistent with \c X. On output, this is updated consistent with updates to \c X.
       If <tt>MX == 0</tt> or <tt>M == 0</tt>: \c MX is not referenced.
            
     @param Q [in] A multivector specifying the space to be orthogonalized against. 
            
     @return Code specifying failure of the routine, as defined by the implementation.
    */
    virtual ReturnType project ( MV &X, Teuchos::RefCountPtr<MV> MX, const MV &Q ) const = 0;


    /*! \brief This method takes a multivector and orthonormalizes the columns, with respect to \c innerProd().
     *  
     @param X [in/out] The multivector to the modified. 
       On output, the columns are M-orthonormal.
    
     @param rank [out] Rank of the basis computed by this method.
    
     @return Code specifying failure of the routine, as defined by the implementation.
    */
    virtual ReturnType normalize ( MV &X, int &rank ) const {
      return normalize(X,Teuchos::null,rank);
    }


    /*! \brief This method takes a multivector and orthonormalizes the columns, with respect to \c innerProd().
     *  The method has the option of
     *  exploiting a caller-provided \c MX, and returning updated information to the caller.
     *  
     @param X [in/out] The multivector to the modified. 
       On output, the columns are M-orthonormal.
    
     @param MX [in/out] The image of \c X under the operator \c M. 
       If <tt>MX != 0</tt>: On input, this is expected to be consistent with \c X. On output, this is updated consistent with updates to \c X.
       If <tt>MX == 0</tt> or <tt>M == 0</tt>: \c MX is not referenced.
      
     @param rank [out] Rank of the basis computed by this method.
    
     @return Code specifying failure of the routine, as defined by the implementation.
    */
    virtual ReturnType normalize ( MV &X, Teuchos::RefCountPtr<MV> MX, int &rank ) const = 0;


    /*! \brief This method takes a multivector and projects it onto the space orthogonal to 
     *  another given multivector.  It also orthonormalizes the 
     *  columns of the resulting multivector. Both of these operations are conducted 
     *  with respect to \c innerProd().
     *  
     @param X [in/out] The multivector to the modified. 
       On output, the columns of \c X are M-orthogonal to the columns of \c Q, and the columns of \c X are M-orthonormal.
      
     @param Q [in] A multivector specifying the space to be orthogonalized against. \c Q is assumed to have orthonormal
     columns with respect to \c innerProd().
      
     @param rank [out] Rank of the basis computed by this method.
      
     @return Code specifying failure of the routine, as defined by the implementation.
    */
    virtual ReturnType projectAndNormalize ( MV &X, const MV &Q, int &rank ) const {
      return projectAndNormalize(X,Teuchos::null,Q,rank);
    }

    /*! \brief This method takes a multivector and projects it onto the space orthogonal to 
     *  another given multivector.  It also orthonormalizes the 
     *  columns of the resulting multivector. Both of these operations are conducted 
     *  with respect to \c innerProd().
     *  The method has the option of
     *  exploiting a caller-provided \c MX, and returning updated information to the caller.
     *  
     @param X [in/out] The multivector to the modified. 
       On output, the columns of \c X are M-orthogonal to the columns of \c Q, and the columns of \c X are M-orthonormal.
      
     @param MX [in/out] The image of \c X under the operator \c M. 
       If <tt>MX != 0</tt>: On input, this is expected to be consistent with \c X. On output, this is updated consistent with updates to \c X.
       If <tt>MX == 0</tt> or <tt>M == 0</tt>: \c MX is not referenced.
      
     @param Q [in] A multivector specifying the space to be orthogonalized against. \c Q is assumed to have orthonormal
     columns with respect to \c innerProd().
      
     @param rank [out] Rank of the basis computed by this method.
      
     @return Code specifying failure of the routine, as defined by the implementation.
    */
    virtual ReturnType projectAndNormalize ( MV &X, Teuchos::RefCountPtr<MV> MX, const MV &Q, int &rank ) const = 0;

    //@}

    //@{ \name Error methods.

    /*! \brief This method computes the error in orthonormality of a multivector.
     */
    virtual typename Teuchos::ScalarTraits<ScalarType>::magnitudeType 
    orthonormError(const MV &X) const {
      return orthonormError(X,Teuchos::null);
    }

    /*! \brief This method computes the error in orthonormality of a multivector.
     *  The method has the option of
     *  exploiting a caller-provided \c MX.
     */
    virtual typename Teuchos::ScalarTraits<ScalarType>::magnitudeType 
    orthonormError(const MV &X, Teuchos::RefCountPtr<const MV> MX) const = 0;

    /*! \brief This method computes the error in orthogonality of two multivectors. This method 
     */
    virtual typename Teuchos::ScalarTraits<ScalarType>::magnitudeType 
    orthogError(const MV &X1, const MV &X2) const {
      return orthogError(X1,Teuchos::null,X2);
    }

    /*! \brief This method computes the error in orthogonality of two multivectors.
     *  The method has the option of
     *  exploiting a caller-provided \c MX.
     */
    virtual typename Teuchos::ScalarTraits<ScalarType>::magnitudeType 
    orthogError(const MV &X1, Teuchos::RefCountPtr<const MV> MX1, const MV &X2) const = 0;

    //@}

  };

} // end of Anasazi namespace


#endif

// end of file AnasaziMatOrthoManager.hpp
