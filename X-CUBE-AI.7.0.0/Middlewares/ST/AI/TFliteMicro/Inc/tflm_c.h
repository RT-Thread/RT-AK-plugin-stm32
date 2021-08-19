/**
 ******************************************************************************
 * @file    tflm_c.h
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

#ifndef __TFLM_C_H__
#define __TFLM_C_H__

/*
 * This file provides a C-wrapper functions to facilitate the usage of TFLm library
 * in a C-project. Only C++ libs/c-start-up libraries are requested to generate
 * the final application firmware.
 * - Simplified version of the tensorflow/tensorflow/c/c_api.h proposal.
 *
 * History:
 * - v1.0: Initial version for STM32 aiSystemPerf/aiValidation support
 * - v1.1: Add reset all variables function
 *         Report only one scale/zero-point values (struct tflm_c_tensor_info)
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

#define TFLM_C_VERSION_MAJOR  (1)
#define TFLM_C_VERSION_MINOR  (1)
#define TFLM_C_VERSION_STR    "1.1"


/* -----------------------------------------------------------------------------
 *  Structure/data definitions
 * -----------------------------------------------------------------------------
 */

#ifndef TENSORFLOW_LITE_C_COMMON_H_

/*
 * copied from
 *    tensorflow/tensorflow/lite/c/common.h
 * TF_LITE_STATIC_MEMORY is defined
 */
typedef enum TfLiteStatus {
  kTfLiteOk = 0,
  kTfLiteError = 1,
  kTfLiteDelegateError = 2,
  kTfLiteApplicationError = 3,
} TfLiteStatus;

typedef enum {
  kTfLiteNoType = 0,
  kTfLiteFloat32 = 1,
  kTfLiteInt32 = 2,
  kTfLiteUInt8 = 3,
  kTfLiteInt64 = 4,
  kTfLiteString = 5,
  kTfLiteBool = 6,
  kTfLiteInt16 = 7,
  kTfLiteComplex64 = 8,
  kTfLiteInt8 = 9,
  kTfLiteFloat16 = 10,
  kTfLiteFloat64 = 11,
  kTfLiteComplex128 = 12,
  kTfLiteUInt64 = 13,
  kTfLiteResource = 14,
  kTfLiteVariant = 15,
  kTfLiteUInt32 = 16,
} TfLiteType;

#endif  /* TENSORFLOW_LITE_C_COMMON_H_ */

struct tflm_c_tensor_info {
  TfLiteType type;
  int32_t  idx;
  uint32_t batch;
  uint32_t height;
  uint32_t width;
  uint32_t channels;
  size_t   bytes;
  float    scale;
  int      zero_point;
  void*    data;
};

struct tflm_c_node_info {
  const char* name;
  uint32_t builtin_code;
  uint32_t version;
  uint32_t idx;
  uint64_t dur;
  uint32_t n_outputs;
};

struct tflm_c_version {
  uint8_t major;
  uint8_t minor;
  uint8_t patch;
  uint8_t schema;
};

struct tflm_c_node {
  struct tflm_c_node_info node_info;
  struct tflm_c_tensor_info* output;
};

/* Client call-back definitions */

typedef int (*tflm_c_observer_node_cb)(
  const void* cookie,
  const uint32_t flags,
  const struct tflm_c_node* node);

typedef uint64_t (*tfm_c_current_time_ticks_cb)(int mode);

struct tflm_c_profile_info {
  uint64_t cb_dur;
  uint64_t node_dur;
  uint32_t n_events;
  uint32_t n_invoks;
};

#define OBSERVER_FLAGS_DEFAULT   (0)  // DATA + TIME
#define OBSERVER_FLAGS_TIME_ONLY (1)

struct tflm_c_observer_options {
  tflm_c_observer_node_cb     notify;
  tfm_c_current_time_ticks_cb get_time;
  void* cookie;
  uint32_t flags;
};


/* -----------------------------------------------------------------------------
 *  Main/core functions
 * -----------------------------------------------------------------------------
 */

/*
 * Returns an TFLm interpreter which is initialized
 * with the provided model including the
 * allocation of the tensors.
 */
TfLiteStatus tflm_c_create(const uint8_t *model_data,
    uint8_t *tensor_arena,
    const uint32_t tensor_arena_size,
    uint32_t *hdl);

TfLiteStatus tflm_c_destroy(uint32_t hdl);

int32_t tflm_c_inputs_size(const uint32_t hdl);
int32_t tflm_c_outputs_size(const uint32_t hdl);

TfLiteStatus tflm_c_input(const uint32_t hdl, int32_t index, struct tflm_c_tensor_info *t_info);
TfLiteStatus tflm_c_output(const uint32_t hdl, int32_t index, struct tflm_c_tensor_info *t_info);

TfLiteStatus tflm_c_invoke(const uint32_t hdl);

TfLiteStatus tflm_c_reset_all_variables(const uint32_t hdl);

int32_t tflm_c_operators_size(const uint32_t hdl);
int32_t tflm_c_tensors_size(const uint32_t hdl);

int32_t tflm_c_arena_used_bytes(const uint32_t hdl);


/* -----------------------------------------------------------------------------
 *  Observer/Profiler functions
 * -----------------------------------------------------------------------------
 */

TfLiteStatus tflm_c_observer_register(const uint32_t hdl, struct tflm_c_observer_options* options);
TfLiteStatus tflm_c_observer_unregister(const uint32_t hdl, struct tflm_c_observer_options* options);

TfLiteStatus tflm_c_observer_start(const uint32_t hdl);
TfLiteStatus tflm_c_observer_info(const uint32_t hdl, struct tflm_c_profile_info* p_info);


/* -----------------------------------------------------------------------------
 *  Utility functions
 * -----------------------------------------------------------------------------
 */

void tflm_c_rt_version(struct tflm_c_version *version);

const char*  tflm_c_TfLiteTypeGetName(TfLiteType type);

#ifdef __cplusplus
}
#endif

#endif /* __TFLM_C_H__ */
