// Copyright (c) 2019 Daniel Abrecht
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>

#include <libasound_module_pcm_tty.h>


CALLBACK( capture,
  snd_pcm_sframes_t, transfer, (
    snd_pcm_ioplug_t *io,
    const snd_pcm_channel_area_t *areas,
    snd_pcm_uframes_t offset,
    snd_pcm_uframes_t size
  )
){
  fprintf(stderr, "capture_transfer: %ld %ld\n", offset, size);
  struct tty_snd_plug* tty = io->private_data;
  ssize_t s, os=size;
  for( unsigned channel=0; channel<io->channels; channel++){
    char* data_start = (char*)areas[channel].addr + areas[channel].first / 8;
    while(os && (s=read(tty->device_fd, data_start, os))>0){
      os -= s;
      data_start += s;
    }
    break; // TODO: Extend if more channels are some day needed
  }
  tty->virtual_offset += size - os;
  fprintf(stderr, "%zd\n", size - os);
  return size - os;
}
