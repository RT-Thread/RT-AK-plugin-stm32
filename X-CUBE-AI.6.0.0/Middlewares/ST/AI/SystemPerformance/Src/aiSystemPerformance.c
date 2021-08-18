/**
 ******************************************************************************
 * @file    aiSystemPerformance.c
 * @author  MCD Vertical Application Team
 * @brief   Entry points for AI system performance application (multiple network)
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
 * - Simple STM32 application to measure and report the system performance of
 *   a generated NN
 * - Use the multiple-network API
 * - Random input values are injected in the NN to measure the inference time
 *   and to monitor the usage of the stack and/or the heap. Output value are
 *   skipped.
 * - After N iterations (_APP_ITER_ C-define), results are reported through a
 *   re-target printf
 * - aiSystemPerformanceInit()/aiSystemPerformanceProcess() functions should
 *   be called from the main application code.
 * - Only UART (to re-target the printf) & CORE clock setting are expected
 *   by the initial run-time (main function).
 *   CRC IP should be also enabled
 *
 * STM32CubeIDE (GCC-base toolchain)
 *  - Linker options "-Wl,--wrap=malloc -Wl,--wrap=free" should be used
 *    to support the HEAP monitoring
 *
 * TODO:
 *  - (nice-to_have) add HEAP monitoring for IAR tool-chain
 *  - (nice-to-have) add HEAP/STACK monitoring MDK-ARM Keil tool-chain
 *
 * History:
 *  - v1.0 - Initial version
 *  - v1.1 - Complete minimal interactive console
 *  - v1.2 - Adding STM32H7 MCU support
 *  - v1.3 - Adding STM32F3 MCU support
 *  - v1.4 - Adding Profiling mode
 *  - v2.0 - Adding Multiple Network support
 *  - v2.1 - Adding F3 str description
 *  - v3.0 - Adding FXP support
 *           Adding initial multiple IO support (legacy mode)
 *           Removing compile-time STM32 family checking
 *  - v3.1 - Fix cycle count overflow
 *           Add support for external memory for data activations
 *  - v4.0 - Adding multiple IO support
 *  - v4.1 - Adding L5 support
 *  - v4.2 - Adding support for inputs in activations buffer
 *  - v4.3 - Fix - fill input samples loop + HAL_delay report
 *  - v4.4 - Complete dev_id str description
 *  - v5.0 - Add inference time by layer (with runtime observer API support)
 *           Improve reported network info (minor)
 *           Fix stack calculation (minor)
 *  - v5.1 - Create separate C-files (aiTestUtility/aiTestHelper) for common (non AI)
 *           functions with aiValidation firmware.
 *           Align aiBootstrap/aiInit/aiDeInit functions with aiValidation.c
 *           Adding support for outputs in activations buffer.
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

/* APP header files */
#include <aiSystemPerformance.h>
#include <aiTestUtility.h>
#include <aiTestHelper.h>


/* AI Run-time header files */
#include "ai_platform_interface.h"

/* AI x-cube-ai files */
#include "app_x-cube-ai.h"


/* -----------------------------------------------------------------------------
 * TEST-related definitions
 * -----------------------------------------------------------------------------
 */

#define _APP_VERSION_MAJOR_     (0x05)
#define _APP_VERSION_MINOR_     (0x01)
#define _APP_VERSION_   ((_APP_VERSION_MAJOR_ << 8) | _APP_VERSION_MINOR_)

#define _APP_NAME_      "AI system performance measurement"
#define _APP_ITER_       16  /* number of iteration for perf. test */


/* Global variables */
static bool observer_mode = true;
static bool profiling_mode = false;
static int  profiling_factor = 5;


/* -----------------------------------------------------------------------------
 * AI-related functions
 * -----------------------------------------------------------------------------
 */

DEF_DATA_IN;

DEF_DATA_OUT;

struct ai_network_exec_ctx {
  ai_handle handle;
  ai_network_report report;
} net_exec_ctx[AI_MNETWORK_NUMBER] = {0};


#if AI_MNETWORK_DATA_ACTIVATIONS_INT_SIZE != 0
AI_ALIGNED(32)
static ai_u8 activations[AI_MNETWORK_DATA_ACTIVATIONS_INT_SIZE];
#endif

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
  ai_network_params params = {
      AI_BUFFER_NULL(NULL),
      AI_BUFFER_NULL(NULL)
  };

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


#if defined(USE_OBSERVER) && USE_OBSERVER == 1

struct u_node_stat {
  uint64_t dur;
  uint32_t n_runs;
};

struct u_observer_ctx {
  uint64_t n_cb;
  uint64_t start_t;
  uint64_t u_dur_t;
  uint64_t k_dur_t;
  struct u_node_stat *nodes;
};

static struct u_observer_ctx u_observer_ctx;

/* User callback */
static ai_u32 user_observer_cb(const ai_handle cookie,
    const ai_u32 flags,
    const ai_observer_node *node) {

  struct u_observer_ctx *u_obs;

  volatile uint64_t ts = dwtGetCycles(); /* time stamp entry */

  u_obs = (struct u_observer_ctx *)cookie;
  u_obs->n_cb += 1;

  if (flags & AI_OBSERVER_POST_EVT) {
    const uint64_t end_t = ts - u_obs->start_t;
    u_obs->k_dur_t += end_t;
    u_obs->nodes[node->c_idx].dur += end_t;
    u_obs->nodes[node->c_idx].n_runs += 1;
  }

  u_obs->start_t = dwtGetCycles(); /* time stamp exit */
  u_obs->u_dur_t += u_obs->start_t  - ts; /* accumulate cycles used by the CB */
  return 0;
}


void aiObserverInit(struct ai_network_exec_ctx *net_ctx)
{
  ai_handle  net_hdl;
  ai_network_params net_params;
  ai_bool res;
  int sz;

  if (!net_ctx || (net_ctx->handle == AI_HANDLE_NULL) || !net_ctx->report.n_nodes)
    return;


  /* retrieve real handle */
  ai_mnetwork_get_private_handle(net_ctx->handle, &net_hdl, &net_params);

  memset((void *)&u_observer_ctx, 0, sizeof(struct u_observer_ctx));

  /* allocate resources to store the state of the nodes */
  sz = net_ctx->report.n_nodes * sizeof(struct u_node_stat);
  u_observer_ctx.nodes = (struct u_node_stat*)malloc(sz);
  if (!u_observer_ctx.nodes) {
    printf("W: enable to allocate the u_node_stats (sz=%d) ..\r\n", sz);
    return;
  }

  memset(u_observer_ctx.nodes, 0, sz);

  /* register the callback */
  res = ai_platform_observer_register(net_hdl, user_observer_cb,
      (ai_handle)&u_observer_ctx, AI_OBSERVER_PRE_EVT | AI_OBSERVER_POST_EVT);
  if (!res) {
    printf("W: unable to register the user CB\r\n");
    free(u_observer_ctx.nodes);
    u_observer_ctx.nodes = NULL;
    return;
  }
}

extern const char* ai_layer_type_name(const int type);

void aiObserverDone(struct ai_network_exec_ctx *net_ctx)
{
  ai_handle  net_hdl;
  ai_network_params net_params;
  struct dwtTime t;
  uint64_t cumul;
  ai_observer_node node_info;

  if (!net_ctx || (net_ctx->handle == AI_HANDLE_NULL) ||
      !net_ctx->report.n_nodes || !u_observer_ctx.nodes)
    return;

  /* retrieve real handle */
  ai_mnetwork_get_private_handle(net_ctx->handle, &net_hdl, &net_params);

  ai_platform_observer_unregister(net_hdl, user_observer_cb,
      (ai_handle)&u_observer_ctx);

  printf("\r\n Inference time by c-node\r\n");
  dwtCyclesToTime(u_observer_ctx.k_dur_t / u_observer_ctx.nodes[0].n_runs, &t);
  printf("  kernel  : %d.%03dms (time passed in the c-kernel fcts)\n", t.s * 1000 + t.ms, t.us);
  dwtCyclesToTime(u_observer_ctx.u_dur_t / u_observer_ctx.nodes[0].n_runs, &t);
  printf("  user    : %d.%03dms (time passed in the user cb)\n", t.s * 1000 + t.ms, t.us);
#if APP_DEBUG == 1
  printf("  cb #    : %d\n", (int)u_observer_ctx.n_cb);
#endif

  printf("\r\n %-6s%-20s%-7s %s\r\n", "c_id", "type", "id", "time (ms)");
  printf(" -------------------------------------------------\r\n");

  cumul = 0;
  node_info.c_idx = 0;
  while (ai_platform_observer_node_info(net_hdl, &node_info)) {
    struct u_node_stat *sn = &u_observer_ctx.nodes[node_info.c_idx];
    const char *fmt;
    cumul +=  sn->dur;
    dwtCyclesToTime(sn->dur / (uint64_t)sn->n_runs, &t);
    if ((node_info.type & (ai_u16)0x8000) >> 15)
      fmt = " %-6dTD-%-17s%-5d %4d.%03d %6.02f %c\r\n";
    else
      fmt = " %-6d%-20s%-5d %4d.%03d %6.02f %c\r\n";

    printf(fmt, node_info.c_idx,
        ai_layer_type_name(node_info.type  & (ai_u16)0x7FFF),
        (int)node_info.id,
        t.s * 1000 + t.ms, t.us,
        ((float)u_observer_ctx.nodes[node_info.c_idx].dur * 100.0f) / (float)u_observer_ctx.k_dur_t,
        '%');
    node_info.c_idx++;
  }

  printf(" -------------------------------------------------\r\n");
  cumul /= u_observer_ctx.nodes[0].n_runs;
  dwtCyclesToTime(cumul, &t);
  printf(" %31s %4d.%03d ms\r\n", "", t.s * 1000 + t.ms, t.us);

  free(u_observer_ctx.nodes);
  memset((void *)&u_observer_ctx, 0, sizeof(struct u_observer_ctx));

  return;
}
#endif


/* -----------------------------------------------------------------------------
 * Specific APP/test functions
 * -----------------------------------------------------------------------------
 */

static int aiTestPerformance(int idx)
{
  int iter;
#if _APP_FIX_CLK_OVERFLOW == 0
  uint32_t irqs;
#endif
  ai_i32 batch;
  int niter;

  struct dwtTime t;
  uint64_t tcumul;
  uint64_t tend;
  uint64_t tmin;
  uint64_t tmax;
  uint32_t cmacc;

#if defined(USE_OBSERVER) && USE_OBSERVER == 1
  int observer_heap_sz = 0UL;
#endif

  ai_buffer ai_input[AI_MNETWORK_IN_NUM];
  ai_buffer ai_output[AI_MNETWORK_OUT_NUM];

  if (net_exec_ctx[idx].handle == AI_HANDLE_NULL) {
    printf("E: network handle is NULL\r\n");
    return -1;
  }

  MON_STACK_INIT();

  if (profiling_mode)
    niter = _APP_ITER_ * profiling_factor;
  else
    niter = _APP_ITER_;

  printf("\r\nRunning PerfTest on \"%s\" with random inputs (%d iterations)...\r\n",
      net_exec_ctx[idx].report.model_name, niter);

#if APP_DEBUG == 1
  MON_STACK_STATE("stack before test");
#endif

#if _APP_FIX_CLK_OVERFLOW == 0
  irqs = disableInts();
#endif

  MON_STACK_CHECK0();

  /* reset/init cpu clock counters */
  tcumul = 0ULL;
  tmin = UINT64_MAX;
  tmax = 0UL;

  MON_STACK_MARK();

  if ((net_exec_ctx[idx].report.n_inputs > AI_MNETWORK_IN_NUM) ||
      (net_exec_ctx[idx].report.n_outputs > AI_MNETWORK_OUT_NUM))
  {
    printf("E: AI_MNETWORK_IN/OUT_NUM definition are incoherent\r\n");
    HAL_Delay(100);
    return -1;
  }

  /* Fill the input tensor descriptors */
  for (int i = 0; i < net_exec_ctx[idx].report.n_inputs; i++) {
    ai_input[i] = net_exec_ctx[idx].report.inputs[i];
    ai_input[i].n_batches  = 1;
    if (net_exec_ctx[idx].report.inputs[i].data)
      ai_input[i].data = AI_HANDLE_PTR(net_exec_ctx[idx].report.inputs[i].data);
    else
      ai_input[i].data = AI_HANDLE_PTR(data_ins[i]);
  }

  /* Fill the output tensor descriptors */
  for (int i = 0; i < net_exec_ctx[idx].report.n_outputs; i++) {
    ai_output[i] = net_exec_ctx[idx].report.outputs[i];
    ai_output[i].n_batches = 1;
    if (net_exec_ctx[idx].report.outputs[i].data)
      ai_output[i].data = AI_HANDLE_PTR(net_exec_ctx[idx].report.outputs[i].data);
    else
      ai_output[i].data = AI_HANDLE_PTR(data_outs[i]);
  }

  if (profiling_mode) {
    printf("Profiling mode (%d)...\r\n", profiling_factor);
    fflush(stdout);
  }

#if defined(USE_OBSERVER) && USE_OBSERVER == 1
  /* Enable observer */
  if (observer_mode) {
    MON_ALLOC_RESET();
    MON_ALLOC_ENABLE();
    aiObserverInit(&net_exec_ctx[idx]);
    observer_heap_sz = MON_ALLOC_MAX_USED();
  }
#endif

  MON_ALLOC_RESET();

  /* Main inference loop */
  for (iter = 0; iter < niter; iter++) {

    /* Fill input tensors with random data */
    for (int i = 0; i < net_exec_ctx[idx].report.n_inputs; i++) {
      const ai_buffer_format fmt = AI_BUFFER_FORMAT(&ai_input[i]);
      ai_i8 *in_data = (ai_i8 *)ai_input[i].data;
      for (ai_size j = 0; j < AI_BUFFER_SIZE(&ai_input[i]); ++j) {
        /* uniform distribution between -1.0 and 1.0 */
        const float v = 2.0f * (ai_float) rand() / (ai_float) RAND_MAX - 1.0f;
        if  (AI_BUFFER_FMT_GET_TYPE(fmt) == AI_BUFFER_FMT_TYPE_FLOAT) {
          *(ai_float *)(in_data + j * 4) = v;
        }
        else {
          in_data[j] = (ai_i8)(v * 127);
          if (AI_BUFFER_FMT_GET_TYPE(fmt) == AI_BUFFER_FMT_TYPE_BOOL) {
            in_data[j] = (in_data[j] > 0)?(ai_i8)1:(ai_i8)0;
          }
        }
      }
    }

    MON_ALLOC_ENABLE();

    // free(malloc(20));

    cyclesCounterStart();
    batch = ai_mnetwork_run(net_exec_ctx[idx].handle, ai_input, ai_output);
    if (batch != 1) {
      aiLogErr(ai_mnetwork_get_error(net_exec_ctx[idx].handle),
          "ai_mnetwork_run");
      break;
    }
    tend = cyclesCounterEnd();

    MON_ALLOC_DISABLE();

    if (tend < tmin)
      tmin = tend;

    if (tend > tmax)
      tmax = tend;

    tcumul += tend;

    dwtCyclesToTime(tend, &t);

#if APP_DEBUG == 1
    printf(" #%02d %8d.%03dms (%ld cycles)\r\n", iter,
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
  /* remove the user cb time */
  tmin = tmin - u_observer_ctx.u_dur_t / (uint64_t)iter;
  tmax = tmax - u_observer_ctx.u_dur_t / (uint64_t)iter;
  tcumul -= u_observer_ctx.u_dur_t;
#endif

  tcumul /= (uint64_t)iter;

  dwtCyclesToTime(tcumul, &t);

  printf("Results for \"%s\", %d inferences @%dMHz/%dMHz (complexity: %d MACC)\r\n",
      net_exec_ctx[idx].report.model_name, (int)iter,
      (int)(HAL_RCC_GetSysClockFreq() / 1000000),
      (int)(HAL_RCC_GetHCLKFreq() / 1000000),
      (int)net_exec_ctx[idx].report.n_macc);

  printf(" duration     : %d.%03d ms (average)\r\n", t.s * 1000 + t.ms, t.us);
  printf(" CPU cycles   : %d -%d/+%d (average,-/+)\r\n",
      (int)(tcumul), (int)(tcumul - tmin),
      (int)(tmax - tcumul));
  printf(" CPU Workload : %d%c (duty cycle = 1s)\r\n", (int)((tcumul * 100) / t.fcpu), '%');
  cmacc = (uint32_t)((tcumul * 100)/ net_exec_ctx[idx].report.n_macc);
  printf(" cycles/MACC  : %d.%02d (average for all layers)\r\n",
      (int)(cmacc / 100), (int)(cmacc - ((cmacc / 100) * 100)));

  MON_STACK_REPORT();
  MON_ALLOC_REPORT();

#if defined(USE_OBSERVER) && USE_OBSERVER == 1
  printf(" observer res : %d bytes used from the heap (%d c-nodes)\r\n", observer_heap_sz,
			(int)net_exec_ctx[idx].report.n_nodes);
  aiObserverDone(&net_exec_ctx[idx]);
#endif

  return 0;
}

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
  printf("# %s %d.%d\r\n", _APP_NAME_ , _APP_VERSION_MAJOR_,
      _APP_VERSION_MINOR_ );
  printf("#\r\n");

  systemSettingLog();

  cyclesCounterInit();

  aiInit();

  srand(3); /* deterministic outcome */

  dwtReset();
  return 0;
}

int aiSystemPerformanceProcess(void)
{
  int r;
  int idx = 0;

  do {
    r = aiTestPerformance(idx);
    idx = (idx+1) % AI_MNETWORK_NUMBER;

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
        observer_mode = false;
        r = 0;
      }

      if (r == CONS_EVT_RESTART) {
        profiling_mode = false;
        observer_mode = true;
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

