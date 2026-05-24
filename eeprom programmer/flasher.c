#include <asm-generic/ioctls.h>
#include <curses.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#define USB_SERIAL "/dev/ttyACM1"
#define DATA_FILE "./data"
#define BYTES_PER_LINE 8
#define MAX_DATA_FILE_SIZE 32768

void dump_buf(uint8_t *buf, int len);
void print_text(char *buf);
int config_termios(int port_fd) {
  struct termios tty;
  if (tcgetattr(port_fd, &tty) != 0) {
    perror("Error getting serial attributes");
    return -1;
  }

  // Set Baud Rate to 9600
  cfsetospeed(&tty, B9600);
  cfsetispeed(&tty, B9600);
  if (tcsetattr(port_fd, TCSANOW, &tty) != 0) {
    perror("Error setting serial attributes");
    return -1;
  }
  return 0;
}
int validate_data_file(FILE *data_fp) {
  struct stat st;
  int data_fd = fileno(data_fp);
  fstat(data_fd, &st);
  if (st.st_size > MAX_DATA_FILE_SIZE) {
    printf("Data file exceeds %d bytes", MAX_DATA_FILE_SIZE);
    return -1;
  }
  return 0;
}

void dump_buf(uint8_t *buf, int len) {
  printf("\nbuffer dump:\n");
  int byte_count = 0;
  for (int i = 0; i < len; i++) {
    printf("%#x ", buf[i]);
    // printf("%c", buf[i]);
    if (byte_count++ == BYTES_PER_LINE) {
      printf("\n");
      byte_count = 0;
    }
  }
}

void print_text(char *buf) { printf("\nstring: %s", buf); }

int main() {
  int port_fd = 0, data_fd = 0, len = 0;

  // open usb port
  port_fd = open(USB_SERIAL, O_RDWR | O_NOCTTY);
  if (port_fd < 0) {
    printf("unable to open port");
    return -1;
  }

  // open data file
  FILE *data_fp = fopen(DATA_FILE, "r");
  if (data_fp == NULL) {
    printf("unable to open data file");
    return -1;
  }
  if (validate_data_file(data_fp) == -1) {
    printf("data file invalid");
  }

  if (config_termios(port_fd) == -1) {
    printf("error setting termios config");
  }

  // setup read buffer
  uint8_t cmd[64];
  memset(cmd, 0, sizeof(cmd));
  cmd[0] = 0x80;
  int addr = 0;
  uint8_t bytes_count = 2;
  bytes_count = bytes_count << 1;
  cmd[0] |= bytes_count;
  memcpy(cmd + 1, &addr, 2);
  cmd[3] = 'A';
  cmd[4] = 'B';
  dump_buf(cmd, sizeof(cmd));
  write(port_fd, cmd, sizeof(cmd));

  // read back
  usleep(500);
  cmd[0] = 0x00;
  memcpy(cmd + 1, &addr, 2);
  dump_buf(cmd, sizeof(cmd));
  write(port_fd, cmd, sizeof(cmd));

  addr = 1;
  cmd[0] = 0x00;
  memcpy(cmd + 1, &addr, 2);
  dump_buf(cmd, sizeof(cmd));
  write(port_fd, cmd, sizeof(cmd));

  close(port_fd);
}
