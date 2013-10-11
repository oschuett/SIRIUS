/** \file atom.hpp
    
    \brief Implementation of methods for Atom class.
*/

Atom::Atom(Atom_type* type__, double* position__, double* vector_field__) : 
    type_(type__), symmetry_class_(NULL), offset_aw_(-1), offset_lo_(-1), offset_wf_(-1), apply_uj_correction_(false), 
    uj_correction_l_(-1)
{
    assert(type__);
        
    for (int i = 0; i < 3; i++)
    {
        position_[i] = position__[i];
        vector_field_[i] = vector_field__[i];
    }
}

void Atom::init(int lmax_pot__, int num_mag_dims__, int offset_aw__, int offset_lo__, int offset_wf__)
{
    assert(lmax_pot__ >= 0);
    assert(offset_aw__ >= 0);
    
    offset_aw_ = offset_aw__;
    offset_lo_ = offset_lo__;
    offset_wf_ = offset_wf__;

    lmax_pot_ = lmax_pot__;
    num_mag_dims_ = num_mag_dims__;

    int lmmax = Utils::lmmax(lmax_pot_);

    h_radial_integrals_.set_dimensions(lmmax, type()->indexr().size(), type()->indexr().size());
    h_radial_integrals_.allocate();
    
    veff_.set_dimensions(lmmax, type()->num_mt_points());
    
    b_radial_integrals_.set_dimensions(lmmax, type()->indexr().size(), type()->indexr().size(), num_mag_dims_);
    b_radial_integrals_.allocate();
    
    for (int j = 0; j < 3; j++) beff_[j].set_dimensions(lmmax, type()->num_mt_points());

    occupation_matrix_.set_dimensions(16, 16, 2, 2);
    occupation_matrix_.allocate();
    
    uj_correction_matrix_.set_dimensions(16, 16, 2, 2);
    uj_correction_matrix_.allocate();
}

void Atom::generate_radial_integrals(MPI_Comm& comm)
{
    Timer t("sirius::Atom::generate_radial_integrals");
    
    int lmmax = Utils::lmmax(lmax_pot_);
    int nmtp = type()->num_mt_points();

    splindex<block> spl_lm(lmmax - 1, Platform::num_mpi_ranks(comm), Platform::mpi_rank(comm));
    
    splindex<block> spl_lm_b(lmmax, Platform::num_mpi_ranks(comm), Platform::mpi_rank(comm));

    std::vector<int> l_by_lm = Utils::l_by_lm(lmax_pot_);

    h_radial_integrals_.zero();
    if (num_mag_dims_) b_radial_integrals_.zero();
    
    #pragma omp parallel default(shared)
    {
        Spline<double> s(nmtp, type()->radial_grid());
        std::vector<double> v(nmtp);

        for (int lm_loc = 0; lm_loc < spl_lm.local_size(); lm_loc++)
        {
            int lm = spl_lm[lm_loc] + 1; // skip spherical part
            int l = l_by_lm[lm];

            #pragma omp for
            for (int i2 = 0; i2 < type()->indexr().size(); i2++)
            {
                int l2 = type()->indexr(i2).l;
                for (int ir = 0; ir < nmtp; ir++) v[ir] = symmetry_class()->radial_function(ir, i2) * veff_(lm, ir);
                
                for (int i1 = 0; i1 <= i2; i1++)
                {
                    int l1 = type()->indexr(i1).l;
                    if ((l + l1 + l2) % 2 == 0)
                    {
                        for (int ir = 0; ir < nmtp; ir++) s[ir] = symmetry_class()->radial_function(ir, i1) * v[ir];
                        
                        h_radial_integrals_(lm, i1, i2) = h_radial_integrals_(lm, i2, i1) = s.interplate().integrate(2);
                    }
                }
            }
        }
    }
    Platform::reduce(h_radial_integrals_.get_ptr(), (int)h_radial_integrals_.size(), comm, 0);

    // copy spherical integrals
    for (int i2 = 0; i2 < type()->indexr().size(); i2++)
    {
        for (int i1 = 0; i1 < type()->indexr().size(); i1++)
            h_radial_integrals_(0, i1, i2) = symmetry_class()->h_spherical_integral(i1, i2);
    }

    #pragma omp parallel default(shared)
    {
        Spline<double> s(nmtp, type()->radial_grid());
        std::vector<double> v(nmtp);
        
        for (int j = 0; j < num_mag_dims_; j++)
        {
            for (int lm_loc = 0; lm_loc < spl_lm_b.local_size(); lm_loc++)
            {
                int lm = spl_lm_b[lm_loc];
                int l = l_by_lm[lm];
                
                #pragma omp for
                for (int i2 = 0; i2 < type()->indexr().size(); i2++)
                {
                    int l2 = type()->indexr(i2).l;
                    for (int ir = 0; ir < nmtp; ir++) v[ir] = symmetry_class()->radial_function(ir, i2) * beff_[j](lm, ir);
                    
                    for (int i1 = 0; i1 <= i2; i1++)
                    {
                        int l1 = type()->indexr(i1).l;
                        if ((l + l1 + l2) % 2 == 0)
                        {
                            for (int ir = 0; ir < nmtp; ir++) s[ir] = symmetry_class()->radial_function(ir, i1) * v[ir];
                            
                            b_radial_integrals_(lm, i1, i2, j) = b_radial_integrals_(lm, i2, i1, j) = s.interpolate().integrate(2);
                        }
                    }
                }
            }
        }
    }
    if (num_mag_dims_) Platform::reduce(b_radial_integrals_.get_ptr(), (int)b_radial_integrals_.size(), comm, 0);
}

