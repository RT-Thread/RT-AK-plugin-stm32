[TOC]

# 1. 安装 X-CUBE-AI

## 1.1 下载 

![](https://gitee.com/lebhoryi/PicGoPictureBed/raw/master/img/20201210165153.png)

下载 STM32CubeMx 的 AI 扩展包

> 传送门 👉：[X-CUBE-AI](https://www.st.com/zh/embedded-software/x-cube-ai.html)

![](https://gitee.com/lebhoryi/PicGoPictureBed/raw/master/img/20201021163736.png)

下载后的文件夹显示：

![](https://gitee.com/lebhoryi/PicGoPictureBed/raw/master/img/20201210165303.png)

## 1.2 第一次解压

第一次解压，获取以下两个文件：

- `stm32ai-windows-5.2.0.zip`

- `STMicroelectronics.X-CUBE-AI.5.2.0.pack`

## 1.3 第二次解压
对第一个文件解压。

处理第二个文件的时候，有两种情况

- 本地无`keil`

  请用压缩软件解压`STMicroelectronics.X-CUBE-AI.5.2.0.pack`

  ![](https://gitee.com/lebhoryi/PicGoPictureBed/raw/master/img/20201210165936.png)

- 本地有安装`Keil` 软件包

  双击 `STMicroelectronics.X-CUBE-AI.5.2.0.pack` 安装

  ![](https://gitee.com/lebhoryi/PicGoPictureBed/raw/master/img/20201021170726.png)

# 2. 添加系统环境变量

## 2.1 推荐

将 `stm32ai` 执行路径添加到系统环境变量中， `stm32ai` 的路径分别为：

- 从官网下载 `X-CUBE-AI` ，**自定义路径**解压安装

  `"D:\Program Files\x_cube_ai_v_520\STMicroelectronics.X-CUBE-AI.5.2.0"`

- 从官网下载 `X-CUBE-AI` ，**双击**后自动安装在 `Keil` 路径下

  `"D:/Program Files (x86)/Keil_v5/ARM/PACK/STMicroelectronics/X-CUBE-AI/5.2.0"`

- 通过 `CUBE-MX` 图形化软件安装 `X-CUBE-AI`

  ```
  "C:\Users\<user>\STM32Cube\Repository\Packs\STMicroelectronics\X-CUBE-AI\5.2.0\Utilities\windows"
  ```

![](https://gitee.com/lebhoryi/PicGoPictureBed/raw/master/img/20201211120518.png)

1. Open a Windows command prompt
2. Update system `PATH` variable

```powershell
>  set STM32AI=D:\Program Files\x_cube_ai_v_520\windows
>  setx path "%path%;%STM32AI%"
```

3. Verify the environment

```powershell
>  stm32ai --version
stm32ai - Neural Network Tools for STM32 v1.4.0 (AI tools v5.2.0)
```

or

```powershell
>  stm32ai --tools_version
Neural Network Tools for STM32 v1.4.0 (AI tools v5.2.0)
- Python version   : 3.5.7
- Numpy version    : 1.17.2
- TF version       : 2.3.0
- TF Keras version : 2.4.0
- Caffe version    : 1.0.0
- Lasagne version  : 0.2.dev1
- ONNX version     : 1.6.0
- ONNX RT version  : 1.1.2
```

## 2.2 手残党

1. 找到`stm32ai-windows-5.2.0.zip` 的解压文件夹路径，此处我解压在同文件夹下面，即：`D:\Program Files\x_cube_ai_v_520\windows`
2. 设置环境变量，如下所示

<img src="https://gitee.com/lebhoryi/PicGoPictureBed/raw/master/img/20200827110942.png" alt="增加环境变量" style="zoom:100%;" />

<img src="https://gitee.com/lebhoryi/PicGoPictureBed/raw/master/img/20201021173618.png" style="zoom:100%;" />

3. 设置成功，验证通过

<img src="https://gitee.com/lebhoryi/PicGoPictureBed/raw/master/img/20201021173101.png" style="zoom: 80%;" />

# 3. 运行示例

```shell
C:\Users\12813>stm32ai --help
usage: stm32ai [-h] [--model FILE] [--version] [--verbosity [{0,1,2}]]
               [--type [keras|tflite|caffe|convnetjs|lasagne|onnx]]
               [--name [STR]] [--compression [1|4|8]] [--quantize [FILE]]
               [--allocate-inputs] [--workspace [DIR]] [--output [DIR]]
               [--batches INT] [--mode MODE] [--desc DESC]
               [--valinput FILE [FILE ...]] [--valoutput FILE [FILE ...]]
               [--full] [--binary] [--address ADDR] [--copy-weights-at ADDR]
               analyze|generate|validate|quantize

Neural Network Tools for STM32 v1.2.0 (AI tools v5.0.0)

positional arguments:
  analyze|generate|validate|quantize
                        command

optional arguments:
  -h, --help            show this help message and exit
  --model FILE, -m FILE
                        model files
  --version             print version of the CLI
  --verbosity [{0,1,2}], -v [{0,1,2}], --verbose [{0,1,2}]
                        set verbosity level
  --type [keras|tflite|caffe|convnetjs|lasagne|onnx], -t [keras|tflite|caffe|convnetjs|lasagne|onnx]
                        force the model type to use
  --name [STR], -n [STR]
                        name of the C-network
  --compression [1|4|8], -c [1|4|8]
                        compression factor
  --quantize [FILE], -q [FILE]
                        configuration file to quantize the network
  --allocate-inputs     indicate that the inputs should be allocated in the
                        activation buffer
  --workspace [DIR], -w [DIR]
                        workspace folder to use
  --output [DIR], -o [DIR]
                        folder where generated files are saved
  --batches INT, -b INT
                        number of samples to use for the validation
  --mode MODE           validation mode to use [x86|stm32]
  --desc DESC, -d DESC  COM port to use, with the format [COMPORT[:Baudrate]]
  --valinput FILE [FILE ...], -vi FILE [FILE ...]
                        files to use as input for validation
  --valoutput FILE [FILE ...], -vo FILE [FILE ...]
                        files to use as output for validation
  --full                enable a full validation process
  --binary              generate model weights as a binary file
  --address ADDR        adress of the weight array (can be external memory)
  --copy-weights-at ADDR, --copy-weight-at ADDR
                        Include code to copy weights to specified address

Examples:
        stm32ai analyze -m mymodel.h5 -c 8

        stm32ai analyze -m myquantizedmodel.h5 -q myquantizeconfig.json

        stm32ai validate -m mymodel.h5 -c 4 -vi test_data.csv

        stm32ai generate -m myquantizedmodel.h5 -q myquantizeconfig.json -o output_dir --binary

        stm32ai quantize -q quantizer_conf.json
```
