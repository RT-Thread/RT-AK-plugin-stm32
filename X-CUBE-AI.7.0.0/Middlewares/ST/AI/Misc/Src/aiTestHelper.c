/**
 ******************************************************************************
 * @file    aiTestUtility.c
 * @author  MCD Vertical Application Team
 * @brief   STM32 Helper functions for STM32 AI test application
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

/*
 * Description:
 *
 *
 * History:
 *  - v1.0 - Initial version (from initial aiSystemPerformance file v5.0)
 */

#include <stdio.h>
#include <string.h>

#if !defined(TFLM_RUNTIME)

#include <aiTestHelper.h>

#include <ai_platform_interface.h>

void aiPlatformVersion(void)
{
  printf("\r\nAI platform (API %d.%d.%d - RUNTIME %d.%d.%d)\r\n",
      AI_PLATFORM_API_MAJOR,
      AI_PLATFORM_API_MINOR,
      AI_PLATFORM_API_MICRO,
      AI_PLATFORM_RUNTIME_MAJOR,
      AI_PLATFORM_RUNTIME_MINOR,
      AI_PLATFORM_RUNTIME_MICRO);
}

ai_u32 aiBufferSize(const ai_buffer* buffer)
{
  return buffer->height * buffer->width * buffer->channels;
}

void aiLogErr(const ai_error err, const char *fct)
{
  if (fct)
    printf("E: AI error (%s) - type=0x%02x code=0x%02x\r\n", fct,
        err.type, err.code);
  else
    printf("E: AI error - type=0x%02x code=0x%02x\r\n", err.type, err.code);
}


void aiPrintLayoutBuffer(const char *msg, int idx,
    const ai_buffer* buffer)
{
  uint32_t type_id = AI_BUFFER_FMT_GET_TYPE(buffer->format);
  printf("%s[%d] ",msg, idx);
  if (type_id == AI_BUFFER_FMT_TYPE_Q) {
    printf(" %s%d,",
        AI_BUFFER_FMT_GET_SIGN(buffer->format)?"s":"u",
            (int)AI_BUFFER_FMT_GET_BITS(buffer->format));
    if (AI_BUFFER_META_INFO_INTQ(buffer->meta_info)) {
      ai_float scale = AI_BUFFER_META_INFO_INTQ_GET_SCALE(buffer->meta_info, 0);
      int zero_point = AI_BUFFER_META_INFO_INTQ_GET_ZEROPOINT(buffer->meta_info, 0);
      printf(" scale=%f, zero=%d,", scale, zero_point);
    } else {
      printf("Q%d.%d,",
          (int)AI_BUFFER_FMT_GET_BITS(buffer->format)
          - ((int)AI_BUFFER_FMT_GET_FBITS(buffer->format) +
              (int)AI_BUFFER_FMT_GET_SIGN(buffer->format)),
              AI_BUFFER_FMT_GET_FBITS(buffer->format));
    }
  }
  else if (type_id == AI_BUFFER_FMT_TYPE_FLOAT)
    printf(" float%d,",
        (int)AI_BUFFER_FMT_GET_BITS(buffer->format));
  else if (type_id == AI_BUFFER_FMT_TYPE_BOOL)
    printf(" bool,");
  else
    printf("NONE");
  printf(" %d bytes, shape=(%d,%d,%d)",
      (int)AI_BUFFER_BYTE_SIZE(AI_BUFFER_SIZE(buffer), buffer->format),
      buffer->height, buffer->width, (int)buffer->channels);
  if (buffer->data)
    printf(" (@0x%08x)\r\n", (int)buffer->data);
  else
    printf(" (USER domain)\r\n");
}

void aiPrintNetworkInfo(const ai_network_report* report)
{
  int i;
  uint32_t w_addr = (uint32_t)report->params.data;

  if ((w_addr) && (*(uint32_t *)w_addr == AI_MAGIC_MARKER)) {
    w_addr = *(uint32_t *)((uint32_t *)(w_addr) + 1);
  }

  printf("Network informations...\r\n");
  printf(" model name         : %s\r\n", report->model_name);
  printf(" model signature    : %s\r\n", report->model_signature);
  printf(" model datetime     : %s\r\n", report->model_datetime);
  printf(" compile datetime   : %s\r\n", report->compile_datetime);
  printf(" runtime version    : %d.%d.%d\r\n",
      report->runtime_version.major,
      report->runtime_version.minor,
      report->runtime_version.micro);
  if (report->tool_revision[0])
    printf(" Tool revision      : %s\r\n", (report->tool_revision[0])?report->tool_revision:"");
  printf(" tools version      : %d.%d.%d\r\n",
      report->tool_version.major,
      report->tool_version.minor,
      report->tool_version.micro);
  printf(" complexity         : %lu MACC\r\n", (unsigned long)report->n_macc);
  printf(" c-nodes            : %d\r\n", (int)report->n_nodes);
  printf(" activations        : %d bytes (0x%08x)\r\n",
      (int)AI_BUFFER_SIZE(&report->activations), (int)report->activations.data);
  printf(" weights            : %d bytes (0x%08x)\r\n",
      (int)AI_BUFFER_SIZE(&report->params), (unsigned int)w_addr);
  printf(" inputs/outputs     : %u/%u\r\n", report->n_inputs,
      report->n_outputs);
  for (i=0; i<report->n_inputs; i++)
    aiPrintLayoutBuffer("  I", i, &report->inputs[i]);
  for (i=0; i<report->n_outputs; i++)
    aiPrintLayoutBuffer("  O", i, &report->outputs[i]);
}

#endif /* !TFLM_RUNTIME) */
