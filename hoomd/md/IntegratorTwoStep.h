// Copyright (c) 2009-2021 The Regents of the University of Michigan
// This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.

#include "hoomd/Integrator.h"
#include "IntegrationMethodTwoStep.h"

#include "ForceComposite.h"

#pragma once

#ifdef __HIPCC__
#error This header cannot be compiled by nvcc
#endif

#include <pybind11/pybind11.h>

/// Integrates the system forward one step with possibly multiple methods
/** See IntegrationMethodTwoStep for most of the design notes regarding group integration. IntegratorTwoStep merely
    implements most of the things discussed there.

    Notable design elements:
    - setDeltaT results in deltaT being set on all current integration methods
    - to ensure that new methods also get set, addIntegrationMethod() also calls setDeltaT on the method
    - to interface with the python script, a removeAllIntegrationMethods() method is provided to clear the list so they
      can be cleared and re-added from hoomd's internal list

    To ensure that the user does not make a mistake and specify more than one method operating on a single particle,
    the particle groups are checked for intersections whenever a new method is added in addIntegrationMethod()

    There is a special registration mechanism for ForceComposites which run after the integration steps
    one and two, and which can use the updated particle positions and velocities to update any slaved degrees
    of freedom (rigid bodies).

    \ingroup updaters
*/
class PYBIND11_EXPORT IntegratorTwoStep : public Integrator
    {
    public:
        /** Anisotropic integration mode: Automatic (detect whether
            aniso forces are defined), Anisotropic (integrate
            rotational degrees of freedom regardless of whether
            anything is defining them), and Isotropic (don't integrate
            rotational degrees of freedom)
        */
        enum AnisotropicMode {Automatic, Anisotropic, Isotropic};

        /// Constructor
        IntegratorTwoStep(std::shared_ptr<SystemDefinition> sysdef, Scalar deltaT);

        /// Destructor
        virtual ~IntegratorTwoStep();

        /// Sets the profiler for the compute to use
        virtual void setProfiler(std::shared_ptr<Profiler> prof);

        /// Returns a list of log quantities this integrator calculates
        virtual std::vector< std::string > getProvidedLogQuantities();

        /// Returns logged values
        virtual Scalar getLogValue(const std::string& quantity, uint64_t timestep);

        /// Take one timestep forward
        virtual void update(uint64_t timestep);

        /// Change the timestep
        virtual void setDeltaT(Scalar deltaT);

        virtual void removeForceComputes();

        /// Add a new integration method to the list that will be run
        virtual void addIntegrationMethod(std::shared_ptr<IntegrationMethodTwoStep> new_method);

        /// Get the list of integration methods
        std::vector< std::shared_ptr<IntegrationMethodTwoStep> >& getIntegrationMethods()
            {
            return m_methods;
            }

        /// Remove all integration methods
        virtual void removeAllIntegrationMethods();

        /// Get the number of degrees of freedom granted to a given group
        virtual Scalar getTranslationalDOF(std::shared_ptr<ParticleGroup> group);

        /// Get the number of degrees of freedom granted to a given group
        virtual Scalar getRotationalDOF(std::shared_ptr<ParticleGroup> group);

        /// Set the anisotropic mode of the integrator
        virtual void setAnisotropicMode(const std::string& mode);

        /// Set the anisotropic mode of the integrator
        virtual const std::string getAnisotropicMode();

        /// Prepare for the run
        virtual void prepRun(uint64_t timestep);

        /// Get needed pdata flags
        virtual PDataFlags getRequestedPDataFlags();

        /// Count the total number of degrees of freedom removed by all constraint forces
        virtual Scalar getNDOFRemoved(std::shared_ptr<ParticleGroup> query);

        /// helper function to compute net force/virial
        virtual void computeNetForce(uint64_t timestep);

#ifdef ENABLE_HIP
        /// helper function to compute net force/virial on the GPU
        virtual void computeNetForceGPU(uint64_t timestep);
#endif

#ifdef ENABLE_MPI
        /// Set the communicator to use
        /** \param comm The Communicator
         */
        virtual void setCommunicator(std::shared_ptr<Communicator> comm);

        /// helper function to determine the ghost communication flags
        virtual CommFlags determineFlags(uint64_t timestep);
#endif

        /// Check if any forces introduce anisotropic degrees of freedom
        virtual bool getAnisotropic();

        /// Updates the rigid body constituent particles
        virtual void updateRigidBodies(uint64_t timestep);

        /// Set autotuner parameters
        virtual void setAutotunerParams(bool enable, unsigned int period);

        /// (Re-)initialize the integration method
        void initializeIntegrationMethods();

        /// Getter and setter for accessing rigid body objects in Python
        std::shared_ptr<ForceComposite> getRigid()
            {
            return m_rigid_bodies;
            }

        void setRigid(std::shared_ptr<ForceComposite> new_rigid)
            {
            m_rigid_bodies = new_rigid;
            }

    protected:
        /// Helper method to test if all added methods have valid restart information
        bool isValidRestart();

        std::vector< std::shared_ptr<IntegrationMethodTwoStep> > m_methods;   //!< List of all the integration methods

        std::shared_ptr<ForceComposite> m_rigid_bodies; /// definition and updater for rigid bodies

        bool m_prepared;              //!< True if preprun has been called
        bool m_gave_warning;          //!< True if a warning has been given about no methods added
        AnisotropicMode m_aniso_mode; //!< Anisotropic mode for this integrator
    };

/// Exports the IntegratorTwoStep class to python
void export_IntegratorTwoStep(pybind11::module& m);
