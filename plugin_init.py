# coding=utf-8
'''
@ Summary: platform plugin init, such as x-cube-ai
@ Update:  add decode stm_info

@ file:    plugin_init.py
@ version: 1.0.0

@ Author:  Lebhoryi@gmail.com
@ Date:    2020/12/16 20:08
'''
import os
import logging
import subprocess


def set_env(plugin_path):
    """ set plugin_path Path"""
    assert os.path.exists(plugin_path), 'No {} here'.format(plugin_path)

    # set stm32ai system env
    os.environ['PATH'] = plugin_path

    # validate
    p = subprocess.Popen("stm32ai --version", shell=True, stdout=subprocess.PIPE).stdout
    stm_info = p.read().split(b"\r\n")[0]
    stm_info = stm_info.decode("utf-8")
    if not stm_info:
        raise Exception("Set plugin env wrong???")
    else:
        logging.info(stm_info)


if __name__ == "__main__":
    plugin = r"D:\Program Files (x86)\stm32ai-windows-5.2.0\windows"
    logging.getLogger().setLevel(logging.INFO)
    set_env(plugin)
    print("u a right...")
