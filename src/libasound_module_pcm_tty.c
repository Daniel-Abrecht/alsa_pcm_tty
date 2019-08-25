// Copyright (c) 2019 Daniel Abrecht
// SPDX-License-Identifier: GPL-3.0-or-later

#include <libasound_module_pcm_tty.h>

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

DEFINE_IOPLUG_CALLBACKS(playback)
DEFINE_IOPLUG_CALLBACKS(capture)

#define SPEEDS \
  X(    50) X(    75) X(   110) X(   134) \
  X(   150) X(   200) X(   300) X(   600) \
  X(  1200) X(  1800) X(  2400) X(  4800) \
  X(  9600) X( 19200) X( 38400) X( 57600) \
  X(115200) X(230400)

#define X(Y) B ## Y,
static const speed_t baudconst_list[] = { SPEEDS };
#undef X
#define X(Y) Y,
static const unsigned long baudrate_list[] = { SPEEDS };
#undef X
#undef SPEEDS
static const size_t baudrate_count = sizeof(baudrate_list) / sizeof(*baudrate_list);

const char* pcm_tty_mode_list[] = {
#define X(Y) #Y,
  PCM_TTY_MODES
#undef X
  0
};

unsigned long const2baud(speed_t b){
  for(size_t i=0; i<baudrate_count; i++)
    if(baudconst_list[i] == b)
      return baudrate_list[i];
  return 0;
}

speed_t baud2const(unsigned long s){
  for(size_t i=0; i<baudrate_count; i++)
    if(baudrate_list[i] == s)
      return baudconst_list[i];
  return 0;
}

void free_settings(struct pcm_tty_settings* settings){
  if(settings->device)
    free(settings->device);
  memset(settings, 0, sizeof(*settings));
}

int parse_settings(snd_config_t* conf, const char*const ignore[], struct pcm_tty_settings* ret){
  int error;
  struct pcm_tty_settings settings = {0};
  snd_config_iterator_t i, next;
  snd_config_for_each(i, next, conf){
    snd_config_t* entry = snd_config_iterator_entry(i);
    const char* property;
    if( snd_config_get_id(entry, &property) < 0 )
      continue;
    if(ignore)
      for( const char*const* it=ignore; *it; it++ )
        if(!strcmp(property, *it))
          goto next_entry;
    if( !strcmp(property, "device") ){
      error = snd_config_get_ascii(entry, &settings.device);
      if(error < 0)
        goto backout;
      continue;
    }
    if( !strcmp(property, "baudrate") ){
      long baudrate = 0;
      error = snd_config_get_integer(entry, &baudrate);
      if(error < 0)
        goto backout;
      bool found = false;
      for(size_t i=0; i<baudrate_count; i++){
        if(baudrate_list[i] == (unsigned long)baudrate){
          found = true;
          break;
        }
      }
      if(!found){
        SNDERR("Invalid baud rate");
        error = -EINVAL;
        goto backout;
      }
      settings.baudrate = (unsigned long)baudrate;
      continue;
    }
    if( !strcmp(property, "samplerate") ){
      long samplerate = 0;
      error = snd_config_get_integer(entry, &samplerate);
      if(error < 0)
        goto backout;
      if(samplerate <= 0){
        SNDERR("Samplerate must be bigger than zero");
        error = -EINVAL;
        goto backout;
      }
      settings.samplerate = samplerate;
      continue;
    }
    if( !strcmp(property, "format") ){
      char* tmp = 0;
      error = snd_config_get_ascii(entry, &tmp);
      if(error < 0)
        goto backout;
      settings.format = snd_pcm_format_value(tmp);
      free(tmp);
      if(settings.format == SND_PCM_FORMAT_UNKNOWN){
        SNDERR("Invalid format");
        goto backout;
      }
      continue;
    }
    if( !strcmp(property, "mode") ){
      char* tmp = 0;
      error = snd_config_get_ascii(entry, &tmp);
      if(error < 0)
        goto backout;
      settings.mode = pcm_tty_indexof(tmp, pcm_tty_mode_list);
      free(tmp);
      if(settings.mode == -1){
        SNDERR("Invalid format");
        goto backout;
      }
      continue;
    }
    SNDERR("Unknown field %s", property);
    error = -EINVAL;
    goto backout;
    next_entry:;
  }
  *ret = settings;
  return 0;
backout:
  free_settings(&settings);
  return error;
}

int pcm_tty_configure(snd_pcm_ioplug_t* io){
  struct tty_snd_plug* tty = io->private_data;
  int error;

  error = snd_pcm_ioplug_set_param_list(io, SND_PCM_IOPLUG_HW_FORMAT, 1, (unsigned int[]){tty->settings.format});
  if(error < 0)
    return error;

  error = snd_pcm_ioplug_set_param_list(io, SND_PCM_IOPLUG_HW_ACCESS, 1, (unsigned int[]){SND_PCM_ACCESS_RW_INTERLEAVED});
  if(error < 0)
    return error;

  error = snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_BUFFER_BYTES, 1, 1<<14);
  if(error < 0)
    return error;

  error = snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_CHANNELS, 1, 1);
  if(error < 0)
    return error;

  error = snd_pcm_ioplug_set_param_list(io, SND_PCM_IOPLUG_HW_RATE, 1, (unsigned int[]){tty->settings.samplerate});
  if(error < 0)
    return error;

  return 0;
}

SND_PCM_PLUGIN_DEFINE_FUNC(tty){
  (void)root;

  int error = 0;
  int shm_fd = -1;
  int device_fd = -1;
  void* shm = 0;
  bool in_out_same_tty = false;
  struct stat ttystat;
  struct termios termios;
  struct tty_snd_plug* tty = 0;
  struct pcm_tty_settings s_both={0}, s_capture={0}, s_playback={0};
  memset(&termios, 0, sizeof(termios));
  memset(&ttystat, 0, sizeof(ttystat));

  s_capture.format = SND_PCM_FORMAT_UNKNOWN;
  s_playback.format = SND_PCM_FORMAT_UNKNOWN;
  s_both.format = SND_PCM_FORMAT_UNKNOWN;

  s_capture.mode = PCM_TTY_MODE_INVALID;
  s_playback.mode = PCM_TTY_MODE_INVALID;
  s_both.mode = PCM_TTY_MODE_INVALID;

  error = parse_settings(conf, (const char*[]){"comment","type","playback","capture","hint","debug",0}, &s_both);
  if(error)
    goto backout;

  {
    snd_config_iterator_t i, next;
    snd_config_for_each(i, next, conf){
      snd_config_t* entry = snd_config_iterator_entry(i);
      const char* property;
      if( snd_config_get_id(entry, &property) < 0 )
        continue;
      if( !strcmp(property, "playback") ){
        error = parse_settings(entry, 0, &s_playback);
        if(error)
          goto backout;
        continue;
      }
      if( !strcmp(property, "capture") ){
        error = parse_settings(entry, 0, &s_capture);
        if(error)
          goto backout;
        continue;
      }
      if( !strcmp(property, "debug") ){
        extern bool b_m_debug;
        b_m_debug = true;
      }
    }
  }

  for(struct pcm_tty_settings** it=(struct pcm_tty_settings*[]){&s_capture, &s_playback, 0}; *it; it++ ){
    struct pcm_tty_settings* s = *it;
    if(!s->device && s_both.device)
      if(!( s->device=strdup(s_both.device) )){
        error = -errno;
        goto backout;
      }
    if(s->mode == PCM_TTY_MODE_INVALID)
      s->mode = s_both.mode;
    if(s->format == SND_PCM_FORMAT_UNKNOWN)
      s->format = s_both.format;
    if(!s->baudrate)
      s->baudrate = s_both.baudrate;
    if(!s->samplerate)
      s->samplerate = s_both.samplerate;
    if(!s->iflag)
      s->iflag = s_both.iflag;
    if(!s->oflag)
      s->oflag = s_both.oflag;
    if(!s->lflag)
      s->lflag = s_both.lflag;
    if(!s->cflag)
      s->cflag = s_both.cflag;
  }

  free_settings(&s_both);

  if(!s_playback.device && !s_capture.device){
    SNDERR("No tty device defined");
    error = -EINVAL;
    goto backout;
  }

  struct pcm_tty_settings* settings = stream == SND_PCM_STREAM_PLAYBACK ? &s_playback : &s_capture;
  if(!settings->device){
    SNDERR("Unsupported stream direction: no tty device specified");
    error = -EINVAL;
    goto backout;
  }

  if(settings->mode == PCM_TTY_MODE_INVALID)
    settings->mode = PCM_TTY_MODE_raw;

  if(settings->format == SND_PCM_FORMAT_UNKNOWN){
    if(settings->mode != PCM_TTY_MODE_v253){
      SNDERR("Format must be specified");
      error = -EINVAL;
      goto backout;
    }else{
      s_both.format = SND_PCM_FORMAT_U8; // If no format is specified, default to U8
    }
  }

  in_out_same_tty = s_playback.device && s_capture.device && !strcmp(s_playback.device, s_capture.device);

  device_fd = open(settings->device, (stream == SND_PCM_STREAM_PLAYBACK ? O_WRONLY : (O_RDONLY | O_NDELAY | O_NONBLOCK)) | O_NOCTTY);
  if(device_fd == -1){
    SNDERR("Failed to open tty device (%s)", settings->device);
    error = -errno;
    goto backout;
  }

  if(fstat(device_fd, &ttystat) == -1){
    SNDERR("Failed to stat tty device (%s)", settings->device);
    error = -errno;
    goto backout_dev_open;
  }

  if(!S_ISCHR(ttystat.st_mode)){
    SNDERR("specified tty device file (%s) is not a character device file", settings->device);
    error = EINVAL;
    goto backout_dev_open;
  }

  if(tcgetattr(device_fd, &termios) != 0){
    error = -errno;
    SNDERR("tcgetattr failed");
    goto backout_dev_open;
  }

  unsigned long cur_baudrate_in = const2baud(cfgetispeed(&termios));
  unsigned long cur_baudrate_out = const2baud(cfgetospeed(&termios));

  if(s_playback.baudrate){
    cur_baudrate_out = s_playback.baudrate;
  }else{
    if(!cur_baudrate_out){
      SNDERR("Please set a baud rate");
      goto backout_dev_open;
    }
    s_playback.baudrate = cur_baudrate_out;
  }

  if(s_capture.baudrate){
    cur_baudrate_in = s_capture.baudrate;
  }else{
    if(!cur_baudrate_in){
      SNDERR("Please set a baud rate");
      error = -EINVAL;
      goto backout_dev_open;
    }
    s_capture.baudrate = cur_baudrate_in;
  }

  if( s_capture.samplerate  > s_capture.baudrate
   || s_playback.samplerate > s_playback.baudrate
  ){
    SNDERR("A sample rate higher than the baud rate is impossible");
    error = -EINVAL;
    goto backout_dev_open;
  }

  if(!s_capture.samplerate)
    s_capture.samplerate = s_capture.baudrate;
  if(!s_playback.samplerate)
    s_playback.samplerate = s_playback.baudrate;

  speed_t baudin  = baud2const(s_capture.baudrate);
  speed_t baudout = baud2const(s_playback.baudrate);

  if(in_out_same_tty){
    cfsetispeed(&termios, baudin);
    cfsetospeed(&termios, baudout);
  }else{
    if(stream == SND_PCM_STREAM_CAPTURE){
      cfsetispeed(&termios, baudin);
      cfsetospeed(&termios, baudin);
    }else{
      cfsetispeed(&termios, baudout);
      cfsetospeed(&termios, baudout);
    }
  }

/*
  if(!settings->iflag)
    termios.c_iflag = settings->iflag;
  if(!settings->oflag)
    termios.c_oflag = settings->oflag;
  if(!settings->iflag)
    termios.c_lflag = settings->lflag;
  if(!settings->iflag)
    termios.c_cflag = settings->cflag;
*/

  // The following options aren't used in non-blocking mode
  termios.c_cc[VMIN]  = 0;
  termios.c_cc[VTIME] = 1;

  cfmakeraw(&termios);

  if(tcsetattr(device_fd, TCSANOW, &termios) != 0){
    error = -errno;
    SNDERR("tcsetattr failed");
    goto backout_dev_open;
  }
  {
    struct termios check;
    if(tcgetattr(device_fd, &check) != 0){
      error = -errno;
      SNDERR("tcgetattr failed");
      goto backout_dev_open;
    }
    if( cfgetispeed(&termios) != cfgetispeed(&check)
     || cfgetospeed(&termios) != cfgetospeed(&check)
    ){
      SNDERR("Failed to set baud rate");
      error = -EINVAL;
      goto backout_dev_open;
    }
  }

  if(settings->mode == PCM_TTY_MODE_v253){
    char shm_name[32] = {0};
    snprintf(shm_name, 32, "tty-pcm:%x.%x", (int)(major(ttystat.st_rdev)), (int)(minor(ttystat.st_rdev)));
    shm_fd = shm_open(shm_name, O_RDONLY, 0666);
    if(shm_fd == -1){
      error = errno;
      SNDERR("shm_open failed");
      goto backout_dev_open;
    }
/*    if(ftruncate(shm_fd, 4096) == -1){
      error = errno;
      SNDERR("ftruncate failed");
      close(shm_fd);
      goto backout_dev_open;
    }*/
    shm = mmap(0, 4096, PROT_READ, MAP_SHARED, shm_fd, 0);
    if(shm == MAP_FAILED){
      error = -errno;
      SNDERR("mmap failed\n");
      close(shm_fd);
      goto backout_dev_open;
    }
    close(shm_fd);
  }

  tty = calloc(1, sizeof(*tty));
  if(!tty){
    error = -errno;
    goto backout_dev_open;
  }

  tty->settings = *settings;
  tty->shm_fd = shm_fd;
  tty->shm = shm;
  tty->ioplug.version = SND_PCM_IOPLUG_VERSION;
  tty->ioplug.name = "TTY sound device";
  tty->ioplug.flags = SND_PCM_IOPLUG_FLAG_BOUNDARY_WA;
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
  if(error < 0)
    goto backout_after_alloc;

  error = pcm_tty_configure(&tty->ioplug);
  if(error < 0)
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
  free_settings(&s_both);
  free_settings(&s_capture);
  free_settings(&s_playback);
  return error;
}
SND_PCM_PLUGIN_SYMBOL(tty)
