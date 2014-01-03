//@HEADER
// ************************************************************************
//
//               ShyLU: Hybrid preconditioner package
//                 Copyright 2012 Sandia Corporation
//
// Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
// the U.S. Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY SANDIA CORPORATION "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SANDIA CORPORATION OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact Michael A. Heroux (maherou@sandia.gov)
//
// ************************************************************************
//@HEADER

/** \file IQRSolver.h

    \brief Encapsulates the IQR inexact solver functionality

    \author Radu Popescu

*/

#ifndef IQRSOLVER_H_
#define IQRSOLVER_H_

#include <vector>

#include <Epetra_BlockMap.h>
#include <Epetra_MultiVector.h>
#include <Epetra_Operator.h>

#include <gmres_tools.h>
#include <gmres.h>

#include <Ifpack_Preconditioner.h>

#include <Teuchos_ParameterList.hpp>
#include <Teuchos_RCP.hpp>

#include <shylu_probing_operator.h>

namespace IQR {

class IQRSolver {
public:
	// Public type definitions
	typedef IQR::GMRESManager<Epetra_BlockMap,
							  Epetra_MultiVector,
							  std::vector<std::vector<double> >,
							  std::vector<double> > GMRESStateManager;

	// Public constructor and destructor
	IQRSolver(const Teuchos::ParameterList& pList);
	virtual ~IQRSolver();

	// Public methods
	void Solve(const ShyLU_Probing_Operator& S,
			   const Epetra_MultiVector& B,
			   Epetra_MultiVector & X);

private:
	// Private methods
	void Compute(const ShyLU_Probing_Operator& S,
			     const Epetra_MultiVector& B,
			     Epetra_MultiVector & X);

	// Private data
	Teuchos::RCP<GMRESStateManager> gmresStateManager_;
	Teuchos::RCP<Ifpack_Preconditioner> prec_;
	bool isComputed_;
    double krylovDim_;
    int numIter_;
    bool doScaling_;
    bool useFullIQR_;
    std::string precType_;
    std::string precAmesosType_;
};

} /* namespace IQR */

#endif /* IQRSOLVER_H_ */
