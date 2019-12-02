// Copyright (c) 2013-2018 Anton Kozhevnikov, Thomas Schulthess
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

/** \file augmentation_operator.cpp
 *
 *  \brief Contains implementation of sirius::Augmentation_operator class.
 */

#include "augmentation_operator.hpp"

namespace sirius {

void Augmentation_operator::generate_pw_coeffs(Radial_integrals_aug<false> const& radial_integrals__, memory_pool& mp__)
{
    if (!atom_type_.augment()) {
        return;
    }
    PROFILE("sirius::Augmentation_operator::generate_pw_coeffs");

    double fourpi_omega = fourpi / gvec_.omega();

    /* maximum l of beta-projectors */
    int lmax_beta = atom_type_.indexr().lmax();
    int lmmax     = utils::lmmax(2 * lmax_beta);

    auto l_by_lm = utils::l_by_lm(2 * lmax_beta);

    std::vector<double_complex> zilm(lmmax);
    for (int l = 0, lm = 0; l <= 2 * lmax_beta; l++) {
        for (int m = -l; m <= l; m++, lm++) {
            zilm[lm] = std::pow(double_complex(0, 1), l);
        }
    }

    /* Gaunt coefficients of three real spherical harmonics */
    Gaunt_coefficients<double> gaunt_coefs(lmax_beta, 2 * lmax_beta, lmax_beta, SHT::gaunt_rlm);

    /* split G-vectors between ranks */
    int gvec_count  = gvec_.count();

    /* array of real spherical harmonics for each G-vector */
    mdarray<double, 2> gvec_rlm(utils::lmmax(2 * lmax_beta), gvec_count);
    #pragma omp parallel for schedule(static)
    for (int igloc = 0; igloc < gvec_count; igloc++) {
        auto rtp = SHT::spherical_coordinates(gvec_.gvec_cart<index_domain_t::local>(igloc));
        SHT::spherical_harmonics(2 * lmax_beta, rtp[1], rtp[2], &gvec_rlm(0, igloc));
    }

    /* number of beta- radial functions */
    int nbrf = atom_type_.mt_radial_basis_size();

    PROFILE_START("sirius::Augmentation_operator::generate_pw_coeffs|1");
    sddk::mdarray<double, 3> ri_values(nbrf * (nbrf + 1) / 2, 2 * lmax_beta + 1, gvec_.num_gvec_shells_local());
    #pragma omp parallel for
    for (int j = 0; j < gvec_.num_gvec_shells_local(); j++) {
        auto ri = radial_integrals__.values(atom_type_.id(), gvec_.gvec_shell_len_local(j));
        for (int l = 0; l <= 2 * lmax_beta; l++) {
            for (int i = 0; i < nbrf * (nbrf + 1) / 2; i++) {
                ri_values(i, l, j) = ri(i, l);
            }
        }
    }
    PROFILE_STOP("sirius::Augmentation_operator::generate_pw_coeffs|1");

    /* number of beta-projectors */
    int nbf = atom_type_.mt_basis_size();

    /* flatten the indices */
    sddk::mdarray<int, 2> idx(3, nbf * (nbf + 1) / 2);
    for (int xi2 = 0; xi2 < nbf; xi2++) {
        int lm2    = atom_type_.indexb(xi2).lm;
        int idxrf2 = atom_type_.indexb(xi2).idxrf;

        for (int xi1 = 0; xi1 <= xi2; xi1++) {
            int lm1    = atom_type_.indexb(xi1).lm;
            int idxrf1 = atom_type_.indexb(xi1).idxrf;

            /* packed orbital index */
            int idx12 = utils::packed_index(xi1, xi2);
            /* packed radial-function index */
            int idxrf12 = utils::packed_index(idxrf1, idxrf2);

            idx(0, idx12) = lm1;
            idx(1, idx12) = lm2;
            idx(2, idx12) = idxrf12;
        }
    }

    PROFILE_START("sirius::Augmentation_operator::generate_pw_coeffs|2");
    /* array of plane-wave coefficients */
    q_pw_ = mdarray<double, 2>(nbf * (nbf + 1) / 2, 2 * gvec_count, mp__, "q_pw_");
    #pragma omp parallel for schedule(static)
    for (int igloc = 0; igloc < gvec_count; igloc++) {
        std::vector<double_complex> v(lmmax);
        for (int idx12 = 0; idx12 < nbf * (nbf + 1) / 2; idx12++) {
            int lm1     = idx(0, idx12);
            int lm2     = idx(1, idx12);
            int idxrf12 = idx(2, idx12);
            for (int lm3 = 0; lm3 < lmmax; lm3++) {
                v[lm3] = std::conj(zilm[lm3]) * gvec_rlm(lm3, igloc) * ri_values(idxrf12, l_by_lm[lm3], gvec_.gvec_shell_idx_local(igloc));
            }
            double_complex z = fourpi_omega * gaunt_coefs.sum_L3_gaunt(lm2, lm1, &v[0]);
            q_pw_(idx12, 2 * igloc)     = z.real();
            q_pw_(idx12, 2 * igloc + 1) = z.imag();
        }
    }
    PROFILE_STOP("sirius::Augmentation_operator::generate_pw_coeffs|2");

    sym_weight_ = mdarray<double, 1>(nbf * (nbf + 1) / 2, mp__, "sym_weight_");
    for (int xi2 = 0; xi2 < nbf; xi2++) {
        for (int xi1 = 0; xi1 <= xi2; xi1++) {
            /* packed orbital index */
            int idx12          = utils::packed_index(xi1, xi2);
            sym_weight_(idx12) = (xi1 == xi2) ? 1 : 2;
        }
    }

    q_mtrx_ = mdarray<double, 2>(nbf, nbf);
    q_mtrx_.zero();

    if (gvec_.comm().rank() == 0) {
        for (int xi2 = 0; xi2 < nbf; xi2++) {
            for (int xi1 = 0; xi1 <= xi2; xi1++) {
                /* packed orbital index */
                int idx12         = utils::packed_index(xi1, xi2);
                q_mtrx_(xi1, xi2) = q_mtrx_(xi2, xi1) = gvec_.omega() * q_pw_(idx12, 0);
            }
        }
    }
    /* broadcast from rank#0 */
    gvec_.comm().bcast(&q_mtrx_(0, 0), nbf * nbf, 0);

    if (atom_type_.parameters().control().print_checksum_) {
        auto cs = q_pw_.checksum();
        auto cs1 = q_mtrx_.checksum();
        gvec_.comm().allreduce(&cs, 1);
        if (gvec_.comm().rank() == 0) {
            utils::print_checksum("q_pw", cs);
            utils::print_checksum("q_mtrx", cs1);
        }
    }
}

} // namespace sirius