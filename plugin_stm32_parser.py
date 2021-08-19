# coding=utf-8
'''
@ Summary: 
@ Update:  

@ file:    stm32_parser.py
@ version: 1.0.0

@ Author:  Lebhoryi@gmail.com
@ Date:    2021/1/29 11:52
'''
def platform_parameters(parser):
    """ STM32 platform parameters """
    parser.add_argument("--ext_tools", type=str, default="D:/Program Files (x86)/stm32ai-windows-5.2.0/windows", help="Where saved stm32ai.exe")
    parser.add_argument("--cube_ai", type=str, default="./platforms/plugin_stm32/X-CUBE-AI.5.2.0",
                        help="X-CUBE-AI libraries dir")
    parser.add_argument("--rt_ai_example", type=str, default="./platforms/plugin_stm32/templates",
                        help="Model & platform informations registered to RT-AK Lib, eg:stm32, k210.")
    parser.add_argument("--stm_out", type=str, default="",
                        help="X-CUBE-AI output dir")
    parser.add_argument("--workspace", type=str, default="stm32ai_ws",
                        help="indicates a working/temporary directory for the intermediate/temporary files")
    parser.add_argument("--val_data", type=str, default="",
                        help="indicates the custom test data set which must be used,"
                             "now is not supported")
    parser.add_argument("--compress", type=int, default=1,
                        help="indicates the expected global factor of compression which will be applied."
                             "1|4|8")
    parser.add_argument("--batches", type=int, default=10,
                        help="indicates how many random data sample is generated (default: 10)")
    parser.add_argument("--mode", type=str, default="001",
                        help="Describe analyze|validate|generate, 0 is False")
    parser.add_argument("--network", type=str, default="mnist",
                        help="The model name in '<tools>/Documents/<stm32> files'")
    parser.add_argument("--enable_rt_lib", type=str, default="RT_AI_USE_CUBE",
                        help="Enabel RT-AK Lib using x-cube-ai")
    parser.add_argument("--clear", action="store_true", help="remove stm32ai middleware")
    return parser
