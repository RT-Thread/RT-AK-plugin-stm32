# coding=utf-8
'''
@ Summary: prepare
            1. create two dirs:
                <cwd>/Middlewares
                <cwd>/X-CUBE-AI
            2. load 'SConscript' files
@ Update:  

@ file:    prepare_work.py
@ version: 1.0.0

@ Author:  Lebhoryi@gmail.com
@ Date:    2020/12/2 17:06

@ Update:  remove stm_out dir first
@ Date:    2021/02/22 17:06
'''
import shutil
import logging
from pathlib import Path


def pre_sconscript(stm_out, sconscripts, stm32_dirs):
    """ prepared works:
         1. create two folders: Middlewares
         2. load Sconscripts to 'Middlewares' & 'X-CUBE-AI'

    Args:
        stm_out: x-cube-ai:stm32ai output file, str
        sconscripts: sconscripts files saved, str, default is "platforms/stm32/Sconscripts"
        stm32_dirs: ["Middlewares", "X-CUBE-AI"], list

    Raises:
        {"Middlewares", "X-CUBE-AI"}/SConscript not exists
    """
    stm_out, sconscripts = Path(stm_out), Path(sconscripts)

    # delete the dir first
    if stm_out.exists():  shutil.rmtree(stm_out)

    for i, dir in enumerate(stm32_dirs):
        # step 1: create two dirs ("Middlewares", "X-CUBE-AI")
        new_dir = stm_out / dir / ("ST/AI/Lib" if i == 0 else "App")
        new_dir.mkdir(parents=True, exist_ok=True)

        # step 2: load sconscript file to <stm_out>
        source_scons = sconscripts / dir
        target_scons = stm_out / dir / "SConscript"
        if not source_scons.exists():
            raise FileNotFoundError(f"Not {source_scons} file found!!!")
        shutil.copy(source_scons, target_scons)

    logging.info(f"Create two dirs: {' '.join(stm32_dirs)} successfully...")


if __name__ == "__main__":
    logging.getLogger().setLevel(logging.INFO)

    stm_out = 'tmp_cwd'
    scons_path = "./Sconscripts"
    stm32_dirs = ["Middlewares", "X-CUBE-AI"]

    # 2. prepare tmp output
    _ = pre_sconscript(stm_out, scons_path, stm32_dirs)
    print("u a right...")
