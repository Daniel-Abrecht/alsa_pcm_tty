#include <libasound_module_pcm_tty.h>
#include <stdbool.h>

extern bool b_m_debug;
bool b_m_debug = false;

int m_debug(const char* format, ...){
  if(!b_m_debug)
    return 0;
  va_list args;
  va_start(args, format);
  int ret = vfprintf(stderr, format, args);
  va_end(args);
  return ret;
}
