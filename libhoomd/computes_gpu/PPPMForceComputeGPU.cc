#include "PPPMForceComputeGPU.h"

#ifdef ENABLE_CUDA
#include "PPPMForceComputeGPU.cuh"

using namespace boost::python;

/*! \param sysdef The system definition
    \param nlist Neighbor list
    \param group Particle group to apply forces to
 */
PPPMForceComputeGPU::PPPMForceComputeGPU(boost::shared_ptr<SystemDefinition> sysdef,
    boost::shared_ptr<NeighborList> nlist,
    boost::shared_ptr<ParticleGroup> group)
    : PPPMForceCompute(sysdef,nlist,group),
      m_local_fft(true),
      m_sum(m_exec_conf),
      m_block_size(256),
      m_gpu_q_max(m_exec_conf)
    {
    // initial value of number of particles per bin
    m_cell_size = 2;

    m_tuner_bin.reset(new Autotuner(32, 1024, 32, 5, 100000, "pppm_bin", this->m_exec_conf));
    m_tuner_assign.reset(new Autotuner(32, 1024, 32, 5, 100000, "pppm_assign", this->m_exec_conf));
    m_tuner_update.reset(new Autotuner(32, 1024, 32, 5, 100000, "pppm_update_mesh", this->m_exec_conf));
    m_tuner_force.reset(new Autotuner(32, 1024, 32, 5, 100000, "pppm_force", this->m_exec_conf));
    m_tuner_influence.reset(new Autotuner(32, 1024, 32, 5, 100000, "pppm_influence", this->m_exec_conf));
    }

PPPMForceComputeGPU::~PPPMForceComputeGPU()
    {
    if (m_local_fft)
        cufftDestroy(m_cufft_plan);
    else
    #ifdef ENABLE_MPI
        {
        dfft_destroy_plan(m_dfft_plan_forward);
        dfft_destroy_plan(m_dfft_plan_inverse);
        }
    #endif
    }

void PPPMForceComputeGPU::initializeFFT()
    {
    #ifdef ENABLE_MPI
    m_local_fft = !m_pdata->getDomainDecomposition();

    if (! m_local_fft)
        {
        // ghost cell communicator for charge interpolation
        m_gpu_grid_comm_forward = boost::shared_ptr<CommunicatorGridGPUComplex>(
            new CommunicatorGridGPUComplex(m_sysdef,
               make_uint3(m_mesh_points.x, m_mesh_points.y, m_mesh_points.z),
               make_uint3(m_grid_dim.x, m_grid_dim.y, m_grid_dim.z),
               m_n_ghost_cells,
               true));
        // ghost cell communicator for force mesh
        m_gpu_grid_comm_reverse = boost::shared_ptr<CommunicatorGridGPUComplex >(
            new CommunicatorGridGPUComplex(m_sysdef,
               make_uint3(m_mesh_points.x, m_mesh_points.y, m_mesh_points.z),
               make_uint3(m_grid_dim.x, m_grid_dim.y, m_grid_dim.z),
               m_n_ghost_cells,
               false));

        // set up distributed FFT
        int gdim[3];
        int pdim[3];
        Index3D decomp_idx = m_pdata->getDomainDecomposition()->getDomainIndexer();
        pdim[0] = decomp_idx.getD();
        pdim[1] = decomp_idx.getH();
        pdim[2] = decomp_idx.getW();
        gdim[0] = m_mesh_points.z*pdim[0];
        gdim[1] = m_mesh_points.y*pdim[1];
        gdim[2] = m_mesh_points.x*pdim[2];
        int embed[3];
        embed[0] = m_mesh_points.z+2*m_n_ghost_cells.z;
        embed[1] = m_mesh_points.y+2*m_n_ghost_cells.y;
        embed[2] = m_mesh_points.x+2*m_n_ghost_cells.x;
        m_ghost_offset = (m_n_ghost_cells.z*embed[1]+m_n_ghost_cells.y)*embed[2]+m_n_ghost_cells.x;
        uint3 pcoord = m_pdata->getDomainDecomposition()->getGridPos();
        int pidx[3];
        pidx[0] = pcoord.z;
        pidx[1] = pcoord.y;
        pidx[2] = pcoord.x;
        int row_m = 0; /* both local grid and proc grid are row major, no transposition necessary */
        ArrayHandle<unsigned int> h_cart_ranks(m_pdata->getDomainDecomposition()->getCartRanks(),
            access_location::host, access_mode::read);
        #ifndef USE_HOST_DFFT
        dfft_cuda_create_plan(&m_dfft_plan_forward, 3, gdim, embed, NULL, pdim, pidx,
            row_m, 0, 1, m_exec_conf->getMPICommunicator(), (int *) h_cart_ranks.data);
        dfft_cuda_create_plan(&m_dfft_plan_inverse, 3, gdim, NULL, embed, pdim, pidx,
            row_m, 0, 1, m_exec_conf->getMPICommunicator(), (int *)h_cart_ranks.data);
        #else
        dfft_create_plan(&m_dfft_plan_forward, 3, gdim, embed, NULL, pdim, pidx,
            row_m, 0, 1, m_exec_conf->getMPICommunicator(), (int *) h_cart_ranks.data);
        dfft_create_plan(&m_dfft_plan_inverse, 3, gdim, NULL, embed, pdim, pidx,
            row_m, 0, 1, m_exec_conf->getMPICommunicator(), (int *) h_cart_ranks.data);
        #endif
        }
    #endif // ENABLE_MPI

    if (m_local_fft)
        {
        cufftPlan3d(&m_cufft_plan, m_mesh_points.z, m_mesh_points.y, m_mesh_points.x, CUFFT_C2C);
        }

    unsigned int n_particle_bins = m_grid_dim.x*m_grid_dim.y*m_grid_dim.z;
    m_bin_idx = Index2D(n_particle_bins,m_cell_size);
    m_scratch_idx = Index2D(n_particle_bins,(2*m_radius+1)*(2*m_radius+1)*(2*m_radius+1));

    // allocate mesh and transformed mesh
    GPUArray<cufftComplex> mesh(m_n_cells,m_exec_conf);
    m_mesh.swap(mesh);

    GPUArray<cufftComplex> fourier_mesh(m_n_inner_cells, m_exec_conf);
    m_fourier_mesh.swap(fourier_mesh);

    GPUArray<cufftComplex> fourier_mesh_G_x(m_n_inner_cells, m_exec_conf);
    m_fourier_mesh_G_x.swap(fourier_mesh_G_x);

    GPUArray<cufftComplex> fourier_mesh_G_y(m_n_inner_cells, m_exec_conf);
    m_fourier_mesh_G_y.swap(fourier_mesh_G_y);

    GPUArray<cufftComplex> fourier_mesh_G_z(m_n_inner_cells, m_exec_conf);
    m_fourier_mesh_G_z.swap(fourier_mesh_G_z);

    GPUArray<cufftComplex> inv_fourier_mesh_x(m_n_cells, m_exec_conf);
    m_inv_fourier_mesh_x.swap(inv_fourier_mesh_x);

    GPUArray<cufftComplex> inv_fourier_mesh_y(m_n_cells, m_exec_conf);
    m_inv_fourier_mesh_y.swap(inv_fourier_mesh_y);

    GPUArray<cufftComplex> inv_fourier_mesh_z(m_n_cells, m_exec_conf);
    m_inv_fourier_mesh_z.swap(inv_fourier_mesh_z);

    GPUArray<Scalar4> particle_bins(m_bin_idx.getNumElements(), m_exec_conf);
    m_particle_bins.swap(particle_bins);

    GPUArray<unsigned int> n_cell(m_bin_idx.getW(), m_exec_conf);
    m_n_cell.swap(n_cell);

    GPUFlags<unsigned int> cell_overflowed(m_exec_conf);
    m_cell_overflowed.swap(cell_overflowed);

    m_cell_overflowed.resetFlags(0);

    // allocate scratch space for density reduction
    GPUArray<Scalar> mesh_scratch(m_scratch_idx.getNumElements(), m_exec_conf);
    m_mesh_scratch.swap(mesh_scratch);

    unsigned int n_blocks = (m_mesh_points.x*m_mesh_points.y*m_mesh_points.z)/m_block_size+1;
    GPUArray<Scalar> sum_partial(n_blocks,m_exec_conf);
    m_sum_partial.swap(sum_partial);

    GPUArray<Scalar> sum_virial_partial(6*n_blocks,m_exec_conf);
    m_sum_virial_partial.swap(sum_virial_partial);

    GPUArray<Scalar> sum_virial(6,m_exec_conf);
    m_sum_virial.swap(sum_virial);

    GPUArray<Scalar4> max_partial(n_blocks, m_exec_conf);
    m_max_partial.swap(max_partial);
    }

void PPPMForceComputeGPU::setupCoeffs()
    {
    // call base-class method
    PPPMForceCompute::setupCoeffs();

    // initialize interpolation coefficients on GPU
    ArrayHandle<Scalar> h_rho_coeff(m_rho_coeff, access_location::host, access_mode::read);
    gpu_initialize_coeff(h_rho_coeff.data, m_order);
    }

//! Assignment of particles to mesh using three-point scheme (triangular shaped cloud)
/*! This is a second order accurate scheme with continuous value and continuous derivative
 */
void PPPMForceComputeGPU::assignParticles()
    {
    if (m_prof) m_prof->push(m_exec_conf, "assign");

    ArrayHandle<Scalar4> d_postype(m_pdata->getPositions(), access_location::device, access_mode::read);
    ArrayHandle<cufftComplex> d_mesh(m_mesh, access_location::device, access_mode::overwrite);
    ArrayHandle<Scalar> d_charge(m_pdata->getCharges(), access_location::device, access_mode::read);

    ArrayHandle<unsigned int> d_n_cell(m_n_cell, access_location::device, access_mode::overwrite);

    bool cont = true;
    while (cont)
        {
        cudaMemset(d_n_cell.data,0,sizeof(unsigned int)*m_n_cell.getNumElements());

            {
            ArrayHandle<Scalar4> d_particle_bins(m_particle_bins, access_location::device, access_mode::overwrite);

            // access the group
            ArrayHandle< unsigned int > d_index_array(m_group->getIndexArray(), access_location::device, access_mode::read);
            unsigned int group_size = m_group->getNumMembers();

            unsigned int block_size = m_tuner_bin->getParam();
            m_tuner_bin->begin();
            gpu_bin_particles(m_pdata->getN(),
                              d_postype.data,
                              d_particle_bins.data,
                              d_n_cell.data,
                              m_cell_overflowed.getDeviceFlags(),
                              m_bin_idx,
                              m_mesh_points,
                              m_n_ghost_cells,
                              d_charge.data,
                              m_pdata->getBox(),
                              m_order,
                              d_index_array.data,
                              group_size,
                              block_size);

            if (m_exec_conf->isCUDAErrorCheckingEnabled())
                CHECK_CUDA_ERROR();
            m_tuner_bin->end();
            }

        unsigned int flags = m_cell_overflowed.readFlags();

        if (flags)
            {
            // reallocate particle bins array
            m_cell_size = flags;

            m_bin_idx = Index2D(m_bin_idx.getW(),m_cell_size);
            GPUArray<Scalar4> particle_bins(m_bin_idx.getNumElements(),m_exec_conf);
            m_particle_bins.swap(particle_bins);
            m_cell_overflowed.resetFlags(0);
            }
        else
            {
            cont = false;
            }

        // assign particles to mesh
        ArrayHandle<Scalar4> d_particle_bins(m_particle_bins, access_location::device, access_mode::read);
        ArrayHandle<Scalar> d_mesh_scratch(m_mesh_scratch, access_location::device, access_mode::overwrite);

        unsigned int block_size = m_tuner_assign->getParam();
        m_tuner_assign->begin();
        gpu_assign_binned_particles_to_mesh(m_mesh_points,
                                            m_n_ghost_cells,
                                            m_grid_dim,
                                            d_particle_bins.data,
                                            d_mesh_scratch.data,
                                            m_bin_idx,
                                            m_scratch_idx,
                                            d_n_cell.data,
                                            d_mesh.data,
                                            m_order,
                                            m_pdata->getBox(),
                                            block_size,
                                            m_exec_conf->dev_prop);
        m_tuner_assign->end();

        if (m_exec_conf->isCUDAErrorCheckingEnabled())
            CHECK_CUDA_ERROR();
        }

    if (m_prof) m_prof->pop(m_exec_conf);
    }

void PPPMForceComputeGPU::updateMeshes()
    {
    if (m_local_fft)
        {
        if (m_prof) m_prof->push(m_exec_conf,"FFT");
        // locally transform the particle mesh
        ArrayHandle<cufftComplex> d_mesh(m_mesh, access_location::device, access_mode::read);
        ArrayHandle<cufftComplex> d_fourier_mesh(m_fourier_mesh, access_location::device, access_mode::overwrite);

        cufftExecC2C(m_cufft_plan, d_mesh.data, d_fourier_mesh.data, CUFFT_FORWARD);
        if (m_prof) m_prof->pop(m_exec_conf);
        }
    #ifdef ENABLE_MPI
    else
        {
        // update inner cells of particle mesh
        if (m_prof) m_prof->push(m_exec_conf,"ghost cell update");
        m_exec_conf->msg->notice(8) << "charge.pppm: Ghost cell update" << std::endl;
        m_gpu_grid_comm_forward->communicate(m_mesh);
        if (m_prof) m_prof->pop(m_exec_conf);

        // perform a distributed FFT
        m_exec_conf->msg->notice(8) << "charge.pppm: Distributed FFT mesh" << std::endl;
        if (m_prof) m_prof->push(m_exec_conf,"FFT");
        #ifndef USE_HOST_DFFT
        ArrayHandle<cufftComplex> d_mesh(m_mesh, access_location::device, access_mode::read);
        ArrayHandle<cufftComplex> d_fourier_mesh(m_fourier_mesh, access_location::device, access_mode::overwrite);

        if (m_exec_conf->isCUDAErrorCheckingEnabled())
            dfft_cuda_check_errors(&m_dfft_plan_forward, 1);
        else
            dfft_cuda_check_errors(&m_dfft_plan_forward, 0);

        dfft_cuda_execute(d_mesh.data+m_ghost_offset, d_fourier_mesh.data, 0, &m_dfft_plan_forward);
        #else
        ArrayHandle<cufftComplex> h_mesh(m_mesh, access_location::host, access_mode::read);
        ArrayHandle<cufftComplex> h_fourier_mesh(m_fourier_mesh, access_location::host, access_mode::overwrite);

        dfft_execute((cpx_t *)(h_mesh.data+m_ghost_offset), (cpx_t *)h_fourier_mesh.data, 0,m_dfft_plan_forward);
        #endif
        if (m_prof) m_prof->pop(m_exec_conf);
        }
    #endif

    if (m_prof) m_prof->push(m_exec_conf,"update");

        {
        ArrayHandle<cufftComplex> d_fourier_mesh(m_fourier_mesh, access_location::device, access_mode::readwrite);
        ArrayHandle<cufftComplex> d_fourier_mesh_G_x(m_fourier_mesh_G_x, access_location::device, access_mode::overwrite);
        ArrayHandle<cufftComplex> d_fourier_mesh_G_y(m_fourier_mesh_G_y, access_location::device, access_mode::overwrite);
        ArrayHandle<cufftComplex> d_fourier_mesh_G_z(m_fourier_mesh_G_z, access_location::device, access_mode::overwrite);

        ArrayHandle<Scalar> d_inf_f(m_inf_f, access_location::device, access_mode::read);
        ArrayHandle<Scalar3> d_k(m_k, access_location::device, access_mode::read);

        unsigned int block_size = m_tuner_update->getParam();
        m_tuner_update->begin();
        gpu_update_meshes(m_n_inner_cells,
                          d_fourier_mesh.data,
                          d_fourier_mesh_G_x.data,
                          d_fourier_mesh_G_y.data,
                          d_fourier_mesh_G_z.data,
                          d_inf_f.data,
                          d_k.data,
                          m_global_dim.x*m_global_dim.y*m_global_dim.z,
                          block_size);

        if (m_exec_conf->isCUDAErrorCheckingEnabled())
            CHECK_CUDA_ERROR();
        m_tuner_update->end();
        }

    if (m_prof) m_prof->pop(m_exec_conf);

    if (m_local_fft)
        {
        if (m_prof) m_prof->push(m_exec_conf, "FFT");

        // do local inverse transform of all three components of the force mesh
        ArrayHandle<cufftComplex> d_fourier_mesh_G_x(m_fourier_mesh_G_x, access_location::device, access_mode::read);
        ArrayHandle<cufftComplex> d_fourier_mesh_G_y(m_fourier_mesh_G_y, access_location::device, access_mode::read);
        ArrayHandle<cufftComplex> d_fourier_mesh_G_z(m_fourier_mesh_G_z, access_location::device, access_mode::read);
        ArrayHandle<cufftComplex> d_inv_fourier_mesh_x(m_inv_fourier_mesh_x, access_location::device, access_mode::overwrite);
        ArrayHandle<cufftComplex> d_inv_fourier_mesh_y(m_inv_fourier_mesh_y, access_location::device, access_mode::overwrite);
        ArrayHandle<cufftComplex> d_inv_fourier_mesh_z(m_inv_fourier_mesh_z, access_location::device, access_mode::overwrite);

        cufftExecC2C(m_cufft_plan,
                     d_fourier_mesh_G_x.data,
                     d_inv_fourier_mesh_x.data,
                     CUFFT_INVERSE);
        cufftExecC2C(m_cufft_plan,
                     d_fourier_mesh_G_y.data,
                     d_inv_fourier_mesh_y.data,
                     CUFFT_INVERSE);
        cufftExecC2C(m_cufft_plan,
                     d_fourier_mesh_G_z.data,
                     d_inv_fourier_mesh_z.data,
                     CUFFT_INVERSE);
        if (m_prof) m_prof->pop(m_exec_conf);
        }
    #ifdef ENABLE_MPI
    else
        {
        if (m_prof) m_prof->push(m_exec_conf, "FFT");

        // Distributed inverse transform of force mesh
        m_exec_conf->msg->notice(8) << "charge.pppm: Distributed iFFT" << std::endl;
        #ifndef USE_HOST_DFFT
        ArrayHandle<cufftComplex> d_fourier_mesh_G_x(m_fourier_mesh_G_x, access_location::device, access_mode::read);
        ArrayHandle<cufftComplex> d_fourier_mesh_G_y(m_fourier_mesh_G_y, access_location::device, access_mode::read);
        ArrayHandle<cufftComplex> d_fourier_mesh_G_z(m_fourier_mesh_G_z, access_location::device, access_mode::read);
        ArrayHandle<cufftComplex> d_inv_fourier_mesh_x(m_inv_fourier_mesh_x, access_location::device, access_mode::overwrite);
        ArrayHandle<cufftComplex> d_inv_fourier_mesh_y(m_inv_fourier_mesh_y, access_location::device, access_mode::overwrite);
        ArrayHandle<cufftComplex> d_inv_fourier_mesh_z(m_inv_fourier_mesh_z, access_location::device, access_mode::overwrite);

        if (m_exec_conf->isCUDAErrorCheckingEnabled())
            dfft_cuda_check_errors(&m_dfft_plan_inverse, 1);
        else
            dfft_cuda_check_errors(&m_dfft_plan_inverse, 0);

        dfft_cuda_execute(d_fourier_mesh_G_x.data, d_inv_fourier_mesh_x.data+m_ghost_offset, 1, &m_dfft_plan_inverse);
        dfft_cuda_execute(d_fourier_mesh_G_y.data, d_inv_fourier_mesh_y.data+m_ghost_offset, 1, &m_dfft_plan_inverse);
        dfft_cuda_execute(d_fourier_mesh_G_z.data, d_inv_fourier_mesh_z.data+m_ghost_offset, 1, &m_dfft_plan_inverse);
        #else
        ArrayHandle<cufftComplex> h_fourier_mesh_G_x(m_fourier_mesh_G_x, access_location::host, access_mode::read);
        ArrayHandle<cufftComplex> h_fourier_mesh_G_y(m_fourier_mesh_G_y, access_location::host, access_mode::read);
        ArrayHandle<cufftComplex> h_fourier_mesh_G_z(m_fourier_mesh_G_z, access_location::host, access_mode::read);
        ArrayHandle<cufftComplex> h_inv_fourier_mesh_x(m_inv_fourier_mesh_x, access_location::host, access_mode::overwrite);
        ArrayHandle<cufftComplex> h_inv_fourier_mesh_y(m_inv_fourier_mesh_y, access_location::host, access_mode::overwrite);
        ArrayHandle<cufftComplex> h_inv_fourier_mesh_z(m_inv_fourier_mesh_z, access_location::host, access_mode::overwrite);
        dfft_execute((cpx_t *)h_fourier_mesh_G_x.data, (cpx_t *)h_inv_fourier_mesh_x.data+m_ghost_offset, 1, m_dfft_plan_inverse);
        dfft_execute((cpx_t *)h_fourier_mesh_G_y.data, (cpx_t *)h_inv_fourier_mesh_y.data+m_ghost_offset, 1, m_dfft_plan_inverse);
        dfft_execute((cpx_t *)h_fourier_mesh_G_z.data, (cpx_t *)h_inv_fourier_mesh_z.data+m_ghost_offset, 1, m_dfft_plan_inverse);
        #endif
        if (m_prof) m_prof->pop(m_exec_conf);
        }
    #endif

    #ifdef ENABLE_MPI
    if (! m_local_fft)
        {
        // update outer cells of inverse Fourier meshes using ghost cells from neighboring processors
        if (m_prof) m_prof->push("ghost cell update");
        m_exec_conf->msg->notice(8) << "charge.pppm: Ghost cell update" << std::endl;
        m_gpu_grid_comm_reverse->communicate(m_inv_fourier_mesh_x);
        m_gpu_grid_comm_reverse->communicate(m_inv_fourier_mesh_y);
        m_gpu_grid_comm_reverse->communicate(m_inv_fourier_mesh_z);
        if (m_prof) m_prof->pop();
        }
    #endif
    }

void PPPMForceComputeGPU::interpolateForces()
    {
    if (m_prof) m_prof->push(m_exec_conf,"forces");

    ArrayHandle<Scalar4> d_postype(m_pdata->getPositions(), access_location::device, access_mode::read);
    ArrayHandle<cufftComplex> d_inv_fourier_mesh_x(m_inv_fourier_mesh_x, access_location::device, access_mode::read);
    ArrayHandle<cufftComplex> d_inv_fourier_mesh_y(m_inv_fourier_mesh_y, access_location::device, access_mode::read);
    ArrayHandle<cufftComplex> d_inv_fourier_mesh_z(m_inv_fourier_mesh_z, access_location::device, access_mode::read);
    ArrayHandle<Scalar> d_charge(m_pdata->getCharges(), access_location::device, access_mode::read);

    ArrayHandle<Scalar4> d_force(m_force, access_location::device, access_mode::overwrite);

    // access the group
    ArrayHandle< unsigned int > d_index_array(m_group->getIndexArray(), access_location::device, access_mode::read);
    unsigned int group_size = m_group->getNumMembers();

    unsigned int block_size = m_tuner_force->getParam();
    m_tuner_force->begin();
    gpu_compute_forces(m_pdata->getN(),
                       d_postype.data,
                       d_force.data,
                       d_inv_fourier_mesh_x.data,
                       d_inv_fourier_mesh_y.data,
                       d_inv_fourier_mesh_z.data,
                       m_grid_dim,
                       m_n_ghost_cells,
                       d_charge.data,
                       m_pdata->getBox(),
                       m_order,
                       d_index_array.data,
                       group_size,
                       block_size);

    if (m_exec_conf->isCUDAErrorCheckingEnabled())
        CHECK_CUDA_ERROR();
    m_tuner_force->end();
    }

void PPPMForceComputeGPU::computeVirial()
    {
    if (m_prof) m_prof->push(m_exec_conf,"virial");

    ArrayHandle<cufftComplex> d_fourier_mesh(m_fourier_mesh, access_location::device, access_mode::read);
    ArrayHandle<Scalar> d_inf_f(m_inf_f, access_location::device, access_mode::read);
    ArrayHandle<Scalar3> d_k(m_k, access_location::device, access_mode::read);
    ArrayHandle<Scalar> d_virial_mesh(m_virial_mesh, access_location::device, access_mode::overwrite);

    bool exclude_dc = true;
    #ifdef ENABLE_MPI
    if (m_pdata->getDomainDecomposition())
        {
        uint3 my_pos = m_pdata->getDomainDecomposition()->getGridPos();
        exclude_dc = !my_pos.x && !my_pos.y && !my_pos.z;
        }
    #endif

    gpu_compute_mesh_virial(m_n_inner_cells,
                            d_fourier_mesh.data,
                            d_inf_f.data,
                            d_virial_mesh.data,
                            d_k.data,
                            exclude_dc,
                            m_kappa);

    if (m_exec_conf->isCUDAErrorCheckingEnabled())
        CHECK_CUDA_ERROR();

        {
        ArrayHandle<Scalar> d_sum_virial(m_sum_virial, access_location::device, access_mode::overwrite);
        ArrayHandle<Scalar> d_sum_virial_partial(m_sum_virial_partial, access_location::device, access_mode::overwrite);

        gpu_compute_virial(m_n_inner_cells,
                           d_sum_virial_partial.data,
                           d_sum_virial.data,
                           d_virial_mesh.data,
                           m_block_size);

        if (m_exec_conf->isCUDAErrorCheckingEnabled())
            CHECK_CUDA_ERROR();
        }

    ArrayHandle<Scalar> h_sum_virial(m_sum_virial, access_location::host, access_mode::read);

    Scalar V = m_pdata->getGlobalBox().getVolume();
    Scalar scale = Scalar(1.0)/((Scalar)(m_global_dim.x*m_global_dim.y*m_global_dim.z));

    for (unsigned int i = 0; i<6; ++i)
        m_external_virial[i] = Scalar(0.5)*V*scale*scale*h_sum_virial.data[i];

    if (m_prof) m_prof->pop(m_exec_conf);
    }

Scalar PPPMForceComputeGPU::computePE()
    {
    if (m_prof) m_prof->push(m_exec_conf,"sum");

    ArrayHandle<cufftComplex> d_fourier_mesh(m_fourier_mesh, access_location::device, access_mode::read);
    ArrayHandle<Scalar> d_inf_f(m_inf_f, access_location::device, access_mode::read);

    ArrayHandle<Scalar> d_sum_partial(m_sum_partial, access_location::device, access_mode::overwrite);

    bool exclude_dc = true;
    #ifdef ENABLE_MPI
    if (m_pdata->getDomainDecomposition())
        {
        uint3 my_pos = m_pdata->getDomainDecomposition()->getGridPos();
        exclude_dc = !my_pos.x && !my_pos.y && !my_pos.z;
        }
    #endif

    gpu_compute_pe(m_n_inner_cells,
                   d_sum_partial.data,
                   m_sum.getDeviceFlags(),
                   d_fourier_mesh.data,
                   d_inf_f.data,
                   m_block_size,
                   m_mesh_points,
                   exclude_dc);

    if (m_exec_conf->isCUDAErrorCheckingEnabled())
        CHECK_CUDA_ERROR();

    Scalar sum = m_sum.readFlags();

    Scalar V = m_pdata->getGlobalBox().getVolume();
    Scalar scale = Scalar(1.0)/((Scalar)(m_global_dim.x*m_global_dim.y*m_global_dim.z));
    sum *= Scalar(0.5)*V*scale*scale;

    if (m_exec_conf->getRank()==0)
        {
        // add correction on rank 0
        sum -= m_q2 * m_kappa / Scalar(1.772453850905516027298168);
        sum -= Scalar(0.5*M_PI)*m_q*m_q / (m_kappa*m_kappa* V);
        }

    // store this rank's contribution as external potential energy
    m_external_energy = sum;

    #ifdef ENABLE_MPI
    if (m_pdata->getDomainDecomposition())
        {
        // reduce sum
        MPI_Allreduce(MPI_IN_PLACE,
                      &sum,
                      1,
                      MPI_HOOMD_SCALAR,
                      MPI_SUM,
                      m_exec_conf->getMPICommunicator());
        }
    #endif

    if (m_prof) m_prof->pop(m_exec_conf);

    return sum;
    }

//! Compute the optimal influence function
void PPPMForceComputeGPU::computeInfluenceFunction()
    {
    if (m_prof) m_prof->push(m_exec_conf, "influence function");

    ArrayHandle<Scalar> d_inf_f(m_inf_f, access_location::device, access_mode::overwrite);
    ArrayHandle<Scalar3> d_k(m_k, access_location::device, access_mode::overwrite);

    uint3 global_dim = m_mesh_points;
    uint3 pidx = make_uint3(0,0,0);
    uint3 pdim = make_uint3(0,0,0);
    #ifdef ENABLE_MPI
    if (m_pdata->getDomainDecomposition())
        {
        const Index3D &didx = m_pdata->getDomainDecomposition()->getDomainIndexer();
        global_dim.x *= didx.getW();
        global_dim.y *= didx.getH();
        global_dim.z *= didx.getD();
        pidx = m_pdata->getDomainDecomposition()->getGridPos();
        pdim = make_uint3(didx.getW(), didx.getH(), didx.getD());
        }
    #endif

    ArrayHandle<Scalar> d_gf_b(m_gf_b, access_location::device, access_mode::read);

    unsigned int block_size = m_tuner_influence->getParam();
    m_tuner_influence->begin();
    gpu_compute_influence_function(m_mesh_points,
                                   global_dim,
                                   d_inf_f.data,
                                   d_k.data,
                                   m_pdata->getGlobalBox(),
                                   m_local_fft,
                                   pidx,
                                   pdim,
                                   EPS_HOC,
                                   m_kappa,
                                   d_gf_b.data,
                                   m_order,
                                   block_size);

    if (m_exec_conf->isCUDAErrorCheckingEnabled())
        CHECK_CUDA_ERROR();

    m_tuner_influence->end();

    if (m_prof) m_prof->pop(m_exec_conf);
    }

void PPPMForceComputeGPU::fixExclusions()
    {
    ArrayHandle<unsigned int> d_exlist(m_nlist->getExListArray(), access_location::device, access_mode::read);
    ArrayHandle<unsigned int> d_n_ex(m_nlist->getNExArray(), access_location::device, access_mode::read);
    ArrayHandle<Scalar4> d_force(m_force, access_location::device, access_mode::overwrite);
    ArrayHandle<Scalar> d_virial(m_virial, access_location::device, access_mode::overwrite);
    ArrayHandle< unsigned int > d_index_array(m_group->getIndexArray(), access_location::device, access_mode::read);
    unsigned int group_size = m_group->getNumMembers();

    ArrayHandle<Scalar4> d_postype(m_pdata->getPositions(), access_location::device, access_mode::read);
    ArrayHandle<Scalar> d_charge(m_pdata->getCharges(), access_location::device, access_mode::read);

    Index2D nex = m_nlist->getExListIndexer();

    gpu_fix_exclusions(d_force.data,
                   d_virial.data,
                   m_virial.getPitch(),
                   m_pdata->getN(),
                   d_postype.data,
                   d_charge.data,
                   m_pdata->getBox(),
                   d_n_ex.data,
                   d_exlist.data,
                   nex,
                   m_kappa,
                   d_index_array.data,
                   group_size,
                   m_block_size,
                   m_exec_conf->getComputeCapability());

    if(m_exec_conf->isCUDAErrorCheckingEnabled())
        CHECK_CUDA_ERROR();
    }

void export_PPPMForceComputeGPU()
    {
    class_<PPPMForceComputeGPU, boost::shared_ptr<PPPMForceComputeGPU>, bases<PPPMForceCompute>, boost::noncopyable >
        ("PPPMForceComputeGPU", init< boost::shared_ptr<SystemDefinition>,
                                      boost::shared_ptr<NeighborList>,
                                      boost::shared_ptr<ParticleGroup> >());
    }

#endif // ENABLE_CUDA
