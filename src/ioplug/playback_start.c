// Copyright (c) 2019 Daniel Abrecht
// SPDX-License-Identifier: GPL-3.0-or-later

#include <libasound_module_pcm_tty.h>


CALLBACK( playback, int, start, (snd_pcm_ioplug_t *io) ){
  (void)io;
  m_debug("playback_start\n");
  return 0;
}
