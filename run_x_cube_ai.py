# coding=utf-8
'''
@ Summary: support analyze|validate(option) generate mode
@ Update:  

@ file:    run_x_cube_ai.py
@ version: 1.0.0

@ Author:  Lebhoryi@gmail.com
@ Date:    2020/12/9 16:58
'''
import os
import re
import subprocess
import shutil
import logging
from pathlib import Path


def readonly_handler(func, path, execinfo):
    # Change the mode of file, to make it could be used of shutil.rmtree
    os.chmod(path, 128)
    func(path)


def excute_cmd(cmd):
    """ Returnning string after the command is executed """
    p = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE).stdout
    return p.read().split(b"\r\n")


def by_read_line(filename):
    """ Read a report, yield line """
    f = open(filename, "r+")
    line = f.readline()
    while line:
        yield line
        line = f.readline()
    f.close()


def is_stm32ai_success(c_model_name, output, mode, result):
    """check the stm23ai:ana|val|gen is running successfully """
    def is_mode_success(result, mode, report_path):
        if b"elapsed time" not in result:
            if not os.path.isfile(report_path):
                raise Exception("Failed to {}, pls check the params".format(mode))
            else:
                reader = by_read_line(report_path)
                for line in reader:
                    if re.findall(r"^error", line):
                        line = line.strip().split(": ")
                        raise Exception("{}: {}".format(line[1], line[2]))
        return True

    # saved strs after cmd excuted.
    flags = [False for _ in range(len(mode))]

    # three modes
    modes_list = ["analyze", "validate", "generate"]
    for i in range(len(modes_list)):
        if mode[i]:
            report_file_name = f"{c_model_name}_{modes_list[i]}_report.txt"
            report_file_path = os.path.join(output[i], report_file_name)
            flags[i] = is_mode_success(result[i], modes_list[i], report_file_path)

    return flags


def stm32ai(model, stm_out, c_model_name, sup_modes, ai_params):
    """ convert model to c-model by x-cube-ai:stm32ai

    Args:
        model: indicates the original model file paths, str
        stm_out: indicates the output directory for the generated C-files and report files, str
        c_model_name: indicates the C-name (C-string type) of the imported model, str
        sup_modes: suppport modes(analyze, validate, generate), {"001", "011", "101", "111"}
        ai_params: [workspace, compress, batches, mode, val_data], list

        workspace: indicates a working/temporary directory for the intermediate/temporary files
        compress: indicates the expected global factor of compression which will be applied, int
        batches: indicates how many random data sample is generated (default: 10), int
        mode: chooses which cmd to excution, list
        val_date: indicates the custom test data set which must be used.

    Returns:
        flag: return the True, list, len(list) == the number of true modes
    """
    def analyze(model, output, model_name, compress):
        analyze_cmd = "stm32ai analyze -m {} -o {} -w {} -n {} -c {}".format(
                       model, output, workspace, model_name, compress)
        return analyze_cmd


    def validate(model, output, model_name, batches, val_date):
        if val_date:
            # using random {batches} val data
            validate_cmd = "stm32ai validate -m {} -o {} -w {} -n {} -vi {} " \
                           "--validate.batch_mode {} -b {}".format(model,
                                                                   output, workspace, model_name, val_date, "random", batches)
        else:
            validate_cmd = "stm32ai validate -m {} -o {} -w {} -n {} -b {}".format(
                            model, output, workspace, model_name, batches)
        return validate_cmd


    def generate(model, output, model_name, compress):
        # generate the dir "X-CUBE-AI/App"
        generate_cmd = "stm32ai generate -m {} -o {} -w {} -n {} -c {}".format(
            model, output, workspace, model_name, compress)
        return generate_cmd


    workspace = ai_params[0]
    compress = ai_params[1]
    batches = ai_params[2]
    mode = ai_params[3]
    val_date = ai_params[4]

    # if the mode is valid: {'001', '011', '101', '111'}
    assert mode in sup_modes, Exception("Wrong mode???")
    mode = list(map(int, list(mode)))  # str to list

    # save generate mode files
    sub_path = Path(stm_out) / "X-CUBE-AI/App"
    # analyze, validate, generate
    output_list = [stm_out, stm_out, str(sub_path)]

    # stm32ai command for different mode
    commands = list()
    commands.append(analyze(model, output_list[0], c_model_name, compress))
    commands.append(validate(model, output_list[1], c_model_name, batches, val_date))
    commands.append(generate(model, output_list[2], c_model_name, compress))

    # Store the returned string after the command is executed
    result = [excute_cmd(commands[index]) if elem else list()
              for index, elem in enumerate(mode)]

    flags = is_stm32ai_success(c_model_name, output_list, mode, result)

    logging.info("Model convert to c-model successfully...")

    # remove workspace
    if os.path.exists(workspace):
        shutil.rmtree(workspace, onerror=readonly_handler)

    return flags


if __name__ == "__main__":
    from prepare_work import pre_sconscript
    from plugin_init import set_env

    logging.getLogger().setLevel(logging.INFO)

    # 1. set env
    ext_tools = r"D:\Program Files (x86)\stm32ai-windows-5.2.0\windows"
    _ = set_env(ext_tools)

    # 2. prepare tmp output
    aitools_out, dir_names, scons_path = 'tmp_cwd', ["Middlewares", "X-CUBE-AI"], "./Sconscripts"
    _ = pre_sconscript(aitools_out, scons_path, dir_names)

    # 3. test stm32ai
    class Opt():
        def __init__(self):
            self.workspace = "./stm32ai_ws"
            self.compress = 1
            self.batches = 10
            self.mode = "111"
            self.val_data = ''

    opt = Opt()
    model_path = "../../Model/keras_mnist.h5"
    stm_out = "tmp_cwd"
    c_model_name = "network"
    sup_modes = ["001", "011", "101", "111"]
    ai_params = [opt.workspace, opt.compress, opt.batches, opt.mode, opt.val_data]
    _ = stm32ai(model_path, stm_out, c_model_name, sup_modes, ai_params)
    print("u a right...")