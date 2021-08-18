/**
 ******************************************************************************
 * @file    aiValidation.c
 * @author  MCD Vertical Application Team
 * @brief   AI Validation application (entry points) - Relocatable network
 ******************************************************************************
 * @attention
 *
 * <h2><center>&copy; Copyright (c) 2020 STMicroelectronics.
 * All rights reserved.</center></h2>
 *
 * This software component is licensed by ST under Ultimate Liberty license
 * SLA0044, the "License"; You may not use this file except in compliance with
 * the License. You may obtain a copy of the License at:
 *                             www.st.com/SLA0044
 *
 ******************************************************************************
 */

/* Description
 *
 * - Entry points) for the AI validation of a relocatable network object.
 *   Support for a simple relocatable network (no multiple network support).
 *
 * History:
 *  - v1.0 - Initial version. Based on aiValidation v5.0
 *  - v1.1 - minor - let irq enabled if USB CDC is used
 */

/* System headers */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>

#ifndef HAS_INSPECTOR  /* For legacy */
#define HAS_INSPECTOR
#endif

#ifdef HAS_INSPECTOR
#define USE_OBSERVER         1 /* 0: disable Observer support */
#endif

#define USER_REL_COPY_MODE   0

#if !defined(APP_DEBUG)
#define APP_DEBUG            0 /* 1: add debug trace - application level */
#endif

/* APP Header files */
#include <aiValidation.h>
#include <aiTestUtility.h>
#include <aiTestHelper.h>
#include <aiPbMgr.h>

/* AI x-cube-ai files */
#include <app_x-cube-ai.h>

/* AI header files */
#include <ai_platform.h>
#include <core_datatypes.h>   /* AI_PLATFORM_RUNTIME_xxx definition */
#include <ai_datatypes_internal.h>
#include <core_common.h>      /* for GET_TENSOR_LIST_OUT().. definition */

#include <ai_reloc_network.h>

/* Include the image of the relocatable network */
#include <network_img_rel.h>

/* -----------------------------------------------------------------------------
 * TEST-related definitions
 * -----------------------------------------------------------------------------
 */
#define _APP_VERSION_MAJOR_     (0x01)
#define _APP_VERSION_MINOR_     (0x00)
#define _APP_VERSION_   ((_APP_VERSION_MAJOR_ << 8) | _APP_VERSION_MINOR_)

#define _APP_NAME_   "AI Validation (RELOC)"

#if AI_MNETWORK_NUMBER > 1 && !defined(AI_NETWORK_MODEL_NAME)
#error Only ONE network is supported (default c-name)
#endif


/* -----------------------------------------------------------------------------
 * Object definition/declaration for AI-related execution context
 * -----------------------------------------------------------------------------
 */
#if defined(USE_OBSERVER) && USE_OBSERVER == 1

struct ai_network_user_obs_ctx {
  bool is_enabled;                /* indicate if the feature is enabled */
  ai_u32 n_cb_in;                 /* indicate the number of the entry cb (debug) */
  ai_u32 n_cb_out;                /* indicate the number of the exit cb (debug) */
  const reqMsg *creq;             /* reference of the current PB request */
  respMsg *cresp;                 /* reference of the current PB response */
  bool no_data;                   /* indicate that the data of the tensor should be not up-loaded */
  uint64_t tcom;                  /* number of cycles to up-load the data by layer (COM) */
  uint64_t tnodes;                /* number of cycles to execute the operators (including nn.init)
                                     nn.done is excluded but added by the adjust function */
  ai_observer_exec_ctx plt_ctx;   /* internal AI platform execution context for the observer
                                     requested to avoid dynamic allocation during the the registration */
};

struct ai_network_user_obs_ctx  net_obs_ctx; /* execution the models is serialized,
                                                only one context is requested */

#endif /* USE_OBSERVER */

struct ai_network_exec_ctx {
  ai_handle handle;
  ai_network_report report;
#if defined(USE_OBSERVER) && USE_OBSERVER == 1
  struct ai_network_user_obs_ctx *obs_ctx;
#endif
} net_exec_ctx[1] = {0};


/* Local activations buffer if necessary. */
#if AI_NETWORK_DATA_ACTIVATIONS_START_ADDR &&\
    AI_NETWORK_DATA_ACTIVATIONS_START_ADDR != 0xFFFFFFFF
AI_ALIGNED(32)
static ai_u8 activations[AI_NETWORK_DATA_ACTIVATIONS_INT_SIZE];
#else
#if AI_NETWORK_DATA_ACTIVATIONS_SIZE > 0
AI_ALIGNED(32)
static ai_u8 activations[AI_NETWORK_DATA_ACTIVATIONS_SIZE];
#else
static ai_u8 activations[4]; /* Dummy act buffer */
#endif
#endif

/* Input and output buffers for the IO tensors. */
DEF_DATA_IN;

DEF_DATA_OUT;

/* RT Network buffer to relocatable network instance */
#if defined(USER_REL_COPY_MODE) && USER_REL_COPY_MODE == 1
AI_ALIGNED(32)
uint8_t reloc_ram[AI_NETWORK_RELOC_RAM_SIZE_COPY];
#else
AI_ALIGNED(32)
uint8_t reloc_ram[AI_NETWORK_RELOC_RAM_SIZE_XIP];
#endif


/* -----------------------------------------------------------------------------
 * Observer-related functions
 * -----------------------------------------------------------------------------
 */
#if defined(USE_OBSERVER) && USE_OBSERVER == 1

static ai_u32 aiOnExecNode_cb(const ai_handle cookie,
    const ai_u32 flags,
    const ai_observer_node *node) {

  struct ai_network_exec_ctx *ctx = (struct ai_network_exec_ctx*)cookie;
  struct ai_network_user_obs_ctx  *obs_ctx = ctx->obs_ctx;

  volatile uint64_t ts = dwtGetCycles(); /* time stamp to mark the entry */

  if (flags & AI_OBSERVER_PRE_EVT) {
    obs_ctx->n_cb_in++;
    if (flags & AI_OBSERVER_FIRST_EVT)
      obs_ctx->tnodes = ts;
  } else if (flags & AI_OBSERVER_POST_EVT) {
    uint32_t type;
    ai_tensor_list *tl;

    dwtReset();
    /* "ts" here indicates the execution time of the
     * operator because the dwt cycle CPU counter has been
     * reset by the entry cb.
     */
    obs_ctx->tnodes += ts;
    obs_ctx->n_cb_out++;

    if (flags & AI_OBSERVER_LAST_EVT)
      type = EnumLayerType_LAYER_TYPE_INTERNAL_LAST;
    else
      type = EnumLayerType_LAYER_TYPE_INTERNAL;

    type = type << 16;

    if (obs_ctx->no_data)
      type |= PB_BUFFER_TYPE_SEND_WITHOUT_DATA;
    type |= (node->type & (ai_u16)0x7FFF);

    tl = GET_TENSOR_LIST_OUT(node->tensors);
    AI_FOR_EACH_TENSOR_LIST_DO(i, t, tl) {
      ai_buffer buffer;
      ai_float scale = AI_TENSOR_INTEGER_GET_SCALE(t, 0);
      ai_i32 zero_point = 0;

      if (AI_TENSOR_FMT_GET_SIGN(t))
        zero_point = AI_TENSOR_INTEGER_GET_ZEROPOINT_I8(t, 0);
      else
        zero_point = AI_TENSOR_INTEGER_GET_ZEROPOINT_U8(t, 0);

      buffer.format = AI_TENSOR_GET_FMT(t);
      buffer.n_batches = 1;
      buffer.data = AI_TENSOR_ARRAY_GET_DATA_ADDR(t);
      buffer.height = AI_SHAPE_H(AI_TENSOR_SHAPE(t));
      buffer.width = AI_SHAPE_W(AI_TENSOR_SHAPE(t));
      buffer.channels = AI_SHAPE_CH(AI_TENSOR_SHAPE(t));
      buffer.meta_info = NULL;

      aiPbMgrSendAiBuffer4(obs_ctx->creq, obs_ctx->cresp, EnumState_S_PROCESSING,
          type,
          node->id,
          dwtCyclesToFloatMs(ts),
          &buffer,
          scale, zero_point);

      obs_ctx->tcom += dwtGetCycles();
      break; /* currently (X-CUBE-AI 5.x) only one output tensor is available by operator */
    }
  }
  dwtReset();
  return 0;
}

#endif /* USE_OBSERVER */


static uint64_t aiObserverAdjustInferenceTime(struct ai_network_exec_ctx *ctx,
    uint64_t tend)
{
#if defined(USE_OBSERVER) && USE_OBSERVER == 1

  /* When the observer is enabled, the duration reported with
   * the output tensors is the sum of NN executing time
   * and the COM to up-load the info by layer.
   *
   * tnodes = nn.init + nn.l0 + nn.l1 ...
   * tcom   = tl0 + tl1 + ...
   * tend   = nn.done
   *
   */
  struct ai_network_user_obs_ctx  *obs_ctx = ctx->obs_ctx;
  tend = obs_ctx->tcom + obs_ctx->tnodes + tend;

#endif /* USE_OBSERVER */
  return tend;
}

static void aiObserverSendReport(const reqMsg *req, respMsg *resp,
    EnumState state, struct ai_network_exec_ctx *ctx,
    const ai_float dur_ms)
{
#if defined(USE_OBSERVER) && USE_OBSERVER == 1

  struct ai_network_user_obs_ctx  *obs_ctx = ctx->obs_ctx;

  if (obs_ctx->is_enabled == false)
    return;

  resp->which_payload = respMsg_report_tag;
  resp->payload.report.id = 0;
  resp->payload.report.elapsed_ms = dur_ms;
  resp->payload.report.n_nodes = ctx->report.n_nodes;
  resp->payload.report.signature = 0;
  resp->payload.report.num_inferences = 1;
  aiPbMgrSendResp(req, resp, state);
  aiPbMgrWaitAck();

#endif /* USE_OBSERVER */
}

static int aiObserverConfig(struct ai_network_exec_ctx *ctx,
    const reqMsg *req)
{
#if defined(USE_OBSERVER) && USE_OBSERVER == 1

  net_obs_ctx.no_data = false;
  net_obs_ctx.is_enabled = false;
  if ((req->param & EnumRunParam_P_RUN_MODE_INSPECTOR) ==
      EnumRunParam_P_RUN_MODE_INSPECTOR)
    net_obs_ctx.is_enabled = true;

  if ((req->param & EnumRunParam_P_RUN_MODE_INSPECTOR_WITHOUT_DATA) ==
      EnumRunParam_P_RUN_MODE_INSPECTOR_WITHOUT_DATA) {
    net_obs_ctx.is_enabled = true;
    net_obs_ctx.no_data = true;
  }

  net_obs_ctx.tcom = 0ULL;
  net_obs_ctx.tnodes = 0ULL;
  net_obs_ctx.n_cb_in  = 0;
  net_obs_ctx.n_cb_out = 0;

  ctx->obs_ctx = &net_obs_ctx;

#endif /* US_OBSERVER */

return 0;
}

static int aiObserverBind(struct ai_network_exec_ctx *ctx,
    const reqMsg *creq, respMsg *cresp)
{
#if defined(USE_OBSERVER) && USE_OBSERVER == 1

  ai_bool res;

  struct ai_network_user_obs_ctx  *obs_ctx = ctx->obs_ctx;

  if (obs_ctx->is_enabled == false)
    return 0;

  if (ctx->handle == AI_HANDLE_NULL)
    return -1;

  obs_ctx->creq = creq;
  obs_ctx->cresp = cresp;

  /* register the user call-back */
  res = ai_rel_platform_observer_register(ctx->handle, aiOnExecNode_cb,
      ctx, AI_OBSERVER_PRE_EVT | AI_OBSERVER_POST_EVT);
  if (!res) {
    return -1;
  }

#endif /* USE_OBSERVER */

  return 0;
}

static int aiObserverUnbind(struct ai_network_exec_ctx *ctx)
{
#if defined(USE_OBSERVER) && USE_OBSERVER == 1

  struct ai_network_user_obs_ctx  *obs_ctx = ctx->obs_ctx;

  if (obs_ctx->is_enabled == false)
    return 0;

  /* un-register the call-back */
  ai_rel_platform_observer_unregister(ctx->handle, aiOnExecNode_cb, ctx);

#endif /* USE_OBSERVER */

  return 0;
}


/* -----------------------------------------------------------------------------
 * AI-related functions
 * -----------------------------------------------------------------------------
 */

static int aiBootstrap(struct ai_network_exec_ctx *ctx)
{
  ai_error err;
  ai_handle ext_act_addr;
  ai_handle weights_addr;
  ai_rel_network_info rt_info;

  /* Creating an instance of the network ------------------------- */
  printf("\r\nInstancing the network (reloc)..\r\n");

  err = ai_rel_network_rt_get_info(ai_network_reloc_img_get(), &rt_info);
  if (err.type != AI_ERROR_NONE) {
      aiLogErr(err, "ai_rel_network_rt_get_info");
      return -1;
    }

#if defined(USER_REL_COPY_MODE) && USER_REL_COPY_MODE == 1
  err = ai_rel_network_load_and_create(ai_network_reloc_img_get(),
      reloc_ram, AI_NETWORK_RELOC_RAM_SIZE_COPY, AI_RELOC_RT_LOAD_MODE_COPY,
      &ctx->handle);
#else
  err = ai_rel_network_load_and_create(ai_network_reloc_img_get(),
      reloc_ram, AI_NETWORK_RELOC_RAM_SIZE_XIP, AI_RELOC_RT_LOAD_MODE_XIP,
      &ctx->handle);
#endif
  if (err.type != AI_ERROR_NONE) {
    aiLogErr(err, "ai_rel_network_load_and_create");
    return -1;
  }

  /* test returned err value (debug purpose) */
  err = ai_rel_network_get_error(ctx->handle);
  if (err.type != AI_ERROR_NONE) {
    aiLogErr(err, "ai_rel_network_get_error");
    return -1;
  }

  /* Initialize the instance --------------------------------------- */
  printf("Initializing the network\r\n");

#if AI_NETWORK_DATA_ACTIVATIONS_START_ADDR &&\
    AI_NETWORK_DATA_ACTIVATIONS_START_ADDR != 0xFFFFFFFF
  ext_act_addr = (ai_handle)AI_NETWORK_DATA_ACTIVATIONS_START_ADDR;
#else
  ext_act_addr = (ai_handle)activations;
#endif

  weights_addr = rt_info.weights;
#if defined(AI_NETWORK_DATA_WEIGHTS_GET_FUNC)
  if (!weights_addr)
    weights_addr = AI_NETWORK_DATA_WEIGHTS_GET_FUNC();
#endif

  if (!ai_rel_network_init(ctx->handle,
      weights_addr, ext_act_addr)) {
    err = ai_rel_network_get_error(ctx->handle);
    aiLogErr(err, "ai_rel_network_init");
    ai_rel_network_destroy(ctx->handle);
    ctx->handle = AI_HANDLE_NULL;
    return -2;
  }

  /* Display the network info -------------------------------------- */
  if (ai_rel_network_get_info(ctx->handle, &ctx->report)) {
    aiPrintNetworkInfo(&ctx->report);
  } else {
    err = ai_rel_network_get_error(ctx->handle);
    aiLogErr(err, "ai_rel_network_get_info");
    ai_rel_network_destroy(ctx->handle);
    ctx->handle = AI_HANDLE_NULL;
    return -3;
  }

  return 0;
}

static void aiDone(struct ai_network_exec_ctx *ctx)
{
  ai_error err;

  /* Releasing the instance(s) ------------------------------------- */
  printf("Releasing the instance...\r\n");

  if (ctx->handle != AI_HANDLE_NULL) {
    if (ai_rel_network_destroy(ctx->handle)
        != AI_HANDLE_NULL) {
      err = ai_rel_network_get_error(ctx->handle);
      aiLogErr(err, "ai_rel_network_destroy");
    }
    ctx->handle = AI_HANDLE_NULL;
  }
}

static int aiInit(void)
{
  int res;

  aiPlatformVersion();

  net_exec_ctx[0].handle = AI_HANDLE_NULL;
  res = aiBootstrap(&net_exec_ctx[0]);

  return res;
}

static void aiDeInit(void)
{
  aiDone(&net_exec_ctx[0]);
}


/* -----------------------------------------------------------------------------
 * Specific test APP commands
 * -----------------------------------------------------------------------------
 */

void aiPbCmdNNInfo(const reqMsg *req, respMsg *resp, void *param)
{
  UNUSED(param);

  if (net_exec_ctx[0].handle && req->param == 0)
    aiPbMgrSendNNInfo(req, resp, EnumState_S_IDLE,
        &net_exec_ctx[0].report);
  else
    aiPbMgrSendAck(req, resp, EnumState_S_ERROR,
        EnumError_E_INVALID_PARAM, EnumError_E_INVALID_PARAM);
}

void aiPbCmdNNRun(const reqMsg *req, respMsg *resp, void *param)
{
  ai_i32 batch;
  uint32_t tend;
  bool res;
  struct ai_network_exec_ctx *ctx;
#if !defined(USE_USB_CDC_CLASS)
  uint32_t ints;
#endif

  ai_buffer ai_input[AI_MNETWORK_IN_NUM];
  ai_buffer ai_output[AI_MNETWORK_OUT_NUM];

  UNUSED(param);

  ctx = &net_exec_ctx[0];

  /* 0 - Check if requested c-name model is available -------------- */
  if (!ctx->handle ||
      (strncmp(ctx->report.model_name, req->name,
          strlen(ctx->report.model_name)) != 0)) {
    aiPbMgrSendAck(req, resp, EnumState_S_ERROR,
        EnumError_E_INVALID_PARAM, EnumError_E_INVALID_PARAM);
    return;
  }

  aiObserverConfig(ctx, req);

  /* Fill the input tensor descriptors */
  for (int i = 0; i < ctx->report.n_inputs; i++) {
    ai_input[i] = ctx->report.inputs[i];
    ai_input[i].n_batches  = 1;
    if (ctx->report.inputs[i].data)
      ai_input[i].data = AI_HANDLE_PTR(ctx->report.inputs[i].data);
    else
      ai_input[i].data = AI_HANDLE_PTR(data_ins[i]);
  }

  /* Fill the output tensor descriptors */
  for (int i = 0; i < ctx->report.n_outputs; i++) {
    ai_output[i] = ctx->report.outputs[i];
    ai_output[i].n_batches = 1;
    if (ctx->report.outputs[i].data)
      ai_output[i].data = AI_HANDLE_PTR(ctx->report.outputs[i].data);
    else
      ai_output[i].data = AI_HANDLE_PTR(data_outs[i]);
  }

  /* 1 - Send a ACK (ready to receive a tensor) -------------------- */
  aiPbMgrSendAck(req, resp, EnumState_S_WAITING,
      aiPbAiBufferSize(&ai_input[0]), EnumError_E_NONE);

  /* 2 - Receive all input tensors --------------------------------- */
  for (int i = 0; i < ctx->report.n_inputs; i++) {
    /* upload a buffer */
    EnumState state = EnumState_S_WAITING;
    if ((i + 1) == ctx->report.n_inputs)
      state = EnumState_S_PROCESSING;
    res = aiPbMgrReceiveAiBuffer3(req, resp, state, &ai_input[i]);
    if (res != true)
      return;
  }

  aiObserverBind(ctx, req, resp);

  /* 3 - Processing ------------------------------------------------ */
#if !defined(USE_USB_CDC_CLASS)
  ints = disableInts();
#endif

  dwtReset();

  batch = ai_rel_network_run(ctx->handle, ai_input, ai_output);
  if (batch != 1) {
    aiLogErr(ai_rel_network_get_error(ctx->handle),
        "ai_rel_network_run");
    aiPbMgrSendAck(req, resp, EnumState_S_ERROR,
        EnumError_E_GENERIC, EnumError_E_GENERIC);
    return;
  }
  tend = dwtGetCycles();

  tend = aiObserverAdjustInferenceTime(ctx, tend);

  /* 4 - Send basic report (optional) ------------------------------ */
  aiObserverSendReport(req, resp, EnumState_S_PROCESSING, ctx,
      dwtCyclesToFloatMs(tend));

  /* 5 - Send all output tensors ----------------------------------- */
  for (int i = 0; i < ctx->report.n_outputs; i++) {
    EnumState state = EnumState_S_PROCESSING;
    if ((i + 1) == ctx->report.n_outputs)
      state = EnumState_S_DONE;
    aiPbMgrSendAiBuffer4(req, resp, state,
        EnumLayerType_LAYER_TYPE_OUTPUT << 16 | 0,
        0, dwtCyclesToFloatMs(tend),
        &ai_output[i], 0.0f, 0);
  }

#if !defined(USE_USB_CDC_CLASS)
  restoreInts(ints);
#endif
  aiObserverUnbind(ctx);
}

static aiPbCmdFunc pbCmdFuncTab[] = {
#if defined(USE_OBSERVER) && USE_OBSERVER == 1
    AI_PB_CMD_SYNC((void *)EnumCapability_CAP_INSPECTOR),
#else
    AI_PB_CMD_SYNC(NULL),
#endif
    AI_PB_CMD_SYS_INFO(NULL),
    { EnumCmd_CMD_NETWORK_INFO, &aiPbCmdNNInfo, NULL },
    { EnumCmd_CMD_NETWORK_RUN, &aiPbCmdNNRun, NULL },
#if defined(AI_PB_TEST) && AI_PB_TEST == 1
    AI_PB_CMD_TEST(NULL),
#endif
    AI_PB_CMD_END,
};


/* -----------------------------------------------------------------------------
 * Exported/Public functions
 * -----------------------------------------------------------------------------
 */

int aiValidationInit(void)
{
  printf("\r\n#\r\n");
  printf("# %s %d.%d\r\n", _APP_NAME_ , _APP_VERSION_MAJOR_, _APP_VERSION_MINOR_);
  printf("#\r\n");

  systemSettingLog();

  return 0;
}

int aiValidationProcess(void)
{
  int r;

  r = aiInit();
  if (r) {
    printf("\r\nE:  aiInit() r=%d\r\n", r);
    HAL_Delay(2000);
    return r;
  } else {
    printf("\r\n");
    printf("-------------------------------------------\r\n");
    printf("| READY to receive a CMD from the HOST... |\r\n");
    printf("-------------------------------------------\r\n");
    printf("\r\n");
    printf("# Note: At this point, default ASCII-base terminal should be closed\r\n");
    printf("# and a stm32com-base interface should be used\r\n");
    printf("# (i.e. Python stm32com module). Protocol version = %d.%d\r\n",
        EnumVersion_P_VERSION_MAJOR,
        EnumVersion_P_VERSION_MINOR);
  }

  aiPbMgrInit(pbCmdFuncTab);

  do {
    r = aiPbMgrWaitAndProcess();
  } while (r==0);

  return r;
}

void aiValidationDeInit(void)
{
  printf("\r\n");
  aiDeInit();
  printf("bye bye ...\r\n");
}

