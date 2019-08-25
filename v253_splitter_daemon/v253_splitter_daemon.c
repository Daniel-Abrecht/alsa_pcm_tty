#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <poll.h>
#include <grp.h>
#include <pty.h>

volatile uint8_t* shm;
const char* userdef;
int modem_fd = -1;
int master = -1;

int send_modem(const char* cmd){
  dprintf(modem_fd, "%s\r", cmd);
  return 0;
}

void iflush(int fd){
  static char devnull[4096];
  tcflush(fd, TCIFLUSH);
  for(int i=0; i<10; i++){
    usleep(1000);
    read(fd, devnull, sizeof(devnull));
  }
  tcflush(fd, TCIFLUSH);
}

int start_vtr(void){
  if(shm[0])
    return 0;
  iflush(modem_fd);
  if(send_modem("AT+VTR") != -1){
    iflush(modem_fd);
    shm[0] = true;
    return 0;
  }
  return -1;
}

int end_vtr(void){
  if(!shm[0])
    return 0;
  shm[0] = false;
  int ret = send_modem("\x10\x03");
  iflush(modem_fd);
}

int send_command(const char* cmd){
  if(shm[0]){
    // There is no good way to do this & make it work together with the also ioplug thing.
    // I should really do all this in the kernel using a line dicipline instead.
    // Let's just pretend everything is OK for now.
/*    end_vtr();
    send_modem(cmd);
    dprintf(master, "%s\r\nOK\r\n", cmd);
    start_vtr();*/
    // Or, let's just pretend any command fails instead
    dprintf(master, "%s\r\nERROR\r\n", cmd);
  }else{
    send_modem(cmd);
  }
  return 0;
}

int on_user_cmd(uint8_t* data){
  if(!strncmp(data, "ATD", 3)){
    iflush(modem_fd);
    send_command("AT+FCLASS=8.0");
    send_command("AT+FCLASS=8");
    if(userdef)
      send_command(userdef);
    iflush(modem_fd);
    usleep(2000);
    iflush(modem_fd);
  }else if(!strcmp(data, "AT+VTR")){
    int ret = start_vtr();
    // There is no good way to do this & make it work together with the also ioplug thing.
    // I should really do all this in the kernel using a line dicipline instead.
    // Let's just pretend everything is OK for now.
    if(ret == 0){
      dprintf(master, "AT+VTR\r\nOK\r\n");
    }else{
      dprintf(master, "AT+VTR\r\nERROR\r\n");
    }
    return ret;
  }else if(!strcmp(data, "ATH")){
    end_vtr();
    iflush(modem_fd);
    sleep(1);
    iflush(modem_fd);
    sleep(1);
    iflush(modem_fd);
    return send_command("ATH");
  }
  return send_command(data);
}

enum { BUFSIZE = 255 };
struct fakemodem_parser_state {
  uint8_t buf[BUFSIZE+1];
  unsigned i;
  bool error;
};

int read_fakemodem(struct fakemodem_parser_state* s){
  int ret = read(master, s->buf+s->i, 1);
  if(ret == 0)
    return 0;
  if(ret < 0){
    if(errno = EINTR)
      return -1;
    perror("read failed");
    return -1;
  }
  if(s->error){
    if(s->buf[s->i] != '\n')
      return 0;
    dprintf(master, "ERROR\n");
    s->i = 0;
    return 0;
  }
  if(s->i >= BUFSIZE){
    s->error = true;
    s->i = 0;
    return 0;
  }
  if(s->i==1){
    if(s->buf[1] == 'T'){
      s->i++;
      return 0;
    }else{
      s->i = 0;
      s->buf[0] = s->buf[1];
    }
  }
  if(s->i == 0){
    if(s->buf[0]=='A'){
      s->i++;
      return 0;
    }else{
      s->i=0;
      return 0;
    }
  }else if(s->buf[s->i] == '\r'){
    s->buf[s->i] = 0;
    on_user_cmd(s->buf);
    s->i = 0;
    return 0;
  }else{
    s->i++;
    return 0;
  }
  return 0;
}

int open_modem_and_shmem(const char* modem){
  int error = 0;
  modem_fd = open(modem, O_RDWR | O_NOCTTY | O_NONBLOCK);
  struct stat ttystat;

  if(modem_fd == -1){
    error = errno;
    fprintf(stderr, "Failed to open tty device (%s): %s\n", modem, strerror(error));
    goto backout;
  }

  if(fstat(modem_fd, &ttystat) == -1){
    error = errno;
    fprintf(stderr, "Failed to stat tty device (%s): %s\n", modem, strerror(error));
    goto backout_dev_open;
  }

  if(!S_ISCHR(ttystat.st_mode)){
    error = EINVAL;
    fprintf(stderr, "specified tty device file (%s) is not a character device file\n", modem);
    goto backout_dev_open;
  }

  char shm_name[32] = {0};
  snprintf(shm_name, 32, "tty-pcm:%x.%x", (int)(major(ttystat.st_rdev)), (int)(minor(ttystat.st_rdev)));
  int shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
  if(shm_fd == -1){
    error = errno;
    perror("shm_open failed");
    goto backout_dev_open;
  }
  if(ftruncate(shm_fd, 4096) == -1){
    error = errno;
    perror("ftruncate failed");
    goto backout_shm_open;
  }
  shm = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
  if(shm == MAP_FAILED){
    error = errno;
    perror("mmap failed\n");
    goto backout_shm_open;
  }
  close(shm_fd);

  return 0;

backout_shm_open:
  close(shm_fd);
backout_dev_open:
  close(modem_fd);
backout:
  errno = error;
  return -1;
}

int main(int argc, char* argv[]){
  if(argc != 2 && argc != 3){
    fprintf(stderr, "Usage: %s /dev/ttyACM123 [userdefined-at-sequence]\n", argv[0]);
    return 1;
  }
  if(argc == 3)
    userdef = argv[2];
  if(open_modem_and_shmem(argv[1]) == -1){
    perror("open_modem_and_shmem failed");
    return 1;
  }
  int slave = -1;
  if(openpty(&master, &slave, 0, 0, 0) == -1){
    perror("openpty failed");
    return 1;
  }
  close(slave);
  struct fakemodem_parser_state fms;
  memset(&fms, 0, sizeof(fms));

  {
    char buf[256] = {0};
    char* dst = ptsname(master);
    snprintf(buf, sizeof(buf), "%s:AT", argv[1]);
    unlink(buf);
    if(symlink(dst, buf) == -1){
      perror("symlink failed");
      return 1;
    }
  }

  // Try to drop privileges
  setgroups(0,0);
  setgid(65534);
  setuid(65534);

  enum {
    PFD_FAKEMODEM,
    PFD_REALMODEM
  };

  struct pollfd fds[] = {
    [PFD_FAKEMODEM] = {
      .fd = master,
      .events = POLLIN
    },
    [PFD_REALMODEM] = {
      .fd = modem_fd,
      .events = POLLIN
    },
  };

  while(true){

    int ret = poll(fds, 1 + !shm[0], -1);
    if( ret == -1 ){
      if( errno == EINTR )
        continue;
      perror("poll failed");
      return 1;
    }
    if(!ret)
      continue;

    if(fds[PFD_FAKEMODEM].revents & POLLIN){
      if(read_fakemodem(&fms) == -1)
        return 1;
    }

    if(!shm[0] && fds[PFD_REALMODEM].revents & POLLIN){
      char buf[256];
      ssize_t s = read(modem_fd, buf, sizeof(buf));
      if(s == -1 && errno != EINTR)
        return -1;
      if(s > 0)
        while(write(master, buf, s) == -1 && errno == EINTR);
    }

  }
  return 0;
}
