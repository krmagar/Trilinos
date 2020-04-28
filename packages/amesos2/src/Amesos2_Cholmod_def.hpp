// @HEADER
//
// ***********************************************************************
//
//           Amesos2: Templated Direct Sparse Solver Package
//                  Copyright 2011 Sandia Corporation
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
// ***********************************************************************
//
// @HEADER

/**
   \file   Amesos2_Cholmod_def.hpp
   \author Kevin Deweese <kdeweese@sandia.gov>
   \date   Wed Jul  24 15::48:51 2013

   \brief  Definitions for the Amesos2 Cholmod solver interface
*/


#ifndef AMESOS2_CHOLMOD_DEF_HPP
#define AMESOS2_CHOLMOD_DEF_HPP

#include <Teuchos_Tuple.hpp>
#include <Teuchos_ParameterList.hpp>
#include <Teuchos_StandardParameterEntryValidators.hpp>

#include "Amesos2_SolverCore_def.hpp"
#include "Amesos2_Cholmod_decl.hpp"

#ifdef HAVE_AMESOS2_TRIANGULAR_SOLVES
  #include "KokkosSparse_sptrsv_cholmod.hpp"
  // TODO: Fix up kernels options and then resolve all of this at config time.
  #ifndef KOKKOSKERNELS_ENABLE_SUPERNODAL_SPTRSV
    static_assert(false, "KOKKOSKERNELS_ENABLE_SUPERNODAL_SPTRSV is required for triangular solves.");
  #endif // KOKKOSKERNELS_ENABLE_SUPERNODAL_SPTRSV
  #ifndef KOKKOSKERNELS_ENABLE_TPL_CHOLMOD
    static_assert(false, "KOKKOSKERNELS_ENABLE_TPL_CHOLMOD is required for triangular solves.");
  #endif // KOKKOSKERNELS_ENABLE_TPL_CHOLMOD
#endif // HAVE_AMESOS2_TRIANGULAR_SOLVES

namespace Amesos2 {


template <class Matrix, class Vector>
Cholmod<Matrix,Vector>::Cholmod(
  Teuchos::RCP<const Matrix> A,
  Teuchos::RCP<Vector>       X,
  Teuchos::RCP<const Vector> B )
  : SolverCore<Amesos2::Cholmod,Matrix,Vector>(A, X, B)
  , firstsolve(true)
  , skip_symfact(false)
  , map()
  , is_contiguous_(true) // default is set by params
  , use_triangular_solves_(false) // default is set by params
{
  data_.L = NULL;
  data_.Y = NULL;
  data_.E = NULL;

  cholmod_l_start(&data_.c); // long form required for CUDA

  // TODO: Add an option so we can support CHOLMOD_INT (if no GPU is required)
  // This will require some kind of templating design in decl.hpp which uses the long type
  data_.c.itype = CHOLMOD_LONG; // required for long support

  data_.c.supernodal = CHOLMOD_SUPERNODAL;
  data_.c.quick_return_if_not_posdef = 1;
}


template <class Matrix, class Vector>
Cholmod<Matrix,Vector>::~Cholmod( )
{
  // useful to check if GPU is being used, but very verbose
  // cholmod_l_gpu_stats(&(data_.c));

  cholmod_l_free_factor(&(data_.L), &(data_.c));
  cholmod_l_free_dense(&(data_.Y), &data_.c);
  cholmod_l_free_dense(&(data_.E), &data_.c);

  cholmod_l_finish(&(data_.c));
}

template<class Matrix, class Vector>
int
Cholmod<Matrix,Vector>::preOrdering_impl()
{
#ifdef HAVE_AMESOS2_TIMERS
  Teuchos::TimeMonitor preOrderTimer(this->timers_.preOrderTime_);
#endif

  int info = 0;

  data_.L = cholmod_l_analyze(&data_.A, &(data_.c));
  info = data_.c.status;
  skip_symfact = true;

  /* All processes should have the same error code */
  Teuchos::broadcast(*(this->matrixA_->getComm()), 0, &info);
  
  TEUCHOS_TEST_FOR_EXCEPTION(info != 0,
    std::runtime_error,
    "Amesos2 cholmod_l_analyze failure in Cholmod preOrdering_impl");

  return(0);
}


template <class Matrix, class Vector>
int
Cholmod<Matrix,Vector>::symbolicFactorization_impl()
{
  int info = 0;

  if(!skip_symfact) {
#ifdef HAVE_AMESOS2_TIMERS
    Teuchos::TimeMonitor symFactTimer(this->timers_.symFactTime_);
#endif

    cholmod_l_resymbol (&data_.A, NULL, 0, true, data_.L, &(data_.c));

    info = data_.c.status;
  } else {
    /*
     * Symbolic factorization has already occured in preOrdering_impl,
     * but if the user calls this routine directly later, we need to
     * redo the symbolic factorization.
     */
    skip_symfact = false;
  }

  /* All processes should have the same error code */
  Teuchos::broadcast(*(this->matrixA_->getComm()), 0, &info);
  
  TEUCHOS_TEST_FOR_EXCEPTION(info != 0,
    std::runtime_error,
    "Amesos2 cholmod_l_resymbol failure in Cholmod symbolicFactorization_impl");

  if(use_triangular_solves_) {
    triangular_solve_symbolic();
  }

  return(0);
}


template <class Matrix, class Vector>
int
Cholmod<Matrix,Vector>::numericFactorization_impl()
{
  int info = 0;

#ifdef HAVE_AMESOS2_DEBUG
  TEUCHOS_TEST_FOR_EXCEPTION(data_.A.ncol != Teuchos::as<size_t>(this->globalNumCols_),
    std::runtime_error,
    "Error in converting to cholmod_sparse: wrong number of global columns." );
  TEUCHOS_TEST_FOR_EXCEPTION(data_.A.nrow != Teuchos::as<size_t>(this->globalNumRows_),
    std::runtime_error,
    "Error in converting to cholmod_sparse: wrong number of global rows." );
#endif

#ifdef HAVE_AMESOS2_TIMERS
  Teuchos::TimeMonitor numFactTimer(this->timers_.numFactTime_);
#endif

#ifdef HAVE_AMESOS2_VERBOSE_DEBUG
  // Add print out of views here - need host conversion first
#endif

  cholmod_l_factorize(&data_.A, data_.L, &(data_.c));
  info = data_.c.status;

  /* All processes should have the same error code */
  Teuchos::broadcast(*(this->matrixA_->getComm()), 0, &info);

  TEUCHOS_TEST_FOR_EXCEPTION(info == 2,
    std::runtime_error,
    "Memory allocation failure in Cholmod factorization");

  TEUCHOS_TEST_FOR_EXCEPTION(info == 1,
    std::runtime_error,
    "Amesos2 cholmod_l_factorize is attempting to use Cholmod features that are not available yet.");

  TEUCHOS_TEST_FOR_EXCEPTION(info != 0,
    std::runtime_error,
    "Amesos2 cholmod_l_factorize failure in Cholmod factorization");

  if(use_triangular_solves_) {
    triangular_solve_numeric();
  }

  return(info);
}


template <class Matrix, class Vector>
int
Cholmod<Matrix,Vector>::solve_impl(const Teuchos::Ptr<MultiVecAdapter<Vector> >       X,
                                   const Teuchos::Ptr<const MultiVecAdapter<Vector> > B) const
{
  const global_size_type ld_rhs = X->getGlobalLength();
  const size_t nrhs = X->getGlobalNumVectors();

  {                             // Get values from RHS B
#ifdef HAVE_AMESOS2_TIMERS
    Teuchos::TimeMonitor mvConvTimer(this->timers_.vecConvTime_);
    Teuchos::TimeMonitor redistTimer( this->timers_.vecRedistTime_ );
#endif

    // In general we may want to write directly to the x space without a copy.
    // So we 'get' x which may be a direct view assignment to the MV.
    if(use_triangular_solves_) { // to device
#ifdef HAVE_AMESOS2_TRIANGULAR_SOLVES
      Util::get_1d_copy_helper_kokkos_view<MultiVecAdapter<Vector>,
          device_solve_array_t>::do_get(B, device_bValues_,
              Teuchos::as<size_t>(ld_rhs),
              (is_contiguous_ == true) ? ROOTED : CONTIGUOUS_AND_ROOTED,
              this->rowIndexBase_);
        Util::get_1d_copy_helper_kokkos_view<MultiVecAdapter<Vector>,
          device_solve_array_t>::do_get(X, device_xValues_,
              Teuchos::as<size_t>(ld_rhs),
              (is_contiguous_ == true) ? ROOTED : CONTIGUOUS_AND_ROOTED,
              this->rowIndexBase_);
#endif
    }
    else { // to host
      Util::get_1d_copy_helper_kokkos_view<MultiVecAdapter<Vector>,
          host_solve_array_t>::do_get(B, host_bValues_,
              Teuchos::as<size_t>(ld_rhs),
              (is_contiguous_ == true) ? ROOTED : CONTIGUOUS_AND_ROOTED,
              this->rowIndexBase_);
        Util::get_1d_copy_helper_kokkos_view<MultiVecAdapter<Vector>,
          host_solve_array_t>::do_get(X, host_xValues_,
              Teuchos::as<size_t>(ld_rhs),
              (is_contiguous_ == true) ? ROOTED : CONTIGUOUS_AND_ROOTED,
              this->rowIndexBase_);
    }

  }

  int ierr = 0; // returned error code

#ifdef HAVE_AMESOS2_TIMERS
  Teuchos::TimeMonitor solveTimer(this->timers_.solveTime_);
#endif

  if(use_triangular_solves_) {
    triangular_solve();
  }
  else {
    function_map::cholmod_init_dense(Teuchos::as<long>(this->globalNumRows_),
      Teuchos::as<int>(nrhs), Teuchos::as<int>(ld_rhs), host_bValues_.data(), &data_.b);
    function_map::cholmod_init_dense(Teuchos::as<long>(this->globalNumRows_),
      Teuchos::as<int>(nrhs), Teuchos::as<int>(ld_rhs), host_xValues_.data(), &data_.x);

    cholmod_dense *xtemp = &(data_.x);
    cholmod_l_solve2(CHOLMOD_A, data_.L, &data_.b, NULL,
      &(xtemp), NULL, &data_.Y, &data_.E, &data_.c);
  }

  ierr = data_.c.status;

  /* All processes should have the same error code */
  Teuchos::broadcast(*(this->getComm()), 0, &ierr);

  TEUCHOS_TEST_FOR_EXCEPTION(ierr == -2, std::runtime_error, "Ran out of memory" );

  /* Update X's global values */
  {
#ifdef HAVE_AMESOS2_TIMERS
    Teuchos::TimeMonitor redistTimer(this->timers_.vecRedistTime_);
#endif

    if(use_triangular_solves_) { // to device
#ifdef HAVE_AMESOS2_TRIANGULAR_SOLVES
      Util::put_1d_data_helper_kokkos_view<
        MultiVecAdapter<Vector>,device_solve_array_t>::do_put(X, device_xValues_,
            Teuchos::as<size_t>(ld_rhs),
            (is_contiguous_ == true) ? ROOTED : CONTIGUOUS_AND_ROOTED,
            this->rowIndexBase_);
#endif
    }
    else { // to host
      Util::put_1d_data_helper_kokkos_view<
        MultiVecAdapter<Vector>,host_solve_array_t>::do_put(X, host_xValues_,
            Teuchos::as<size_t>(ld_rhs),
            (is_contiguous_ == true) ? ROOTED : CONTIGUOUS_AND_ROOTED,
            this->rowIndexBase_);
    }
  }

  return(ierr);
}


template <class Matrix, class Vector>
bool
Cholmod<Matrix,Vector>::matrixShapeOK_impl() const
{
  return( this->matrixA_->getGlobalNumRows() == this->matrixA_->getGlobalNumCols() );
}


template <class Matrix, class Vector>
void
Cholmod<Matrix,Vector>::setParameters_impl(const Teuchos::RCP<Teuchos::ParameterList> & parameterList )
{
  using Teuchos::RCP;
  using Teuchos::getIntegralValue;
  using Teuchos::ParameterEntryValidator;

  RCP<const Teuchos::ParameterList> valid_params = getValidParameters_impl();

  is_contiguous_ = parameterList->get<bool>("IsContiguous", true);
  use_triangular_solves_ = parameterList->get<bool>("TriangularSolves", false);

  if(use_triangular_solves_) {
#ifndef HAVE_AMESOS2_TRIANGULAR_SOLVES
    TEUCHOS_TEST_FOR_EXCEPTION(true, std::runtime_error,
      "Calling for triangular solves but Amesos2_ENABLE_TRIANGULAR_SOLVES was not configured." );
#endif
  }

  data_.c.dbound = parameterList->get<double>("dbound", 0.0);
  data_.c.prefer_upper = (parameterList->get<bool>("PreferUpper", true)) ? 1 : 0;
  data_.c.print = parameterList->get<int>("print", 3);
  data_.c.nmethods = parameterList->get<int>("nmethods", 0); // tries AMD and may try METIS if necessary and available

  // useGPU = 1 means use GPU if possible - note this must be set after cholmod_l_start
  // since cholmod_l_start sets it to the default value of -1.
  // Setting to -1 means GPU is only used if env variable CHOLMOD_USE_GPU is set to 1.
#ifdef KOKKOS_ENABLE_CUDA
  const int default_gpu_setting = 1; // use gpu by default
#else
  // If building Trilinos with Cuda off, Cholmod can stil use GPU and the solver will still work.
  // But that is likely not what was expected/wanted. If you do still want it, set the param to 1.
  const int default_gpu_setting = 0; // use gpu by default
#endif

  data_.c.useGPU = parameterList->get<int>("useGPU", default_gpu_setting);
}


template <class Matrix, class Vector>
Teuchos::RCP<const Teuchos::ParameterList>
Cholmod<Matrix,Vector>::getValidParameters_impl() const
{
  using std::string;
  using Teuchos::tuple;
  using Teuchos::ParameterList;
  using Teuchos::EnhancedNumberValidator;
  using Teuchos::setStringToIntegralParameter;
  using Teuchos::stringToIntegralParameterEntryValidator;

  static Teuchos::RCP<const Teuchos::ParameterList> valid_params;

  if( is_null(valid_params) ){
    Teuchos::RCP<Teuchos::ParameterList> pl = Teuchos::parameterList();


    Teuchos::RCP<EnhancedNumberValidator<int> > print_validator
      = Teuchos::rcp( new EnhancedNumberValidator<int>(0,5));

    Teuchos::RCP<EnhancedNumberValidator<int> > nmethods_validator
      = Teuchos::rcp( new EnhancedNumberValidator<int>(0,9));

    pl->set("nmethods", 0, "Specifies the number of different ordering methods to try", nmethods_validator);

    pl->set("print", 3, "Specifies the verbosity of the print statements", print_validator);

    pl->set("dbound", 0.0,
            "Specifies the smallest absolute value on the diagonal D for the LDL' factorization");


    pl->set("Equil", true, "Whether to equilibrate the system before solve");

    pl->set("PreferUpper", true,
            "Specifies whether the matrix will be " 
            "stored in upper triangular form.");

    pl->set("useGPU", -1, "1: Use GPU is 1, 0: Do not use GPU, -1: ENV CHOLMOD_USE_GPU set GPU usage.");

    pl->set("TriangularSolves", false, "Whether to use triangular solves.");

    pl->set("IsContiguous", true, "Whether GIDs contiguous");

    valid_params = pl;
  }

  return valid_params;
}


template <class Matrix, class Vector>
bool
Cholmod<Matrix,Vector>::loadA_impl(EPhase current_phase)
{
#ifdef HAVE_AMESOS2_TIMERS
  Teuchos::TimeMonitor convTimer(this->timers_.mtxConvTime_);
#endif

  // Only the root image needs storage allocated

  Kokkos::resize(host_nzvals_view_, this->globalNumNonZeros_);
  Kokkos::resize(host_rows_view_, this->globalNumNonZeros_);
  Kokkos::resize(host_col_ptr_view_, this->globalNumRows_ + 1);

  long nnz_ret = 0;
  {
#ifdef HAVE_AMESOS2_TIMERS
    Teuchos::TimeMonitor mtxRedistTimer( this->timers_.mtxRedistTime_ );
#endif

    TEUCHOS_TEST_FOR_EXCEPTION(this->rowIndexBase_ != this->columnIndexBase_,
              std::runtime_error,
              "Row and column maps have different indexbase ");

    if ( is_contiguous_ == true ) {
      Util::get_ccs_helper_kokkos_view<
        MatrixAdapter<Matrix>,host_value_type_array,host_ordinal_type_array,
          host_size_type_array>::do_get(this->matrixA_.ptr(),
            host_nzvals_view_, host_rows_view_,
            host_col_ptr_view_, nnz_ret, ROOTED,
            ARBITRARY,
            this->rowIndexBase_);
    }
    else {
      Util::get_ccs_helper_kokkos_view<
        MatrixAdapter<Matrix>,host_value_type_array,host_ordinal_type_array,
          host_size_type_array>::do_get(this->matrixA_.ptr(),
            host_nzvals_view_, host_rows_view_,
            host_col_ptr_view_, nnz_ret, CONTIGUOUS_AND_ROOTED,
            ARBITRARY,
            this->rowIndexBase_);
    }
  }

  TEUCHOS_TEST_FOR_EXCEPTION(nnz_ret != Teuchos::as<long>(this->globalNumNonZeros_),
           std::runtime_error,
           "Did not get the expected number of non-zero vals");

  function_map::cholmod_init_sparse(Teuchos::as<size_t>(this->globalNumRows_),
            Teuchos::as<size_t>(this->globalNumCols_),
            Teuchos::as<size_t>(this->globalNumNonZeros_),
            0,
            host_col_ptr_view_.data(),
            host_nzvals_view_.data(),
            host_rows_view_.data(),
            &(data_.A));

  TEUCHOS_TEST_FOR_EXCEPTION(data_.A.stype == 0, std::runtime_error,
    "CHOLMOD loadA_impl loaded matrix but it is not symmetric.");

  return true;
}

template <class Matrix, class Vector>
void
Cholmod<Matrix,Vector>::triangular_solve_symbolic()
{
#ifdef HAVE_AMESOS2_TRIANGULAR_SOLVES
  // Create handles for U and U^T solves
  device_khL_.create_sptrsv_handle(
    KokkosSparse::Experimental::SPTRSVAlgorithm::SUPERNODAL_ETREE, data_.L->n, true);
  device_khU_.create_sptrsv_handle(
    KokkosSparse::Experimental::SPTRSVAlgorithm::SUPERNODAL_ETREE, data_.L->n, false);

  // extract etree and iperm from CHOLMOD
  long *long_etree = static_cast<long*>(data_.c.Iwork) + 2 * data_.L->n;
  Kokkos::resize(host_trsv_etree_, data_.L->nsuper);
  for (size_t i = 0 ; i < data_.L->nsuper; ++i) { // convert long to int array for trsv API
    host_trsv_etree_(i) = long_etree[i];
  }

  // set etree
  device_khL_.set_sptrsv_etree(host_trsv_etree_.data());
  device_khU_.set_sptrsv_etree(host_trsv_etree_.data());

  size_t ld_rhs = this->matrixA_->getGlobalNumRows();
  Kokkos::resize(host_trsv_perm_, ld_rhs);
  long *iperm = static_cast<long*>(data_.L->Perm);
  for (size_t i = 0; i < ld_rhs; i++) { // convert long to int array for trsv API
    host_trsv_perm_(iperm[i]) = i;
  }
  deep_copy_or_assign_view(device_trsv_perm_, host_trsv_perm_); // will use device to permute

  // set permutation
  device_khL_.set_sptrsv_perm(host_trsv_perm_.data());
  device_khU_.set_sptrsv_perm(host_trsv_perm_.data());

  // Do symbolic analysis
  KokkosSparse::Experimental::sptrsv_symbolic<long, kernel_handle_type>
    (&device_khL_, &device_khU_, data_.L, &data_.c);
#endif
}

template <class Matrix, class Vector>
void
Cholmod<Matrix,Vector>::triangular_solve_numeric()
{
#ifdef HAVE_AMESOS2_TRIANGULAR_SOLVES
  // Do numerical compute
  KokkosSparse::Experimental::sptrsv_compute<long, kernel_handle_type>
    (&device_khL_, &device_khU_, data_.L, &data_.c);
#endif // HAVE_AMESOS2_TRIANGULAR_SOLVE
}

template <class Matrix, class Vector>
void
Cholmod<Matrix,Vector>::triangular_solve() const
{
#ifdef HAVE_AMESOS2_TRIANGULAR_SOLVES
  size_t ld_rhs = device_xValues_.extent(0);
  size_t nrhs = device_xValues_.extent(1);

  Kokkos::resize(device_trsv_rhs_, ld_rhs, nrhs);
  Kokkos::resize(device_trsv_sol_, ld_rhs, nrhs);

  // forward pivot
  auto local_device_bValues = device_bValues_;
  auto local_device_trsv_perm = device_trsv_perm_;
  auto local_device_trsv_rhs = device_trsv_rhs_;
  Kokkos::parallel_for(Kokkos::RangePolicy<DeviceExecSpaceType>(0, ld_rhs),
    KOKKOS_LAMBDA(size_t j) {
    for(size_t k = 0; k < nrhs; ++k) {
      local_device_trsv_rhs(local_device_trsv_perm(j),k) = local_device_bValues(j,k);
    }
  });

  for(size_t k = 0; k < nrhs; ++k) { // sptrsv_solve does not batch
    auto sub_sol = Kokkos::subview(device_trsv_sol_, Kokkos::ALL, k);
    auto sub_rhs = Kokkos::subview(device_trsv_rhs_, Kokkos::ALL, k);

    // do L solve= - numeric (only rhs is modified) on the default device/host space
    KokkosSparse::Experimental::sptrsv_solve(&device_khL_, sub_sol, sub_rhs);

    // do L^T solve - numeric (only rhs is modified) on the default device/host space
    KokkosSparse::Experimental::sptrsv_solve(&device_khU_, sub_rhs, sub_sol);
  } // end loop over rhs vectors

  // backward pivot
  auto local_device_xValues = device_xValues_;
  Kokkos::parallel_for(Kokkos::RangePolicy<DeviceExecSpaceType>(0, ld_rhs),
    KOKKOS_LAMBDA(size_t j) {
    for(size_t k = 0; k < nrhs; ++k) {
      local_device_xValues(j,k) = local_device_trsv_rhs(local_device_trsv_perm(j),k);
    }
  });
#endif // HAVE_AMESOS2_TRIANGULAR_SOLVE
}

template<class Matrix, class Vector>
const char* Cholmod<Matrix,Vector>::name = "Cholmod";
  

} // end namespace Amesos2

#endif  // AMESOS2_CHOLMOD_DEF_HPP
