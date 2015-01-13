// Copyright (c) 2010, Lawrence Livermore National Security, LLC. Produced at
// the Lawrence Livermore National Laboratory. LLNL-CODE-443211. All Rights
// reserved. See file COPYRIGHT for details.
//
// This file is part of the MFEM library. For more information and source code
// availability see http://mfem.googlecode.com.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the GNU Lesser General Public License (as published by the Free
// Software Foundation) version 2.1 dated February 1999.

#ifndef MFEM_MESH_HEADERS
#define MFEM_MESH_HEADERS

// Mesh header file

#include "../general/array.hpp"
#include "../general/table.hpp"
#include "../general/stable3d.hpp"
#include "../linalg/linalg.hpp"
#include "../fem/intrules.hpp"
#include "../fem/geom.hpp"
#include "../fem/fe.hpp"
#include "../fem/eltrans.hpp"
#include "../fem/coefficient.hpp"

#include "vertex.hpp"
#include "element.hpp"
#include "point.hpp"
#include "segment.hpp"
#include "triangle.hpp"
#include "quadrilateral.hpp"
#include "hexahedron.hpp"
#include "tetrahedron.hpp"
#include "ncmesh.hpp"
#include "mesh.hpp"

#ifdef MFEM_USE_MPI
#include <mpi.h>
#include "../general/sets.hpp"
#include "../general/communication.hpp"
#include "pmesh.hpp"
#endif

#include "nurbs.hpp"

#endif
