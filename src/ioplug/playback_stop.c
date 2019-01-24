// Copyright (c) 2019 Daniel Abrecht
// SPDX-License-Identifier: GPL-3.0-or-later

#include <libasound_module_pcm_tty.h>


CALLBACK( playback, int, stop, (snd_pcm_ioplug_t *io) ){
  (void)io;
  fprintf(stderr, "playback_stop\n");
  return 0;
}
