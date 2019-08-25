#include <libasound_module_pcm_tty.h>

int pcm_tty_indexof(const char* search, const char*const* list){
  for(int i=0; *list; list++, i++)
    if(!strcmp(search, *list))
      return i;
  return -1;
}
