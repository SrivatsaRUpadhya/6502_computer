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

#define USB_SERIAL "/dev/ttyACM0"
#define DATA_FILE "./data"
#define BYTES_PER_LINE 8
#define MAX_DATA_FILE_SIZE 32768
#define CMD_LEN 63
#define DATA_OFFSET 3
#define OP_WRITE 0x80
#define OP_READ 0x00
#define MAX_BYTES_PER_READ 62
#define MAX_BYTES_PER_WRITE 60
#define ADDRESS_OFFSET 1
#define ADDRESS_SIZE 2
#define RES_PACKET_SIZE 63
#define RES_DATA_OFFSET 1
#define EEPROM_SIZE 32767

void dump_buf(uint8_t *buf, int len);
void print_text(char *buf);
void write_data(short addr, FILE *data_fp, int port_fd, int data_file_size);
void dump_eeprom(int port_fd, short addr);

int config_termios(int port_fd) {
  struct termios tty;
  if (tcgetattr(port_fd, &tty) < 0) {
    printf("error getting termios config");
    return -1;
  }
  struct termios config;

  cfmakeraw(&config);
  config.c_cflag |= (CLOCAL | CREAD);
  config.c_iflag &= ~(IXOFF | IXANY);

  // set vtime, vmin, baud rate...
  config.c_cc[VMIN] = RES_PACKET_SIZE; // you likely don't want to change this
  config.c_cc[VTIME] = 0;              // or this

  cfsetispeed(&config, B9600);
  cfsetospeed(&config, B9600);

  // write port configuration to driver
  tcsetattr(port_fd, TCSANOW, &config);
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
  return st.st_size;
}

void dump_cmd_data(uint8_t *buf, int len) {
  printf("\nbuffer dump:\n");
  int byte_count = 0;
  for (int i = 0; i < len; i++) {
    // printf("%x ", buf[i]);
    printf("%c", buf[i]);
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

  int data_file_size = validate_data_file(data_fp);
  if (data_file_size == -1) {
    printf("data file invalid");
  }

  if (config_termios(port_fd) == -1) {
    printf("error setting termios config");
  }

  // setup read buffer
  uint8_t cmd[64];
  short addr = 0;
  write_data(addr, data_fp, port_fd, data_file_size);
  sleep(1);
  printf("\n==========================\n");
  dump_eeprom(port_fd, addr);

  close(port_fd);
}

void write_data(short addr, FILE *data_fp, int port_fd, int data_file_size) {
  uint8_t cmd[CMD_LEN];

  while (data_file_size != 0) {
    printf("start addr: %d\n", addr);
    memset(cmd, 0, sizeof(cmd));
    int n =
        fread(cmd + DATA_OFFSET, sizeof(uint8_t), MAX_BYTES_PER_WRITE, data_fp);
    cmd[0] = OP_WRITE | (uint8_t)(n << 1);
    memcpy(cmd + ADDRESS_OFFSET, &addr, ADDRESS_SIZE);
    dump_cmd_data(cmd, sizeof(cmd));
    write(port_fd, cmd, sizeof(cmd));
    fsync(port_fd);
    read(port_fd, NULL, RES_PACKET_SIZE);
    data_file_size -= n;
    addr += n;
  }
}

void read_packet(int port_fd, uint8_t buf[]) {
  int bytes_read = 0;
  while (bytes_read < RES_PACKET_SIZE) {
    bytes_read += read(port_fd, buf + bytes_read, RES_PACKET_SIZE);
    printf("%d\n", bytes_read);
  }
}

void dump_eeprom(int port_fd, short addr) {
  u_short bytes_read = 0;
  uint8_t cmd[CMD_LEN];

  while (1) {
    printf("start addr: %d\n", addr);
    memset(cmd, 0, sizeof(cmd));
    cmd[0] = OP_READ | ((uint8_t)MAX_BYTES_PER_READ << 1);
    memcpy(cmd + ADDRESS_OFFSET, &addr, ADDRESS_SIZE);
    write(port_fd, cmd, sizeof(cmd));
    fsync(port_fd);
    read(port_fd, cmd, RES_PACKET_SIZE);
    printf("bytes read: %d\n", cmd[0]);
    bytes_read += cmd[0];
    dump_cmd_data(cmd + RES_DATA_OFFSET, RES_PACKET_SIZE);
    addr += bytes_read;
    if (bytes_read == EEPROM_SIZE) {
      break;
    }
  }
}
