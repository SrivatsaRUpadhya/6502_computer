#define WE A0        // Write enable for EEPROM (Active low)
#define OE A1        // Output enable for EEPROM (Active low)
#define SRCLK A2     // Serial clock
#define RCLK A3      // Register clock  (Keep low and pulse to load address to register)
#define SDA A4       // Serial data
#define DATA_SIZE 8  // Data size in bits
#define ADDRESS_LINE_SIZE 15
#define MAX_ADDRESS 32768
#define SERIAL_DATA_MASK 0x8000
int data_pins[] = { 6, 7, 8, 9, 10, 11, 12, 13 };

void setOperation(bool isWrite) {
  if (isWrite) {
    digitalWrite(OE, LOW);
    setDataMode(OUTPUT);
  }
}
void setDataMode(byte mode) {
  for (int i = 0; i < DATA_SIZE; i++) {
    pinMode(data_pins[i], mode);
  }
}

void setAddress(int address) {
  shiftOut(SDA, SRCLK, MSBFIRST, address & 0xFF);

  // Pulse the Register clock to latch in the address to storage register
  digitalWrite(RCLK, HIGH);
  digitalWrite(RCLK, LOW);

  clearSerialBuffer();
  Serial.print("address set to: ");
  Serial.println(address);
  Serial.flush();
}

void writeData(int data) {
  if (data > 255) {
    Serial.println("Data can be only a byte long!");
    Serial.flush();
    return;
  }

  setDataMode(OUTPUT);
  // set the data on the data pins
  for (int i = 7; i >= 0; i--) {
    digitalWrite(data_pins[i], data & 0x0001);
    data = data >> 1;
  }

  // Disable output
  digitalWrite(OE, HIGH);
  // Pulse write enable to write the data
  digitalWrite(WE, LOW);
  digitalWrite(WE, HIGH);
}

void clearSerialBuffer() {
  while (Serial.available() > 0) {
    Serial.read();
  }
}

int readData() {
  // Disable write and enable output from EEPROM
  digitalWrite(WE, HIGH);
  digitalWrite(OE, LOW);

  setDataMode(INPUT);
  int res = 0;
  // Read in the byte
  for (int i = 0; i < DATA_SIZE; i++) {
    int bit = digitalRead(data_pins[i]);
    res = (res << 1);
    res ^= bit;
  }
  Serial.print(res, HEX);
  Serial.print(" , read data in decimal: ");
  Serial.println(res);
}

int getSerialInput() {
  while (Serial.available() == 0)
    ;
  return Serial.parseInt();
}
void setup() {
  // put your setup code here, to run once:
  pinMode(WE, OUTPUT);
  pinMode(OE, OUTPUT);
  pinMode(RCLK, OUTPUT);
  pinMode(SRCLK, OUTPUT);
  pinMode(SDA, OUTPUT);

  digitalWrite(RCLK, LOW);
  digitalWrite(WE, HIGH);

  Serial.begin(9600);
}

void loop() {
  Serial.println("1 for reading from address\n2 for writing to address");

  int operation = getSerialInput();

  Serial.print("Operation: ");
  Serial.println(operation);

  Serial.println("enter address for operation: ");
  int address = getSerialInput();

  if (address > MAX_ADDRESS) {
    Serial.println("Address exceeds max range!");
    Serial.flush();
    return;
  }
  setAddress(address);

  if (operation == 1) {
    int res = readData();
  } else {
    Serial.println("Data: ");
    int data = getSerialInput();
    writeData(data);
  }
}
