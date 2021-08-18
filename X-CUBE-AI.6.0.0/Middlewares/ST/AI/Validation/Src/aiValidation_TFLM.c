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
 * - Entry points) for the AI validation, TFLM runtime
 *
 * History:
 *  - v1.0 - Initial version. Use the X-CUBE-AI protocol vX.X
 *           Emulate X-CUBE-AI interface
 *  - v1.1 - minor - let irq enabled if USB CDC is used
 */

/* System headers */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>

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
    to_->extra.scale = *from->scale;
    to_->extra.zero_point = *from->zero_point;
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
  const ai_buffer _def_buffer = { AI_BUFFER_FORMAT_U8, 1, 1, 2, 1, NULL, NULL };
  struct tflm_c_version ver;
  tflm_c_rt_version(&ver);
  _version.major = ver.major;
  _version.minor = ver.minor;
  _version.micro = ver.patch;
  _version.reserved = ver.schema;

  report->model_name = "network";
  report->model_signature = _null;
  report->model_datetime = _null;
  report->compile_datetime = _null;
  report->runtime_revision = _null;
  report->tool_revision = "TFLM";

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
}


/* -----------------------------------------------------------------------------
 * TEST-related definitions
 * -----------------------------------------------------------------------------
 */
#define _APP_VERSION_MAJOR_     (0x01)
#define _APP_VERSION_MINOR_     (0x00)
#define _APP_VERSION_   ((_APP_VERSION_MAJOR_ << 8) | _APP_VERSION_MINOR_)

#define _APP_NAME_   "AI Validation (TFLM)"


/* -----------------------------------------------------------------------------
 * Object definition/declaration for AI-related execution context
 * -----------------------------------------------------------------------------
 */

struct tflm_context {
  uint32_t hdl;
  ai_network_report report;
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

static uint64_t aiObserverAdjustInferenceTime(struct tflm_context *ctx,
    uint64_t tend)
{
  return tend;
}

static void aiObserverSendReport(const reqMsg *req, respMsg *resp,
    EnumState state, struct tflm_context *ctx,
    const ai_float dur_ms)
{

}

static int aiObserverConfig(struct tflm_context *ctx,
    const reqMsg *req)
{
  return 0;
}

static int aiObserverBind(struct tflm_context *ctx,
    const reqMsg *creq, respMsg *cresp)
{
  return 0;
}

static int aiObserverUnbind(struct tflm_context *ctx)
{
  return 0;
}


/* -----------------------------------------------------------------------------
 * AI-related functions
 * -----------------------------------------------------------------------------
 */

static int aiBootstrap(struct tflm_context *ctx)
{
  TfLiteStatus res;

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

  set_ai_network_report(ctx->hdl, &ctx->report);

  if (res != kTfLiteOk) {
    return -1;
  }

  printf(" Operator size      : %d\r\n", (int)tflm_c_operators_size(ctx->hdl));
  printf(" Tensor size        : %d\r\n", (int)tflm_c_tensors_size(ctx->hdl));
  printf(" Allocated size     : %d / %d\r\n", (int)tflm_c_arena_used_bytes(ctx->hdl),
      TFLM_NETWORK_TENSOR_AREA_SIZE);
  printf(" Inputs size        : %d\r\n", (int)tflm_c_inputs_size(ctx->hdl));
  for (int i=0; i<tflm_c_inputs_size(ctx->hdl); i++) {
    struct tflm_c_tensor_info t_info;
    tflm_c_input(ctx->hdl, i, &t_info);
    printf("  %d: %s (%d bytes) (%d, %d, %d)", i, tflm_c_TfLiteTypeGetName(t_info.type),
        (int)t_info.bytes, (int)t_info.height, (int)t_info.width, (int)t_info.channels);
    if (t_info.scale)
      printf(" scale=%f, zp=%d\r\n", (float)*t_info.scale, (int)*t_info.zero_point);
    else
      printf("\r\n");
  }
  printf(" Outputs size       : %d\r\n", (int)tflm_c_outputs_size(ctx->hdl));
  for (int i=0; i<tflm_c_outputs_size(ctx->hdl); i++) {
    struct tflm_c_tensor_info t_info;
    tflm_c_output(ctx->hdl, i, &t_info);
    printf("  %d: %s (%d bytes) (%d, %d, %d)", i, tflm_c_TfLiteTypeGetName(t_info.type),
        (int)t_info.bytes, (int)t_info.height, (int)t_info.width, (int)t_info.channels);
    if (t_info.scale)
      printf(" scale=%f, zp=%d\r\n", (float)*t_info.scale, (int)*t_info.zero_point);
    else
      printf("\r\n");
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

  // aiPlatformVersion();

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
  else
    aiPbMgrSendAck(req, resp, EnumState_S_ERROR,
        EnumError_E_INVALID_PARAM, EnumError_E_INVALID_PARAM);
}

void aiPbCmdNNRun(const reqMsg *req, respMsg *resp, void *param)
{
  TfLiteStatus tflm_res;
  uint32_t tend;
  bool res;
#if !defined(USE_USB_CDC_CLASS)
  uint32_t ints;
#endif
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
#if !defined(USE_USB_CDC_CLASS)
  ints = disableInts();
#endif

  dwtReset();
  tflm_res = tflm_c_invoke(ctx->hdl);
  tend = dwtGetCycles();

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
        *(ai_float *)t_info.scale,
        *(ai_i32 *)t_info.zero_point);
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

