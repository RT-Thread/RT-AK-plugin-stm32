# coding=utf-8
'''
@ Summary: 
@ Update:  

@ file:    config.py.py
@ version: 1.0.0

@ Author:  Lebhoryi@gmail.com
@ Date:    2021/2/25 11:08
'''


# support models
sup_models = {
    "keras": [".h5", ".hdf5", ".json", ".yml", ".yaml"],
    "tflite": [".tflite"],
    "lasagne": [".npz", ".npz"],
    "caffe": [".prototxt", ".caffemodel"],
    "convnetjs": [".json"],
    "onnx": [".onnx"],
}

# support cpus
sup_cpus = ["H7", "MP1", "WL", "M4", "M7", "M33"]

# x-cube-ai libraries and c-model dir
stm32_dirs = ["Middlewares", "X-CUBE-AI"]

# sconscripts path
sconscript_path = "platforms/stm32/Sconscripts"

# support modes:{analyze, validate, generate}
sup_modes = {"001", "011", "101", "111"}


def input_and_output_info(model_name):
    model_name_upper = model_name.upper()
    cube_ai_info = {
        # activations and weights
        "cube_ai_activations": f"#define AI_{model_name_upper}_DATA_ACTIVATIONS_SIZE",
        "cube_ai_weights": f"#define AI_{model_name_upper}_DATA_WEIGHTS_SIZE",

        # inputs info
        "input_info":{
            # activations alignment
            "cube_ai_alignment": f"#define AI_{model_name_upper}_ACTIVATIONS_ALIGNMENT",
            # input num
            "cube_ai_in_num": f"#define AI_{model_name_upper}_IN_NUM",

            "inputs":[f"#define AI_{model_name_upper}_IN_1_SIZE",
                     f"#define AI_{model_name_upper}_IN_1_SIZE_BYTES"],
            "input_size": f"#define AI_{model_name_upper}_IN_SIZE",
        },

        # output info
        "output_info":{
            # output num
            "cube_ai_out_num": f"#define AI_{model_name_upper}_OUT_NUM",
            "outputs": [f"#define AI_{model_name_upper}_OUT_1_SIZE",
                        f"#define AI_{model_name_upper}_OUT_1_SIZE_BYTES"],
            "output_size": f"#define AI_{model_name_upper}_OUT_SIZE"
        }
    }

    rt_ai_info = {
        "head_info":[
        # the 5 head lines
            f"#ifndef __RT_AI_{model_name_upper}_MODEL_H\n",
            f"#define __RT_AI_{model_name_upper}_MODEL_H\n\n",
            "/* model info ... */\n\n",
            "// model name\n",
            f"#define RT_AI_{model_name_upper}_MODEL_NAME\t\t\t\"{model_name}\"\n\n",],

        "tail_info":[
            # the last two lines
            f"\n#define RT_AI_{model_name_upper}_TOTAL_BUFFER_SIZE\t\t//unused\n\n",
            "#endif\t//end\n"],

        # activations and weights
        "rt_ai_activations": f"#define RT_AI_{model_name_upper}_WORK_BUFFER_BYTES\t\t",
        "rt_ai_weights": f"#define AI_{model_name_upper}_DATA_WEIGHTS_SIZE\t\t",

        # inputs info
        "input_info": {
            # alignment
            "rt_ai_alignment": f"#define RT_AI_{model_name_upper}_BUFFER_ALIGNMENT\t\t",
            # input num
            "rt_ai_in_num": f"#define RT_AI_{model_name_upper}_IN_NUM\t\t\t\t",

            "inputs": [
                f"#define RT_AI_{model_name_upper}_IN_1_SIZE",
                f"#define RT_AI_{model_name_upper}_IN_1_SIZE_BYTES"
            ],

            "input_size": f"#define RT_AI_{model_name_upper}_IN_SIZE_BYTES\t{'{'}\t\\\n",
            "total_input_size": f"#define RT_AI_{model_name_upper}_IN_TOTAL_SIZE_BYTES\t"
        },

        # output info
        "output_info":{
            # output num
            "rt_ai_out_num": f"#define RT_AI_{model_name_upper}_OUT_NUM\t\t\t\t",

            "outputs": [
                f"#define RT_AI_{model_name_upper}_OUT_1_SIZE",
                f"#define RT_AI_{model_name_upper}_OUT_1_SIZE_BYTES"
            ],

            "output_size": f"#define RT_AI_{model_name_upper}_OUT_SIZE_BYTES\t{'{'}\t\\\n",
            "total_output_size": f"#define RT_AI_{model_name_upper}_OUT_TOTAL_SIZE_BYTES\t"
        },
    }

    return cube_ai_info, rt_ai_info, model_name_upper
