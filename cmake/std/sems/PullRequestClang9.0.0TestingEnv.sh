
# This script can be used to load the appropriate environment for the
# Clang 9.0.0 Pull Request testing build on a Linux machine that has access to
# the SEMS NFS mount.

# usage: $ source PullRequestClang9.0.0TestingEnv.sh

# After the environment is no longer needed, it can be purged using
# $ module purge
# or Trilinos/cmake/unload_sems_dev_env.sh

module purge

source /projects/sems/modulefiles/utils/sems-modules-init.sh

module load sems-git/2.10.1
module load sems-gcc/5.3.0
module load sems-clang/9.0.0
module load sems-openmpi/1.10.1
module load sems-boost/1.63.0/base
module load sems-zlib/1.2.8/base
module load sems-hdf5/1.10.6/parallel
module load sems-netcdf/4.7.3/parallel
module load sems-parmetis/4.0.3/parallel
module load sems-scotch/6.0.3/nopthread_64bit_parallel
module load sems-superlu/4.3/base

# Load the SEMS CMake Module
# - One of the SEMS modules will load CMake 3.4.x also,
#   so this will pull in the SEMS cmake 3.10.3 version
#   for Trilinos compatibility.
module load sems-cmake/3.10.3

# Using CMake and Ninja modules from the ATDM project space.
# SEMS does not yet supply a recent enough version of CMake
# for the single configure/build/test capability. We are also
# using a custom version of Ninja (with Fortran support not
# available in main-line Ninja) to significantly speed up
# compile and link times.
module load atdm-env
#module load atdm-cmake/3.10.1
module load atdm-ninja_fortran/1.7.2

module unload sems-python
module load sems-python/3.5.2


# add the OpenMP environment variable we need
export OMP_NUM_THREADS=2
