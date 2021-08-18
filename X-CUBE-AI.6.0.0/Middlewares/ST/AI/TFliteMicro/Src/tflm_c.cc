/**
 ******************************************************************************
 * @file    tflm_c.cc.h
 * @author  MCD/AIS Vertical Application Team
 * @brief   Light C-wrapper for TFLight micro runtime
 ******************************************************************************
 * @attention
 *
 * <h2><center>&copy; Copyright (c) YYYY STMicroelectronics.
 * All rights reserved.</center></h2>
 *
 * This software component is licensed by ST under Ultimate Liberty license
 * SLA0044, the "License"; You may not use this file except in compliance with
 * the License. You may obtain a copy of the License at:
 *                             www.st.com/SLA0044
 *
 ******************************************************************************
 */

/*
 * This file provides a C-wrapper functions to facilitate the usage of TFLm library
 * in a C-project. Only C++ libs/c-start-up libraries are requested to generate the final
 * application firmware.
 * - Simplified version of the tensorflow/tensorflow/c/c_api.h proposal.
 *
 * Notes: (implementation consideration)
 * - when an instance of the interpreter is created, a context object is created,
 *   dynamically allocated (see tflm_c_create() fct). ~136bytes.
 *   This allocation is done, through the new C++ operator. To be able to
 *   monitor the usage of the heap, new operator is expected to be
 *   based on the C-malloc/free functions.
 */

#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/kernels/micro_ops.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/version.h"
#include "tensorflow/lite/micro/micro_allocator.h"

#include "tensorflow/lite/micro/micro_optional_debug_tools.h"

#include "tflm_c.h"

#define _MULTIPLE_NODE_OUTPUTS_SUPPORT (0)

#if !defined(TFLM_RUNTIME_USE_ALL_OPERATORS)
#define TFLM_RUNTIME_USE_ALL_OPERATORS 1
#endif

// forward declaration
class CTfLiteInterpreterContext;


class CTfLiteProfiler : public tflite::Profiler {
public:
  CTfLiteProfiler(CTfLiteInterpreterContext* interp) : ctx_(interp), options_(nullptr),
    event_starts_(0), event_ends_(0) {}
  ~CTfLiteProfiler() override = default;

  // AddEvent is unused for Tf Micro.
  void AddEvent(const char* tag, EventType event_type, uint64_t start,
      uint64_t end, int64_t event_metadata1,
      int64_t event_metadata2) override{};

  // BeginEvent followed by code followed by EndEvent will profile the code
  // enclosed. Multiple concurrent events are unsupported, so the return value
  // is always 0. Event_metadata1 and event_metadata2 are unused. The tag
  // pointer must be valid until EndEvent is called.
  // `tag`             name of the operator
  // `EventType`       OPERATOR_INVOKE_EVENT
  // `event_metadata1` represents the index of a TFLite node, and
  // `event_metadata2` represents the index of the subgraph that this event
  // OPERATOR_INVOKE_EVENT : The event is an operator invocation and
  //   the event_metadata field is the index of operator node.
  uint32_t BeginEvent(const char* tag, EventType event_type,
      int64_t event_metadata1,
      int64_t event_metadata2) override {
    if (options_)
      return begin_event(tag, event_type, event_metadata1, event_metadata2);
    else
      return 0;
  }

  // Event_handle is ignored since TF Micro does not support concurrent events.
  void EndEvent(uint32_t event_handle) override {
    if (options_)
      end_event(event_handle);
  }

  TfLiteStatus register_cb(struct tflm_c_observer_options* options) {
    /* a get_time cb is mandatory to use the profiler */
    if (options_ || options == nullptr || options->get_time == nullptr)
      return kTfLiteError;
    options_ = options;
    start();
    return kTfLiteOk;
  }

  TfLiteStatus unregister_cb(struct tflm_c_observer_options* options) {
    if (options_ == options) {
      options_ = nullptr;
      return kTfLiteOk;
    }
    return kTfLiteError;
  }

  TfLiteStatus start(void) {
    cb_dur_ = 0;
    node_dur_ = 0;
    node_idx_ = -1;
    node_ts_begin_ = 0;
    event_starts_ = 0;
    event_ends_ = 0;
    return kTfLiteOk;
  }

  TfLiteStatus info(struct tflm_c_profile_info* p_info) {
    if (!p_info) {
      return kTfLiteError;
    }
    p_info->cb_dur = cb_dur_;
    p_info->node_dur = node_dur_;
    p_info->n_events = event_starts_;
    return kTfLiteOk;
  }

protected:
  uint32_t begin_event(const char* tag, EventType event_type,
      int64_t event_metadata1,
      int64_t event_metadata2);

  void end_event(uint32_t event_handle);

  int event_starts() { return event_starts_; }
  int event_ends() { return event_ends_; }

private:
  CTfLiteInterpreterContext* ctx_;
  struct tflm_c_observer_options* options_;
  int event_starts_;
  int event_ends_;
  uint32_t node_ts_begin_;
  const char* node_tag_;
  int32_t node_idx_;
  uint64_t cb_dur_;
  uint64_t node_dur_;

  TF_LITE_REMOVE_VIRTUAL_DELETE
};


class CTfLiteInterpreterContext {
public:
  CTfLiteInterpreterContext(const tflite::Model* model,
      const tflite::MicroOpResolver& op_resolver,
      uint8_t* tensor_arena, size_t tensor_arena_size,
      tflite::ErrorReporter* error_reporter): profiler(this), interpreter(model, op_resolver,
          tensor_arena, tensor_arena_size, error_reporter, &profiler) {}

  CTfLiteProfiler profiler;
  tflite::MicroInterpreter interpreter;
  int n_invoks;

  static TfLiteStatus input(const uint32_t hdl, int32_t index, struct tflm_c_tensor_info* t_info) {
    CTfLiteInterpreterContext *ctx = reinterpret_cast<CTfLiteInterpreterContext *>(hdl);
    const TfLiteTensor* tens = ctx->interpreter.input(index);
    return ctx->tflitetensor_to(tens, t_info);
  }

  static TfLiteStatus output(const uint32_t hdl, int32_t index, struct tflm_c_tensor_info* t_info) {
    CTfLiteInterpreterContext *ctx = reinterpret_cast<CTfLiteInterpreterContext *>(hdl);
    const TfLiteTensor* tens = ctx->interpreter.output(index);
    return ctx->tflitetensor_to(tens, t_info);
  }

  static TfLiteStatus invoke(const uint32_t hdl) {
    CTfLiteInterpreterContext *ctx = reinterpret_cast<CTfLiteInterpreterContext *>(hdl);
    ctx->n_invoks++;
    return ctx->interpreter.Invoke();
  }

  static TfLiteStatus observer_register(const uint32_t hdl, struct tflm_c_observer_options* options)
  {
    CTfLiteInterpreterContext *ctx = reinterpret_cast<CTfLiteInterpreterContext *>(hdl);
    return ctx->profiler.register_cb(options);
  }

  static TfLiteStatus observer_unregister(const uint32_t hdl, struct tflm_c_observer_options* options)
  {
    CTfLiteInterpreterContext *ctx = reinterpret_cast<CTfLiteInterpreterContext *>(hdl);
    return ctx->profiler.unregister_cb(options);
  }

  static TfLiteStatus observer_start(const uint32_t hdl)
  {
    CTfLiteInterpreterContext *ctx = reinterpret_cast<CTfLiteInterpreterContext *>(hdl);
    ctx->n_invoks = 0;
    return ctx->profiler.start();
 }

  static TfLiteStatus observer_info(const uint32_t hdl, struct tflm_c_profile_info* p_info)
  {
    CTfLiteInterpreterContext *ctx = reinterpret_cast<CTfLiteInterpreterContext *>(hdl);
    TfLiteStatus res = ctx->profiler.info(p_info);
    if (res == kTfLiteOk)
      p_info->n_invoks = ctx->n_invoks;
    return res;
  }

  uint32_t get_handle() {
    return (uint32_t)this;
  }

private:
  TfLiteStatus tflitetensor_to(const TfLiteTensor* tfls, struct tflm_c_tensor_info* t_info, int32_t idx=-1);

protected:
 friend class CTfLiteProfiler;
};


TfLiteStatus CTfLiteInterpreterContext::tflitetensor_to(const TfLiteTensor* tft,
    struct tflm_c_tensor_info* ti, int32_t idx)
{
  if (!tft || !ti)
    return kTfLiteError;

  ti->type = tft->type;
  ti->idx = idx;
  ti->bytes = tft->bytes;

  if (tft->dims->size == 2) { /* 1d array */
      ti->height = 1;
      ti->width = 1;
      ti->batch = tft->dims->data[0];
      ti->channels = tft->dims->data[1];
  } else if (tft->dims->size == 3) { /* 2d array */
      ti->batch = tft->dims->data[0];
      ti->height = tft->dims->data[1];
      ti->width = 1;
      ti->channels = tft->dims->data[2];
  } else if (tft->dims->size == 4) { /* 3d array */
      ti->batch = tft->dims->data[0];
      ti->height = tft->dims->data[1];
      ti->width =  tft->dims->data[2];
      ti->channels = tft->dims->data[3];
  } else {
      return kTfLiteError;
  }

  ti->data = (void *)tft->data.uint8;

  if (tft->quantization.type == kTfLiteAffineQuantization) {
      TfLiteAffineQuantization *quant = (TfLiteAffineQuantization *)tft->quantization.params;
      ti->scale = quant->scale->data;
      ti->zero_point = quant->zero_point->data;
  } else {
      ti->scale = nullptr;
      ti->zero_point = nullptr;
  }

  return kTfLiteOk;
}

static tflite::MicroErrorReporter micro_error_reporter;


#ifdef __cplusplus
extern "C" {
#endif

TfLiteStatus tflm_c_create(const uint8_t *model_data,
    uint8_t *tensor_arena,
    const uint32_t tensor_arena_size,
    uint32_t *hdl)
{
  TfLiteStatus status;

  if (!hdl || !tensor_arena_size || !tensor_arena || !model_data)
    return kTfLiteError;

  *hdl = 0;

  const tflite::Model* model = ::tflite::GetModel(model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    printf("E: invalid expected TFLite model version %d instead %d\r\n",
        (int)model->version(), (int)TFLITE_SCHEMA_VERSION);
    return kTfLiteError;
  }

#if defined(TFLM_RUNTIME_USE_ALL_OPERATORS) && TFLM_RUNTIME_USE_ALL_OPERATORS == 1
  static tflite::AllOpsResolver _resolver;
#else
  static tflite::MicroMutableOpResolver<5> _resolver;
  _resolver.AddConv2D();
  _resolver.AddDepthwiseConv2D();
  _resolver.AddFullyConnected();
  _resolver.AddSoftmax();
  _resolver.AddMaxPool2D();
#endif

  CTfLiteInterpreterContext *ctx = new CTfLiteInterpreterContext(
      model,
      _resolver,
      tensor_arena,
      tensor_arena_size,
      &micro_error_reporter
  );

  // Allocate the resources
  status = ctx->interpreter.AllocateTensors();
  if (status != kTfLiteOk) {
    printf("E: unable to allocate the tensors (%d bytes is not sufficient)\r\n", (int)tensor_arena_size);
    delete ctx;
    return kTfLiteError;
  }

  *hdl = ctx->get_handle();

  return kTfLiteOk;
}

TfLiteStatus tflm_c_destroy(uint32_t hdl)
{
  CTfLiteInterpreterContext *ctx = reinterpret_cast<CTfLiteInterpreterContext *>(hdl);
  delete ctx;
  return kTfLiteOk;
}

int32_t tflm_c_inputs_size(const uint32_t hdl)
{
  CTfLiteInterpreterContext *ctx = reinterpret_cast<CTfLiteInterpreterContext *>(hdl);
  return ctx->interpreter.inputs_size();
}

int32_t tflm_c_outputs_size(const uint32_t hdl)
{
  CTfLiteInterpreterContext *ctx = reinterpret_cast<CTfLiteInterpreterContext *>(hdl);
  return ctx->interpreter.outputs_size();
}

TfLiteStatus tflm_c_input(const uint32_t hdl, int32_t index, struct tflm_c_tensor_info *t_info)
{
  return CTfLiteInterpreterContext::input(hdl, index, t_info);
}

TfLiteStatus tflm_c_output(const uint32_t hdl, int32_t index, struct tflm_c_tensor_info *t_info)
{
  return CTfLiteInterpreterContext::output(hdl, index, t_info);
}

TfLiteStatus tflm_c_invoke(const uint32_t hdl)
{
  return CTfLiteInterpreterContext::invoke(hdl);
}

int32_t tflm_c_operators_size(const uint32_t hdl)
{
  CTfLiteInterpreterContext *ctx = reinterpret_cast<CTfLiteInterpreterContext *>(hdl);
  return ctx->interpreter.operators_size();
}

int32_t tflm_c_tensors_size(const uint32_t hdl)
{
  CTfLiteInterpreterContext *ctx = reinterpret_cast<CTfLiteInterpreterContext *>(hdl);
  return ctx->interpreter.tensors_size();
}

int32_t tflm_c_arena_used_bytes(const uint32_t hdl)
{
  CTfLiteInterpreterContext *ctx = reinterpret_cast<CTfLiteInterpreterContext *>(hdl);
  return ctx->interpreter.arena_used_bytes();
}

const char* tflm_c_TfLiteTypeGetName(TfLiteType type)
{
  return TfLiteTypeGetName(type);
}

void tflm_c_rt_version(struct tflm_c_version *version)
{
  if (version) {
    version->major = TF_MAJOR_VERSION;
    version->minor = TF_MINOR_VERSION;
    version->patch = TF_PATCH_VERSION;
    version->schema = TFLITE_SCHEMA_VERSION;
  }

}

TfLiteStatus tflm_c_observer_register(const uint32_t hdl, struct tflm_c_observer_options* options)
{
  return CTfLiteInterpreterContext::observer_register(hdl, options);
}

TfLiteStatus tflm_c_observer_unregister(const uint32_t hdl, struct tflm_c_observer_options* options)
{
  return CTfLiteInterpreterContext::observer_unregister(hdl, options);
}

TfLiteStatus tflm_c_observer_start(const uint32_t hdl)
{
  return CTfLiteInterpreterContext::observer_start(hdl);
}

TfLiteStatus tflm_c_observer_info(const uint32_t hdl, struct tflm_c_profile_info* p_info)
{
  return CTfLiteInterpreterContext::observer_info(hdl, p_info);
}

uint32_t CTfLiteProfiler::begin_event(const char* tag, EventType event_type,
                    int64_t event_metadata1,
                    int64_t event_metadata2)
{
  volatile uint32_t ts = options_->get_time();
  event_starts_++;
  node_tag_ = tag;
  node_idx_ = (int32_t)event_metadata1;
  node_ts_begin_ = options_->get_time();
  cb_dur_ += (node_ts_begin_ - ts);
  return 0;
}

void CTfLiteProfiler::end_event(uint32_t event_handle)
{
  volatile uint32_t ts = options_->get_time();
  event_ends_++;
  ts = options_->get_time();

  if (options_->notify) {
    struct tflm_c_node node;
    const tflite::NodeAndRegistration node_imp = ctx_->interpreter.node_and_registration(node_idx_);
    node.node_info.name = node_tag_;
    node.node_info.idx = node_idx_;
    node.node_info.dur = ts - node_ts_begin_;
    node_dur_ += node.node_info.dur;
    if (node_imp.node.outputs && ((options_->flags & OBSERVER_FLAGS_TIME_ONLY) == 0)) {
#if defined(_MULTIPLE_NODE_OUTPUTS_SUPPORT) && _MULTIPLE_NODE_OUTPUTS_SUPPORT == 1
      const int n_ouputs = node_imp.node.outputs->size;
      struct tflm_c_tensor_info t_info[n_ouputs];
      node.node_info.n_outputs = n_ouputs;
      for (int i=0; i<n_ouputs; i++) {
        const int ts_idx = node_imp.node.outputs->data[i];
        TfLiteTensor* tens = ctx_->interpreter.tensor(ts_idx);
        ctx_->tflitetensor_to(tens, &t_info[i], ts_idx);
      }
      node.output = &t_info[0];
#else
      struct tflm_c_tensor_info t_info;
      TfLiteTensor* tens = ctx_->interpreter.tensor(0);
      ctx_->tflitetensor_to(tens, &t_info, 0);
      node.node_info.n_outputs = 1;
      node.output = &t_info;
#endif
      options_->notify(options_->cookie, options_->flags, &node);
    }
    else {
      node.node_info.n_outputs = 0;
      node.output = nullptr;
      options_->notify(options_->cookie, options_->flags, &node);
    }
  }
  cb_dur_ += (options_->get_time() - ts);
}

#ifdef __cplusplus
}
#endif
