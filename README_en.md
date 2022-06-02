<center><h1>RT-AK on STM32</h1></center>

 [中文](./README.md) | [English](./README_en.md) 

- [Intro](#Intro)
- [Project Tree](#Project-Tree)
- [Command Description](#Command-Description)
- [Install Plug-in](#Install-Plug-in)
- [Run RT-AK Command](#Run-RT-AK-Command)
  - [1 Run command](#1-Run-command)
  - [2 Set command parameter](#2-Set-command-parameter)
- [Plug-in Workflow](#Plug-in-Workflow)

## Intor

> Date: 2021/08/18
>
> Update: add the support for  X-CUBE-AI v7.0.0 . you need to download corresponding version for x-cube-ai and set the option `--cube_ai`, as follow of command to run ai tool.
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
> Update: Current version is not support to Quantitation function

*The project is submodule of `RT-AK` .It depends on STM32 X-CUBE-AI*

- Dependency：`X-CUBE-AI`
- Model format：`Keras | TFLite | ONNX`
- Support op：refor to the  `layer_support.html`  in the folder `docs`  

## Project Tree

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
│   ├── RT-AK_on_STM32_quickly_start.md (Chinese only yet)
│   ├── stm32.c
│   ├── stm32programmer-cli.pdf
│   └── Quantization(todo).md
├── generate_rt_ai_model_h.py  # generate file `rt_ai_<model_name>_model.h` ，and save it to folder <BSP>/applications
├── gen_rt_ai_model_c.py  # generate file `rt_ai_<model_name>_model.c` ，and save it to folder <BSP>/applications
├── __init__.py
├── plugin_init.py  # Add environment variable `stm32ai` （`X-CUBE-AI` tool）to system.
├── plugin_stm32_parser.py  # `STM32` ai tool parpser
├── plugin_stm32.py  # Call STM32 ai tool
├── prepare_work.py  # generate two folder，one is for x-cube-ai lib and another for c-model source.
├── README.md
├── run_x_cube_ai.py  # run `stm32ai` , 
├── Sconscripts  # `scons` build script
│   ├── Middlewares
│   └── X-CUBE-AI
└── X-CUBE-AI.5.2.0  # `STM32Cube.AI` lib
    ├── Copyrights.txt
    └── Middlewares
```

## Command Description

$$
RT-AK\ parameters = （RT-AK\ parameters + STM32\ parameters）
$$

- RT-AK parameters refer to [link](https://github.com/RT-Thread/RT-AK/tree/main/RT-AK/rt_ai_tools#0x03-%E5%8F%82%E6%95%B0%E8%AF%B4%E6%98%8E)

- STM32 parameters refer to  `plugin_stm32_parser.py` 

| Parameter           | Description                                                  |
| ------------------- | ------------------------------------------------------------ |
| **`--ext_tools`**   | **`X-CUBE-AI` path; It include the tool `stm32ai` and you need set this parameter in command** |
| `--cube_ai`         | `X-CUBE-AI` stm32 ai run time lib，default `./platforms/stm32/X-CUBE-AI.5.2.0` |
| `--rt_ai_example`   | example `rt_ai_<model_name>_model.c` ，default `./platforms/stm32/docs` |
| `--stm_out`         | the folder for `stm32ai` generate intermediate file，default is to create a folder using timestamp as name. |
| `--workspace`       | `stm32ai` scratch file ，default`./stm32ai_ws` |
| `--val_data`        | default empty，validate dataset, |
| `--compress`        | compress the model，only works for dense layer，optional "1\|4\|8"，default：`1` |
| `--batches`         | the number of random sample，default `10` |
| `--mode`            | "analyze\|validate" mode (optional）+”generate“ mode（must）, `1`indicate selected，{'001', '011', '101', '111'} defualt `001` |
| **--network**       | **In`Documents`, the model name of example，default `mnist`** |
| **--enable_rt_lib** | **Enable the define in `project/rtconfgi.h` ，default `RT_AI_USE_CUBE`** |
| --clear              | delete the folder of  `stm_out` ，default `False` |

- example：

  `--ext_tools="D:/Program Files (x86)/stm32ai-windows-5.2.0/windows"`

## Install Plug-in

The Plug-in is installed automatically. You just clone the Project ：[RT-AK](https://github.com/RT-Thread/RT-AK)

In the path`RT-AK/rt_ai_tools` , 

run command `python aitools.py --xxx` and set the parameter `platform`  = `stm32` ，then download  automatically the plug-in.

## Run RT-AK Command

### 1 Run command

Please run this tools in path  `edge-ai/RTAK/tools` 

![](https://gitee.com/lebhoryi/PicGoPictureBed/raw/master/img/20210223145923.png)

```shell
# run command template
python aitools.py --project=<your_project_path> --model=<your_model_path> --platform=stm32 --ext_tools=<your_x-cube-ai_path> --clear

# example
python aitools.py --project="D:\RT-ThreadStudio\workspace\test" --model="./Models/keras_mnist.h5" --platform=stm32 --ext_tools="D:\Program Files (x86)\stm32ai-windows-5.2.0\windows" --clear
```

![image-20210401181247394](https://gitee.com/lebhoryi/PicGoPictureBed/raw/master/img/20210401181248.png)

### 2 Set command parameter

```shell
# set the converted model name，--model_name default network
python aitools.py --project=<your_project_path> --model=<your_model_path>  --model_name=<model_name>  --platform=stm32 --ext_tools=<your_x-cube-ai_path>

# saved stm32ai intermediate file，--clear default None
# if it already exist，then delete the folder generate by `--stm_out`
python aitools.py --project=<your_project_path> --model=<your_model_path> --platform=stm32 --ext_tools=<your_x-cube-ai_path>

# set the log save path, --log default None
python aitools.py --project=<your_project_path> --model=<your_model_path> --log=./log.log --platform=stm32 --ext_tools=<your_x-cube-ai_path>

# set the intermediate file save path，--stm_out default timestamp，like './20210223'
python aitools.py --project=<your_project_path> --model=<your_model_path> --platform=stm32 --ext_tools=<your_x-cube-ai_path> --stm_out <new_dir>

# set the converted c-model name，--c_model_name default network
python aitools.py --project=<your_project_path> --model=<your_model_path> --platform=stm32 --ext_tools=<your_x-cube-ai_path> --c_model_name=<new_model_name>
```

**Note: 'ai_tools.py' will add '#define RT_AI_USE_CUBE' to 'rtconfig.h'. If the project is configured after the model is injected into the project using the tool, this macro will be refreshed, resulting in compilation errors.  Therefore, it is necessary to complete the project configuration before using the tool for model injection**
 
Complete routines：[RT-AK_on_STM32_quickly_start.md](./docs/RT-AK之STM32快速上手.md) *(Chinese only yet)*

## Plug-in Workflow

- [ ] Quantitation
- [x] Check whether the model is supported
- [x] Check whether the `CPU` is supported
- [x] Set the environment variable `stm32ai` to system，`x-cube-ai`
- [x] Generate folder for lib and folder for c-model in  `stm_out` 
- [x] Convert to  `c-model`，and save it to path  `<stm_out>/X-CUBE-AI` 
- [x] Generate header `rt_ai_<model_name>_model.h` ，and save it to `project/applications` 
- [x] Generate source `rt_ai_<model_name>_model.c` ，and save it to `project/applications` 
- [x] Copy  `x-cube-ai` lib  to path  `stm_out` 
- [x] Copy the files in  `stm_out` to  `project` 
- [x] Enable  stm32 `HAL_CRC`
- [x] Delete `stm_out`


<details>
<summary>Functions</summary> 
<pre><code>	
1 Check whether the model is supported

- @fn：`is_valid_model(model, sup_models)`
- @brief：Check whether the model is supported
- input: (model, sup_models_list)

2 Check whether the CPU is supported
- @fn：`is_valid_cpu(project, sup_cpus, cpu="")`
- @brief：Check whether the CPU is supported
- input: (project, sup_cpus)
- output: cpu

3 Set the environment variable
- @fn：`set_env(plugin_path)`
- @brief：Set the environment variable
- input: (x-cube-ai_path)

4 Generate tow folder
- @fn：`pre_sconscript(aitools_out, stm32_dirs, scons_path="platforms/stm32/Sconscripts")`
- @brief：
  1. Generate folder for lib and folder for c-model
- input: (stm_out, sconscript_dir, ["Middlewares", "X-CUBE-AI"])

5 Convert model

- @fn：`stm32ai(model, stm_out, c_model_name, sup_modes, ai_params)`
- @brief：Convert model
- input: (model, stm_out, c_model_name, sup_modes_list, [workspace, compress, batches, mode, val_data])
- output: flag_list, etc: [False, True, True] euqal to modes=“011” 

6.1 Generate rt_ai_model.h
- @fn：`rt_ai_model_gen(stm_out, project, model_name)`
- @brief：generate rt_ai_model.h, and save it to `project/applications` 
- input: (stm_out, project, c_model_name)

6.2 Generate rt_ai_model.c
- @fn：`load_rt_ai_example(project, rt_ai_example, platform, old_name, new_name)`
- @brief：generate rt_ai_model.c, and save it to `project/applications` 
- input: (project, rt_ai_exampl_path, platform, default_model_name, c_model_name)

7 Copy x-cube-ai libs
- @fn：`load_lib(stm_out, cube_ai_path, cpu, middle=r"Middlewares/ST/AI")`
- @brief：copy `x-cube-ai` lib to `stm_out` 
- input: (stm_out, cube_ai_path, cpu, middle=r"Middlewares/ST/AI")

8 Copy to project

- @fn：`load_to_project(stm_out, project, stm32_dirs)`
- @brief：copy some files in `stm_out` to `project`, it will delete files  already exist 
- input: (stm_out, project, ["Middlewares", "X-CUBE-AI"])

9 Enable HAL-CRC
- @fn：`enable_hal_crc(project)`
- @brief：Enable `HAL_CRC_MODULE_ENABLED`
- input: (project)
</code></pre>
</details>
