// Copyright (c) 2019 Daniel Abrecht
// SPDX-License-Identifier: GPL-3.0-or-later

#include <libasound_module_pcm_tty.h>

#include <sys/ioctl.h>
#include <termios.h>


CALLBACK( capture, snd_pcm_sframes_t, pointer, (snd_pcm_ioplug_t *io) ){
  struct tty_snd_plug* tty = io->private_data;
  int available = 0;
  if(ioctl(tty->device_fd, TIOCINQ, &available) == -1 || available < 0)
    available = 0;
  m_debug("capture_pointer %ld + %d = %ld\n", tty->virtual_offset, available, tty->virtual_offset + available );
  return tty->virtual_offset + available;
}
