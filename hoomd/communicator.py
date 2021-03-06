# Copyright (c) 2009-2021 The Regents of the University of Michigan
# This file is part of the HOOMD-blue project, released under the BSD 3-Clause
# License.

"""MPI communicator."""

from hoomd import _hoomd
import hoomd

import contextlib


class Communicator(object):
    """MPI communicator.

    Args:
        mpi_comm: Accepts an mpi4py communicator. Use this argument to perform
          many independent hoomd simulations where you communicate between those
          simulations using your own mpi4py code.
        nrank (int): (MPI) Number of ranks to include in a partition
    """

    def __init__(self, mpi_comm=None, nrank=None):

        # check nrank
        if nrank is not None:
            if not hoomd.version.mpi_enabled:
                raise RuntimeError(
                    "The nrank option is only available in MPI builds.\n")

        mpi_available = hoomd.version.mpi_enabled

        self.cpp_mpi_conf = None

        # create the specified configuration
        if mpi_comm is None:
            self.cpp_mpi_conf = _hoomd.MPIConfiguration()
        else:
            if not mpi_available:
                raise RuntimeError("mpi_comm is not supported in serial builds")

            handled = False

            # pass in pointer to MPI_Comm object provided by mpi4py
            try:
                import mpi4py
                if isinstance(mpi_comm, mpi4py.MPI.Comm):
                    addr = mpi4py.MPI._addressof(mpi_comm)
                    self.cpp_mpi_conf = \
                        _hoomd.MPIConfiguration._make_mpi_conf_mpi_comm(addr)
                    handled = True
            except ImportError:
                # silently ignore when mpi4py is missing
                pass

            # undocumented case: handle plain integers as pointers to MPI_Comm
            # objects
            if not handled and isinstance(mpi_comm, int):
                self.cpp_mpi_conf = \
                    _hoomd.MPIConfiguration._make_mpi_conf_mpi_comm(mpi_comm)
                handled = True

            if not handled:
                raise RuntimeError(
                    "Invalid mpi_comm object: {}".format(mpi_comm))

        if nrank is not None:
            # check validity
            if (self.cpp_mpi_conf.getNRanksGlobal() % nrank):
                raise RuntimeError(
                    'Total number of ranks is not a multiple of --nrank')

            # split the communicator into partitions
            self.cpp_mpi_conf.splitPartitions(nrank)

    @property
    def num_ranks(self):
        """int: The number of ranks in this partition.

        Note:
            Returns 1 in non-mpi builds.
        """
        if hoomd.version.mpi_enabled:
            return self.cpp_mpi_conf.getNRanks()
        else:
            return 1

    @property
    def rank(self):
        """int: The current rank.

        Note:
            Always returns 0 in non-mpi builds.
        """
        if hoomd.version.mpi_enabled:
            return self.cpp_mpi_conf.getRank()
        else:
            return 0

    @property
    def partition(self):
        """int: The current partition.

        Note:
            Always returns 0 in non-mpi builds.
        """
        if hoomd.version.mpi_enabled:
            return self.cpp_mpi_conf.getPartition()
        else:
            return 0

    def barrier_all(self):
        """Perform a MPI barrier synchronization across all ranks.

        Note:
            Does nothing in non-MPI builds.
        """
        if hoomd.version.mpi_enabled:
            _hoomd.mpi_barrier_world()

    def barrier(self):
        """Perform a barrier synchronization across all ranks in the partition.

        Note:
            Does nothing in in non-MPI builds.
        """
        if hoomd.version.mpi_enabled:
            self.cpp_mpi_conf.barrier()

    @contextlib.contextmanager
    def localize_abort(self):
        """Localize MPI_Abort to this partition.

        HOOMD calls MPI_Abort to tear down all running MPI processes whenever
        there is an uncaught exception. By default, this will abort the entire
        MPI execution. When using partitions (``nrank is not None``), an
        uncaught exception on one partition will therefore abort all of them.

        Use the return value of :py:meth:`localize_abort()` as a context manager
        to tell HOOMD that all operations within the context will use only
        that MPI communicator so that an uncaught exception in one partition
        will only abort that partition and leave the others running.
        """
        global _current_communicator
        prev = _current_communicator

        _current_communicator = self
        yield None
        _current_communicator = prev


# store the "current" communicator to be used for MPI_Abort calls. This defaults
# to the world communicator, but users can opt in to a more specific
# communicator using the Device.localize_abort context manager
_current_communicator = Communicator()
