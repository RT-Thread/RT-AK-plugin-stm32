/**
  ******************************************************************************
  * @file    aiTestUtility.h
  * @author  MCD Vertical Application Team
  * @brief   Low Level nano PB stack functions
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2020 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                             www.st.com/SLA0044
  *
  ******************************************************************************
  */

#ifndef _AI_PB_IO_H_
#define _AI_PB_IO_H_

#include <pb.h>

int pb_io_stream_init(void);

void pb_io_flush_ostream(void);
void pb_io_flush_istream(void);

pb_ostream_t pb_io_ostream(int fd);
pb_istream_t pb_io_istream(int fd);

#endif /* _AI_PB_IO_H_ */
