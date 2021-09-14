

<center><h1>RT-AK 之 STM32</h1></center>

- [简介](#简介)
- [目录结构](#目录结构)
- [命令行参数详细说明](#命令行参数详细说明)
- [插件安装](#插件安装)
- [命令行运行 RT-AK](#命令行运行-RT-AK)
  - [1 基础运行命令](#1-基础运行命令)
  - [2 指定参数运行](#2-指定参数运行)
- [插件内部工作流程](#插件内部工作流程)

## Intor

> Date: 2021/08/18
>
> Update: add the support for  X-CUBE-AI v7.0.0 。you need to download corresponding version for x-cube-ai and set the option `--cube_ai` ，as follow of command to run ai tool.
>
> The folder of --cube_ai is already exist，and local at  `RT-AK\rt_ai_tools\platforms\plugin_stm32\X-CUBE-AI.7.0.0`
>
> ```shell
> # template
> python aitools.py --project <bsp> --model_name <model_name> --platform stm32 --ext_tools "D:\Program Files (x86)\stm32ai-windows-7.0.0\windows" --cube_ai platforms\plugin_stm32\X-CUBE-AI.7.0.0 --clear
> 
> # example
> D:\Project\edge-ai\RT-AK\rt_ai_tools>python aitools.py --project D:\RT-ThreadStudio\workspace\test --model_name mnist --platform stm32 --ext_tools D:\download\stm32ai-windows-7.0.0\windows --cube_ai platforms\plugin_stm32\X-CUBE-AI.7.0.0 --clear
> ```
> 

> Date: 2021/06/21
>
> Update: 该版本插件尚未支持量化功能，需要量化模型的话请自行研究或者与我们联系，欢迎提 PR，下一个版本将会支持量化功能

*The project is submodule of `RT-AK` 。It depends on STM32 X-CUBE-AI*

- Dependency：`X-CUBE-AI`
- Model format：`Keras | TFLite | ONNX`
- Support op：refor to the  `layer_support.html`  in the folder `docs`  

## Folder Tree

```shell
% tree -L 2 stm32 
stm32
├── backend_plugin_stm32
│   ├── backend_cubeai.c
│   ├── backend_cubeai.h
│   └── readme.md
├── config.py  # generate some configuration for `rt_ai_<model_name>_model.h`，and save it to folder <BSP>/applications
├── docs  # `X-CUBE-AI` docs； 
│   ├── command_line_interface.html
│   ├── embedded_client_api.html
│   ├── en.stsw-link009.zip  # `STLink` driver
│   ├── ...
│   ├── relocatable.html
│   ├── RT-AK之STM32快速上手.md
│   ├── stm32.c
│   ├── stm32programmer-cli.pdf
│   └── 量化(未完成).md
├── generate_rt_ai_model_h.py  # generate file `rt_ai_<model_name>_model.h` ，and save it to folder <BSP>/applications
├── gen_rt_ai_model_c.py  # generate file `rt_ai_<model_name>_model.c` ，and save it to folder <BSP>/applications
├── __init__.py
├── plugin_init.py  # Add environment variable `stm32ai` （`X-CUBE-AI` tool）to system.
├── plugin_stm32_parser.py  # `STM32` ai tool parpser
├── plugin_stm32.py  # Call STM32 ai tool
├── prepare_work.py  # 生成两个文件夹，存放 x-cube-ai 静态库和 c-model 文件; 加载对应的 Sconscript
├── README.md
├── run_x_cube_ai.py  # 运行 `stm32ai` 工具，进行模型转换工作
├── Sconscripts  # 模型转换之后，参与到项目 `scons` 编译的脚本文件
│   ├── Middlewares
│   └── X-CUBE-AI
└── X-CUBE-AI.5.2.0  # `STM32Cube.AI` 所提供的静态库
    ├── Copyrights.txt
    └── Middlewares
```

## Command Description

$$
RT-AK\ parameters = （RT-AK\ parameters + STM32\ parameters）
$$

- RT-AK parameters refer to [链接](https://github.com/RT-Thread/RT-AK/tree/main/RT-AK/rt_ai_tools#0x03-%E5%8F%82%E6%95%B0%E8%AF%B4%E6%98%8E)

- STM32 parameters refer to  `plugin_stm32_parser.py` 

| Parameter           | Description                                                  |
| ------------------- | ------------------------------------------------------------ |
| **`--ext_tools`**   | **`X-CUBE-AI` path; It include the tool `stm32ai` and you need set this parameter in command** |
| `--cube_ai`         | `X-CUBE-AI` stm32 ai run time lib，default `./platforms/stm32/X-CUBE-AI.5.2.0` |
| `--rt_ai_example`   | example `rt_ai_<model_name>_model.c` ，default `./platforms/stm32/docs` |
| `--stm_out`         | the folder for `stm32ai` generate intermediate file，default the timestamp. |
| `--workspace`       | `stm32ai` scratch file ，default`./stm32ai_ws` |
| `--val_data`        | default empty，validate dataset， |
| `--compress`        | compress the model，only works for dense layer，optional "1\|4\|8"，default：`1` |
| `--batches`         | the number of random sample，default `10` |
| `--mode`            | "analyze\|validate" mode (optional）+”generate“ mode（must），`1`indicate selected，{'001', '011', '101', '111'} defualt `001` |
| **--network**       | **In`Documents`, the model name of example，default `mnist`** |
| **--enable_rt_lib** | **Enable the define in `project/rtconfgi.h` ，default `RT_AI_USE_CUBE`** |
| --clear              | delete the folder of  `stm_out` ，default `False` |

- example：

  `--ext_tools="D:/Program Files (x86)/stm32ai-windows-5.2.0/windows"`

## Install Plug-in

该插件无需主动安装，

只需要克隆主项目工程：[RT-AK](https://github.com/RT-Thread/RT-AK)

进入到 `RT-AK/rt_ai_tools` 路径下，

**仅需要**在执行 `python aitools.py --xxx` 的同时指定 `platform` 参数为 `stm32` 即可，插件会自动下载。

## 命令行运行 RT-AK

### 1 基础运行命令

请在 `edge-ai/RTAK/tools` 路径下运行该程序。

![](https://gitee.com/lebhoryi/PicGoPictureBed/raw/master/img/20210223145923.png)

```shell
# 基础运行命令
python aitools.py --project=<your_project_path> --model=<your_model_path> --platform=stm32 --ext_tools=<your_x-cube-ai_path> --clear

# 示例
python aitools.py --project="D:\RT-ThreadStudio\workspace\test" --model="./Models/keras_mnist.h5" --platform=stm32 --ext_tools="D:\Program Files (x86)\stm32ai-windows-5.2.0\windows" --clear
```

![image-20210401181247394](https://gitee.com/lebhoryi/PicGoPictureBed/raw/master/img/20210401181248.png)

### 2 指定参数运行

```shell
# 指定转换模型的名称，--model_name 默认为 network
python aitools.py --project=<your_project_path> --model=<your_model_path>  --model_name=<model_name>  --platform=stm32 --ext_tools=<your_x-cube-ai_path>

# 保存运行 stm32ai 线程过程中产生的文件，--clear 默认为空
# 如果存在，则将会删除`stm32ai` 运行时产生的工作文件夹，即`--stm_out`
python aitools.py --project=<your_project_path> --model=<your_model_path> --platform=stm32 --ext_tools=<your_x-cube-ai_path>

# 指定保存运行日志, --log 默认为空
python aitools.py --project=<your_project_path> --model=<your_model_path> --log=./log.log --platform=stm32 --ext_tools=<your_x-cube-ai_path>

# 指定保存的文件夹名称，--stm_out 默认是当天时间，比如 './20210223'
python aitools.py --project=<your_project_path> --model=<your_model_path> --platform=stm32 --ext_tools=<your_x-cube-ai_path> --stm_out <new_dir>

# 指定生成的 c-model 名，--c_model_name 默认是network
python aitools.py --project=<your_project_path> --model=<your_model_path> --platform=stm32 --ext_tools=<your_x-cube-ai_path> --c_model_name=<new_model_name>
```

完整的项目实战例程，请阅读：[RT-AK之STM32快速上手.md](./docs/RT-AK之STM32快速上手.md)

## 插件内部工作流程

- [ ] Quantitation

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

<details>
<summary>功能函数</summary> 
<pre><code>
1 模型是否支持
- 函数：`is_valid_model(model, sup_models)`
- 功能：判断模型是否支持
- input: (model, sup_models_list)
<br>
2 cpu是否支持
- 函数：`is_valid_cpu(project, sup_cpus, cpu="")`
- 功能：根据 `project/rtconfig.py` 提供的 `CPU` 信息判断是否支持
- input: (project, sup_cpus)
- output: cpu
<br>
3 设置环境变量
- 函数：`set_env(plugin_path)`
- 功能：设置 `x-cube-ai: stm32.exe` 为系统变量
- input: (x-cube-ai_path)
<br>
4 生成两个文件夹
- 函数：`pre_sconscript(aitools_out, stm32_dirs, scons_path="platforms/stm32/Sconscripts")`
- 功能：
  1. 生成两个文件夹，分别存放 `x-cube-ai` 静态库和 `c-model` 文件，如果之前存在，先删除原本的文件夹
  2. 加载对应的 `Sconscript`
- input: (stm_out, sconscript_dir, ["Middlewares", "X-CUBE-AI"])
<br>
5 模型转换
- 函数：`stm32ai(model, stm_out, c_model_name, sup_modes, ai_params)`
- 功能：
  1. 将模型转换成 `c-model`，支持三种模式：分析、验证、生成（必须有）
  2. 如果有报错，根据生成的 `report.txt` 文件抛出异常
- input: (model, stm_out, c_model_name, sup_modes_list, [workspace, compress, batches, mode, val_data])
- output: flag_list, etc: [False, True, True] 对应 modes=“011” 三种模型执行是否成功
<br>
6.1 生成 rt_ai_model.h
- 函数：`rt_ai_model_gen(stm_out, project, model_name)`
- 功能：根据生成的 `c-model` 文件生成  `rt_ai_<model_name>_model.h` 文件，保存在 `project/applications` 
- input: (stm_out, project, c_model_name)
<br>
6.2 生成 rt_ai_model.c
- 函数：`load_rt_ai_example(project, rt_ai_example, platform, old_name, new_name)`
- 功能：根据提供的模板文件，生成 `rt_ai_<model_name>_model.c` + `rt_ai_template.c/h`文件，保存在 `project/applications` 
- input: (project, rt_ai_exampl_path, platform, default_model_name, c_model_name)
<br>
7 加载 x-cube-ai libs
- 函数：`load_lib(stm_out, cube_ai_path, cpu, middle=r"Middlewares/ST/AI")`
- 功能：加载 `x-cube-ai` 静态库到 `stm_out` 中
- input: (stm_out, cube_ai_path, cpu, middle=r"Middlewares/ST/AI")
<br>
8 加载到 project
- 函数：`load_to_project(stm_out, project, stm32_dirs)`
- 功能：加载 `stm_out` 两个文件夹到 `project` 中。如果之前有存在，则先删除
- input: (stm_out, project, ["Middlewares", "X-CUBE-AI"])
<br>
9 使能 HAL-CRC
- 函数：`enable_hal_crc(project)`
- 功能：在 `project/board/...` 文件中使能 `HAL_CRC_MODULE_ENABLED`
- input: (project)
</code></pre>
</details>
