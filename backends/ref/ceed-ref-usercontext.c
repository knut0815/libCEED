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

#include "ceed-ref.h"

//------------------------------------------------------------------------------
// UserContext Set Data
//------------------------------------------------------------------------------
static int CeedUserContextSetData_Ref(CeedUserContext ctx, CeedMemType mtype,
                                      CeedCopyMode cmode, CeedScalar *data) {
  int ierr;
  CeedUserContext_Ref *impl;
  ierr = CeedUserContextGetBackendData(ctx, (void *)&impl); CeedChk(ierr);
  size_t ctxsize;
  ierr = CeedUserContextGetContextSize(ctx, &ctxsize); CeedChk(ierr);
  Ceed ceed;
  ierr = CeedUserContextGetCeed(ctx, &ceed); CeedChk(ierr);

  if (mtype != CEED_MEM_HOST)
    // LCOV_EXCL_START
    return CeedError(ceed, 1, "Only MemType = HOST supported");
  // LCOV_EXCL_STOP
  ierr = CeedFree(&impl->data_allocated); CeedChk(ierr);
  switch (cmode) {
  case CEED_COPY_VALUES:
    ierr = CeedMallocArray(1, ctxsize, &impl->data_allocated); CeedChk(ierr);
    impl->data = impl->data_allocated;
    memcpy(impl->data, data, ctxsize);
    break;
  case CEED_OWN_POINTER:
    impl->data_allocated = data;
    impl->data = data;
    break;
  case CEED_USE_POINTER:
    impl->data = data;
  }
  return 0;
}

//------------------------------------------------------------------------------
// UserContext Get Data
//------------------------------------------------------------------------------
static int CeedUserContextGetData_Ref(CeedUserContext ctx, CeedMemType mtype,
                                      CeedScalar **data) {
  int ierr;
  CeedUserContext_Ref *impl;
  ierr = CeedUserContextGetBackendData(ctx, (void *)&impl); CeedChk(ierr);
  Ceed ceed;
  ierr = CeedUserContextGetCeed(ctx, &ceed); CeedChk(ierr);

  if (mtype != CEED_MEM_HOST)
    // LCOV_EXCL_START
    return CeedError(ceed, 1, "Can only provide to HOST memory");
  // LCOV_EXCL_STOP
  if (!impl->data)
    // LCOV_EXCL_START
    return CeedError(ceed, 1, "No context data set");
  // LCOV_EXCL_STOP
  *data = impl->data;
  return 0;
}

//------------------------------------------------------------------------------
// UserContext Restore Data
//------------------------------------------------------------------------------
static int CeedUserContextRestoreData_Ref(CeedUserContext ctx) {
  return 0;
}

//------------------------------------------------------------------------------
// UserContext Destroy
//------------------------------------------------------------------------------
static int CeedUserContextDestroy_Ref(CeedUserContext ctx) {
  int ierr;
  CeedUserContext_Ref *impl;
  ierr = CeedUserContextGetBackendData(ctx, (void *)&impl); CeedChk(ierr);

  ierr = CeedFree(&impl->data_allocated); CeedChk(ierr);
  ierr = CeedFree(&impl); CeedChk(ierr);
  return 0;
}

//------------------------------------------------------------------------------
// UserContext Create
//------------------------------------------------------------------------------
int CeedUserContextCreate_Ref(CeedUserContext ctx) {
  int ierr;
  CeedUserContext_Ref *impl;
  Ceed ceed;
  ierr = CeedUserContextGetCeed(ctx, &ceed); CeedChk(ierr);

  ierr = CeedSetBackendFunction(ceed, "UserContext", ctx, "SetData",
                                CeedUserContextSetData_Ref); CeedChk(ierr);
  ierr = CeedSetBackendFunction(ceed, "UserContext", ctx, "GetData",
                                CeedUserContextGetData_Ref); CeedChk(ierr);
  ierr = CeedSetBackendFunction(ceed, "UserContext", ctx, "RestoreData",
                                CeedUserContextRestoreData_Ref); CeedChk(ierr);
  ierr = CeedSetBackendFunction(ceed, "UserContext", ctx, "Destroy",
                                CeedUserContextDestroy_Ref); CeedChk(ierr);
  ierr = CeedCalloc(1, &impl); CeedChk(ierr);
  ierr = CeedUserContextSetBackendData(ctx, (void *)&impl); CeedChk(ierr);
  return 0;
}
//------------------------------------------------------------------------------
