/**
 ******************************************************************************
 * @file    aiSystemPerformance_TFLM.c
 * @author  AIS Team
 * @brief   AI System perf. application (entry points) - TFLM runtime
 ******************************************************************************
 * @attention
 *
 * <h2><center>&copy; Copyright (c) 2020-2021 STMicroelectronics.
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
 * Description
 *
 * - Entry points) for the AI System Perf. which uses a TFLM runtime.
 *
 * History:
 *  - v1.0 - Initial version
 *  - v1.1 - Code clean-up
 *           Add CB log (APP_DEBUG only)
 */

/* System headers */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>

#define USE_OBSERVER         1 /* 0: remove the registration of the user CB to evaluate the inference time by layer */
#define USE_CORE_CLOCK_ONLY  0 /* 1: remove usage of the HAL_GetTick() to evaluate the number of CPU clock
                                *    HAL_Tick() is requested to avoid an overflow with the DWT clock counter (32b register)
                                */

#if !defined(APP_DEBUG)
#define APP_DEBUG     	     0 /* 1: add debug trace - application level */
#endif

#if defined(USE_CORE_CLOCK_ONLY) && USE_CORE_CLOCK_ONLY == 1
#define _APP_FIX_CLK_OVERFLOW 0
#else
#define _APP_FIX_CLK_OVERFLOW 1
#endif

#if APP_DEBUG != 1
#define _APP_ITER_     16  /* number of iteration for perf. test */
#else
#define _APP_ITER_     4  /* number of iteration for perf. test */
#endif

/* APP header files */
#include <aiSystemPerformance.h>
#include <aiTestUtility.h>

/* AI x-cube-ai files */
#include <app_x-cube-ai.h>

#include <tflm_c.h>

#include "network_tflite_data.h"

/* -----------------------------------------------------------------------------
 * TEST-related definitions
 * -----------------------------------------------------------------------------
 */
#define _APP_VERSION_MAJOR_     (0x01)
#define _APP_VERSION_MINOR_     (0x01)
#define _APP_VERSION_           ((_APP_VERSION_MAJOR_ << 8) | _APP_VERSION_MINOR_)

#define _APP_NAME_              "AI system performance measurement TFLM"

/* Global variables */
static bool profiling_mode = false;
static int  profiling_factor = 5;


/* -----------------------------------------------------------------------------
 * Object definition/declaration for AI-related execution context
 * -----------------------------------------------------------------------------
 */

struct tflm_context {
  uint32_t hdl;
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

struct u_node_stat {
  char name[22];
  uint64_t dur;
};

static uint64_t _current_time_ticks_cb(int mode)
{
  uint64_t val = cyclesCounterEnd(); // dwtGetCycles();
  // if (mode)
  //  dwtReset();
  return val;
}

static int _observer_node_cb(const void* cookie,
    const uint32_t flags,
    const struct tflm_c_node* node)
{
  if (cookie) {
    struct u_node_stat* stat = (struct u_node_stat*)cookie;
    // stat[node->node_info.idx].name =
    strncpy(stat[node->node_info.idx].name, node->node_info.name, 20); // strlen(node->node_info.name));
    stat[node->node_info.idx].dur += node->node_info.dur;
  }

#if APP_DEBUG == 1
  if (node->node_info.n_outputs) {
      printf("CB %s:%d:%d %d %d\r\n", node->node_info.name,
          (int)node->node_info.builtin_code, (int)node->node_info.idx,
          (int)node->node_info.dur, (int)node->node_info.n_outputs);
      for (int i=0; i<node->node_info.n_outputs; i++) {
          struct tflm_c_tensor_info* t_info = &node->output[i];
          log_tensor(t_info, i);
        }
  }
  else
    printf("CB %s:%d %d\r\n", node->node_info.name, (int)node->node_info.idx,
        (int)node->node_info.dur);
#endif

  return 0;
}

struct tflm_c_observer_options _observer_options = {
  .notify = _observer_node_cb,
  .get_time = _current_time_ticks_cb,
  .cookie = NULL,
#if APP_DEBUG == 1
  .flags = OBSERVER_FLAGS_DEFAULT,
#else
  .flags = OBSERVER_FLAGS_TIME_ONLY,
#endif
};

static TfLiteStatus observer_init(struct tflm_context *ctx)
{
  TfLiteStatus res;
  int sz;

  if (_observer_options.cookie)
      return kTfLiteError;

  sz = tflm_c_operators_size(ctx->hdl) * sizeof(struct u_node_stat);
  _observer_options.cookie = (struct u_node_stat*)malloc(sz);

  if (!_observer_options.cookie)
    return kTfLiteError;

  memset(_observer_options.cookie, 0, sz);

  res = tflm_c_observer_register(ctx->hdl, &_observer_options);
  if (res != kTfLiteOk) {
    printf("Unable to register the callback observer..\r\n");
    return kTfLiteError;
  }
  return kTfLiteOk;
}

static void observer_done(struct tflm_context *ctx)
{
  struct dwtTime t;
  struct tflm_c_profile_info p_info;

  if (ctx->hdl == 0)
    return;

  tflm_c_observer_info(ctx->hdl, &p_info);

  if (_observer_options.cookie && p_info.node_dur) {
    struct u_node_stat* stat = (struct u_node_stat*)_observer_options.cookie;
    uint64_t cumul = 0;
    printf("\r\n Inference time by c-node\r\n");
    dwtCyclesToTime(p_info.node_dur / p_info.n_invoks, &t);
    printf("  kernel  : %d.%03dms (time passed in the c-kernel fcts)\r\n", t.s * 1000 + t.ms, t.us);
    dwtCyclesToTime(p_info.cb_dur / p_info.n_invoks, &t);
    printf("  user    : %d.%03dms (time passed in the user cb)\r\n", t.s * 1000 + t.ms, t.us);

    printf("\r\n %-6s%-25s %s\r\n", "idx", "name", "time (ms)");
    printf(" ---------------------------------------------------\r\n");

    const char *fmt = " %-6d%-25s %6d.%03d %6.02f %c\r\n";
    for (int i=0; i<tflm_c_operators_size(ctx->hdl); i++) {
      dwtCyclesToTime(stat[i].dur / p_info.n_invoks, &t);
      cumul += stat[i].dur;
      printf(fmt,
          i,
          stat[i].name,
          t.s * 1000 + t.ms, t.us,
          ((float)stat[i].dur * 100.0f) / (float)p_info.node_dur,
          '%');
    }
    printf(" ---------------------------------------------------\r\n");
    dwtCyclesToTime(cumul / p_info.n_invoks, &t);
    printf(" %31s %6d.%03d ms\r\n", "", t.s * 1000 + t.ms, t.us);

    free(_observer_options.cookie);
    _observer_options.cookie = NULL;

    tflm_c_observer_unregister(ctx->hdl, &_observer_options);
  }
}

#endif

static int aiBootstrap(struct tflm_context *ctx)
{
  TfLiteStatus res;
  struct tflm_c_version ver;

  /* Creating an instance of the network ------------------------- */
  printf("\r\nInstancing the network.. (cWrapper: v%s)\r\n", TFLM_C_VERSION_STR);

  /* TFLm runtime expects that the tensor arena is aligned on 16-bytes */
  uint32_t uaddr = (uint32_t)tensor_arena;
  uaddr = (uaddr + (16 - 1)) & (uint32_t)(-16);  // Round up to 16-byte boundary

  MON_ALLOC_RESET();
  MON_ALLOC_ENABLE();

  res = tflm_c_create(g_tflm_network_model_data, (uint8_t*)uaddr,
          TFLM_NETWORK_TENSOR_AREA_SIZE, &ctx->hdl);

  MON_ALLOC_DISABLE();

  if (res != kTfLiteOk) {
    return -1;
  }

  tflm_c_rt_version(&ver);

  printf(" TFLM version       : %d.%d.%d\r\n", (int)ver.major, (int)ver.minor, (int)ver.patch);
  printf(" TFLite file        : 0x%08x (%d bytes)\r\n", (int)g_tflm_network_model_data,
      (int)g_tflm_network_model_data_len);
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
 * Specific APP/test functions
 * -----------------------------------------------------------------------------
 */

static int aiTestPerformance(void)
{
  int iter;
#if _APP_FIX_CLK_OVERFLOW == 0
  uint32_t irqs;
#endif
  TfLiteStatus res;
  int niter;
  struct tflm_c_version ver;

  struct dwtTime t;
  uint64_t tcumul;
  uint64_t tend;

#if defined(USE_OBSERVER) && USE_OBSERVER == 1
  struct tflm_c_profile_info p_info;
#endif

  struct tflm_context *ctx = &net_exec_ctx[0];

  tflm_c_rt_version(&ver);

  if (ctx->hdl == 0) {
    printf("E: network handle is NULL\r\n");
    return -1;
  }

  MON_STACK_INIT();

  if (profiling_mode)
    niter = _APP_ITER_ * profiling_factor;
  else
    niter = _APP_ITER_;

  printf("\r\nRunning PerfTest with random inputs (%d iterations)...\r\n", niter);


#if APP_DEBUG == 1
  MON_STACK_STATE("stack before loop");
#endif

#if _APP_FIX_CLK_OVERFLOW == 0
  irqs = disableInts();
#endif

  MON_STACK_CHECK0();

  /* reset/init cpu clock counters */
  tcumul = 0ULL;

  MON_STACK_MARK();

  if (profiling_mode) {
    printf("Profiling mode (%d)...\r\n", profiling_factor);
    fflush(stdout);
  }

#if defined(USE_OBSERVER) && USE_OBSERVER == 1
  observer_init(ctx);
  tflm_c_observer_start(ctx->hdl);
#endif

  MON_ALLOC_RESET();

  /* Main inference loop */
  for (iter = 0; iter < niter; iter++) {

    /* Fill input tensors with random data */
    /* .. */

    MON_ALLOC_ENABLE();

    // free(malloc(20));

    cyclesCounterStart();
    res = tflm_c_invoke(ctx->hdl);
    tend = cyclesCounterEnd();

    if (res != kTfLiteOk) {
      printf("tflm_c_invoke() fails\r\n");
      return res;
    }

    MON_ALLOC_DISABLE();

    tcumul += tend;

    dwtCyclesToTime(tend, &t);

#if APP_DEBUG == 1
    printf("#%02d %8d.%03dms (%ld cycles)\r\n", iter,
        t.ms, t.us, (long)tend);
#else
    if (!profiling_mode) {
      if (t.s > 10)
        niter = iter;
      printf(".");
      fflush(stdout);
    }
#endif
  } /* end of the main loop */

#if APP_DEBUG != 1
  printf("\r\n");
#endif

  MON_STACK_EVALUATE();

#if _APP_FIX_CLK_OVERFLOW == 0
  restoreInts(irqs);
#endif

  printf("\r\n");

#if defined(USE_OBSERVER) && USE_OBSERVER == 1
  tflm_c_observer_info(ctx->hdl, &p_info);
  if (p_info.node_dur)
    tcumul -= p_info.cb_dur;
#endif

  tcumul /= (uint64_t)iter;

  dwtCyclesToTime(tcumul, &t);

  printf("Results TFLM %d.%d.%d, %d inferences @%dMHz/%dMHz\r\n",
      (int)ver.major, (int)ver.minor, (int)ver.patch,
      (int)iter,
      (int)(HAL_RCC_GetSysClockFreq() / 1000000),
      (int)(HAL_RCC_GetHCLKFreq() / 1000000));

  printf(" duration     : %d.%03d ms (average)\r\n", t.s * 1000 + t.ms, t.us);

  if (tcumul / 100000)
    printf(" CPU cycles   : %ld%ld (average)\r\n",
      (unsigned long)(tcumul / 100000), (unsigned long)(tcumul - ((tcumul / 100000) * 100000)));
  else
    printf(" CPU cycles   : %ld (average)\r\n",
      (unsigned long)(tcumul));

  printf(" CPU Workload : %d%c (duty cycle = 1s)\r\n", (int)((tcumul * 100) / t.fcpu), '%');

  MON_STACK_REPORT();
  MON_ALLOC_REPORT();

#if defined(USE_OBSERVER) && USE_OBSERVER == 1
  observer_done(ctx);
#endif

  return 0;
}

/* -----------------------------------------------------------------------------
 * Basic interactive console
 * -----------------------------------------------------------------------------
 */

#define CONS_EVT_TIMEOUT    (0)
#define CONS_EVT_QUIT       (1)
#define CONS_EVT_RESTART    (2)
#define CONS_EVT_HELP       (3)
#define CONS_EVT_PAUSE      (4)
#define CONS_EVT_PROF       (5)
#define CONS_EVT_HIDE       (6)

#define CONS_EVT_UNDEFINED  (100)

static int aiTestConsole(void)
{
  uint8_t c = 0;

  if (ioRawGetUint8(&c, 5000) == -1) /* Timeout */
    return CONS_EVT_TIMEOUT;

  if ((c == 'q') || (c == 'Q'))
    return CONS_EVT_QUIT;

  if ((c == 'd') || (c == 'D'))
    return CONS_EVT_HIDE;

  if ((c == 'r') || (c == 'R'))
    return CONS_EVT_RESTART;

  if ((c == 'h') || (c == 'H') || (c == '?'))
    return CONS_EVT_HELP;

  if ((c == 'p') || (c == 'P'))
    return CONS_EVT_PAUSE;

  if ((c == 'x') || (c == 'X'))
    return CONS_EVT_PROF;

  return CONS_EVT_UNDEFINED;
}


/* -----------------------------------------------------------------------------
 * Exported/Public functions
 * -----------------------------------------------------------------------------
 */

int aiSystemPerformanceInit(void)
{
  printf("\r\n#\r\n");
  printf("# %s %d.%d\r\n", _APP_NAME_ , _APP_VERSION_MAJOR_, _APP_VERSION_MINOR_ );
  printf("#\r\n");

  systemSettingLog();

  cyclesCounterInit();

  if (aiInit()) {
    while (1);
  }

  srand(3); /* deterministic outcome */

  dwtReset();
  return 0;
}

int aiSystemPerformanceProcess(void)
{
  int r;

  do {
    r = aiTestPerformance();

    if (!r) {
      r = aiTestConsole();

      if (r == CONS_EVT_UNDEFINED) {
        r = 0;
      } else if (r == CONS_EVT_HELP) {
        printf("\r\n");
        printf("Possible key for the interactive console:\r\n");
        printf("  [q,Q]      quit the application\r\n");
        printf("  [r,R]      re-start (NN de-init and re-init)\r\n");
        printf("  [p,P]      pause\r\n");
        printf("  [d,D]      hide detailed information ('r' to restore)\r\n");
        printf("  [h,H,?]    this information\r\n");
        printf("   xx        continue immediately\r\n");
        printf("\r\n");
        printf("Press any key to continue..\r\n");

        while ((r = aiTestConsole()) == CONS_EVT_TIMEOUT) {
          HAL_Delay(1000);
        }
        if (r == CONS_EVT_UNDEFINED)
          r = 0;
      }
      if (r == CONS_EVT_PROF) {
        profiling_mode = true;
        profiling_factor *= 2;
        r = 0;
      }

      if (r == CONS_EVT_HIDE) {
        // observer_mode = false;
        r = 0;
      }

      if (r == CONS_EVT_RESTART) {
        profiling_mode = false;
        // observer_mode = true;
        profiling_factor = 5;
        printf("\r\n");
        aiDeInit();
        aiSystemPerformanceInit();
        r = 0;
      }
      if (r == CONS_EVT_QUIT) {
        profiling_mode = false;
        printf("\r\n");
        disableInts();
        aiDeInit();
        printf("\r\n");
        printf("Board should be reseted...\r\n");
        while (1) {
          HAL_Delay(1000);
        }
      }
      if (r == CONS_EVT_PAUSE) {
        printf("\r\n");
        printf("Press any key to continue..\r\n");
        while ((r = aiTestConsole()) == CONS_EVT_TIMEOUT) {
          HAL_Delay(1000);
        }
        r = 0;
      }
    }
  } while (r==0);

  return r;
}

void aiSystemPerformanceDeInit(void)
{
  printf("\r\n");
  aiDeInit();
  printf("bye bye ...\r\n");
}

