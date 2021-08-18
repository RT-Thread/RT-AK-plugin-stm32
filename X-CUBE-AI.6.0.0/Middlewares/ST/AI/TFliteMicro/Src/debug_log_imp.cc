/* Copyright 2018 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "tensorflow/lite/micro/debug_log.h"

#include <string.h>
#include <stdint.h>

extern "C" int tflm_io_write(const void *buff, uint16_t count);

extern "C" void DebugLog(const char* s)
{
  if (!s)
    return;
    
  size_t sl = strlen(s);
  if (sl)
	  tflm_io_write(s, (uint16_t)sl);
}
