// Copyright (c) 2019 Daniel Abrecht
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LIBASOUND_MODULE_PCM_TTY
#define LIBASOUND_MODULE_PCM_TTY

#include <alsa/asoundlib.h>
#include <alsa/pcm_ioplug.h>
#include <alsa/pcm_external.h>
#include <termios.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef SND_PCM_IOPLUG_FLAG_BOUNDARY_WA
#define SND_PCM_IOPLUG_FLAG_BOUNDARY_WA (1<<2)
#endif

#define IOPLUG_CALLBACKS_REF(PREFIX) tty_snd_plug_ ## PREFIX ## _instance

#define DEFINE_IOPLUG_CALLBACKS(PREFIX) \
  snd_pcm_ioplug_callback_t IOPLUG_CALLBACKS_REF(PREFIX);

#define DECLARE_IOPLUG_CALLBACKS(PREFIX) \
  extern snd_pcm_ioplug_callback_t IOPLUG_CALLBACKS_REF(PREFIX);

#define CALLBACK(PREFIX, RET, IDENTIFIER, ARGS) \
  static void IOPLUG_CALLBACK_SETTER_ ## PREFIX ## _ ## IDENTIFIER(void) __attribute__((constructor,used)); \
  extern RET CALLBACK_ ## PREFIX ## _ ## IDENTIFIER ARGS; \
  static void IOPLUG_CALLBACK_SETTER_ ## PREFIX ## _ ## IDENTIFIER(void) {\
    IOPLUG_CALLBACKS_REF(PREFIX).IDENTIFIER = &CALLBACK_ ## PREFIX ## _ ## IDENTIFIER; \
  } \
  RET CALLBACK_ ## PREFIX ## _ ## IDENTIFIER ARGS


DECLARE_IOPLUG_CALLBACKS(playback)
DECLARE_IOPLUG_CALLBACKS(capture)

#define PCM_TTY_MODES \
  X(raw) \
  X(v253)

enum {
  C_DLE = 0x10,
  C_SUB = 0x1A
};

enum pcm_tty_mode {
  PCM_TTY_MODE_INVALID = -1,
#define X(Y) PCM_TTY_MODE_ ## Y,
  PCM_TTY_MODES
#undef X
};

struct pcm_tty_settings {
  char* device;
  snd_pcm_format_t format;
  unsigned long baudrate;
  unsigned long samplerate;
  tcflag_t iflag;
  tcflag_t oflag;
  tcflag_t cflag;
  tcflag_t lflag;
  enum pcm_tty_mode mode;
};

struct tty_snd_plug {
  snd_pcm_ioplug_t ioplug;
  struct pcm_tty_settings settings;
  int shm_fd;
  volatile const uint8_t* shm;
  int device_fd;
  snd_pcm_sframes_t virtual_offset;
};

int pcm_tty_indexof(const char* search, const char*const* list);

#ifdef __GNUC__
int m_debug(const char* format, ...) __attribute__((format(printf, 1, 2)));
#else
int m_debug(const char* format, ...);
#endif

#endif
