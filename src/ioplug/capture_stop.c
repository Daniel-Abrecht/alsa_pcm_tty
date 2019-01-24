// Copyright (c) 2019 Daniel Abrecht
// SPDX-License-Identifier: GPL-3.0-or-later

#include <libasound_module_pcm_tty.h>


CALLBACK( capture, int, stop, (snd_pcm_ioplug_t *io) ){
  (void)io;
  fprintf(stderr, "capture_stop\n");
  return 0;
}
