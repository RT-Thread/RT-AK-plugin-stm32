/**
 ******************************************************************************
 * @file    aiTestHelper.h
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

#ifndef __AI_TEST_HELPER_H__
#define __AI_TEST_HELPER_H__

#include <stdint.h>

#if !defined(TFLM_RUNTIME)

#include <ai_platform.h>


#ifdef __cplusplus
extern "C" {
#endif

#define AI_BUFFER_NULL(ptr_)  \
    AI_BUFFER_OBJ_INIT( \
        AI_BUFFER_FORMAT_NONE|AI_BUFFER_FMT_FLAG_CONST, \
        0, 0, 0, 0, \
        AI_HANDLE_PTR(ptr_))

void aiPlatformVersion(void);

ai_u32 aiBufferSize(const ai_buffer* buffer);
void aiLogErr(const ai_error err, const char *fct);
void aiPrintLayoutBuffer(const char *msg, int idx, const ai_buffer* buffer);
void aiPrintNetworkInfo(const ai_network_report* report);

#endif /* !TFLM_RUNTIME */

#ifdef __cplusplus
}
#endif

#endif /* __AI_TEST_HELPER_H__ */
