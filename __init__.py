# coding=utf-8
'''
@ Summary: 
@ Update:  

@ file:    __init__.py.py
@ version: 1.0.0

@ Author:  Lebhoryi@gmail.com
@ Date:    2020/12/23 16:08
'''
import os
import sys
path = os.path.dirname(__file__)
sys.path.append(os.path.join(path, '../../'))

from platforms.plugin_stm32.config import *
from platforms.plugin_stm32 import prepare_work
from platforms.plugin_stm32 import plugin_init
from platforms.plugin_stm32 import run_x_cube_ai
from platforms.plugin_stm32 import generate_rt_ai_model_h
from platforms.plugin_stm32 import gen_rt_ai_model_c