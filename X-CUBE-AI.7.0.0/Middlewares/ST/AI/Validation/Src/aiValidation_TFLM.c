/**
 ******************************************************************************
 * @file    aiValidation_TFLM.c
 * @author  MCD Vertical Application Team
 * @brief   AI Validation application (entry points) - TFLM runtime
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
 * - Entry points for the AI validation, TFLM runtime
 *
 * History:
 *  - v1.0 - Initial version. Use the X-CUBE-AI protocol vX.X
 *           Emulate X-CUBE-AI interface
 *  - v1.1 - minor - let irq enabled if USB CDC is used
 *  - v2.0 - align code for TFLM 2.5.0
 *           add observer support to upload time by layer with or w/o data
 *  - v2.1 - Use the fix cycle count overflow support
 */

/* System headers */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>

#define USE_OBSERVER   1 /* 0: remove the registration of the user CB to evaluate the inference time by layer */

#define USE_CORE_CLOCK_ONLY  0 /* 1: remove usage of the HAL_GetTick() to evaluate the number of CPU clock
                                *    HAL_Tick() is requested to avoid an overflow with the DWT clock counter (32b register)
                                */

#if defined(USE_CORE_CLOCK_ONLY) && USE_CORE_CLOCK_ONLY == 1
#define _APP_FIX_CLK_OVERFLOW 0
#else
#define _APP_FIX_CLK_OVERFLOW 1
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
#include <core_common.h>      /* for GET_TENSOR_LIST_OUT().. definition */

#include <tflm_c.h>

#include "network_tflite_data.h"


/* -----------------------------------------------------------------------------
 * TFLM_C to X-CUBE-AI helper functions
 * -----------------------------------------------------------------------------
 */

#include "ai_platform.h"


ai_buffer_format set_ai_buffer_format(TfLiteType tflm_type)
{
  switch (tflm_type) {
    case kTfLiteFloat32: return AI_BUFFER_FORMAT_FLOAT;
    case kTfLiteUInt8: return AI_BUFFER_FORMAT_U8;
    case kTfLiteInt8: return AI_BUFFER_FORMAT_S8;
    case kTfLiteUInt32: return AI_BUFFER_FORMAT_U32;
    case kTfLiteInt32: return AI_BUFFER_FORMAT_S32;
    case kTfLiteBool: return AI_BUFFER_FORMAT_BOOL;
    default: return AI_BUFFER_FORMAT_NONE;
  }
}

void set_ai_buffer(struct tflm_c_tensor_info *from, ai_buffer* to)
{
  struct tflm_c_buffer *to_ = (struct tflm_c_buffer *)to;
  to_->buffer.n_batches = (ai_u16)from->batch;
  to_->buffer.height = (ai_u16)from->height;
  to_->buffer.width = (ai_u16)from->width;
  to_->buffer.channels = (ai_u16)from->channels;
  to_->buffer.data = (ai_handle)from->data;
  to_->buffer.format = set_ai_buffer_format(from->type);
  to_->buffer.meta_info = NULL;

  if (from->scale) {
    to_->buffer.meta_info = (ai_buffer_meta_info*)&to_->extra;
    to_->extra.scale = from->scale;
    to_->extra.zero_point = from->zero_point;
  } else {
    to_->buffer.meta_info = NULL;
    to_->extra.scale = 0.0f;
    to_->extra.zero_point = 0;
  }
}

void set_ai_network_report(uint32_t tflm_c_hdl, ai_network_report* report)
{
  const char *_null = "NULL";
  ai_platform_version _version;
  const ai_buffer _def_buffer = { AI_BUFFER_FORMAT_U8, 1, 1, 1, 1, NULL, NULL };
  struct tflm_c_version ver;
  tflm_c_rt_version(&ver);
  _version.major = ver.major;
  _version.minor = ver.minor;
  _version.micro = ver.patch;
  _version.reserved = ver.schema;

  report->model_name = "network";
  report->model_signature = _null;
  report->model_datetime = _null;
  report->compile_datetime =  __DATE__ " " __TIME__;
  report->runtime_revision = _null;
  report->tool_revision = TFLM_C_VERSION_STR;

  report->runtime_version = _version;
  report->tool_version = _version;
  report->tool_api_version = _version;
  report->api_version = _version;
  report->interface_api_version = _version;

  report->n_macc = 1;

  report->n_inputs = (ai_u16)tflm_c_inputs_size(tflm_c_hdl);
  report->inputs = (ai_buffer *)malloc(report->n_inputs * sizeof(struct tflm_c_buffer));
  for (int idx=0; idx<report->n_inputs; idx++) {
    struct tflm_c_tensor_info info;
    tflm_c_input(tflm_c_hdl, idx, &info);
    set_ai_buffer(&info, &report->inputs[idx]);
  }

  report->n_outputs = (ai_u16)tflm_c_outputs_size(tflm_c_hdl);
  report->outputs = (ai_buffer *)malloc(report->n_outputs * sizeof(struct tflm_c_buffer));
  for (int idx=0; idx<report->n_outputs; idx++) {
    struct tflm_c_tensor_info info;
    tflm_c_output(tflm_c_hdl, idx, &info);
    set_ai_buffer(&info, &report->outputs[idx]);
  }

  report->n_nodes = (ai_u32)tflm_c_operators_size(tflm_c_hdl);
  report->signature = (ai_signature)0;

  report->activations = _def_buffer;
  report->activations.channels = (int)tflm_c_arena_used_bytes(tflm_c_hdl);
  report->params = _def_buffer;
  report->params.channels = (int)g_tflm_network_model_data_len;
}


/* -----------------------------------------------------------------------------
 * TEST-related definitions
 * -----------------------------------------------------------------------------
 */
#define _APP_VERSION_MAJOR_  (0x02)
#define _APP_VERSION_MINOR_  (0x01)
#define _APP_VERSION_        ((_APP_VERSION_MAJOR_ << 8) | _APP_VERSION_MINOR_)

#define _APP_NAME_           "AI Validation TFLM"


/* -----------------------------------------------------------------------------
 * Object definition/declaration for AI-related execution context
 * -----------------------------------------------------------------------------
 */

struct tflm_context {
  uint32_t hdl;
  ai_network_report report;
  int error;
#if defined(USE_OBSERVER) && USE_OBSERVER == 1
  bool obs_is_enabled;
  bool obs_no_data;
  ai_u32 obs_cb_out;
  const reqMsg *creq;             /* reference of the current PB request */
  respMsg *cresp;                 /* reference of the current PB response */
#endif
} net_exec_ctx[1] = {0};


/* Local activations buffer */
MEM_ALIGNED(16)
static uint8_t tensor_arena[TFLM_NETWORK_TENSOR_AREA_SIZE+32];

extern UART_HandleTypeDef UartHandle;

#ifdef __cplusplus
extern "C"
{
#endif

int tflm_io_write(const void *buff, uint16_t count)
{
    HAL_StatusTypeDef status;

    status = HAL_UART_Transmit(&UartHandle, (uint8_t *)buff, count,
            HAL_MAX_DELAY);

    return (status == HAL_OK ? count : 0);
}

#ifdef __cplusplus
}
#endif

void log_tensor(struct tflm_c_tensor_info* t_info, int idx)
{
  printf(" - %d:%s:%d:(%d, %d, %d, %d)", idx,
      tflm_c_TfLiteTypeGetName(t_info->type), (int)t_info->bytes,
      (int)t_info->batch, (int)t_info->height,
      (int)t_info->width, (int)t_info->channels);
  if (t_info->scale)
    printf(":s=%f:zp=%d\r\n", t_info->scale, t_info->zero_point);
  else
    printf("\r\n");
}


#if defined(USE_OBSERVER) && USE_OBSERVER == 1

static uint64_t _current_time_ticks_cb(int mode)
{
  uint64_t val = cyclesCounterEnd();
  if (mode)
    cyclesCounterStart();
  return val;
}

static int _observer_node_cb(const void* cookie,
    const uint32_t flags,
    const struct tflm_c_node* node)
{
  uint32_t type, n_type;

  struct tflm_context *ctx = (struct tflm_context*)cookie;
  ctx->obs_cb_out++;

  if (ctx->obs_cb_out == ctx->report.n_nodes) {
    type = EnumLayerType_LAYER_TYPE_INTERNAL_LAST;
    ctx->obs_cb_out = 0;
  }
  else
    type = EnumLayerType_LAYER_TYPE_INTERNAL;

  type = type << 16;

  if (ctx->obs_no_data)
    type |= PB_BUFFER_TYPE_SEND_WITHOUT_DATA;

  type |= ((ai_u16)node->node_info.builtin_code & (ai_u16)0x7FFF);

  if (node->node_info.n_outputs) {
    for (int i=0; i<node->node_info.n_outputs; i++) {
      ai_buffer buffer;
      ai_float scale = 0.0f;
      ai_i32 zero_point = 0;

      const struct tflm_c_tensor_info* t_info = &node->output[i];

      buffer.format = set_ai_buffer_format(t_info->type);
      buffer.n_batches = 1;
      buffer.data = (ai_handle)t_info->data;
      buffer.height = t_info->height;
      buffer.width = t_info->width;
      buffer.channels = t_info->channels;
      buffer.meta_info = NULL;

      if (t_info->scale) {
        scale = (ai_float)t_info->scale;
        zero_point = (ai_i32)t_info->zero_point;
      }

      if (i < (node->node_info.n_outputs - 1)) {
        n_type = type | (EnumLayerType_LAYER_TYPE_INTERNAL_DATA_NO_LAST << 16);
      } else {
        n_type = type;
      }

      aiPbMgrSendAiBuffer4(ctx->creq, ctx->cresp, EnumState_S_PROCESSING,
          n_type,
          node->node_info.idx,
          dwtCyclesToFloatMs(node->node_info.dur),
          &buffer,
          scale, zero_point);
    }
  }

  return 0;
}

struct tflm_c_observer_options _observer_options = {
  .notify = _observer_node_cb,
  .get_time = _current_time_ticks_cb,
  .cookie = NULL,
  .flags = OBSERVER_FLAGS_DEFAULT,
};

static TfLiteStatus observer_init(struct tflm_context *ctx)
{
  TfLiteStatus res;

  if ((ctx->hdl == 0) || (ctx->obs_is_enabled == false))
    return kTfLiteOk;

  if (_observer_options.cookie)
      return kTfLiteError;

  _observer_options.cookie = ctx;

  res = tflm_c_observer_register(ctx->hdl, &_observer_options);
  if (res != kTfLiteOk) {
    _observer_options.cookie = NULL;
    return kTfLiteError;
  }
  return kTfLiteOk;
}

static void observer_done(struct tflm_context *ctx)
{
  struct tflm_c_profile_info p_info;

  if ((ctx->hdl == 0) || (ctx->obs_is_enabled == false))
    return;

  tflm_c_observer_info(ctx->hdl, &p_info);

  if (_observer_options.cookie) {
    _observer_options.cookie = NULL;
    tflm_c_observer_unregister(ctx->hdl, &_observer_options);
  }
}

#endif


static uint64_t aiObserverAdjustInferenceTime(struct tflm_context *ctx,
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
  if (ctx->obs_is_enabled) {
    struct tflm_c_profile_info p_info;
    tflm_c_observer_info(ctx->hdl, &p_info);
    tend = p_info.cb_dur + p_info.node_dur + tend;
  }
#endif
  return tend;
}

static void aiObserverSendReport(const reqMsg *req, respMsg *resp,
    EnumState state, struct tflm_context *ctx,
    const ai_float dur_ms)
{
#if defined(USE_OBSERVER) && USE_OBSERVER == 1
  if ((ctx->hdl == 0) || (ctx->obs_is_enabled == false))
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

static int aiObserverConfig(struct tflm_context *ctx,
    const reqMsg *req)
{
#if defined(USE_OBSERVER) && USE_OBSERVER == 1
  ctx->obs_no_data = false;
  ctx->obs_is_enabled = false;
  ctx->obs_cb_out = 0;
  if ((req->param & EnumRunParam_P_RUN_MODE_INSPECTOR) ==
      EnumRunParam_P_RUN_MODE_INSPECTOR)
    ctx->obs_is_enabled = true;
  if ((req->param & EnumRunParam_P_RUN_MODE_INSPECTOR_WITHOUT_DATA) ==
      EnumRunParam_P_RUN_MODE_INSPECTOR_WITHOUT_DATA) {
    ctx->obs_is_enabled = true;
    ctx->obs_no_data = true;
  }
#endif
  return 0;
}

static int aiObserverBind(struct tflm_context *ctx,
    const reqMsg *creq, respMsg *cresp)
{
#if defined(USE_OBSERVER) && USE_OBSERVER == 1
  if (observer_init(ctx) == kTfLiteError)
    ctx->obs_is_enabled = false;
  ctx->creq = creq;
  ctx->cresp = cresp;
#endif
  return 0;
}

static int aiObserverUnbind(struct tflm_context *ctx)
{
#if defined(USE_OBSERVER) && USE_OBSERVER == 1
  observer_done(ctx);
#endif
  return 0;
}


/* -----------------------------------------------------------------------------
 * AI-related functions
 * -----------------------------------------------------------------------------
 */

static int aiBootstrap(struct tflm_context *ctx)
{
  TfLiteStatus res;
  struct tflm_c_version ver;

  /* Creating an instance of the network ------------------------- */
  printf("\r\nInstancing the network (TFLM)..\r\n");

  /* TFLm runtime expects that the tensor arena is aligned on 16-bytes */
  uint32_t uaddr = (uint32_t)tensor_arena;
  uaddr = (uaddr + (16 - 1)) & (uint32_t)(-16);  // Round up to 16-byte boundary

  MON_ALLOC_RESET();
  MON_ALLOC_ENABLE();

  res = tflm_c_create(g_tflm_network_model_data, (uint8_t*)uaddr,
          TFLM_NETWORK_TENSOR_AREA_SIZE, &ctx->hdl);

  MON_ALLOC_DISABLE();

  if (res != kTfLiteOk) {
    ctx->hdl = 0;
    ctx->error = -1;
    return -1;
  }

  set_ai_network_report(ctx->hdl, &ctx->report);

  tflm_c_rt_version(&ver);

  printf(" TFLM version       : %d.%d.%d\r\n", (int)ver.major, (int)ver.minor, (int)ver.patch);
  printf(" TFLite file        : 0x%08x\r\n", (int)g_tflm_network_model_data);
  printf(" Arena location     : 0x%08x\r\n", (int)uaddr);
  printf(" Operator size      : %d\r\n", (int)tflm_c_operators_size(ctx->hdl));
  printf(" Tensor size        : %d\r\n", (int)tflm_c_tensors_size(ctx->hdl));
  printf(" Allocated size     : %d / %d\r\n", (int)tflm_c_arena_used_bytes(ctx->hdl),
      TFLM_NETWORK_TENSOR_AREA_SIZE);
  printf(" Inputs size        : %d\r\n", (int)tflm_c_inputs_size(ctx->hdl));
  for (int i=0; i<tflm_c_inputs_size(ctx->hdl); i++) {
    struct tflm_c_tensor_info t_info;
    tflm_c_input(ctx->hdl, i, &t_info);
    log_tensor(&t_info, i);
  }
  printf(" Outputs size       : %d\r\n", (int)tflm_c_outputs_size(ctx->hdl));
  for (int i=0; i<tflm_c_outputs_size(ctx->hdl); i++) {
    struct tflm_c_tensor_info t_info;
    tflm_c_output(ctx->hdl, i, &t_info);
    log_tensor(&t_info, i);
  }
#if defined(_APP_HEAP_MONITOR_) && _APP_HEAP_MONITOR_ == 1
  printf(" Used heap          : %d bytes (max=%d bytes) (for c-wrapper)\r\n",
      MON_ALLOC_USED(), MON_ALLOC_MAX_USED());
  // MON_ALLOC_REPORT();
#endif

  return 0;
}

static void aiDone(struct tflm_context *ctx)
{
  /* Releasing the instance(s) ------------------------------------- */
  printf("Releasing the instance...\r\n");

  if (ctx->hdl != 0) {
    free(ctx->report.inputs);
    free(ctx->report.outputs);
    tflm_c_destroy(ctx->hdl);
    ctx->hdl = 0;
  }
}

static int aiInit(void)
{
  int res;

  net_exec_ctx[0].hdl = 0;
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

  if (net_exec_ctx[0].hdl && req->param == 0)
    aiPbMgrSendNNInfo(req, resp, EnumState_S_IDLE,
        &net_exec_ctx[0].report);
  else {
    if (req->param > 0)
      aiPbMgrSendAck(req, resp, EnumState_S_ERROR,
                EnumError_E_INVALID_PARAM, EnumError_E_INVALID_PARAM);
    else
      aiPbMgrSendAck(req, resp, EnumState_S_ERROR,
          net_exec_ctx[0].error, EnumError_E_GENERIC);
  }
}

void aiPbCmdNNRun(const reqMsg *req, respMsg *resp, void *param)
{
  TfLiteStatus tflm_res;
  uint64_t tend;
  bool res;
  UNUSED(param);

  struct tflm_context *ctx = &net_exec_ctx[0];

  /* 0 - Check if requested c-name model is available -------------- */
  if ((ctx->hdl == 0) ||
      (strncmp(ctx->report.model_name, req->name,
          strlen(ctx->report.model_name)) != 0)) {
    aiPbMgrSendAck(req, resp, EnumState_S_ERROR,
        EnumError_E_INVALID_PARAM, EnumError_E_INVALID_PARAM);
    return;
  }

  aiObserverConfig(ctx, req);

  /* 1 - Send a ACK (ready to receive a tensor) -------------------- */
  aiPbMgrSendAck(req, resp, EnumState_S_WAITING,
      aiPbAiBufferSize(&ctx->report.inputs[0]), EnumError_E_NONE);

  /* 2 - Receive all input tensors --------------------------------- */
  for (int i = 0; i < ctx->report.n_inputs; i++) {
    /* upload a buffer */
    EnumState state = EnumState_S_WAITING;
    if ((i + 1) == ctx->report.n_inputs)
      state = EnumState_S_PROCESSING;
    res = aiPbMgrReceiveAiBuffer3(req, resp, state, &ctx->report.inputs[i]);
    if (res != true)
      return;
  }

  aiObserverBind(ctx, req, resp);

  /* 3 - Processing ------------------------------------------------ */

  cyclesCounterStart();
  tflm_res = tflm_c_invoke(ctx->hdl);
  tend = cyclesCounterEnd();

  if (tflm_res != kTfLiteOk) {
    printf("E: tflm_c_invoke() fails\r\n");
    aiPbMgrSendAck(req, resp, EnumState_S_ERROR,
        EnumError_E_GENERIC, EnumError_E_GENERIC);
    return;
  }

  tend = aiObserverAdjustInferenceTime(ctx, tend);

  /* 4 - Send basic report (optional) ------------------------------ */
  aiObserverSendReport(req, resp, EnumState_S_PROCESSING, ctx,
      dwtCyclesToFloatMs(tend));

  /* 5 - Send all output tensors ----------------------------------- */
  for (int i = 0; i < ctx->report.n_outputs; i++) {
    EnumState state = EnumState_S_PROCESSING;
    struct tflm_c_tensor_info t_info;
    tflm_c_output(ctx->hdl, i, &t_info);
    if ((i + 1) == ctx->report.n_outputs)
      state = EnumState_S_DONE;
    aiPbMgrSendAiBuffer4(req, resp, state,
        EnumLayerType_LAYER_TYPE_OUTPUT << 16 | 0,
        0, dwtCyclesToFloatMs(tend),
        &ctx->report.outputs[i],
        t_info.scale,
        t_info.zero_point);
  }

  aiObserverUnbind(ctx);
}

static aiPbCmdFunc pbCmdFuncTab[] = {
#if defined(USE_OBSERVER) && USE_OBSERVER == 1
    AI_PB_CMD_SYNC((void *)(EnumCapability_CAP_INSPECTOR | (EnumAiRuntime_AI_RT_TFLM << 16))),
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
  if (r)
    printf("aiInit() fails with r=%d\r\n", r);

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

