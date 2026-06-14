#include <asm-generic/ioctls.h>
#include <curses.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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
#define DUMP_FILE "eeprom_dump"

uint8_t CMD_DUMP_ENABLED = 0;

int config_termios(int port_fd);
int write_data(int port_fd, char *datafile_path);
int validate_data_file_and_return_size(FILE *data_fp);
void dump_cmd_data(uint8_t *buf, int len);
int dump_eeprom(int port_fd, short addr);
void invalid_usage_message();
void make_read_cmd(uint8_t cmd[], short addr, uint8_t bytes_count);
int make_write_cmd(uint8_t cmd[], FILE *data_fp, short addr);
void dump_byte(int port_fd, short addr);

int main(int argc, char *argv[]) {
  int port_fd = 0, data_fd = 0, len = 0;

  // open usb port
  port_fd = open(USB_SERIAL, O_RDWR | O_NOCTTY);
  if (port_fd < 0) {
    printf("unable to open port");
    return -1;
  }

  if (config_termios(port_fd) == -1) {
    printf("error setting termios config");
  }

  if (argc == 1) {
    invalid_usage_message();
    goto close;
  }

  char *op = argv[1];

  if (argc == 4 && (strcmp(argv[3], "-g") == 0)) {
    CMD_DUMP_ENABLED = 1;
  }
  if (strcmp(op, "-w") == 0) {
    if (argc != 3) {
      invalid_usage_message();
      goto close;
    }
    write_data(port_fd, argv[2]);
  } else if (strcmp(op, "-d") == 0) {
    int start_addr = 0;
    if (argc == 3) {
      start_addr = atoi(argv[2]);
    }
    dump_eeprom(port_fd, start_addr);
  } else if (strcmp(op, "-db") == 0) {
    if (argc <= 3) {
      invalid_usage_message();
      goto close;
    }
    int addr = atoi(argv[2]);
    dump_byte(port_fd, addr);
  } else {
    invalid_usage_message();
  }

close:
  close(port_fd);
  return 0;
}

int write_data(int port_fd, char *datafile_path) {
  // open data file
  int addr = 0;
  FILE *data_fp = fopen(datafile_path, "r");
  if (data_fp == NULL) {
    printf("unable to open data file");
    return -1;
  }

  int data_file_size = validate_data_file_and_return_size(data_fp);
  if (data_file_size == -1) {
    printf("data file invalid");
  }

  uint8_t cmd[CMD_LEN];
  while (data_file_size != 0) {
    int n = make_write_cmd(cmd, data_fp, addr);
    dump_cmd_data(cmd, CMD_LEN);
    write(port_fd, cmd, sizeof(cmd));
    fsync(port_fd);
    // wait for ack and discard data by reading to NULL
    read(port_fd, NULL, RES_PACKET_SIZE);
    data_file_size -= n;
    addr += n;
  }

  return 0;
}

void read_packet(int port_fd, uint8_t buf[]) {
  int bytes_read = 0;
  while (bytes_read < RES_PACKET_SIZE) {
    bytes_read += read(port_fd, buf + bytes_read, RES_PACKET_SIZE);
    printf("%d\n", bytes_read);
  }
}

int dump_eeprom(int port_fd, short addr) {
  FILE *data_fp = fopen(DUMP_FILE, "w");
  if (data_fp == NULL) {
    printf("unable to open data file");
    return -1;
  }

  u_short bytes_read = 0;
  uint8_t cmd[CMD_LEN];

  while (1) {
    make_read_cmd(cmd, addr, MAX_BYTES_PER_READ);
    write(port_fd, cmd, sizeof(cmd));
    fsync(port_fd);
    read(port_fd, cmd, RES_PACKET_SIZE);
    dump_cmd_data(cmd + RES_DATA_OFFSET, RES_PACKET_SIZE);
    bytes_read += cmd[0];
    fwrite(cmd + RES_DATA_OFFSET, sizeof(uint8_t), bytes_read, data_fp);
    addr += bytes_read;
    if (bytes_read == EEPROM_SIZE) {
      break;
    }
  }
  fflush(data_fp);
  fclose(data_fp);
  return 0;
}

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

int validate_data_file_and_return_size(FILE *data_fp) {
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
  if (CMD_DUMP_ENABLED == 0) {
    return;
  }
  printf("\ncmd buffer dump:\n");
  int byte_count = 0;
  for (int i = 0; i < len; i++) {
    printf("%x ", buf[i]);
    // printf("%c", buf[i]);
    if (byte_count++ == BYTES_PER_LINE) {
      printf("\n");
      byte_count = 0;
    }
  }
}
void invalid_usage_message() {
  printf("Usage:\n -d to dump eeprom\n -w datafile_path to write data from "
         "file to eeprom\n");
}

void make_read_cmd(uint8_t cmd[], short addr, uint8_t bytes_count) {
  memset(cmd, 0, CMD_LEN);
  cmd[0] = OP_READ | ((uint8_t)bytes_count << 1);
  memcpy(cmd + ADDRESS_OFFSET, &addr, ADDRESS_SIZE);
}

int make_write_cmd(uint8_t cmd[], FILE *data_fp, short addr) {
  memset(cmd, 0, CMD_LEN);
  int n =
      fread(cmd + DATA_OFFSET, sizeof(uint8_t), MAX_BYTES_PER_WRITE, data_fp);
  cmd[0] = OP_WRITE | (uint8_t)(n << 1);
  memcpy(cmd + ADDRESS_OFFSET, &addr, ADDRESS_SIZE);
  return n;
}

void dump_byte(int port_fd, short addr) {
  uint8_t cmd[CMD_LEN];
  make_read_cmd(cmd, addr, 1);
  dump_cmd_data(cmd, CMD_LEN);
  write(port_fd, cmd, sizeof(cmd));
  fsync(port_fd);
  read(port_fd, cmd, RES_PACKET_SIZE);
  dump_cmd_data(cmd, RES_PACKET_SIZE);
  printf("Byte at address %d: %#X", addr, cmd[1]);
}
