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


def check_file(file):
    assert Path(file).exists(), logging.error(f"No such file {file} exists!")


def read_file(file):
    check_file(file)
    with open(file, "r") as f:
        lines = f.readlines()
    return lines


def get_model_info(template, model_h, model_name, default_name="network"):
    # model_info = []  # modol informations
    input_nums, output_nums = 1, 1

    template_lines = read_file(template)
    model_h_lines = read_file(model_h)

    for line in model_h_lines:
        if "AI_NETWORK_IN_NUM" in line:
            input_nums = int(line.split()[-1][1:-1])
        elif "AI_NETWORK_OUT_NUM" in line:
            output_nums = int(line.split()[-1][1:-1])

    all_input_size_bytes = [f"AI_{model_name.upper()}_IN_{i+1}_SIZE_BYTES" for i in range(input_nums)]
    all_output_size_bytes = [f"AI_{model_name.upper()}_OUT_{i+1}_SIZE_BYTES" for i in range(output_nums)]

    for i, line in enumerate(template_lines):
        # replace model name
        if model_name != default_name and default_name in line:
            template_lines[i] = template_lines[i].replace(default_name, model_name)
        if model_name != default_name and default_name.upper() in line:
            template_lines[i] = template_lines[i].replace(default_name.upper(), model_name.upper())

        if "IN_TOTAL_SIZE_BYTES" in line:
            in_total_size_bytes = "(" + "+".join(all_input_size_bytes) + ")"
            template_lines[i] = template_lines[i].replace("NULL", in_total_size_bytes)
        elif "IN_SIZE_BYTES" in line:
            in_size_bytes = "{" + ", ".join(all_input_size_bytes) + "}"
            template_lines[i] = template_lines[i].replace("NULL", in_size_bytes)
        elif "OUT_TOTAL_SIZE_BYTES" in line:
            out_total_size_bytes = "(" + "+".join(all_output_size_bytes) + ")"
            template_lines[i] = template_lines[i].replace("NULL", out_total_size_bytes)
        elif "OUT_SIZE_BYTES" in line:
            out_size_bytes = "{" + ", ".join(all_output_size_bytes) + "}"
            template_lines[i] = template_lines[i].replace("NULL", out_size_bytes)

    return template_lines


def rt_ai_model_gen(stm_out, project, model_name, rt_ai_example):
    ''' generate rt_ai_<model_name>_model.h'''
    # where save <model_name>.h and <model_name>_data.h
    x_cube_ai = Path(stm_out) / "X-CUBE-AI/App"
    assert x_cube_ai.exists(), "No X-CUBE-AI/App exists, pls check the path!!!"

    # the files from x-cube-ai
    model_h = x_cube_ai / (model_name + ".h")

    # modol informations
    template_file = Path(rt_ai_example)/"rt_ai_template_model.h"
    model_info = get_model_info(template_file, model_h, model_name)

    # project/applications/<model>.h
    pro_app_model_h = Path(project) / "applications" / f"rt_ai_{model_name}_model.h"

    if pro_app_model_h.exists():  pro_app_model_h.unlink()

    with pro_app_model_h.open("w+") as fw:
        fw.write("".join(model_info))

    logging.info(f"Generate rt_ai_{model_name}_model.h successfully...")


if __name__ == "__main__":
    from prepare_work import pre_sconscript
    from plugin_init import set_env
    from run_x_cube_ai import stm32ai

    logging.getLogger().setLevel(logging.INFO)

    # 1. set env
    ext_tools = r"D:\Program Files (x86)\stm32ai-windows-7.0.0\windows"
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
            self.cube_ai = r"D:\Program Files (x86)\Keil_v5\PACK\STMicroelectronics\X-CUBE-AI\7.0.0"

    opt = Opt()
    model = "../../Models/mnist.tflite"
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
