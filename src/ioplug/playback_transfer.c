// Copyright (c) 2019 Daniel Abrecht
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>

#include <libasound_module_pcm_tty.h>

CALLBACK( playback,
  snd_pcm_sframes_t, transfer, (
    snd_pcm_ioplug_t *io,
    const snd_pcm_channel_area_t *areas,
    snd_pcm_uframes_t offset,
    snd_pcm_uframes_t size
  )
){
  m_debug("playback_transfer: %ld %ld\n", offset, size);
  struct tty_snd_plug* tty = io->private_data;
  ssize_t s, os=size;
  for( unsigned channel=0; channel<io->channels; channel++){
    uint8_t* data_start = (uint8_t*)areas[channel].addr + areas[channel].first / 8;
    if(tty->settings.mode == PCM_TTY_MODE_v253){
      if(tty->shm[0]){
        uint8_t convbuf[256];
        while(os){
          ssize_t m, i;
          for(m=0, i=0; i<os && m<256; i++,m++){
            uint8_t b = data_start[i];
            if(b == C_DLE){
              if(m >= 255)
                break;
              convbuf[m++] = C_DLE;
            }
            convbuf[m] = b;
          }
          os -= i;
          data_start += i;
          uint8_t* cb = convbuf;
          while(m && (s=write(tty->device_fd, cb, m))>0){
            m -= s;
            cb += s;
          }
          if(m) break;
        }
      }else{
        data_start += os;
        os = 0;
      }
    }else{
      while(os && (s=write(tty->device_fd, data_start, os))>0){
        os -= s;
        data_start += s;
      }
    }
    break;
  }
  m_debug("%zd\n", size - os);
  tty->virtual_offset += size - os;
  return size - os;
}
