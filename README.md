<center><h1>RT-AK 之 STM32</h1></center>

[TOC]

本项目归属于 `RT-AK` 主项目中的一个子模块。

使用 `STM32` 原厂插件进行开发。

- 原厂插件：`X-CUBE-AI`

# 前期工作

- 下载并解压 `X-CUBE-AI`，下载地址：[X-CUBE-AI](https://www.st.com/zh/embedded-software/x-cube-ai.html) | Version: v5.2.0
- 模型支持：Keras、TFLite、Caffe、ONNX

#  - stm32

### Part2：stm32 参数

| Parameter           | Description                                                  |
| ------------------- | ------------------------------------------------------------ |
| --ext_tools         | `X-CUBE-AI` 存放路径，模型转换工具，内有 `stm32ai` 可执行软件，**需要用户指定** |
| --cube_ai           | `X-CUBE-AI` 运行所需的静态库，默认为`./platforms/stm32/X-CUBE-AI.5.2.0` |
| --stm_out           | 经过 `stm32ai` 线程处理之后产生的中间文件夹路径，默认是当天的时间戳命名 |
| --workspace         | `stm32ai` 运行时产生的临时工作区，默认是`./stm32ai_ws`       |
| --val_data          | 默认为空，即使用内部自生成的随机数据集，允许用户自定义测试数据集， |
| --compress          | 表示将应用的全局压缩因子，仅应用在全连接层，"1\|4\|8"默认值：`1` |
| --batches           | 指示生成了多少随机数据样本，默认是`10`                       |
| --mode              | "analyze\|validate" 模式（可选）+”generate“模式（必须有），`1`表示选中，在`{'001', '011', '101', '111'}`中选一个，默认是 `001` |
| **--network**       | **在 `Documents` 中的模板文件的模型名，默认是 `mnist`**      |
| **--enable_rt_lib** | **在 `project/rtconfgi.h` 中打开宏定义，默认是 `RT_AI_USE_CUBE`** |
| **--flag**          | **是否需要删除 `stm32ai` 生成的中间文件夹 `stm_out` ，默认为`False`** |

## 2. 使用说明

> 目前版本必须指定硬件平台且系统为 Windows10，具有 python3.5 以上运行环境

请在 `edge-ai/RTAK/tools` 路径下运行该程序。

![](https://gitee.com/lebhoryi/PicGoPictureBed/raw/master/img/20210223145923.png)

### Part1：stm32

1. 基础运行命令：

```shell
python aitools.py --project=<your_project_path> --model_path=<your_model_path> --platform=stm32 --ext_tools=<your_x-cube-ai_path> --flag

# 示例
python aitools.py --project="D:\RT-ThreadStudio\workspace\test" --model_path="./Model/keras_mnist.h5" --platform=stm32 --ext_tools="D:\Program Files (x86)\stm32ai-windows-5.2.0\windows" --flag
```

![](https://gitee.com/lebhoryi/PicGoPictureBed/raw/master/img/20210223144253.png)

2. 其他：

```shell
# 指定转换模型的名称，--model_name 默认为 network
python aitools.py --project=<your_project_path> --model_path=<your_model_path>  --model_name=<model_name>  --platform=stm32 --ext_tools=<your_x-cube-ai_path>

# 保存运行 stm32ai 线程过程中产生的文件，--flag 默认为空
# 如果存在，则将会删除`stm32ai` 运行时产生的工作文件夹，即`--stm_out`
python aitools.py --project=<your_project_path> --model_path=<your_model_path> --platform=stm32 --ext_tools=<your_x-cube-ai_path>

# 指定保存运行日志, --log 默认为空
python aitools.py --project=<your_project_path> --model_path=<your_model_path> --log=./log.log --platform=stm32 --ext_tools=<your_x-cube-ai_path>

# 指定保存的文件夹名称，--stm_out 默认是当天时间，比如 './20210223'
python aitools.py --project=<your_project_path> --model_path=<your_model_path> --platform=stm32 --ext_tools=<your_x-cube-ai_path> --stm_out <new_dir>

# 指定生成的 c-model 名，--c_model_name 默认是network
python aitools.py --project=<your_project_path> --model_path=<your_model_path> --platform=stm32 --ext_tools=<your_x-cube-ai_path> --c_model_name=<new_model_name>
```

## 2. stm32

- [x] 判断模型是否支持
- [x] 判断 `CPU` 是否支持
- [x] 设置 `stm32ai` 系统环境变量，`x-cube-ai`
- [x] 在 `stm_out` 下生成静态库文件夹和存放 `c-model` 的文件夹
- [x] 将模型转换成 `c-model`，保存在 `<stm_out>/X-CUBE-AI` 路径下
- [x] 生成 `rt_ai_<model_name>_model.h` 文件，保存在 `project/applications` 
- [x] 生成 `rt_ai_<model_name>_model.c` 文件，保存在 `project/applications` 
- [x] 加载 `x-cube-ai` 的静态库到 `stm_out` 路径下
- [x] 把 `stm_out` 内的两个关键文件夹加载到 `project` 下
- [x] 在 `project` 中使能 `HAL_CRC`
- [x] 判断是否删除 `stm_out`

### 2.1 Function1 - 模型是否支持

- 函数：`is_valid_model(model, sup_models)`
- 功能：判断模型是否支持
- input: (model, sup_models_list)

### 2.2 Function2 - cpu是否支持

- 函数：`is_valid_cpu(project, sup_cpus, cpu="")`
- 功能：根据 `project/rtconfig.py` 提供的 `CPU` 信息判断是否支持
- input: (project, sup_cpus)
- output: cpu

### 2.3 Function3 - 设置环境变量

- 函数：`set_env(plugin_path)`
- 功能：设置 `x-cube-ai: stm32.exe` 为系统变量
- input: (x-cube-ai_path)

### 2.4 Function4 - 生成两个文件夹

- 函数：`pre_sconscript(aitools_out, stm32_dirs, scons_path="platforms/stm32/Sconscripts")`
- 功能：
  1. 生成两个文件夹，分别存放 `x-cube-ai` 静态库和 `c-model` 文件，如果之前存在，先删除原本的文件夹
  2. 加载对应的 `Sconscript`
- input: (stm_out, sconscript_dir, ["Middlewares", "X-CUBE-AI"])

### 2.5 Function5 - 模型转换

- 函数：`stm32ai(model, stm_out, c_model_name, sup_modes, ai_params)`
- 功能：
  1. 将模型转换成 `c-model`，支持三种模式：分析、验证、生成（必须有）
  2. 如果有报错，根据生成的 `report.txt` 文件抛出异常
- input: (model, stm_out, c_model_name, sup_modes_list, [workspace, compress, batches, mode, val_data])
- output: flag_list, etc: [False, True, True] 对应 modes=“011” 三种模型执行是否成功

### 2.6 Function6 - 生成 rt_ai_model.h

- 函数：`rt_ai_model_gen(stm_out, project, model_name)`
- 功能：根据生成的 `c-model` 文件生成  `rt_ai_<model_name>_model.h` 文件，保存在 `project/applications` 
- input: (stm_out, project, c_model_name)

### 2.6 Function6 - 生成 rt_ai_model.c

- 函数：`load_rt_ai_example(project, rt_ai_example, platform, old_name, new_name)`
- 功能：根据提供的模板文件，生成 `rt_ai_<model_name>_model.c` + `rt_ai_template.c/h`文件，保存在 `project/applications` 
- input: (project, rt_ai_exampl_path, platform, default_model_name, c_model_name)

### 2.7 Function7 - 加载 x-cube-ai libs

- 函数：`load_lib(stm_out, cube_ai_path, cpu, middle=r"Middlewares/ST/AI")`
- 功能：加载 `x-cube-ai` 静态库到 `stm_out` 中
- input: (stm_out, cube_ai_path, cpu, middle=r"Middlewares/ST/AI")

### 2.8 Function8 - 加载到 project

- 函数：`load_to_project(stm_out, project, stm32_dirs)`
- 功能：加载 `stm_out` 两个文件夹到 `project` 中。如果之前有存在，则先删除
- input: (stm_out, project, ["Middlewares", "X-CUBE-AI"])

### 2.9 Function9 - 使能 HAL-CRC

- 函数：`enable_hal_crc(project)`
- 功能：在 `project/board/...` 文件中使能 `HAL_CRC_MODULE_ENABLED`
- input: (project)