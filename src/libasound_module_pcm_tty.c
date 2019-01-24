// Copyright (c) 2019 Daniel Abrecht
// SPDX-License-Identifier: GPL-3.0-or-later

#include <alsa/asoundlib.h>
#include <alsa/pcm_ioplug.h>
#include <alsa/pcm_external.h>

#include <libasound_module_pcm_tty.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdint.h>


DEFINE_IOPLUG_CALLBACKS(playback)
DEFINE_IOPLUG_CALLBACKS(capture)

#define SPEEDS \
  X(    50) X(    75) X(   110) X(   134) \
  X(   150) X(   200) X(   300) X(   600) \
  X(  1200) X(  1800) X(  2400) X(  4800) \
  X(  9600) X( 19200) X( 38400) X( 57600) \
  X(115200) X(230400)

/*#define X(Y) B ## Y,
static const speed_t ttyspeed[] = { SPEEDS };
#undef X
*/
#define X(Y) Y,
static const unsigned int ttybaud[] = { SPEEDS };
#undef X
#undef SPEEDS

static int ioplug_hw_setup(struct tty_snd_plug *tty){
  int error;

  unsigned int access_list[] = {
    SND_PCM_ACCESS_RW_INTERLEAVED,
    SND_PCM_ACCESS_RW_NONINTERLEAVED
  };

  unsigned int format_list[] = { SND_PCM_FORMAT_U8, SND_PCM_FORMAT_S16_LE, SND_PCM_FORMAT_S32_LE };

  error = snd_pcm_ioplug_set_param_list(&tty->ioplug, SND_PCM_IOPLUG_HW_FORMAT, sizeof(format_list) / sizeof(format_list[0]), format_list);
  if(error < 0)
    return error;

  error = snd_pcm_ioplug_set_param_list(&tty->ioplug, SND_PCM_IOPLUG_HW_ACCESS, sizeof(access_list) / sizeof(*access_list), access_list);
  if(error < 0)
    return error;

  error = snd_pcm_ioplug_set_param_minmax(&tty->ioplug, SND_PCM_IOPLUG_HW_BUFFER_BYTES, 1, 1024);
  if(error < 0)
    return error;

  error = snd_pcm_ioplug_set_param_minmax(&tty->ioplug, SND_PCM_IOPLUG_HW_CHANNELS, 1, 1);
  if(error < 0)
    return error;

  error = snd_pcm_ioplug_set_param_list(&tty->ioplug, SND_PCM_IOPLUG_HW_RATE, sizeof(ttybaud) / sizeof(*ttybaud), ttybaud);
  if(error < 0)
    return error;

  return 0;
}

SND_PCM_PLUGIN_DEFINE_FUNC(tty){
  (void)root;

  int error = 0, device_fd = 0;
  struct tty_snd_plug* tty = 0;
  snd_config_t* device = 0;

  {
    snd_config_iterator_t i, next;
    snd_config_for_each(i, next, conf){
      snd_config_t* entry = snd_config_iterator_entry(i);
      const char* property;
      if( snd_config_get_id(entry, &property) < 0 )
        continue;
      if( !strcmp(property, "comment") || !strcmp(property, "type") )
        continue;
      if( !strcmp(property, "card") ){
        device = entry;
        continue;
      }
      SNDERR("Unknown field %s", property);
      error = -EINVAL;
      goto backout;
    }
  }

  if(!device){
    SNDERR("No card/device defined for tty");
    error = -EINVAL;
    goto backout;
  }

  {
    char* tmp;
    error = snd_config_get_ascii(device, &tmp);
    if(error < 0)
      goto backout;
    device_fd = open(tmp, (stream == SND_PCM_STREAM_PLAYBACK ? O_WRONLY : O_RDONLY) | O_NOCTTY | O_NDELAY | O_NONBLOCK);
    if(device_fd == -1){
      SNDERR("Failed to open tty device (%s)", tmp);
      free(tmp);
      error = -errno;
      goto backout;
    }
    free(tmp);
  }

  tty = calloc(1, sizeof(*tty));
  if(!tty){
    error = -ENOMEM;
    goto backout_dev_open;
  }

  tty->ioplug.version = SND_PCM_IOPLUG_VERSION;
  tty->ioplug.name = "TTY sound device";
  tty->ioplug.flags = SND_PCM_IOPLUG_FLAG_BOUNDARY_WA;
  tty->ioplug.buffer_size = 1024;
  tty->ioplug.poll_fd = tty->device_fd = device_fd;
  switch(stream){
    case SND_PCM_STREAM_PLAYBACK: {
      tty->ioplug.callback = &IOPLUG_CALLBACKS_REF(playback);
      tty->ioplug.poll_events = POLLOUT;
    } break;
    case SND_PCM_STREAM_CAPTURE: {
      tty->ioplug.callback = &IOPLUG_CALLBACKS_REF(capture);
      tty->ioplug.poll_events = POLLIN;
    } break;
  }
  tty->ioplug.private_data = tty;

  error = snd_pcm_ioplug_create(&tty->ioplug, name, stream, mode);
  if( error < 0 )
    goto backout_after_alloc;

  error = ioplug_hw_setup(tty);
  if( error < 0 )
    goto backout_after_snd_pcm_ioplug_create;

  *pcmp = tty->ioplug.pcm;
  return 0;

backout_after_snd_pcm_ioplug_create:
  snd_pcm_ioplug_delete(&tty->ioplug);
backout_after_alloc:
  free(tty);
backout_dev_open:
  close(device_fd);
backout:
  return error;
}
SND_PCM_PLUGIN_SYMBOL(tty)
