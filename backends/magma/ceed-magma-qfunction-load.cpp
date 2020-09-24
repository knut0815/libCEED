// Copyright (c) 2017-2018, Lawrence Livermore National Security, LLC.
// Produced at the Lawrence Livermore National Laboratory. LLNL-CODE-734707.
// All Rights reserved. See files LICENSE and NOTICE for details.
//
// This file is part of CEED, a collection of benchmarks, miniapps, software
// libraries and APIs for efficient high-order finite element and spectral
// element discretizations for exascale applications. For more information and
// source code availability see http://github.com/ceed.
//
// The CEED research is supported by the Exascale Computing Project 17-SC-20-SC,
// a collaborative effort of two U.S. Department of Energy organizations (Office
// of Science and the National Nuclear Security Administration) responsible for
// the planning and preparation of a capable exascale ecosystem, including
// software, applications, hardware, advanced system engineering and early
// testbed platforms, in support of the nation's exascale computing imperative.

#include <string.h>
#include <iostream>
#include <sstream>
#include "../cuda/ceed-cuda.h"
#include "ceed-magma.h"

static const char *qReadWrite = QUOTE(
template <int SIZE>
//------------------------------------------------------------------------------
// Read from quadrature points
//------------------------------------------------------------------------------
inline __device__ void readQuads(const CeedInt quad, const CeedInt nquads, const CeedScalar* d_u, CeedScalar* r_u) {
  for(CeedInt comp = 0; comp < SIZE; ++comp) {
    r_u[comp] = d_u[quad + nquads * comp];
  }
}

//------------------------------------------------------------------------------
// Write at quadrature points
//------------------------------------------------------------------------------
template <int SIZE>
inline __device__ void writeQuads(const CeedInt quad, const CeedInt nquads, const CeedScalar* r_v, CeedScalar* d_v) {
  for(CeedInt comp = 0; comp < SIZE; ++comp) {
    d_v[quad + nquads * comp] = r_v[comp];
  }
}
);

//------------------------------------------------------------------------------
// Build QFunction kernel
//------------------------------------------------------------------------------
extern "C" int CeedMagmaBuildQFunction(CeedQFunction qf) {
  CeedInt ierr;
  using std::ostringstream;
  using std::string;
  CeedQFunction_Magma *data;
  ierr = CeedQFunctionGetData(qf, (void **)&data); CeedChk(ierr);
  // QFunction is built
  if (!data->qFunctionSource)
    return 0;

  ceed_magma_printf("CeedMagmaBuildQFunction: (dim, Q) = (%d, %d) \n", data->dim, data->qe);

  // QFunction kernel generation
  CeedInt numinputfields, numoutputfields, size;
  ierr = CeedQFunctionGetNumArgs(qf, &numinputfields, &numoutputfields);
  CeedQFunctionField *qfinputfields, *qfoutputfields;
  ierr = CeedQFunctionGetFields(qf, &qfinputfields, &qfoutputfields);
  CeedChk(ierr);

  // Build strings for final kernel
  string qFunction(data->qFunctionSource);
  string qReadWriteS(qReadWrite);
  ostringstream code;
  string qFunctionName(data->qFunctionName);
  string qFunctionKernelName = "magma_qfunction_" + qFunctionName;

  // Defintions
  code << "\n#define CEED_QFUNCTION(name) inline __device__ int name\n";
  code << "\n#define CeedPragmaSIMD\n";
  code << "\n#define CEED_Q_VLA 1\n\n";
  code << "typedef struct { const CeedScalar* inputs[16]; CeedScalar* outputs[16]; } Fields_Magma;\n";
  code << qReadWriteS;
  code << qFunction;
  code << "extern \"C\" __global__ void "<< qFunctionKernelName <<"(void *ctx, CeedInt Q, Fields_Magma fields) {\n";

  code << "  const CeedInt Q1d = "<<data->qe<<";\n";
  code << "  const CeedInt dim = "<<data->dim<<";\n";
  code << "  const CeedInt Quads_per_block = "<<CeedIntPow(data->qe, data->dim)<<";\n";

  // Inputs
  for (CeedInt i = 0; i < numinputfields; i++) {
    code << "  // Input field "<<i<<"\n";
    ierr = CeedQFunctionFieldGetSize(qfinputfields[i], &size); CeedChk(ierr);
    code << "  const CeedInt size_in_"<<i<<" = "<<size<<";\n";
    //code << "  CeedScalar r_q"<<i<<"[size_in_"<<i<<"];\n";
    code << "  CeedScalar r_q"<<i<<"[Q1d]"<<"[size_in_"<<i<<"];\n";
  }

  // Outputs
  for (CeedInt i = 0; i < numoutputfields; i++) {
    code << "  // Output field "<<i<<"\n";
    ierr = CeedQFunctionFieldGetSize(qfoutputfields[i], &size); CeedChk(ierr);
    code << "  const CeedInt size_out_"<<i<<" = "<<size<<";\n";
    code << "  CeedScalar r_qq"<<i<<"[Q1d]"<<"[size_out_"<<i<<"];\n";
  }

  // read all quadrature points into registers
  code << "  for (CeedInt iq = 0; iq < Q1d; iq++) {\n";
  code << "    CeedInt q = (blockIdx.x * Quads_per_block) + (iq * blockDim.x) + threadIdx.x;\n";
  for (CeedInt i = 0; i < numinputfields; i++) {
    code << "    // Input field "<<i<<"\n";
    code << "    readQuads<size_in_"<<i<<">(q, Q, fields.inputs["<<i<<"], r_q"<<i<<"[iq]);\n";
  }
  code << "  }\n";

  // Loop over quadrature points
  code << "  for (CeedInt iq = 0; iq < Q1d; iq++) {\n";
  code << "    CeedInt q = (blockIdx.x * Quads_per_block) + (iq * blockDim.x) + threadIdx.x;\n";

  // Setup input/output arrays
  code << "    const CeedScalar* in["<<numinputfields<<"];\n";
  for (CeedInt i = 0; i < numinputfields; i++) {
    code << "    in["<<i<<"] = r_q"<<i<<"[iq];\n";
  }
  code << "    CeedScalar* out["<<numoutputfields<<"];\n";
  for (CeedInt i = 0; i < numoutputfields; i++) {
    code << "    out["<<i<<"] = r_qq"<<i<<"[iq];\n";
  }

  // QFunction
  code << "    // QFunction\n";
  code << "    "<<qFunctionName<<"(ctx, 1, in, out);\n";
  code << "  }\n";

  // Write outputs
  code << "  for (CeedInt iq = 0; iq < Q1d; iq++) {\n";
  code << "    CeedInt q = (blockIdx.x * Quads_per_block) + (iq * blockDim.x) + threadIdx.x;\n";
  for (CeedInt i = 0; i < numoutputfields; i++) {
    code << "// Output field "<<i<<"\n";
    code << "  writeQuads<size_out_"<<i<<">(q, Q, r_qq"<<i<<"[iq], fields.outputs["<<i<<"]);\n";
  }
  code << "  }\n";
  code << "}\n";

  // View kernel for debugging
  Ceed ceed;
  CeedQFunctionGetCeed(qf, &ceed);
  if(0) {
    printf("\n************************************************************************\n");
    printf("%s \n", code.str().c_str());
    printf("\n************************************************************************\n");
  }

  // Compile kernel
  ierr = magma_rtc_cuda(ceed, code.str().c_str(), &data->module, 0);
  CeedChk(ierr);
  ierr = CeedGetKernelCuda(ceed, data->module, (const char*)qFunctionKernelName.c_str(), &data->qFunction);
  CeedChk(ierr);

  // Cleanup
  ierr = CeedFree(&data->qFunctionSource); CeedChk(ierr);
  return 0;
}
//------------------------------------------------------------------------------
