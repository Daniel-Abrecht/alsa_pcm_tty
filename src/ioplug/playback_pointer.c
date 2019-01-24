// Copyright (c) 2019 Daniel Abrecht
// SPDX-License-Identifier: GPL-3.0-or-later

#include <sys/ioctl.h>
#include <termios.h>

#include <libasound_module_pcm_tty.h>


CALLBACK( playback, snd_pcm_sframes_t, pointer, (snd_pcm_ioplug_t *io) ){
  fprintf(stderr, "playback_pointer\n");
  struct tty_snd_plug* tty = io->private_data;
  return tty->virtual_offset;
}
