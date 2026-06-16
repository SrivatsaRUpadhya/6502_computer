#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
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
#define OP_READ_BYTE 0x01
#define MAX_BYTES_PER_READ 62
#define MAX_BYTES_PER_WRITE 60
#define ADDRESS_OFFSET 1
#define ADDRESS_SIZE 2
#define DEFAULT_DUMP_START_ADDRESS 0
#define RES_DATA_OFFSET 1
#define EEPROM_SIZE 32767
#define DUMP_FILE "eeprom_dump"
#define FLAG_BUF_LEN 100

// config_termios sets the termios config values including baudrate and flags
int config_termios(int port_fd);
int validate_data_file_and_return_size(FILE *data_fp);
// write_data accepts a datafile path from where the data is read and written to
// eeprom
int write_data(int port_fd, char *datafile_path);
// dump_cmd_data dumps len bytes of given command buffer
void dump_cmd_data(uint8_t *buf, int len);
// dump_eeprom dumps all the content of the eeprom into a file
int dump_eeprom(int port_fd, short addr);
// invalid_usage_message is a helper function to display usage instructions
void invalid_usage_message();
// make_read_cmd prepares the read command by setting up the header and address
void make_read_cmd(uint8_t cmd[], short addr, uint8_t bytes_count);
// make_write_cmd prepares the write command by setting up the header and
// address
int make_write_cmd(uint8_t cmd[], FILE *data_fp, short addr);
// dump_byte dumps just one byte at specified address
void dump_byte(int port_fd, short addr);
// parse_args parses the input arguments and populates the input_config struct
void parse_args(char **argv, int argc);
// set_flag sets config in input_config struct based on flag
void set_flag(char flag);

typedef struct eeprom_cmd_config {
  int op;
  uint8_t cmd_dump_enabled;
  char *flags;
  char non_flag_args[100][100];
  int non_flag_args_idx;
} cmd_config;

cmd_config input_params = {
    -1,
    0,
};

int main(int argc, char *argv[]) {
  struct termios prev_tty_config;
  int port_fd = 0;

  // open usb port
  port_fd = open(USB_SERIAL, O_RDWR | O_NOCTTY);
  if (port_fd < 0) {
    printf("unable to open port");
    return -1;
  }

  tcgetattr(port_fd, &prev_tty_config);
  if (config_termios(port_fd) == -1) {
    printf("error setting termios config");
  }

  sleep(2);
  parse_args(argv, argc);

  switch (input_params.op) {
  case OP_READ: {
    dump_eeprom(port_fd, DEFAULT_DUMP_START_ADDRESS);
    break;
  }
  case OP_WRITE: {
    if (input_params.non_flag_args_idx == 0) {
      printf("Missing data file path");
      break;
    }
    write_data(port_fd, input_params.non_flag_args[0]);
    break;
  }
  case OP_READ_BYTE: {
    int addr = atoi(input_params.non_flag_args[0]);
    if (input_params.non_flag_args_idx == 0) {
      printf("Missing address");
      break;
    }
    dump_byte(port_fd, addr);
    break;
  }
  default:
    invalid_usage_message();
    break;
  }

  tcsetattr(port_fd, TCSANOW, &prev_tty_config);
  close(port_fd);
  return 0;
}

int write_data(int port_fd, char *datafile_path) {
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
    read(port_fd, NULL, CMD_LEN);
    data_file_size -= n;
    addr += n;
  }

  return 0;
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
    dump_cmd_data(cmd, CMD_LEN);
    write(port_fd, cmd, sizeof(cmd));
    fsync(port_fd);
    read(port_fd, cmd, CMD_LEN);
    dump_cmd_data(cmd + RES_DATA_OFFSET, CMD_LEN);
    bytes_read += cmd[0];
    fwrite(cmd + RES_DATA_OFFSET, sizeof(uint8_t), cmd[0], data_fp);
    addr += cmd[0];
    if (bytes_read >= EEPROM_SIZE) {
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
  config.c_cflag |= (CLOCAL | CREAD | ~HUPCL);
  config.c_iflag &= ~(IXOFF | IXANY);

  // set vtime, vmin, baud rate...
  config.c_cc[VMIN] = CMD_LEN;
  config.c_cc[VTIME] = 0;

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
  if (input_params.cmd_dump_enabled == 0) {
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
  printf("Usage:\n"
         "-d to dump eeprom\n"
         "-w to write data from file to eeprom. Sepcifiy filepath as argument\n"
         "-b to read a single byte from a specific address. Specify address as "
         "argument. Address range: 0 - 32767"
         "-g to enable cmd buffer dumps");
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
  read(port_fd, cmd, CMD_LEN);
  dump_cmd_data(cmd, CMD_LEN);
  printf("Byte at address %d: %#X", addr, cmd[1]);
}

void parse_args(char **argv, int argc) {
  int flag_idx = 0;
  for (int i = 1; i < argc && flag_idx < FLAG_BUF_LEN; i++) {
    char *cur_arg = argv[i];
    int cur_arg_len = strlen(argv[i]);
    if (cur_arg[0] == '-') {
      for (int j = 1; j < cur_arg_len; j++) {
        set_flag(cur_arg[j]);
      }
    } else {
      strcpy(input_params.non_flag_args[input_params.non_flag_args_idx],
             cur_arg);
      input_params.non_flag_args_idx += cur_arg_len;
    }
  }
}

void set_flag(char flag) {
  switch (flag) {
  case 'd': {
    if (input_params.op != -1) {
      printf("Multiple operations provided");
      exit(0);
    }
    input_params.op = OP_READ;
    break;
  }
  case 'w': {
    if (input_params.op != -1) {
      printf("Multiple operations provided");
      exit(0);
    }
    input_params.op = OP_WRITE;
    break;
  }
  case 'b': {
    if (input_params.op != -1) {
      printf("Multiple operations provided");
      exit(0);
    }
    input_params.op = OP_READ_BYTE;
    break;
  }
  case 'g': {
    input_params.cmd_dump_enabled = 1;
    break;
  }
  default:
    invalid_usage_message();
    exit(0);
  }
}
