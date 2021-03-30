# coding=utf-8
'''
@ Summary: Platform: stm32ai
            1. check part
            2. prepare part
            3. convert model
            4. load lib
            5. load to project
@ Update:  

@ file:    stm32.py
@ version: 1.0.0

@ Author:  Lebhoryi@gmail.com
@ Date:    2020/12/22 20:55

@ Update:  add is_valid_model function
@ Date:    2021/02/18

@ Update:  load ai_app_template.c/h & rt_ai_<model_name>_model.c from Documents
            to project/applications
            在稳定版将会移除该函数
@ Date:     2021/02/23

@ Update:   remove template .c/.h
@ Date:     2021/03/12
'''
import os
import sys
import re
import logging
import datetime
import shutil
from pathlib import Path

path = os.path.dirname(__file__)
sys.path.append(os.path.join(path, '../../'))

from platforms.stm32.config import *
from platforms.stm32 import prepare_work
from platforms.stm32 import plugin_init
from platforms.stm32 import run_x_cube_ai
from platforms.stm32 import generate_rt_ai_model_h
from platforms.stm32 import gen_rt_ai_model_c



def readonly_handler(func, path):
    # Change the mode of file, to make it could be used of shutil.rmtree
    os.chmod(path, 128)
    func(path)


class Plugin(object):
    def __init__(self, opt):
        self.project = opt.project  # project path
        self.model_path = opt.model_path  # model path
        self.rt_ai_example = opt.rt_ai_example  # Documents
        self.platform = opt.platform
        self.c_model_name = opt.model_name  # c model name

        # config.py
        self.sup_models = sup_models
        self.sup_cpus = sup_cpus
        self.stm32_dirs = stm32_dirs  # x-cube-ai libraries and c-model dir
        self.sconscript_path = sconscript_path
        self.sup_modes = sup_modes  # support modes:{analyze, validate, generate}

        # stm32
        self.ext_tools = opt.ext_tools  # x-cube-ai: stm32ai
        self.cube_ai = opt.cube_ai  # x-cube-ai libraries
        # self.c_model_name = opt.c_model_name  # c model name
        # self.stm32_dirs = opt.stm32_dirs  # Middlewares X-CUBE-AI
        self.network = opt.network  # default network name in sample files
        self.enable_rt_lib = opt.enable_rt_lib  # enable stm32 in <pro>/rtconfig.h
        self.flag = opt.flag

        # x-cube-ai:stm32ai fixed parameters
        self.stm32_ai_fixed_params = [opt.workspace, opt.compress, opt.batches, opt.mode, opt.val_data]

        # setting aitools: x-cube-ai output path
        self.stm_out = opt.stm_out if opt.stm_out else \
            datetime.date.today().strftime("%Y%m%d")

        ##############
        # check part #
        ##############
        # check x-cube-ai libraries
        assert "Middlewares" in os.listdir(self.cube_ai), \
            IOError("No stm32ai liabaries found, pls check the path...")

        # check the model
        self.is_valid_model(self.model_path, self.sup_models)

        # check the cpu
        self.cpu = self.is_valid_cpu(self.project, self.sup_cpus)


    def is_valid_model(self, model, sup_models):
        """ Determine whether the model supports"""
        # model suffix: ".h5"
        m_suf = Path(model).suffix

        # all supportted models suffix
        m_suf_lists = list()
        for value in sup_models.values():
            m_suf_lists += value

        logging.info("The model is '{}'".format(Path(model).name))
        if m_suf not in m_suf_lists:
            raise IOError("The '{}' is not surpported now...".format(model))


    def is_valid_cpu(self, project, sup_cpus, cpu=""):
        """ Determine whether the cpu supports"""
        project = Path(project)
        assert project.exists(), IOError("{} does not exist".format(project))

        # get cpu information
        sys.path.append(str(project))  # add rt_config.py path
        import rtconfig
        # CPU = 'cortex-m3'
        real_cpu = rtconfig.CPU[7:].upper()  # M4 M7 M33

        # get chip information
        rt_config_path = project / "rtconfig.h"
        with open(rt_config_path, "r") as f:
            rt_config_text = f.read()
        chip = re.findall(r"SOC_SERIES_STM32\w\d", rt_config_text)[0]
        platform = chip[16:]  # H7 MP1 WL

        if real_cpu in sup_cpus:
            cpu = real_cpu
        elif platform in sup_cpus:
            cpu = platform
        else:
            raise Exception("The cpu is not in supported now...")

        logging.info("The cpu is '{}'".format(cpu))
        return cpu


    def get_lib_path(self, stm_lib, cpu):
        """ load lib path """
        # select M7 folders
        for dir in os.listdir(stm_lib):
            if cpu in dir:
                lib_path = stm_lib / dir
                lib_path = list(lib_path.iterdir())[0]
                filename = "lib" + lib_path.name if stm_lib.name[:3] == "GCC" \
                    else lib_path.name
                return lib_path, filename


    def load_lib(self, stm_out, cube_ai_path, cpu, middle=r"Middlewares/ST/AI"):
        """ Loading x-cube-ai libs to <stm_out> from stm32ai package

        Args:
            stm_out: x_cube_ai output path, str
            cube_ai_path: x_cube_ai libraries
            path, str
            cpu: the project's cpu, str
            middle: r"Middlewares/ST/AI", str

        Returns:
            result: AI Lib files would be copied. list

        Raise:
            Failed copy Inc/Lib dir from <cube_ai_path> to <stm_out>
        """
        # list of aitools_out files
        result = list()
        target, source = Path(stm_out), Path(cube_ai_path)

        # load x-cube-ai package path
        source_list = [source / middle / "Inc", source / middle / "Lib"]
        target_list = [target / middle / "Inc", target / middle / "Lib"]

        # load Inc
        if target_list[0].exists():  # if the file have existed, delete it first.
            shutil.rmtree(target_list[0], onerror=readonly_handler)
        try:
            shutil.copytree(source_list[0], target_list[0])
        except Exception:
            raise Exception("Failed to load Inc???")

        # load Lib
        for dir in source_list[1].iterdir():
            # support IAR GCC MDK 2020/12/11
            if dir.name[:3] in {"GCC", "MDK", "ABI"}:
                # lib file path, new lib file name
                lib_file, filename = self.get_lib_path(dir, cpu)
                # maybe there is no lib file exists
                if not lib_file:
                    raise Exception("Failed to load X-CUBE-AI Lib,"
                                    " no matched libs???")
                shutil.copyfile(lib_file, target_list[1] / filename)
                result.append(filename)
        logging.info("Loading stm32ai libs successfully...")


    def load_to_project(self, stm_out, project, stm32_dirs):
        """ load X-CUBE-AI / Middleware dir to project """
        # load X-CUBE-AI & Middleware
        for path in stm32_dirs:
            source, target = Path(stm_out) / path, Path(project) / path
            if target.exists():
                shutil.rmtree(target, onerror=readonly_handler)
            try:
                shutil.copytree(source, target)
            except Exception:
                raise Exception("Failed to load {}???".format(path))
            logging.info("{} loading to project successfully...".format(source.name))


    def enable_hal_crc(self, project):
        """ enable HAL_CRC_MODULE_ENABLED """
        Inc_path = Path(project) / "board"
        Inc_path = list(Inc_path.rglob("Inc"))[0]
        # stm32l4xx_hal_conf.h
        file_path = [path for path in Inc_path.iterdir() if "hal_conf" in path.name]

        try:
            with file_path[0].open() as fr:
                lines = fr.readlines()
        except:
            raise FileNotFoundError("No hal crc file!!!")
        else:
            # hal_crc index
            index = [i for i in range(len(lines))
                     if "HAL_CRC_MODULE_ENABLED" in lines[i]][0]
            new_line = " ".join(lines[index].split()[1:-1])
            new_line += "\n"

            if "*" in lines[index]:
                lines[index] = new_line
                with file_path[0].open("w") as fw:
                    fw.write("".join(lines))
                logging.info("Enable HAL_CRC successfully...")
            else:
                logging.info("Don't need to enable HAL_CRC angain!!!")


    def run_plugin(self,):
        """start x-cube-ai:stm32ai running """
        # 1. prepare part
        # 1.1 stm32 ext_tools env settings
        if self.ext_tools:
            plugin_init.set_env(self.ext_tools)

        # 1.2 create two dirs and SConscripts
        prepare_work.pre_sconscript(self.stm_out, self.sconscript_path, self.stm32_dirs)


        # 2. convert model
        flags_list = run_x_cube_ai.stm32ai(self.model_path, self.stm_out, self.c_model_name, self.sup_modes,
                                           self.stm32_ai_fixed_params)


        # 3.1 generate rt_ai_<model_name>_model.h
        _ = generate_rt_ai_model_h.rt_ai_model_gen(self.stm_out, self.project, self.c_model_name)

        # 3.2 load rt_ai_<model_name>_model.c
        _ = gen_rt_ai_model_c.load_rt_ai_example(self.project, self.rt_ai_example, self.platform,
                                    self.network, self.c_model_name)


        # 4. load lib from <cube_ai> to <stm_out>
        # copy lib files from stm to current dir
        self.load_lib(self.stm_out, self.cube_ai, self.cpu)


        # 5. load <stm_out> to project
        self.load_to_project(self.stm_out, self.project, self.stm32_dirs)


        # 6. hal crc enable
        self.enable_hal_crc(self.project)


        # 7. remove x-cube-ai output dirs or not
        if os.path.exists(self.stm_out) and self.flag:
            shutil.rmtree(self.stm_out, onerror=readonly_handler)


if __name__ == "__main__":
    os.chdir("../..")
    logging.getLogger().setLevel(logging.INFO)

    class Opt():
        def __init__(self):
            self.project = r"D:\RT-ThreadStudio\workspace\test"
            self.cube_ai = "./platforms/stm32/X-CUBE-AI.5.2.0"
            self.ext_tools = r"D:\Program Files (x86)\stm32ai-windows-5.2.0\windows"
            self.stm_out = "./tmp_cwd"
            self.rt_ai_example = "./Documents"
            self.model_name = "network"
            self.platform = "stm32"

            # stm32
            self.model_path = "./Model/keras_mnist.h5"
            self.flag = False
            self.cpu = "M7"
            self.enable_rt_lib = "RT_AI_USE_CUBE"
            self.workspace = "./tmp_cwd/stm32ai_ws"
            self.compress = 1
            self.batches = 10
            self.mode = "011"
            self.val_data = ''
            self.network = "mnist"

    opt = Opt()
    stm32 = Plugin(opt)
    stm32.run_plugin()