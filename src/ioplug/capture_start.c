// Copyright (c) 2019 Daniel Abrecht
// SPDX-License-Identifier: GPL-3.0-or-later

#include <libasound_module_pcm_tty.h>


CALLBACK( capture, int, start, (snd_pcm_ioplug_t *io) ){
  (void)io;
  fprintf(stderr, "capture_start\n");
  return 0;
}
