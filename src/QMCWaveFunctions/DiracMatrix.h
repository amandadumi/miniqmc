//////////////////////////////////////////////////////////////////////////////////////
// This file is distributed under the University of Illinois/NCSA Open Source License.
// See LICENSE file in top directory for details.
//
// Copyright (c) 2019 QMCPACK developers.
//
// File developed by: Ye Luo, yeluo@anl.gov, Argonne National Laboratory
//
// File created by: Jeongnim Kim, jeongnim.kim@intel.com, Intel Corp.
//////////////////////////////////////////////////////////////////////////////////////

#ifndef QMCPLUSPLUS_DIRAC_MATRIX_H
#define QMCPLUSPLUS_DIRAC_MATRIX_H

#include "CPU/Blasf.h"
#include "CPU/BLAS.hpp"
#include "Numerics/OhmmsPETE/OhmmsMatrix.h"
#include "CPU/BlasThreadingEnv.h"
#include "Utilities/OpenMP.h"
#include "Utilities/scalar_traits.h"
#include "CPU/SIMD/simd.hpp"
#include "QMCWaveFunctions/DeterminantHelper.h"

namespace qmcplusplus
{
template<typename T>
inline T computeLogDet(const T* restrict diag, int n, const int* restrict pivot, T& phase)
{
  T logdet(0);
  int sign_det = 1;
  for (size_t i = 0; i < n; i++)
  {
    sign_det *= (pivot[i] == i + 1) ? 1 : -1;
    sign_det *= (diag[i] > 0) ? 1 : -1;
    logdet += std::log(std::abs(diag[i]));
  }
  phase = (sign_det > 0) ? T(0) : M_PI;
  return logdet;
}

template<typename T>
inline T computeLogDet(const std::complex<T>* restrict diag, int n, const int* restrict pivot, T& phase)
{
  T logdet(0);
  phase = T(0);
  for (size_t i = 0; i < n; i++)
  {
    phase += std::arg(diag[i]);
    if (pivot[i] != i + 1)
      phase += M_PI;
    logdet += std::log(diag[i].real() * diag[i].real() + diag[i].imag() * diag[i].imag());
    //slightly smaller error with the following
    //        logdet+=2.0*std::log(std::abs(x[ii]);
  }
  constexpr T one_over_2pi = T(1) / TWOPI;
  phase -= std::floor(phase * one_over_2pi) * TWOPI;
  return 0.5 * logdet;
}

template<typename T_FP, typename T = T_FP>
class DiracMatrix
{
  typedef typename scalar_traits<T>::real_type real_type;
  typedef typename scalar_traits<T_FP>::real_type real_type_fp;
  aligned_vector<T_FP> m_work;
  aligned_vector<int> m_pivot;
  int Lwork;
  /// scratch space used for mixed precision
  Matrix<T_FP> psiM_fp;
  /// LU diagonal elements
  aligned_vector<T_FP> LU_diag;

  /// reset internal work space
  inline void reset(T_FP* invMat_ptr, const int lda)
  {
    m_pivot.resize(lda);
    Lwork = -1;
    T_FP tmp;
    real_type_fp lw;
    int status;
    LAPACK::getri(lda, invMat_ptr, lda, m_pivot.data(), &tmp, Lwork, status);
    convert(tmp, lw);
    Lwork = static_cast<int>(lw);
    m_work.resize(Lwork);
    LU_diag.resize(lda);
  }

public:
  DiracMatrix() : Lwork(0) {}

  /** compute the inverse of the transpose of matrix A
   * assume precision T_FP >= T, do the inversion always with T_FP
   */
  inline void invert_transpose(const Matrix<T>& amat, Matrix<T>& invMat, real_type& LogDet, real_type& Phase)
  {
    BlasThreadingEnv knob(getNextLevelNumThreads());
    const int n   = invMat.rows();
    const int lda = invMat.cols();
    T_FP* invMat_ptr(nullptr);
#if !defined(MIXED_PRECISION)
    simd::transpose(amat.data(), n, amat.cols(), invMat.data(), n, invMat.cols());
    invMat_ptr = invMat.data();
#else
    psiM_fp.resize(n, lda);
    simd::transpose(amat.data(), n, amat.cols(), psiM_fp.data(), n, psiM_fp.cols());
    invMat_ptr = psiM_fp.data();
#endif
    if (Lwork < lda)
      reset(invMat_ptr, lda);
    int status;
    LAPACK::getrf(n, n, invMat_ptr, lda, m_pivot.data(), status);
    for (int i = 0; i < n; i++)
      LU_diag[i] = invMat_ptr[i * lda + i];
    real_type_fp Phase_tmp;
    LogDet = computeLogDet(LU_diag.data(), n, m_pivot.data(), Phase_tmp);
    Phase  = Phase_tmp;
    LAPACK::getri(n, invMat_ptr, lda, m_pivot.data(), m_work.data(), Lwork, status);
#if defined(MIXED_PRECISION)
    invMat = psiM_fp;
#endif
  }
};
} // namespace qmcplusplus

#endif // QMCPLUSPLUS_DIRAC_MATRIX_H
