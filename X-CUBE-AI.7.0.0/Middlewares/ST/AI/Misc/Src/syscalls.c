/*
 *  minimum syscall redefinition to force Keil AC6 to use functions without breakpoints
 */

#if defined (__CC_ARM) || defined(__ARMCC_VERSION)
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <rt_sys.h>
#define FH_STDIN    0x8001
#define FH_STDOUT   0x8002
#define FH_STDERR   0x8003
/* Standard IO device name defines. */
const char __stdin_name[]  = ":STDIN";
const char __stdout_name[] = ":STDOUT";
const char __stderr_name[] = ":STDERR";
__attribute__((weak))
FILEHANDLE _sys_open (const char *name, int openmode) {
  if (name[0] == ':') {
    if (strcmp(name, ":STDIN") == 0) {
      return (FH_STDIN);
    }
    if (strcmp(name, ":STDOUT") == 0) {
      return (FH_STDOUT);
    }
    if (strcmp(name, ":STDERR") == 0) {
      return (FH_STDERR);
    }
  }
  return (-1);
}
 
 
__attribute__((weak))
int _sys_close (FILEHANDLE fh) {
  switch (fh) {
    case FH_STDIN:
      return (0);
    case FH_STDOUT:
      return (0);
    case FH_STDERR:
      return (0);
  }
  return (-1);
}
__attribute__((weak))
int _sys_write (FILEHANDLE fh, const uint8_t *buf, uint32_t len, int mode) {
  switch (fh) {
    case FH_STDIN:
      return (-1);
    case FH_STDOUT:
      return (0);
    case FH_STDERR:
      return (0);
  }
 
  return (-1);
}
__attribute__((weak))
int _sys_read (FILEHANDLE fh, uint8_t *buf, uint32_t len, int mode) {
  return (-1);
}
__attribute__((weak))
int _sys_istty (FILEHANDLE fh) {
  switch (fh) {
    case FH_STDIN:
      return (1);
    case FH_STDOUT:
      return (1);
    case FH_STDERR:
      return (1);
  }
 
  return (0);
}
 
__attribute__((weak))
int _sys_seek (FILEHANDLE fh, long pos) {
      return (-1);
}

__attribute__((weak))
long _sys_flen (FILEHANDLE fh) {
      return (0);
}
#endif
