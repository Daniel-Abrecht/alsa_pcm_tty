# Copyright (c) 2019 Daniel Abrecht
# SPDX-License-Identifier: GPL-3.0-or-later

CC = gcc
LD = gcc
AR = ar

SRC += src/libasound_module_pcm_tty.c
SRC += $(wildcard src/ioplug/*.c)

OPTIONS += -g -Og
OPTIONS += -fPIC -DPIC
OPTIONS += -std=c99 -Wall -Wextra -Werror -pedantic
OPTIONS += -I include
OPTIONS += -D _DEFAULT_SOURCE

OBJECTS = $(addprefix tmp/,$(addsuffix .o,$(SRC)))


all: build

build: bin/libasound_module_pcm_tty.so

tmp/libasound_module_pcm_tty.a: $(OBJECTS)
	mkdir -p tmp
	rm -f $@
	$(AR) rs $@ $^

tmp/%.c.o: %.c
	mkdir -p $(basename $@)
	$(CC) $(OPTIONS) $< -lasound -c -o $@

bin/libasound_module_pcm_tty.so: tmp/libasound_module_pcm_tty.a
	$(LD) -shared -fPIC -Werror -Wl,--no-undefined -Wl,--whole-archive $< -Wl,--no-whole-archive -lasound -o $@

clean:
	rm -rf tmp
	rm -f bin/libasound_module_pcm_tty.so

install: build
	cp bin/libasound_module_pcm_tty.so /usr/lib/aarch64-linux-gnu/alsa-lib/libasound_module_pcm_tty.so
	chmod 644 /usr/lib/aarch64-linux-gnu/alsa-lib/libasound_module_pcm_tty.so
	chown root: /usr/lib/aarch64-linux-gnu/alsa-lib/libasound_module_pcm_tty.so
