#define WE A0        // Write enable for EEPROM (Active low)
#define OE A1        // Output enable for EEPROM (Active low)
#define SRCLK A2     // Serial clock
#define RCLK A3      // Register clock  (Keep low and pulse to load address to register)
#define SDA A4       // Serial data
#define DATA_SIZE 8  // Data size in bits
#define ADDRESS_LINE_SIZE 15
#define MAX_ADDRESS 32768
#define SERIAL_DATA_MASK 0x8000
#define PACKET_SIZE 64
#define OP_MASK 0x80
#define WRITE_MODE_MASK 0x40
#define BYTE_COUNT_MASK 0x7E
#define OP_WRITE 0x80
#define OP_READ 0x00
#define DATA_OFFSET 3
#define RES_DATA_OFFSET 1
#define RES_LEN_OFFSET 0
#define RES_PACKET_SIZE 63

byte data_pins[] = { 6, 7, 8, 9, 10, 11, 12, 13 };

void op_unknown(int op) {
  Serial.print("unknow operation: ");
  Serial.println(op);
}

void setDataMode(byte mode) {
  for (int i = 0; i < DATA_SIZE; i++) {
    pinMode(data_pins[i], mode);
  }
}
short getAddr(byte *buf) {
  short addr = -1;
  memcpy(&addr, buf + 1, 2);
  return addr;
}
void setAddress(short addr) {
  // send first byte MSBFIRST
  shiftOut(SDA, SRCLK, MSBFIRST, addr & 0xFF00);
  // send second byte MSBFIRST
  shiftOut(SDA, SRCLK, MSBFIRST, addr & 0x00FF);

  // Pulse the Register clock to latch in the address to storage register
  digitalWrite(RCLK, HIGH);
  digitalWrite(RCLK, LOW);
  delay(20);
  // TODO: remove debug statements
  // Serial.print("address set to: ");
  // Serial.flush();
  // Serial.println(addr);
  // Serial.flush();
}

void writeData(byte *buf, short addr, byte bytes_count) {
  for (byte idx = 0; idx < bytes_count; idx++) {
    setAddress(addr++);
    writeToAddress(buf[DATA_OFFSET + idx]);
  }
}

void writeToAddress(byte data) {
  digitalWrite(OE, HIGH);
  setDataMode(OUTPUT);
  // set the data on the data pins
  for (int i = 7; i >= 0; i--) {
    digitalWrite(data_pins[i], data & 1);
    data = data >> 1;
  }

  // Disable output
  // Pulse write enable to write the data
  digitalWrite(WE, LOW);
  delayMicroseconds(10);
  digitalWrite(WE, HIGH);
}

void readData(short startAddr, byte bytesCount) {
  byte buf[RES_PACKET_SIZE];
  memset(buf, 0, RES_PACKET_SIZE);
  byte i = 0;
  for (i = 0; i < bytesCount; i++) {
    setAddress(startAddr++);
    buf[RES_DATA_OFFSET + i] = readFromAddress();
  }
  buf[RES_LEN_OFFSET] = i + 1;

  Serial.write(buf, RES_PACKET_SIZE);
  Serial.flush();
}

byte readFromAddress() {
  // Disable write and enable output from EEPROM
  digitalWrite(WE, HIGH);
  digitalWrite(OE, LOW);

  setDataMode(INPUT);
  byte res = 0;
  // Read in the byte
  for (byte i = 0; i < DATA_SIZE; i++) {
    int b = digitalRead(data_pins[i]);
    res = (res << 1);
    res |= b;
  }

  // Serial.print("Data: ");
  // Serial.println(res);
  // Serial.flush();
  return res;
}

void readPacket(byte *buf) {
  while (Serial.available() == 0)
    ;
  int bytes_read = Serial.readBytes(buf, PACKET_SIZE);
}
void setup() {
  // put your setup code here, to run once:
  pinMode(WE, OUTPUT);
  pinMode(OE, OUTPUT);
  pinMode(RCLK, OUTPUT);
  pinMode(SRCLK, OUTPUT);
  pinMode(SDA, OUTPUT);

  digitalWrite(RCLK, LOW);
  digitalWrite(OE, HIGH);
  digitalWrite(WE, HIGH);

  Serial.begin(9600);
}

void loop() {
  byte buf[64];
  memset(buf, 0, sizeof(buf));
  readPacket(buf);

  byte header = buf[0];
  byte op = header & OP_MASK;                               // first bit of header indicates operation
  byte data_bytes_count = (header & BYTE_COUNT_MASK) >> 1;  // number of bytes to write

  short addr = getAddr(buf);
  delay(10);

  switch (op) {
    case OP_READ:
      readData(addr, data_bytes_count);
      break;
    case OP_WRITE:
      writeData(buf, addr, data_bytes_count);
      break;
    default:
      op_unknown(op);
      break;
  }
}
