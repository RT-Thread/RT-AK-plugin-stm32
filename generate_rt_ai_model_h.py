# coding=utf-8
'''
@ Summary: based on {X-CUBE-AI/<model>.h, X-CUBE-AI/<model>_data.h, Documents/rt_ai_mnist_model.h}
           to generate <project>/applications/rt_ai_<model>_model.h

@ Update:  

@ file:    generate_rt_ai_model_h.py
@ version: 1.0.0

@ Author:  Lebhoryi@gmail.com
@ Date:    2021/1/29 14:12
'''
import os
import sys
import logging
from pathlib import Path

path = os.path.dirname(__file__)
sys.path.append(os.path.join(path, '../../'))

from platforms.stm32.config import input_and_output_info


def config_activates_weights_info(model_name_upper, model_data_h, cube_ai_info, rt_ai_info):
    """ save activate and weight size"""
    model_info = list()
    # activates and weights buffers
    with model_data_h.open() as fr:
        lines = fr.readlines()

    for line in lines:
        if cube_ai_info["cube_ai_activations"] in line:
            act_size = line.split()[-1]
            # ['#define', 'AI_NETWORK_DATA_ACTIVATIONS_SIZE', '(3536)']
            model_info.append(f"{rt_ai_info['rt_ai_activations']}{act_size}\n\n")
        elif cube_ai_info["cube_ai_weights"] in line:
            weights_size = line.split()[-1]
            model_info.append(f"{rt_ai_info['rt_ai_weights']}{weights_size}\n\n")

    return model_info


def multiple_inputs_and_outputs(cube_ai, rt_ai, num):
    """ add input num, default is one input output """
    # 1. expand raw input/output
    cube_ai *= num
    rt_ai *= num

    # 2. replace default index(1) with true index
    for i in range(1, num):
        cube_ai[2*i] = cube_ai[2*i].replace("1", str(i+1))
        cube_ai[2*i + 1] = cube_ai[2*i + 1].replace("1", str(i+1))
        rt_ai[2*i] = rt_ai[2*i].replace("1", str(i+1))
        rt_ai[2*i + 1] = rt_ai[2*i + 1].replace("1", str(i+1))

    return cube_ai, rt_ai


def handle_inputs_outputs(numbers, cube_ai, rt_ai, line, in_out_size, model_info, size_index,
                          total_size, rt_ai_total_size):
    """ 1. get RT_AI_NETWORK_IN/OUT_1_SIZE; RT_AI_NETWORK_IN/OUT_1_SIZE_BYTES, save new list
        2. modify 'model_info' RT_AI_NETWORK_IN/OUT_SIZE_BYTES values
        3. get RT_AI_MNIST_IN_TOTAL_SIZE_BYTES
    """

    value = "(" + line.strip().split("(")[-1]
    for i in range(numbers):
        if cube_ai[2*i + 1] in line:
            # size bytes
            in_out_size.append(f"{rt_ai[2*i + 1]}\t\t({value}\n")

            # step2. need to modify model_info["input_size"]
            model_info[size_index+i+1] = f"\t({value},\t\\\n"

            # step3. total_size, str
            total_size = value if i == 0 else (total_size + " + " + value)

            # add total input size
            if i == (numbers-1):
                in_out_size.append(f"{rt_ai_total_size}\t({total_size}\n\n\n\n")
        elif cube_ai[2*i] in line:
            in_out_size.append(f"{rt_ai[2*i]}\t\t\t{value}\n")

    return in_out_size, total_size


def config_input_output_info(model_info, model_name_upper, model_h, cube_ai_info, rt_ai_info):
    """ save input and output size"""
    with model_h.open() as fr:
        lines = fr.readlines()

    cube_ai_input_info = cube_ai_info["input_info"]
    rt_ai_input_info = rt_ai_info["input_info"]
    cube_ai_output_info = cube_ai_info["output_info"]
    rt_ai_output_info = rt_ai_info["output_info"]


    # input size and size_bytes
    cube_ai_inputs = cube_ai_input_info["inputs"]
    rt_ai_inputs = rt_ai_input_info["inputs"]
    cube_ai_outputs = cube_ai_output_info["outputs"]
    rt_ai_outputs = rt_ai_output_info["outputs"]

    # input numbers, output numbers
    input_num, output_num, = 1, 1,

    for i, line in enumerate(lines):
        value = "(" + line.strip().split("(")[-1]
        # alignment
        if cube_ai_input_info["cube_ai_alignment"] in line:
            model_info.append(f"{rt_ai_input_info['rt_ai_alignment']}{value}\n\n")

        # input nums
        elif cube_ai_input_info["cube_ai_in_num"] in line:
            model_info.append(f"{rt_ai_input_info['rt_ai_in_num']}{value}\n\n")
            input_num = int(value[1:-1])
            if input_num > 1:
                cube_ai_inputs, rt_ai_inputs = multiple_inputs_and_outputs(cube_ai_inputs,
                                                                           rt_ai_inputs, input_num)
        # per input size bytes
        elif cube_ai_input_info["input_size"] in line:
            model_info.append(rt_ai_input_info["input_size"])
            model_info += lines[i+1:i+2+input_num]

        # output info
        elif cube_ai_output_info["cube_ai_out_num"] in line:
            model_info.append(f"{rt_ai_output_info['rt_ai_out_num']}{value}\n\n")
            output_num = int(value[1:-1])
            if output_num > 1:
                cube_ai_outputs, rt_ai_outputs = multiple_inputs_and_outputs(cube_ai_outputs,
                                                                             rt_ai_outputs, output_num)
        # per output size bytes
        elif cube_ai_output_info["output_size"] in line:
            model_info.append(rt_ai_output_info["output_size"])
            model_info += lines[i+1:i+2+output_num]

    # rt_ai input_size/output_size index
    input_size_index = model_info.index(rt_ai_input_info["input_size"])
    output_size_index = model_info.index(rt_ai_output_info["output_size"])

    # rt_ai input_size/output_size
    rt_ai_inputs_size, rt_ai_outputs_size = list(), list()

    # total_size
    total_size = str()
    for line in lines:
        rt_ai_inputs_size, total_size  = \
            handle_inputs_outputs(input_num, cube_ai_inputs, rt_ai_inputs,line,
                                  rt_ai_inputs_size, model_info, input_size_index,
                                  total_size, rt_ai_input_info["total_input_size"])
        rt_ai_outputs_size, total_size = \
            handle_inputs_outputs(output_num, cube_ai_outputs, rt_ai_outputs, line,
                                  rt_ai_outputs_size, model_info, output_size_index,
                                  total_size, rt_ai_output_info["total_output_size"])

    # add inputs
    model_info = model_info[:input_size_index+input_num+2] + rt_ai_inputs_size + \
                 model_info[input_size_index+input_num+2:]

    # update output_size_index
    output_size_index = model_info.index(rt_ai_output_info["output_size"])

    # add outputs
    model_info = model_info[:output_size_index+output_num+2] + rt_ai_outputs_size + \
                 model_info[output_size_index +output_num+2:]

    return model_info


def rt_ai_model_gen(stm_out, project, model_name):
    ''' generate rt_ai_<model_name>_model.h'''
    # where save <model_name>.h and <model_name>_data.h
    x_cube_ai = Path(stm_out) / "X-CUBE-AI/App"
    assert x_cube_ai.exists(), "No X-CUBE-AI/App exists, pls check the path!!!"

    # load perpared cube_ai_info and rt_ai_info
    cube_ai_info, rt_ai_info, model_name_upper = input_and_output_info(model_name)

    # the files from x-cube-ai
    model_h = x_cube_ai / (model_name + ".h")
    model_data_h = x_cube_ai / (model_name + "_data.h")

    # save model info; include model name/input/output/bytes
    # activates and weights size
    model_info = config_activates_weights_info(model_name_upper, model_data_h,
                                               cube_ai_info, rt_ai_info)

    # input and output
    model_info = config_input_output_info(model_info, model_name_upper, model_h,
                                          cube_ai_info, rt_ai_info)

    # save the new line
    result = rt_ai_info["head_info"] + model_info + rt_ai_info["tail_info"]

    # project/applications/<model>.h
    pro_app_model_h = Path(project) / "applications" / f"rt_ai_{model_name}_model.h"

    if pro_app_model_h.exists():  pro_app_model_h.unlink()

    with pro_app_model_h.open("w+") as fw:
        fw.write("".join(result))

    logging.info(f"Generate rt_ai_{model_name}_model.h successfully...")


if __name__ == "__main__":
    from prepare_work import pre_sconscript
    from plugin_init import set_env
    from run_x_cube_ai import stm32ai

    logging.getLogger().setLevel(logging.INFO)

    # 1. set env
    ext_tools = r"D:\Program Files (x86)\stm32ai-windows-5.2.0\windows"
    _ = set_env(ext_tools)

    # 2. prepare tmp output
    aitools_out, dir_names, scons_path = 'tmp_cwd', ["Middlewares", "X-CUBE-AI"], "Sconscripts"
    _ = pre_sconscript(aitools_out, scons_path, dir_names)

    # 3. test stm32ai
    class Opt():
        def __init__(self):
            self.stm_out = "tmp_cwd/stm32ai_middle"
            self.model_name = "network"
            self.workspace = "./stm32ai_ws"
            self.compress = 1
            self.batches = 10
            self.mode = "011"
            self.val_data = ''

    opt = Opt()
    model = "../../Model/keras_mnist.h5"
    stm_out = "tmp_cwd"
    c_model_name = "network"
    sup_modes = ["001", "011", "101", "111"]

    ai_params = [opt.workspace, opt.compress, opt.batches, opt.mode, opt.val_data]
    _ = stm32ai(model, stm_out, c_model_name, sup_modes, ai_params)

    project = Path("tmp_cwd")
    tmp_project = project / "applications"
    if not tmp_project.exists():
        tmp_project.mkdir()

    rt_ai_example = "../../Documents"
    _ = rt_ai_model_gen(stm_out, project, "network")
    print("u a right...")
