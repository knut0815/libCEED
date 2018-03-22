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
#include "ceed-occa.h"
#include <sys/stat.h>

// *****************************************************************************
// * buildKernel
// *****************************************************************************
static int CeedQFunctionBuildKernel(CeedQFunction qf) {
  CeedQFunction_Occa *data=qf->data;
  const Ceed_Occa *ceed_data=qf->ceed->data;
  assert(ceed_data);
  const occaDevice dev = ceed_data->device;
  CeedDebug("\033[36m[CeedQFunction][BuildKernel] nc=%d",data->nc);
  CeedDebug("\033[36m[CeedQFunction][BuildKernel] dim=%d",data->dim);
  occaProperties pKR = occaCreateProperties();
  occaPropertiesSet(pKR, "defines/NC", occaInt(data->nc));
  occaPropertiesSet(pKR, "defines/DIM", occaInt(data->dim));
  occaPropertiesSet(pKR, "defines/TILE_SIZE", occaInt(TILE_SIZE));
  CeedDebug("\033[36m[CeedQFunction][BuildKernel] occaDeviceBuildKernel");
  CeedDebug("\033[36m[CeedQFunction][BuildKernel] oklPath=%s",data->oklPath);
  CeedDebug("\033[36m[CeedQFunction][BuildKernel] name=%s",data->qFunctionName);
  data->kQFunctionApply =
    occaDeviceBuildKernel(dev, data->oklPath, data->qFunctionName, pKR);
  occaPropertiesFree(pKR);
  return 0;
}

// *****************************************************************************
// * Fill function for t20 case, should be removed
// *****************************************************************************
__attribute__((unused))
static int CeedQFunctionFill20_Occa(occaMemory d_u,
                                    const CeedScalar *const *u,
                                    CeedInt size) {
  occaCopyPtrToMem(d_u,u[0],size,0,NO_PROPS);
  return 0;
}

// *****************************************************************************
// *  Fill function for the operator case
// *****************************************************************************
__attribute__((unused))
static int CeedQFunctionFillOp_Occa(occaMemory d_u,
                                    const CeedScalar *const *u,
                                    CeedInt *uoffset,
                                    const CeedEvalMode inmode,
                                    const CeedInt Q, const CeedInt nc,
                                    const CeedInt dim, const size_t bytes) {
  if (inmode & CEED_EVAL_INTERP){
    CeedDebug("\033[36m[CeedQFunction][FillOp] CEED_EVAL_INTERP");
    occaCopyPtrToMem(d_u, u[0],Q*nc*bytes,0,NO_PROPS);
  }
  if (inmode & CEED_EVAL_GRAD){
    CeedDebug("\033[36m[CeedQFunction][FillOp] CEED_EVAL_GRAD");
    occaCopyPtrToMem(d_u, u[1],Q*nc*dim*bytes,Q*nc*bytes,NO_PROPS);
  }
  if (inmode & CEED_EVAL_WEIGHT){
    CeedDebug("\033[36m[CeedQFunction][FillOp] CEED_EVAL_WEIGHT");
    occaCopyPtrToMem(d_u, u[4],Q*bytes,Q*nc*(dim+1)*bytes,NO_PROPS);
  }
  return 0;
}

// *****************************************************************************
// * Q-functions: Apply, Destroy & Create
// *****************************************************************************
static int CeedQFunctionApply_Occa(CeedQFunction qf, void *qdata, CeedInt Q,
                                   const CeedScalar *const *u,
                                   CeedScalar *const *v) {
  //CeedDebug("\033[36m[CeedQFunction][Apply]");
  int ierr;
  CeedQFunction_Occa *data = qf->data;
  const Ceed_Occa *ceed = qf->ceed->data;
  const CeedOperator_Occa *operator = ceed->op->data;assert(operator);
  const CeedInt nc = data->nc, dim = data->dim;
  const CeedEvalMode inmode = qf->inmode;
  const CeedEvalMode outmode = qf->outmode;
  const CeedInt bytes = qf->qdatasize;
  const CeedInt qbytes = Q*bytes;
  const CeedInt ubytes = (Q+Q*nc*(dim+1))*bytes;
  const CeedInt vbytes = Q*nc*dim*bytes;
  const CeedInt e = data->offset;
  const CeedInt qoffset = e*Q;
  CeedDebug("\033[36m[CeedQFunction][Apply] e=%d",e);
  //const CeedInt esize = ceed->op->Erestrict->elemsize;
  CeedInt uoffset = e*Q*nc*(dim+2);
  const CeedInt ready =  data->ready;
  assert((Q%qf->vlength)==0); // Q must be a multiple of vlength
  // ***************************************************************************
  if (!ready) { // If the kernel has not been built, do it now
    data->ready=true;
    CeedQFunctionBuildKernel(qf);
    if (!data->op) // like from t20
      data->d_q = occaDeviceMalloc(ceed->device,qbytes, qdata, NO_PROPS);
    data->d_u = occaDeviceMalloc(ceed->device,ubytes, NULL, NO_PROPS);
    data->d_v = occaDeviceMalloc(ceed->device,vbytes, NULL, NO_PROPS);
  }
  const occaMemory d_u = data->d_u;
  //assert(operator->BEu);
  //CeedVector_Occa *BEu = operator->BEu->data;
  //const occaMemory b_u = BEu->d_array;
  const occaMemory b_u = data->b_u;
  const occaMemory d_v = data->d_v;
  const occaMemory d_q = data->d_q;
  // ***************************************************************************
  if (!data->op)
    CeedQFunctionFill20_Occa(d_u,u,qbytes);
  else
    CeedQFunctionFillOp_Occa(d_u,u,&uoffset,inmode,Q,nc,dim,bytes);
  // ***************************************************************************
  occaKernelRun(data->kQFunctionApply,
                qf->ctx?occaPtr(qf->ctx):occaInt(0),
                d_q,occaInt(qoffset),
                occaInt(Q), d_u, b_u, occaInt(uoffset), d_v, occaPtr(&ierr));
  CeedChk(ierr);
  if (outmode==CEED_EVAL_NONE && !data->op)
    occaCopyMemToPtr(qdata,d_q,qbytes,NO_OFFSET,NO_PROPS);
  if (outmode==CEED_EVAL_INTERP)
    occaCopyMemToPtr(*v,d_v,vbytes,NO_OFFSET,NO_PROPS);
  assert(outmode==CEED_EVAL_NONE || outmode==CEED_EVAL_INTERP);
  return 0;
}

// *****************************************************************************
// * CeedQFunctionDestroy_Occa
// *****************************************************************************
static int CeedQFunctionDestroy_Occa(CeedQFunction qf) {
  int ierr;
  CeedQFunction_Occa *data=qf->data;
  free(data->oklPath);
  CeedDebug("\033[36m[CeedQFunction][Destroy]");
  if (data->ready) {
    if (!data->op) occaMemoryFree(data->d_q);
    occaMemoryFree(data->d_u);
    occaMemoryFree(data->d_v);
  }
  ierr = CeedFree(&data); CeedChk(ierr);
  return 0;
}

// *****************************************************************************
// * CeedQFunctionCreate_Occa
// *****************************************************************************
int CeedQFunctionCreate_Occa(CeedQFunction qf) {
  CeedQFunction_Occa *data;
  int ierr = CeedCalloc(1,&data); CeedChk(ierr);
  // Populate the CeedQFunction structure **************************************
  qf->Apply = CeedQFunctionApply_Occa;
  qf->Destroy = CeedQFunctionDestroy_Occa;
  qf->data = data;
  // Fill CeedQFunction_Occa struct ********************************************
  data->op = false;
  data->ready = false;
  data->nc = data->dim = 1;
  data->offset = 0;
  // Locate last ':' character in qf->focca ************************************
  CeedDebug("\033[36;1m[CeedQFunction][Create] focca=%s",qf->focca);
  const char *last_colon = strrchr(qf->focca,':');
  char *last_dot = strrchr(qf->focca,'.');
  if (!last_colon)
    return CeedError(qf->ceed, 1, "Can not find ':' in focca field!");
  if (!last_dot)
    return CeedError(qf->ceed, 1, "Can not find '.' in focca field!");
  // Focus on the function name
  data->qFunctionName = last_colon+1;
  CeedDebug("\033[36;1m[CeedQFunction][Create] qFunctionName=%s",
            data->qFunctionName);
  // Now extract filename
  data->oklPath=calloc(4096,sizeof(char));
  const size_t oklPathLen = last_dot - qf->focca;
  memcpy(data->oklPath,qf->focca,oklPathLen);
  data->oklPath[oklPathLen]='\0';
  strcpy(&data->oklPath[oklPathLen],".okl");
  CeedDebug("\033[36;1m[CeedQFunction][Create] filename=%s",data->oklPath);
  // Test if we can get file's status ******************************************
  struct stat buf;
  if (stat(data->oklPath, &buf)!=0)
    return CeedError(qf->ceed, 1, "Can not find OKL file!");
  return 0;
}
