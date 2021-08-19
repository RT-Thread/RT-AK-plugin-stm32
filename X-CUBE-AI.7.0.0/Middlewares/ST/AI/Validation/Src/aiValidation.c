/**
 ******************************************************************************
 * @file    aiValidation.c
 * @author  MCD Vertical Application Team
 * @brief   AI Validation application
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

/* Description:
 *
 * Main entry points for AI validation on-target process.
 *
 * History:
 *  - v1.0 - Initial version
 *  - v2.0 - Add FXP support
 *           Adding initial multiple IO support (legacy mode)
 *           Removing compile-time STM32 family checking
 *  - v2.1 - Adding integer (scale/zero-point) support
 *           Add support for external memory for data activations
 *  - v3.0 - Adding multiple IO support
 *  - v3.1 - Adding L5 support
 *  - v3.2 - Adding support for inputs in activations buffer
 *  - v4.0 - Use common C-files (aiTestUtility/aiTestHelper) for generic functions
 *           with aiSystemPerformance firmware
 *           Adding support for outputs in activations buffer
 *  - v5.0 - Replace Inspector interface by Observer interface,
 *           Host API (stm32nn.py module) is fully compatible.
 *           code clean-up: remove legacy code/add comments
 *  - v5.1 - minor - let irq enabled if USB CDC is used
 *  - v5.2 - Use the fix cycle count overflow support
 */

#ifndef HAS_INSPECTOR
#define HAS_INSPECTOR
#endif

#ifdef HAS_INSPECTOR
#define HAS_OBSERVER
#endif

#define USE_CORE_CLOCK_ONLY  0 /* 1: remove usage of the HAL_GetTick() to evaluate the number of CPU clock
                                *    HAL_Tick() is requested to avoid an overflow with the DWT clock counter (32b register)
                                */

#if defined(USE_CORE_CLOCK_ONLY) && USE_CORE_CLOCK_ONLY == 1
#define _APP_FIX_CLK_OVERFLOW 0
#else
#define _APP_FIX_CLK_OVERFLOW 1
#endif

/* System headers */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>

/* APP Header files */
#include <aiValidation.h>
#include <aiTestUtility.h>
#include <aiTestHelper.h>
#include <aiPbMgr.h>

/* AI x-cube-ai files */
#include <bsp_ai.h>

/* AI header files */
#include <ai_platform.h>
#include <core_datatypes.h>   /* AI_PLATFORM_RUNTIME_xxx definition */
#include <ai_datatypes_internal.h>
#include <core_common.h>      /* for GET_TENSOR_LIST_OUT().. definition */
#include <core_private.h>      /* for GET_TENSOR_LIST_OUT().. definition */


/* -----------------------------------------------------------------------------
 * TEST-related definitions
 * -----------------------------------------------------------------------------
 */

/* APP configuration 0: disabled 1: enabled */
#define _APP_DEBUG_         			0

#define _APP_VERSION_MAJOR_     (0x05)
#define _APP_VERSION_MINOR_     (0x02)
#define _APP_VERSION_   ((_APP_VERSION_MAJOR_ << 8) | _APP_VERSION_MINOR_)

#define _APP_NAME_   "AI Validation (Observer based)"


/* -----------------------------------------------------------------------------
 * object definition/declaration for AI-related execution context
 * -----------------------------------------------------------------------------
 */

#ifdef HAS_OBSERVER
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

#endif

struct ai_network_exec_ctx {
  ai_handle handle;
  ai_network_report report;
#ifdef HAS_OBSERVER
  struct ai_network_user_obs_ctx *obs_ctx;
#endif
} net_exec_ctx[AI_MNETWORK_NUMBER] = {0};


#if AI_MNETWORK_DATA_ACTIVATIONS_INT_SIZE != 0
AI_ALIGNED(32)
static ai_u8 activations[AI_MNETWORK_DATA_ACTIVATIONS_INT_SIZE];
#endif


DEF_DATA_IN;

DEF_DATA_OUT;


/* -----------------------------------------------------------------------------
 * Observer-related functions
 * -----------------------------------------------------------------------------
 */

#ifdef HAS_OBSERVER
static ai_u32 aiOnExecNode_cb(const ai_handle cookie,
    const ai_u32 flags,
    const ai_observer_node *node) {

  struct ai_network_exec_ctx *ctx = (struct ai_network_exec_ctx*)cookie;
  struct ai_network_user_obs_ctx  *obs_ctx = ctx->obs_ctx;

  volatile uint64_t ts = cyclesCounterEnd(); // dwtGetCycles(); /* time stamp to mark the entry */

  if (flags & AI_OBSERVER_PRE_EVT) {
    obs_ctx->n_cb_in++;
    if (flags & AI_OBSERVER_FIRST_EVT)
      obs_ctx->tnodes = ts;
  } else if (flags & AI_OBSERVER_POST_EVT) {
    uint32_t type, n_type;
    ai_tensor_list *tl;

    cyclesCounterStart();
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

      if (i < (GET_TENSOR_LIST_SIZE(tl) - 1)) {
        n_type = type | (EnumLayerType_LAYER_TYPE_INTERNAL_DATA_NO_LAST << 16);
      } else {
        n_type = type;
      }
      aiPbMgrSendAiBuffer4(obs_ctx->creq, obs_ctx->cresp, EnumState_S_PROCESSING,
          n_type,
          node->id,
          dwtCyclesToFloatMs(ts),
          &buffer,
          scale, zero_point);

      // break; /* currently (X-CUBE-AI 5.x) only one output tensor is available by operator */
    }
    obs_ctx->tcom += cyclesCounterEnd();
  }

  cyclesCounterStart();
  return 0;
}
#endif


static uint64_t aiObserverAdjustInferenceTime(struct ai_network_exec_ctx *ctx,
    uint64_t tend)
{
#ifdef HAS_OBSERVER
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
#endif
  return tend;
}

static void aiObserverSendReport(const reqMsg *req, respMsg *resp,
    EnumState state, struct ai_network_exec_ctx *ctx,
    const ai_float dur_ms)
{
#ifdef HAS_OBSERVER
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
#endif
}

static int aiObserverConfig(struct ai_network_exec_ctx *ctx,
    const reqMsg *req)
{
#ifdef HAS_OBSERVER
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
#endif
return 0;
}

static int aiObserverBind(struct ai_network_exec_ctx *ctx,
    const reqMsg *creq, respMsg *cresp)
{
#ifdef HAS_OBSERVER
  ai_handle net_hdl;
  ai_network_params pparams;
  ai_bool res;

  struct ai_network_user_obs_ctx  *obs_ctx = ctx->obs_ctx;

  if (obs_ctx->is_enabled == false)
    return 0;

  if (ctx->handle == AI_HANDLE_NULL)
    return -1;

  obs_ctx->creq = creq;
  obs_ctx->cresp = cresp;

  /* retrieve real net handle to use the AI platform API */
  ai_mnetwork_get_private_handle(ctx->handle,
      &net_hdl,
      &pparams);

  /* register the user call-back */
  obs_ctx->plt_ctx.on_node = aiOnExecNode_cb;
  obs_ctx->plt_ctx.cookie = (ai_handle)ctx;
  obs_ctx->plt_ctx.flags = AI_OBSERVER_PRE_EVT | AI_OBSERVER_POST_EVT;

  res = ai_platform_observer_register_s(net_hdl, &obs_ctx->plt_ctx);
  if (!res) {
    return -1;
  }
#endif
  return 0;
}

static int aiObserverUnbind(struct ai_network_exec_ctx *ctx)
{
#ifdef HAS_OBSERVER
  ai_handle net_hdl;
  ai_network_params pparams;

  struct ai_network_user_obs_ctx  *obs_ctx = ctx->obs_ctx;

  if (obs_ctx->is_enabled == false)
    return 0;

  /* retrieve real handle */
  ai_mnetwork_get_private_handle(ctx->handle, &net_hdl, &pparams);

  /* un-register the call-back */
  ai_platform_observer_unregister_s(net_hdl, &obs_ctx->plt_ctx);
#endif
  return 0;
}


/* -----------------------------------------------------------------------------
 * AI-related functions
 * -----------------------------------------------------------------------------
 */

static struct ai_network_exec_ctx *aiExecCtx(const char *nn_name, int pos)
{
  struct ai_network_exec_ctx *cur = NULL;

  if (!nn_name)
    return NULL;

  if (!nn_name[0]) {
    if ((pos >= 0) && (pos < AI_MNETWORK_NUMBER) && net_exec_ctx[pos].handle)
      cur = &net_exec_ctx[pos];
  } else {
    int idx;
    for (idx=0; idx < AI_MNETWORK_NUMBER; idx++) {
      cur = &net_exec_ctx[idx];
      if (cur->handle &&
          (strlen(cur->report.model_name) == strlen(nn_name)) &&
          (strncmp(cur->report.model_name, nn_name,
              strlen(cur->report.model_name)) == 0)) {
        break;
      }
      cur = NULL;
    }
  }
  return cur;
}

static int aiBootstrap(struct ai_network_exec_ctx *ctx, const char *nn_name)
{
  ai_error err;
  ai_u32 ext_addr;
  ai_u32 sz;

  /* Creating the instance of the  network ------------------------- */
  printf("Creating the network \"%s\"..\r\n", nn_name);

  err = ai_mnetwork_create(nn_name, &ctx->handle, NULL);
  if (err.type) {
    aiLogErr(err, "ai_mnetwork_create");
    return -1;
  }

  /* Initialize the instance --------------------------------------- */
  printf("Initializing the network\r\n");

  if (ai_mnetwork_get_info(ctx->handle,
      &ctx->report)) {
  } else {
    err = ai_mnetwork_get_error(ctx->handle);
    aiLogErr(err, "ai_mnetwork_get_info");
    ai_mnetwork_destroy(ctx->handle);
    ctx->handle = AI_HANDLE_NULL;
    return -2;
  }

  /* Addresses of the weights and activations buffers
   *
   * - @ of the weights buffer is always provided by the multiple network wrapper
   *   thanks to the ai_<network>_data_weights_get() function (see app_x-cube-ai.c file
   *   generated by the X-CUBE-AI plug-in).
   * - @ of the activations buffer can be a local buffer (activations object) or a buffer
   *   located in the external memory (network dependent feature). For the last case,
   *   the address (hard-coded @) is defined by the X-CUBE-AI plug-in and stored in the
   *   multiple network structure (see app_x-cube-ai.c file, ai_network_entry_t definition).
   *   0xFFFFFFFF indicates that the local buffer should be used.
   */
  ai_network_params params = AI_NETWORK_PARAMS_INIT(
      AI_BUFFER_NULL(NULL),
      AI_BUFFER_NULL(NULL)
  );

  if (ai_mnetwork_get_ext_data_activations(ctx->handle, &ext_addr, &sz) == 0) {
    if (ext_addr == 0xFFFFFFFF) {
#if AI_MNETWORK_DATA_ACTIVATIONS_INT_SIZE != 0
      params.activations.data = (ai_handle)activations;
      if (sz > AI_MNETWORK_DATA_ACTIVATIONS_INT_SIZE) {
        printf("E: APP error (aiBootstrap for %s) - size of the local activations buffer is not enough\r\n",
            nn_name);
        ai_mnetwork_destroy(ctx->handle);
        ctx->handle = AI_HANDLE_NULL;
        return -5;
      }
#else
      if (ctx->report.activations.channels != 0) {
        printf("E: APP error (aiBootstrap for %s) - a local activations buffer is requested\r\n",
            nn_name);
        ai_mnetwork_destroy(ctx->handle);
        ctx->handle = AI_HANDLE_NULL;
        return -5;
      } else {
        params.activations.data = AI_HANDLE_NULL;
      }
#endif
    }
    else {
      params.activations.data = (ai_handle)ext_addr;
    }
  }

  if (!ai_mnetwork_init(ctx->handle, &params)) {
    err = ai_mnetwork_get_error(ctx->handle);
    aiLogErr(err, "ai_mnetwork_init");
    ai_mnetwork_destroy(ctx->handle);
    ctx->handle = AI_HANDLE_NULL;
    return -4;
  }

  /* Display the network info -------------------------------------- */
  if (ai_mnetwork_get_info(ctx->handle,
      &ctx->report)) {
    aiPrintNetworkInfo(&ctx->report);
  } else {
    err = ai_mnetwork_get_error(ctx->handle);
    aiLogErr(err, "ai_mnetwork_get_info");
    ai_mnetwork_destroy(ctx->handle);
    ctx->handle = AI_HANDLE_NULL;
    return -2;
  }

  return 0;
}

static int aiInit(void)
{
  int res = -1;
  const char *nn_name;
  int idx;

  aiPlatformVersion();

  /* Reset the contexts -------------------------------------------- */
  for (idx=0; idx < AI_MNETWORK_NUMBER; idx++) {
    net_exec_ctx[idx].handle = AI_HANDLE_NULL;
  }

  /* Discover and initialize the network(s) ------------------------ */
  printf("Discovering the network(s)...\r\n");

  idx = 0;
  do {
    nn_name = ai_mnetwork_find(NULL, idx);
    if (nn_name) {
      printf("\r\nFound network \"%s\"\r\n", nn_name);
      res = aiBootstrap(&net_exec_ctx[idx], nn_name);
      if (res)
        nn_name = NULL;
    }
    idx++;
  } while (nn_name);

  return res;
}

static void aiDeInit(void)
{
  ai_error err;
  int idx;

  /* Releasing the instance(s) ------------------------------------- */
  printf("Releasing the instance(s)...\r\n");

  for (idx=0; idx<AI_MNETWORK_NUMBER; idx++) {
    if (net_exec_ctx[idx].handle != AI_HANDLE_NULL) {
      if (ai_mnetwork_destroy(net_exec_ctx[idx].handle)
          != AI_HANDLE_NULL) {
        err = ai_mnetwork_get_error(net_exec_ctx[idx].handle);
        aiLogErr(err, "ai_mnetwork_destroy");
      }
      net_exec_ctx[idx].handle = AI_HANDLE_NULL;
    }
  }
}


/* -----------------------------------------------------------------------------
 * Specific test APP commands
 * -----------------------------------------------------------------------------
 */

void aiPbCmdNNInfo(const reqMsg *req, respMsg *resp, void *param)
{
  struct ai_network_exec_ctx *ctx;

  UNUSED(param);

  ctx = aiExecCtx(req->name, req->param);
  if (ctx)
    aiPbMgrSendNNInfo(req, resp, EnumState_S_IDLE,
        &ctx->report);
  else
    aiPbMgrSendAck(req, resp, EnumState_S_ERROR,
        EnumError_E_INVALID_PARAM, EnumError_E_INVALID_PARAM);
}

void aiPbCmdNNRun(const reqMsg *req, respMsg *resp, void *param)
{
  ai_i32 batch;
  uint64_t tend;
  bool res;
  struct ai_network_exec_ctx *ctx;

  ai_buffer ai_input[AI_MNETWORK_IN_NUM];
  ai_buffer ai_output[AI_MNETWORK_OUT_NUM];

  UNUSED(param);

  /* 0 - Check if requested c-name model is available -------------- */
  ctx = aiExecCtx(req->name, -1);
  if (!ctx) {
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

  cyclesCounterStart();

  batch = ai_mnetwork_run(ctx->handle, ai_input, ai_output);
  if (batch != 1) {
    aiLogErr(ai_mnetwork_get_error(ctx->handle),
        "ai_mnetwork_run");
    aiPbMgrSendAck(req, resp, EnumState_S_ERROR,
        EnumError_E_GENERIC, EnumError_E_GENERIC);
    return;
  }
  tend = cyclesCounterEnd();

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

  aiObserverUnbind(ctx);
}

static aiPbCmdFunc pbCmdFuncTab[] = {
#ifdef HAS_INSPECTOR
    AI_PB_CMD_SYNC((void *)(EnumCapability_CAP_INSPECTOR | (EnumAiRuntime_AI_RT_STM_AI << 16))),
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

  cyclesCounterInit();

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

