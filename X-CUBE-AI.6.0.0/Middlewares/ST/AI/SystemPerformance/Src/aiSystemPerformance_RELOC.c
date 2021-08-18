/**
 ******************************************************************************
 * @file    aiSystemPerformance_RELOC.c
 * @author  MCD Vertical Application Team
 * @brief   AI System perf. application (entry points) - Relocatable network
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
 * Description
 *
 * - Entry points) for the AI System Perf. of a relocatable network object.
 *   Support for a simple relocatable network (no multiple network support).
 *
 * - Allows to report the inference time (global and by layer/operator)
 *   with random inputs (outputs are skipped). Heap and stack usage is also
 *   reported.
 *
 * History:
 *  - v1.0 - Initial version (based on the original aiSystemPerformance v5.1)
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

#define USER_REL_COPY_MODE   0

#if !defined(APP_DEBUG)
#define APP_DEBUG     	     0 /* 1: add debug trace - application level */
#endif

#if defined(USE_CORE_CLOCK_ONLY) && USE_CORE_CLOCK_ONLY == 1
#define _APP_FIX_CLK_OVERFLOW 0
#else
#define _APP_FIX_CLK_OVERFLOW 1
#endif

#define _APP_ITER_     16  /* number of iteration for perf. test */

/* APP header files */
#include <aiSystemPerformance.h>
#include <aiTestUtility.h>
#include <aiTestHelper.h>

/* AI x-cube-ai files */
#include <app_x-cube-ai.h>

#include <ai_reloc_network.h>

/* Include the image of the relocatable network */
#include <network_img_rel.h>

/* -----------------------------------------------------------------------------
 * TEST-related definitions
 * -----------------------------------------------------------------------------
 */
#define _APP_VERSION_MAJOR_     (0x01)
#define _APP_VERSION_MINOR_     (0x00)
#define _APP_VERSION_   ((_APP_VERSION_MAJOR_ << 8) | _APP_VERSION_MINOR_)

#define _APP_NAME_     "AI system performance measurement (RELOC)"


#if AI_MNETWORK_NUMBER > 1 && !defined(AI_NETWORK_MODEL_NAME)
#error Only ONE network is supported (default c-name)
#endif

/* Global variables */
static bool observer_mode = true;
static bool profiling_mode = false;
static int  profiling_factor = 5;


/* -----------------------------------------------------------------------------
 * Object definition/declaration for AI-related execution context
 * -----------------------------------------------------------------------------
 */

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

#endif /* USE_OBSERVER */

struct ai_network_exec_ctx {
  ai_handle handle;
  ai_network_report report;
} net_exec_ctx[1] = {0};


/* Local activations buffer if necessary. */
#if defined(AI_NETWORK_DATA_ACTIVATIONS_START_ADDR) &&\
    AI_NETWORK_DATA_ACTIVATIONS_START_ADDR != 0xFFFFFFFF
AI_ALIGNED(32)
static ai_u8 activations[AI_NETWORK_DATA_ACTIVATIONS_SIZE];
#else
#if AI_NETWORK_DATA_ACTIVATIONS_SIZE > 0
AI_ALIGNED(32)
static ai_u8 activations[AI_NETWORK_DATA_ACTIVATIONS_SIZE];
#else
static ai_u8 activations[4]; /* Dummy act buffer */
#endif
#endif

/* Input and output buffers for the IO tensors. */
DEF_DATA_IN;

DEF_DATA_OUT;

/* RT Network buffer to relocatable network instance */
#if defined(USER_REL_COPY_MODE) && USER_REL_COPY_MODE == 1
AI_ALIGNED(32)
uint8_t reloc_ram[AI_NETWORK_RELOC_RAM_SIZE_COPY];
#else
AI_ALIGNED(32)
uint8_t reloc_ram[AI_NETWORK_RELOC_RAM_SIZE_XIP];
#endif


/* -----------------------------------------------------------------------------
 * Observer-related functions
 * -----------------------------------------------------------------------------
 */
#if defined(USE_OBSERVER) && USE_OBSERVER == 1

/* User callback for observer */
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


void aiObserverInit(struct ai_network_exec_ctx *ctx)
{
  ai_bool res;
  int sz;

  if ((ctx->handle == AI_HANDLE_NULL) || !ctx->report.n_nodes)
    return;

  memset((void *)&u_observer_ctx, 0, sizeof(struct u_observer_ctx));

  /* allocate resources to store the state of the nodes */
  sz = ctx->report.n_nodes * sizeof(struct u_node_stat);
  u_observer_ctx.nodes = (struct u_node_stat*)malloc(sz);
  if (!u_observer_ctx.nodes) {
    printf("W: enable to allocate the u_node_stats (sz=%d) ..\r\n", sz);
    return;
  }

  memset(u_observer_ctx.nodes, 0, sz);

  /* register the callback */
  res = ai_rel_platform_observer_register(ctx->handle, user_observer_cb,
      (ai_handle)&u_observer_ctx, AI_OBSERVER_PRE_EVT | AI_OBSERVER_POST_EVT);
  if (!res) {
    printf("W: unable to register the user CB\r\n");
    free(u_observer_ctx.nodes);
    u_observer_ctx.nodes = NULL;
    return;
  }
}

extern const char* ai_layer_type_name(const int type);

void aiObserverDone(struct ai_network_exec_ctx *ctx)
{
  struct dwtTime t;
  uint64_t cumul;
  ai_observer_node node_info;

  if ((ctx->handle == AI_HANDLE_NULL) || !ctx->report.n_nodes || !u_observer_ctx.nodes)
    return;

  ai_rel_platform_observer_unregister(ctx->handle, user_observer_cb,
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
  while (ai_rel_platform_observer_node_info(ctx->handle, &node_info)) {

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

#endif /* USE_OBSERVER */

static int aiBootstrap(struct ai_network_exec_ctx *ctx)
{
  ai_error err;
  ai_handle ext_act_addr;
  ai_handle weights_addr;
  ai_rel_network_info rt_info;

  /* Creating an instance of the network ------------------------- */
  printf("\r\nInstancing the network (reloc)..\r\n");

  err = ai_rel_network_rt_get_info(ai_network_reloc_img_get(), &rt_info);
  if (err.type != AI_ERROR_NONE) {
      aiLogErr(err, "ai_rel_network_rt_get_info");
      return -1;
    }

#if defined(USER_REL_COPY_MODE) && USER_REL_COPY_MODE == 1
  err = ai_rel_network_load_and_create(ai_network_reloc_img_get(),
      reloc_ram, AI_NETWORK_RELOC_RAM_SIZE_COPY, AI_RELOC_RT_LOAD_MODE_COPY,
      &ctx->handle);
#else
  err = ai_rel_network_load_and_create(ai_network_reloc_img_get(),
      reloc_ram, AI_NETWORK_RELOC_RAM_SIZE_XIP, AI_RELOC_RT_LOAD_MODE_XIP,
      &ctx->handle);
#endif
  if (err.type != AI_ERROR_NONE) {
    aiLogErr(err, "ai_rel_network_load_and_create");
    return -1;
  }

  /* test returned err value (debug purpose) */
  err = ai_rel_network_get_error(ctx->handle);
  if (err.type != AI_ERROR_NONE) {
    aiLogErr(err, "ai_rel_network_get_error");
    return -1;
  }

  /* Initialize the instance --------------------------------------- */
  printf("Initializing the network\r\n");

#if AI_NETWORK_DATA_ACTIVATIONS_START_ADDR &&\
    AI_NETWORK_DATA_ACTIVATIONS_START_ADDR != 0xFFFFFFFF
  ext_act_addr = (ai_handle)AI_NETWORK_DATA_ACTIVATIONS_START_ADDR;
#else
  ext_act_addr = (ai_handle)activations;
#endif

  weights_addr = rt_info.weights;
#if defined(AI_NETWORK_DATA_WEIGHTS_GET_FUNC)
  if (!weights_addr)
    weights_addr = AI_NETWORK_DATA_WEIGHTS_GET_FUNC();
#endif

  if (!ai_rel_network_init(ctx->handle,
      weights_addr, ext_act_addr)) {
    err = ai_rel_network_get_error(ctx->handle);
    aiLogErr(err, "ai_rel_network_init");
    ai_rel_network_destroy(ctx->handle);
    ctx->handle = AI_HANDLE_NULL;
    return -2;
  }

  /* Display the network info -------------------------------------- */
  if (ai_rel_network_get_info(ctx->handle, &ctx->report)) {
    aiPrintNetworkInfo(&ctx->report);
  } else {
    err = ai_rel_network_get_error(ctx->handle);
    aiLogErr(err, "ai_rel_network_get_info");
    ai_rel_network_destroy(ctx->handle);
    ctx->handle = AI_HANDLE_NULL;
    return -3;
  }

  return 0;
}

static void aiDone(struct ai_network_exec_ctx *ctx)
{
  ai_error err;

  /* Releasing the instance(s) ------------------------------------- */
  printf("Releasing the instance...\r\n");

  if (ctx->handle != AI_HANDLE_NULL) {
    if (ai_rel_network_destroy(ctx->handle)
        != AI_HANDLE_NULL) {
      err = ai_rel_network_get_error(ctx->handle);
      aiLogErr(err, "ai_rel_network_destroy");
    }
    ctx->handle = AI_HANDLE_NULL;
  }
}

static int aiInit(void)
{
  int res;

  aiPlatformVersion();

  net_exec_ctx[0].handle = AI_HANDLE_NULL;
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
  ai_i32 batch;
  int niter;

  struct dwtTime t;
  uint64_t tcumul;
  uint64_t tend;
  uint64_t tmin;
  uint64_t tmax;
  uint32_t cmacc;

  struct ai_network_exec_ctx *ctx = &net_exec_ctx[0];

#if defined(USE_OBSERVER) && USE_OBSERVER == 1
  int observer_heap_sz = 0UL;
#endif

  ai_buffer ai_input[AI_NETWORK_IN_NUM];
  ai_buffer ai_output[AI_NETWORK_OUT_NUM];

  if (ctx->handle == AI_HANDLE_NULL) {
    printf("E: network handle is NULL\r\n");
    return -1;
  }

  MON_STACK_INIT();

  if (profiling_mode)
    niter = _APP_ITER_ * profiling_factor;
  else
    niter = _APP_ITER_;

  printf("\r\nRunning PerfTest on \"%s\" with random inputs (%d iterations)...\r\n",
      ctx->report.model_name, niter);


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

  /* Fill the input tensor descriptors */
  for (int i = 0; i < ctx->report.n_inputs; i++) {
    ai_input[i] = ctx->report.inputs[i];
    ai_input[i].n_batches  = 1;
    if (ctx->report.inputs[i].data)
      ai_input[i].data = AI_HANDLE_PTR(ctx->report.inputs[i].data);
    else
      ai_input[i].data = AI_HANDLE_PTR(data_ins[i]);
  }

  /* Fill the output tensor descriptors */
  for (int i = 0; i < ctx->report.n_outputs; i++) {
    ai_output[i] = ctx->report.outputs[i];
    ai_output[i].n_batches = 1;
    if (ctx->report.outputs[i].data)
      ai_output[i].data = AI_HANDLE_PTR(ctx->report.outputs[i].data);
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
    aiObserverInit(ctx);
    observer_heap_sz = MON_ALLOC_MAX_USED();
  }
#endif /* USE_OBSERVER */

  MON_ALLOC_RESET();

  /* Main inference loop */
  for (iter = 0; iter < niter; iter++) {

    /* Fill input tensors with random data */
    for (int i = 0; i < ctx->report.n_inputs; i++) {
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
    batch = ai_rel_network_run(ctx->handle, ai_input, ai_output);
    if (batch != 1) {
      aiLogErr(ai_rel_network_get_error(ctx->handle),
          "ai_rel_network_run");
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

  printf("Results for \"%s\" (R), %d inferences @%dMHz/%dMHz (complexity: %d MACC)\r\n",
      ctx->report.model_name, (int)iter,
      (int)(HAL_RCC_GetSysClockFreq() / 1000000),
      (int)(HAL_RCC_GetHCLKFreq() / 1000000),
      (int)ctx->report.n_macc);

  printf(" duration     : %d.%03d ms (average)\r\n", t.s * 1000 + t.ms, t.us);
  printf(" CPU cycles   : %d -%d/+%d (average,-/+)\r\n",
      (int)(tcumul), (int)(tcumul - tmin),
      (int)(tmax - tcumul));
  printf(" CPU Workload : %d%c (duty cycle = 1s)\r\n", (int)((tcumul * 100) / t.fcpu), '%');
  cmacc = (uint32_t)((tcumul * 100)/ ctx->report.n_macc);
  printf(" cycles/MACC  : %d.%02d (average for all layers)\r\n",
      (int)(cmacc / 100), (int)(cmacc - ((cmacc / 100) * 100)));

  MON_STACK_REPORT();
  MON_ALLOC_REPORT();

#if defined(USE_OBSERVER) && USE_OBSERVER == 1
  printf(" observer res : %d bytes used from the heap (%d c-nodes)\r\n", observer_heap_sz,
      (int)ctx->report.n_nodes);
  aiObserverDone(ctx);
#endif /* USE_OBSERVER */

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
  printf("# %s %d.%d\r\n", _APP_NAME_ , _APP_VERSION_MAJOR_,
      _APP_VERSION_MINOR_ );
  printf("#\r\n");

  systemSettingLog();

  cyclesCounterInit();

  aiInit();

  srand(3); /* deterministic outcome */

  // test_reloc();

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

