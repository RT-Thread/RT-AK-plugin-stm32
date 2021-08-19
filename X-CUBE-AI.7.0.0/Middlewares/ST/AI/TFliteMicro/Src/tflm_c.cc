/**
 ******************************************************************************
 * @file    tflm_c.cc
 * @author  AIS Team
 * @brief   Light C-wrapper for TFLight micro runtime
 ******************************************************************************
 * @attention
 *
 * <h2><center>&copy; Copyright (c) 2021 STMicroelectronics.
 * All rights reserved.</center></h2>
 *
 * This software component is licensed by ST under BSD 3-Clause license,
 * the "License"; You may not use this file except in compliance with the
 * License. You may obtain a copy of the License at:
 *                        opensource.org/licenses/BSD-3-Clause
 *
 ******************************************************************************
 */

/*
 * see header file
 *
 * Notes: (implementation consideration)
 * - when an instance of the interpreter is created, a context object (~1248 Bytes)
 *   is created, dynamically allocated (see tflm_c_create() fct).
 *   This allocation is done, through the new C++ operator. To be able to
 *   monitor the usage of the heap, new operator is expected to be
 *   based on the C-malloc/free functions.
 * - Default SimpleAllocator has been extended to avoid the memory leak. In the
 *   'EndEvent()' function, tensor object ('ctx_->interpreter.tensor(ts_idx)')
 *   is created as a persistent object (default behavior). This derived allocator
 *   allows to have the scoped allocations in the Temporary Section.
 */

#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/kernels/micro_ops.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/version.h"
#include "tensorflow/lite/micro/micro_allocator.h"
#include "tensorflow/lite/micro/micro_profiler.h"
#include "tensorflow/lite/micro/simple_memory_allocator.h"
#include "tensorflow/lite/micro//memory_helpers.h"

#include "tflm_c.h"

// enable (=1) the capability to manage multiple output tensors in observer CB
//  else only the first output tensor will be provided.
#define _MULTIPLE_NODE_OUTPUTS_SUPPORT (1)

// if (=0), resolver is created with all built-in operators
#if !defined(TFLM_RUNTIME_USE_ALL_OPERATORS)
#define TFLM_RUNTIME_USE_ALL_OPERATORS 1
#endif

// if enabled indicates the maximum number of output tensors
#if defined(_MULTIPLE_NODE_OUTPUTS_SUPPORT) and _MULTIPLE_NODE_OUTPUTS_SUPPORT == 1
#define _MAX_NODE_OUTPUTS_SUPPORT (10)
#endif

// forward declaration
class CTfLiteInterpreterContext;

class CTfLiteProfiler : public tflite::MicroProfiler {
public:
  CTfLiteProfiler(CTfLiteInterpreterContext* interp) : ctx_(interp), options_(nullptr),
    event_starts_(0), event_ends_(0) {}
  ~CTfLiteProfiler() override = default;

  uint32_t BeginEvent(const char* tag) override {
    if (options_)
      return begin_event(tag);
    else
      return 0;
  }

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

  TfLiteStatus reset(void) {
    node_idx_ = -1;
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
  uint32_t begin_event(const char* tag);

  void end_event(uint32_t event_handle);

  int event_starts() { return event_starts_; }
  int event_ends() { return event_ends_; }

private:
  CTfLiteInterpreterContext* ctx_;
  struct tflm_c_observer_options* options_;
  int event_starts_;
  int event_ends_;
  uint64_t node_ts_begin_;
  const char* node_tag_;
  int32_t node_idx_;
  uint64_t cb_dur_;
  uint64_t node_dur_;
#if defined(_MULTIPLE_NODE_OUTPUTS_SUPPORT) and _MULTIPLE_NODE_OUTPUTS_SUPPORT == 1
  struct tflm_c_tensor_info t_info_[_MAX_NODE_OUTPUTS_SUPPORT];
#endif

  TF_LITE_REMOVE_VIRTUAL_DELETE
};


class CTfLiteAllocator : public tflite::SimpleMemoryAllocator {
public:
  CTfLiteAllocator(tflite::ErrorReporter* error_reporter, uint8_t* buffer,
      size_t buffer_size): tflite::SimpleMemoryAllocator(error_reporter, buffer, buffer_size),
      tmp_mode(false) {}

  void SetTempMode(void) {
    tmp_mode = true;
  };

  void ClearTempMode(void) {
    tmp_mode = false;
    tflite::SimpleMemoryAllocator::ResetTempAllocations();
  };

  virtual uint8_t* AllocateFromTail(size_t size, size_t alignment) {
    uint8_t* res;
    if (tmp_mode) {
      res = tflite::SimpleMemoryAllocator::AllocateTemp(size, alignment);
    } else {
      res = tflite::SimpleMemoryAllocator::AllocateFromTail(size, alignment);
    }
    return res;
  };

private:
  bool tmp_mode;

  TF_LITE_REMOVE_VIRTUAL_DELETE
};

class CTfLiteInterpreterContext {
public:
  CTfLiteInterpreterContext(const tflite::Model* model,
      const tflite::MicroOpResolver& op_resolver,
      uint8_t* tensor_arena, size_t tensor_arena_size,
      tflite::ErrorReporter* error_reporter): profiler(this),
          memory_allocator(error_reporter, tensor_arena, tensor_arena_size),
          interpreter(model, op_resolver,
              tflite::MicroAllocator::Create((tflite::SimpleMemoryAllocator *)&memory_allocator, error_reporter),
          error_reporter, (tflite::MicroProfiler *)&profiler) {}

public:
  CTfLiteProfiler profiler;
  CTfLiteAllocator memory_allocator;
  tflite::MicroInterpreter interpreter;
  int n_invoks;

public:
  static TfLiteStatus input(const uint32_t hdl, int32_t index, struct tflm_c_tensor_info* t_info) {
    CTfLiteInterpreterContext *ctx = reinterpret_cast<CTfLiteInterpreterContext *>(hdl);
    const TfLiteTensor* tens = ctx->interpreter.input(index);
    return ctx->tflitetensor_to(tens, t_info, -1);
  }

  static TfLiteStatus output(const uint32_t hdl, int32_t index, struct tflm_c_tensor_info* t_info) {
    CTfLiteInterpreterContext *ctx = reinterpret_cast<CTfLiteInterpreterContext *>(hdl);
    const TfLiteTensor* tens = ctx->interpreter.output(index);
    return ctx->tflitetensor_to(tens, t_info, -1);
  }

  static TfLiteStatus invoke(const uint32_t hdl) {
    CTfLiteInterpreterContext *ctx = reinterpret_cast<CTfLiteInterpreterContext *>(hdl);
    ctx->profiler.reset();
    ctx->n_invoks++;
    return ctx->interpreter.Invoke();
  }

  static TfLiteStatus reset_all_variables(const uint32_t hdl) {
    CTfLiteInterpreterContext *ctx = reinterpret_cast<CTfLiteInterpreterContext *>(hdl);
    ctx->profiler.reset();
    return ctx->interpreter.ResetVariableTensors();
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

  // mapping is aligned to STM32 Cube.AI expectation
  if (tft->dims->size == 2) { /* 1d array */
    ti->batch = tft->dims->data[0];
    ti->height = 1;
    ti->width = 1;
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
    ti->scale = *(quant->scale->data);
    ti->zero_point = *(quant->zero_point->data);
  } else {
    ti->scale = 0.0f; // nullptr;
    ti->zero_point = 0; // nullptr;
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
    printf("Invalid expected TFLite model version %d instead %d\r\n",
        (int)model->version(), (int)TFLITE_SCHEMA_VERSION);
    return kTfLiteError;
  }

#if defined(TFLM_RUNTIME_USE_ALL_OPERATORS) && TFLM_RUNTIME_USE_ALL_OPERATORS == 1
  static tflite::AllOpsResolver _resolver;
#else
  static tflite::MicroMutableOpResolver<11> _resolver;
  _resolver.AddConv2D();
  _resolver.AddAveragePool2D();
  _resolver.AddDepthwiseConv2D();
  _resolver.AddFullyConnected();
  _resolver.AddSoftmax();
  _resolver.AddMaxPool2D();
  _resolver.AddReshape();
  _resolver.AddQuantize();
  _resolver.AddDequantize();
  _resolver.AddMul();
  _resolver.AddAdd();
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
    printf("AllocateTensors() fails\r\n");
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

TfLiteStatus tflm_c_reset_all_variables(const uint32_t hdl)
{
  return CTfLiteInterpreterContext::reset_all_variables(hdl);
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

uint32_t CTfLiteProfiler::begin_event(const char* tag)
{
  volatile uint64_t ts = options_->get_time(0);
  event_starts_++;
  node_tag_ = tag;
  node_idx_++;
  node_ts_begin_ = options_->get_time(0);  // ts before node execution
  cb_dur_ += (node_ts_begin_ - ts);
  return 0;
}

void CTfLiteProfiler::end_event(uint32_t event_handle)
{
  volatile uint64_t ts = options_->get_time(0);
  event_ends_++;

  if (options_->notify) {
    struct tflm_c_node node;
    ctx_->memory_allocator.SetTempMode();
    const tflite::NodeAndRegistration node_imp = ctx_->interpreter.node_and_registration(node_idx_);
    node.node_info.name = node_tag_;
    node.node_info.idx = node_idx_;
    node.node_info.dur = ts - node_ts_begin_;
    node.node_info.version = node_imp.registration->version;
    node.node_info.builtin_code = node_imp.registration->builtin_code;
    node_dur_ += node.node_info.dur;

    if (node_imp.node.outputs && ((options_->flags & OBSERVER_FLAGS_TIME_ONLY) == 0)) {

#if defined(_MULTIPLE_NODE_OUTPUTS_SUPPORT) && _MULTIPLE_NODE_OUTPUTS_SUPPORT == 1
      const int n_ouputs = node_imp.node.outputs->size;
      node.node_info.n_outputs = n_ouputs>_MAX_NODE_OUTPUTS_SUPPORT?_MAX_NODE_OUTPUTS_SUPPORT:n_ouputs;
      for (uint32_t i=0; i<node.node_info.n_outputs; i++) {
        struct tflm_c_tensor_info* t_info = &t_info_[i];
        const int ts_idx = node_imp.node.outputs->data[i];
        TfLiteTensor* tens = ctx_->interpreter.tensor(ts_idx);
        ctx_->tflitetensor_to(tens, t_info, ts_idx);
        ctx_->memory_allocator.ResetTempAllocations();
      }
      node.output = t_info_;
#else
      struct tflm_c_tensor_info t_info;
      const int ts_idx = node_imp.node.outputs->data[0];
      TfLiteTensor* tens = ctx_->interpreter.tensor(ts_idx);
      ctx_->tflitetensor_to(tens, &t_info, ts_idx);
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
    ctx_->memory_allocator.ClearTempMode();
  }
  cb_dur_ += (options_->get_time(1) - ts);
}

#ifdef __cplusplus
}
#endif
