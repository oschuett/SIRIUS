// Copyright (c) 2013-2016 Anton Kozhevnikov, Thomas Schulthess
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification, are permitted provided that 
// the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the 
//    following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions 
//    and the following disclaimer in the documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED 
// WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A 
// PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR 
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, 
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER 
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR 
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

/** \file wave_functions.h
 *   
 *  \brief Contains declaration and implementation of sirius::Wave_functions class.
 */

#ifndef __WAVE_FUNCTIONS_H__
#define __WAVE_FUNCTIONS_H__

#include "gvec.h"
#include "mpi_grid.h"
#include "linalg.h"

#include "matrix_storage.h"

namespace sirius {

// pw wave-functions, lapwlo eigen vectors (pw + lo coeffs), lapwlo wave functions (mt+lo+pw)
// for mt part: split between atoms, provide counts + offsets
// local size is counts[r], total size is offsets[r] + counts[r]
class wave_functions
{
    private:

        Simulation_parameters const& params_;

        Communicator const& comm_;

        Gvec const& gkvec_;

        block_data_descriptor mt_distr_;

        int num_wf_{0};

        std::unique_ptr<matrix_storage<double_complex, matrix_storage_t::fft_slab>> pw_coeffs_{nullptr};

        std::unique_ptr<matrix_storage<double_complex, matrix_storage_t::slab>> mt_coeffs_{nullptr};

    public:
        
        /// Constructor for PW wave-functions.
        wave_functions(Simulation_parameters const& params__,
                       Communicator const& comm__,
                       Gvec const& gkvec__,
                       int num_wf__)
            : params_(params__),
              comm_(comm__),
              gkvec_(gkvec__),
              num_wf_(num_wf__)
        {
            pw_coeffs_ = std::unique_ptr<matrix_storage<double_complex, matrix_storage_t::fft_slab>>(
                new matrix_storage<double_complex, matrix_storage_t::fft_slab>(gkvec_.gvec_count(comm_.rank()), num_wf_, params_.processing_unit()));
        }

        /// Constructor for LAPW wave-functions.
        wave_functions(Simulation_parameters const& params__,
                       Communicator const& comm__,
                       Gvec const& gkvec__,
                       block_data_descriptor const& mt_distr__,
                       int num_wf__)
            : params_(params__),
              comm_(comm__),
              gkvec_(gkvec__),
              mt_distr_(mt_distr__),
              num_wf_(num_wf__)
        {
            pw_coeffs_ = std::unique_ptr<matrix_storage<double_complex, matrix_storage_t::fft_slab>>(
                new matrix_storage<double_complex, matrix_storage_t::fft_slab>(gkvec_.gvec_count(comm_.rank()), num_wf_, params_.processing_unit()));
            
            mt_coeffs_ = std::unique_ptr<matrix_storage<double_complex, matrix_storage_t::slab>>(
                new matrix_storage<double_complex, matrix_storage_t::slab>(mt_distr_.counts[comm_.rank()], num_wf_, params_.processing_unit()));
        }

        matrix_storage<double_complex, matrix_storage_t::fft_slab>& pw_coeffs()
        {
            return *pw_coeffs_;
        }

        matrix_storage<double_complex, matrix_storage_t::fft_slab> const& pw_coeffs() const
        {
            return *pw_coeffs_;
        }

        matrix_storage<double_complex, matrix_storage_t::slab>& mt_coeffs()
        {
            return *mt_coeffs_;
        }

        matrix_storage<double_complex, matrix_storage_t::slab> const& mt_coeffs() const
        {
            return *mt_coeffs_;
        }

        Simulation_parameters const& params() const
        {
            return params_;
        }

        inline void copy_from(wave_functions const& src__,
                              int i0__,
                              int n__,
                              int j0__)
        {
            switch (params_.processing_unit()) {
                case CPU: {
                    std::memcpy(pw_coeffs().prime().at<CPU>(0, j0__),
                                src__.pw_coeffs().prime().at<CPU>(0, i0__),
                                pw_coeffs().num_rows_loc() * n__ * sizeof(double_complex));
                    if (params_.full_potential()) {
                        std::memcpy(mt_coeffs().prime().at<CPU>(0, j0__),
                                    src__.mt_coeffs().prime().at<CPU>(0, i0__),
                                    mt_coeffs().num_rows_loc() * n__ * sizeof(double_complex));
                    }
                    break;
                }
                case GPU: {
                    #ifdef __GPU
                    acc::copy(wf_coeffs_.at<GPU>(0, j0__), src__.wf_coeffs_.at<GPU>(0, i0__), num_gvec_loc_ * n__);
                    if (params_.full_potential()) {
                        acc::copy(&mt_coeffs().prime_(0, j0__), &src__.mt_coeffs().prime_(0, i0__), mt_coeffs().num_rows_loc() * n__ * sizeof(double_complex));
                    }
                    #endif
                    break;
                }
            }
        }
        
        inline void copy_from(wave_functions const& src__, int i0__, int n__)
        {
            copy_from(src__, i0__, n__, i0__);
        }

        template <typename T>
        inline void transform_from(wave_functions& wf__,
                                   int nwf__,
                                   matrix<T>& mtrx__,
                                   int n__);
};

template<>
inline void wave_functions::transform_from<double_complex>(wave_functions& wf__,
                                                           int nwf__,
                                                           matrix<double_complex>& mtrx__,
                                                           int n__)
{
    assert(&params_ == &wf__.params());
    assert(pw_coeffs().num_rows_loc() == wf__.pw_coeffs().num_rows_loc());
    if (params_.full_potential()) {
        assert(mt_coeffs().num_rows_loc() == wf__.mt_coeffs().num_rows_loc());
    }

    if (params_.processing_unit() == CPU) {
        linalg<CPU>::gemm(0, 0, pw_coeffs().num_rows_loc(), n__, nwf__,
                          wf__.pw_coeffs().prime().at<CPU>(), wf__.pw_coeffs().prime().ld(),
                          mtrx__.at<CPU>(), mtrx__.ld(),
                          pw_coeffs().prime().at<CPU>(), pw_coeffs().prime().ld());
        if (params_.full_potential()) {
            linalg<CPU>::gemm(0, 0, mt_coeffs().num_rows_loc(), n__, nwf__,
                              wf__.mt_coeffs().prime().at<CPU>(), wf__.mt_coeffs().prime().ld(),
                              mtrx__.at<CPU>(), mtrx__.ld(),
                              mt_coeffs().prime().at<CPU>(), mt_coeffs().prime().ld());
        }
    }
    #ifdef __GPU
    if (pu_ == GPU) {
        linalg<GPU>::gemm(0, 0, num_rows_loc(), n__, nwf__, wf__.prime().at<GPU>(), wf__.prime().ld(),
                          mtrx__.at<GPU>(), mtrx__.ld(), prime_.at<GPU>(), prime_.ld());
    }
    #endif
}

template<>
inline void wave_functions::transform_from<double>(wave_functions& wf__,
                                                   int nwf__,
                                                   matrix<double>& mtrx__,
                                                   int n__)
{
    assert(&params_ == &wf__.params());
    assert(pw_coeffs().num_rows_loc() == wf__.pw_coeffs().num_rows_loc());
    if (params_.full_potential()) {
        assert(mt_coeffs().num_rows_loc() == wf__.mt_coeffs().num_rows_loc());
    }

    if (params_.processing_unit() == CPU) {
        linalg<CPU>::gemm(0, 0, 2 * pw_coeffs().num_rows_loc(), n__, nwf__,
                          (double*)wf__.pw_coeffs().prime().at<CPU>(), 2 * wf__.pw_coeffs().prime().ld(),
                          mtrx__.at<CPU>(), mtrx__.ld(),
                          (double*)pw_coeffs().prime().at<CPU>(), 2 * pw_coeffs().prime().ld());
        if (params_.full_potential()) {
            TERMINATE_NOT_IMPLEMENTED;
        }
    }
    #ifdef __GPU
    if (pu_ == GPU) {
        linalg<GPU>::gemm(0, 0, 2 * num_rows_loc(), n__, nwf__, (double*)wf__.prime().at<GPU>(), 2 * wf__.prime().ld(),
                          mtrx__.at<GPU>(), mtrx__.ld(), (double*)prime_.at<GPU>(), 2 * prime_.ld());
    }
    #endif
}

inline mdarray<double, 1>& inner_prod_buf(size_t new_size__)
{
    static mdarray<double, 1> buf;
    if (new_size__ > buf.size()) {
        buf = mdarray<double, 1>(new_size__);
    }
    return buf;
}

inline void inner(wave_functions& bra__,
                  int i0__,
                  int m__,
                  wave_functions& ket__,
                  int j0__,
                  int n__,
                  mdarray<double_complex, 2>& result__,
                  int irow__,
                  int icol__,
                  Communicator const& comm__,
                  device_t pu__)
{
    PROFILE_WITH_TIMER("sirius::wave_functions::inner");
    
    assert(&bra__.params() == &ket__.params());
    assert(bra__.pw_coeffs().num_rows_loc() == ket__.pw_coeffs().num_rows_loc());
    if (bra__.params().full_potential()) {
        assert(bra__.mt_coeffs().num_rows_loc() == ket__.mt_coeffs().num_rows_loc());
    }

    /* single rank, CPU: store result directly in the output matrix */
    if (comm__.size() == 1 && pu__ == CPU) {
        linalg<CPU>::gemm(2, 0, m__, n__, bra__.pw_coeffs().num_rows_loc(),
                          bra__.pw_coeffs().prime().at<CPU>(0, i0__), bra__.pw_coeffs().prime().ld(),
                          ket__.pw_coeffs().prime().at<CPU>(0, j0__), ket__.pw_coeffs().prime().ld(),
                          result__.at<CPU>(irow__, icol__), result__.ld());
        if (bra__.params().full_potential()) {
            double_complex alpha(1, 0);
            linalg<CPU>::gemm(2, 0, m__, n__, bra__.mt_coeffs().num_rows_loc(),
                              alpha,
                              bra__.mt_coeffs().prime().at<CPU>(0, i0__), bra__.mt_coeffs().prime().ld(),
                              ket__.mt_coeffs().prime().at<CPU>(0, j0__), ket__.mt_coeffs().prime().ld(),
                              alpha,
                              result__.at<CPU>(irow__, icol__), result__.ld());
        }
    } else {
        auto& buf = inner_prod_buf(2 * m__ * n__);
        switch (pu__) {
            case CPU: {
                linalg<CPU>::gemm(2, 0, m__, n__, bra__.pw_coeffs().num_rows_loc(),
                                  bra__.pw_coeffs().prime().at<CPU>(0, i0__), bra__.pw_coeffs().prime().ld(),
                                  ket__.pw_coeffs().prime().at<CPU>(0, j0__), ket__.pw_coeffs().prime().ld(),
                                  (double_complex*)buf.at<CPU>(), m__);
                if (bra__.params().full_potential()) {
                    double_complex alpha(1, 0);
                    linalg<CPU>::gemm(2, 0, m__, n__, bra__.mt_coeffs().num_rows_loc(),
                                      alpha,
                                      bra__.mt_coeffs().prime().at<CPU>(0, i0__), bra__.mt_coeffs().prime().ld(),
                                      ket__.mt_coeffs().prime().at<CPU>(0, j0__), ket__.mt_coeffs().prime().ld(),
                                      alpha,
                                      (double_complex*)buf.at<CPU>(), m__);
                }
                break;
            }
            case GPU: {
                #ifdef __GPU
                buf.allocate(memory_t::device);
                linalg<GPU>::gemm(2, 0, m__, n__, bra__.num_rows_loc(),
                                  bra__.prime().at<GPU>(0, i0__), bra__.prime().ld(),
                                  ket__.prime().at<GPU>(0, j0__), ket__.prime().ld(),
                                  (double_complex*)buf.at<GPU>(), m__);
                buf.copy_to_host(2 * m__ * n__);
                buf.deallocate_on_device();
                #else
                TERMINATE_NO_GPU
                #endif
                break;
            }
        }

        comm__.allreduce(&buf[0], 2 * m__ * n__);

        for (int i = 0; i < n__; i++) {
            std::memcpy(&result__(irow__, icol__ + i), &buf[2 * i * m__], m__ * sizeof(double_complex));
        }
    }
}

inline void inner(wave_functions& bra__,
                  int i0__,
                  int m__,
                  wave_functions& ket__,
                  int j0__,
                  int n__,
                  mdarray<double, 2>& result__,
                  int irow__,
                  int icol__,
                  Communicator const& comm__,
                  device_t pu__)
{
    PROFILE_WITH_TIMER("sirius::wave_functions::inner");

    assert(&bra__.params() == &ket__.params());
    assert(bra__.pw_coeffs().num_rows_loc() == ket__.pw_coeffs().num_rows_loc());
    if (bra__.params().full_potential()) {
        TERMINATE_NOT_IMPLEMENTED;
    }

    /* single rank, CPU: store result directly in the output matrix */
    if (comm__.size() == 1 && pu__ == CPU) {
        linalg<CPU>::gemm(2, 0, m__, n__, bra__.pw_coeffs().num_rows_loc(),
                          (double*)bra__.pw_coeffs().prime().at<CPU>(0, i0__), 2 * bra__.pw_coeffs().prime().ld(),
                          (double*)ket__.pw_coeffs().prime().at<CPU>(0, j0__), 2 * ket__.pw_coeffs().prime().ld(),
                          result__.at<CPU>(irow__, icol__), result__.ld());
        
        for (int j = 0; j < n__; j++) {
            for (int i = 0; i < m__; i++) {
                result__(irow__ + i, icol__ + j) = 2 * result__(irow__ + i, icol__ + j) -
                                                   bra__.pw_coeffs().prime(0, i0__ + i).real() * ket__.pw_coeffs().prime(0, j0__ + j).real();
            }
        }
    } else {
        auto& buf = inner_prod_buf(m__ * n__);
        double alpha = 2;
        double beta = 0;
        switch (pu__) {
            case CPU: {
                linalg<CPU>::gemm(1, 0, m__, n__, 2 * bra__.pw_coeffs().num_rows_loc(),
                                  alpha,
                                  (double*)bra__.pw_coeffs().prime().at<CPU>(0, i0__), 2 * bra__.pw_coeffs().prime().ld(),
                                  (double*)ket__.pw_coeffs().prime().at<CPU>(0, j0__), 2 * ket__.pw_coeffs().prime().ld(),
                                  beta,
                                  buf.at<CPU>(), m__);
                if (comm__.rank() == 0) {
                    /* subtract one extra G=0 contribution */
                    linalg<CPU>::ger(m__, n__, -1.0,
                                    (double*)bra__.pw_coeffs().prime().at<CPU>(0, i0__), 2 * bra__.pw_coeffs().prime().ld(),
                                    (double*)ket__.pw_coeffs().prime().at<CPU>(0, j0__), 2 * ket__.pw_coeffs().prime().ld(),
                                    buf.at<CPU>(), m__); 
                }
                break;
            }
            case GPU: {
                #ifdef __GPU
                buf.allocate(memory_t::device);
                linalg<GPU>::gemm(1, 0, m__, n__, 2 * bra__.num_rows_loc(),
                                  &alpha,
                                  (double*)bra__.pw_coeffs().prime().at<GPU>(0, i0__), 2 * bra__.pw_coeffs().prime().ld(),
                                  (double*)ket__.pw_coeffs().prime().at<GPU>(0, j0__), 2 * ket__.pw_coeffs().prime().ld(),
                                  &beta,
                                  buf.at<GPU>(), m__);
                double alpha1 = -1;
                if (comm__.rank() == 0) {
                    /* subtract one extra G=0 contribution */
                    linalg<GPU>::ger(m__, n__, &alpha1,
                                    (double*)bra__.pw_coeffs().prime().at<GPU>(0, i0__), 2 * bra__.pw_coeffs().prime().ld(),
                                    (double*)ket__.pw_coeffs().prime().at<GPU>(0, j0__), 2 * ket__.pw_coeffs().prime().ld(),
                                    buf.at<GPU>(), m__); 
                }
                buf.copy_to_host(m__ * n__);
                buf.deallocate_on_device();
                #else
                TERMINATE_NO_GPU
                #endif
                break;
            }
        }

        comm__.allreduce(&buf[0], m__ * n__);

        for (int i = 0; i < n__; i++) {
            std::memcpy(&result__(irow__, icol__ + i), &buf[i * m__], m__ * sizeof(double));
        }
    }
}

}

#endif
