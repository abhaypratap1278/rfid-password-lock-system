/*
  Password Based Lock System with RFID (Arduino Nano)

  - 4x4 matrix keypad: enter the password, press # to submit, * to clear
  - RC522 RFID reader: scan an authorized card
  - 16x2 I2C LCD: shows "Access Granted" or "Access Denied"
  - Relay + solenoid lock: unlocks the door for a few seconds, then locks again

  Board in Arduino IDE: "Arduino Nano" (Processor: ATmega328P, try "Old Bootloader" if upload fails)

  Libraries to install (Sketch > Include Library > Manage Libraries):
    - MFRC522          by GithubCommunity / Miguel Balboa
    - Keypad           by Mark Stanley, Alexander Brevig
    - LiquidCrystal I2C (any version that provides LiquidCrystal_I2C.h)

  ================= WIRING (power OFF while connecting; common GND everywhere) =================
  RC522 RFID reader (use 3.3V, NOT 5V):
    RC522 SDA(SS) -> Nano D10      RC522 SCK  -> Nano D13
    RC522 MOSI    -> Nano D11      RC522 MISO -> Nano D12
    RC522 RST     -> Nano D9       RC522 3.3V -> Nano 3V3
    RC522 GND     -> Nano GND      (RC522 IRQ is not used)

  4x4 keypad (8 pins, left to right as seen from the front: R1 R2 R3 R4 C1 C2 C3 C4):
    R1 -> D2   R2 -> D3   R3 -> D4   R4 -> D5
    C1 -> D6   C2 -> D7   C3 -> D8   C4 -> A0

  16x2 I2C LCD:
    LCD SDA -> Nano A4     LCD SCL -> Nano A5
    LCD VCC -> Nano 5V     LCD GND -> Nano GND

  Relay module:
    Relay IN -> Nano A1    Relay VCC -> Nano 5V    Relay GND -> Nano GND

  Solenoid lock (12V, own power supply) through the relay:
    12V supply (+) -> Relay COM
    Relay NO       -> Solenoid lock (+)
    Solenoid lock (-) -> 12V supply (-)
    (A solenoid lock is locked without power. Relay ON powers it = bolt retracts = door unlocks.)
    Put a 1N4007 diode across the lock terminals (stripe towards +).
  ===============================================================================================
  Full details with tables: see README.md
*/

#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>

// ---------- Pins ----------
#define SS_PIN    10
#define RST_PIN    9
#define RELAY_PIN A1

// ---------- Settings ----------
const char PASSWORD[] = "1234";          // Change this for your own use
const unsigned long UNLOCK_TIME_MS = 5000;   // how long the door stays unlocked
const bool RELAY_ACTIVE_LOW = true;      // Most relay modules switch ON when the pin is LOW.
                                         // If yours works the other way round, set false.

// Authorized RFID cards (4-byte UIDs). Scan a card, read its UID in the Serial Monitor
// (9600 baud), and put it here. These two are only examples.
byte AUTHORIZED_UIDS[][4] = {
  {0xDE, 0xAD, 0xBE, 0xEF},
  {0x12, 0x34, 0x56, 0x78}
};
const byte NUM_CARDS = sizeof(AUTHORIZED_UIDS) / sizeof(AUTHORIZED_UIDS[0]);

// ---------- Keypad ----------
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};
byte rowPins[ROWS] = {2, 3, 4, 5};
byte colPins[COLS] = {6, 7, 8, A0};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ---------- Other objects ----------
LiquidCrystal_I2C lcd(0x27, 16, 2);   // If the LCD stays blank, try address 0x3F
MFRC522 rfid(SS_PIN, RST_PIN);

const byte MAX_LEN = 8;
char entered[MAX_LEN + 1];
byte enteredLen = 0;

// ---------- Lock control ----------
// unlocked = true  -> relay ON  -> solenoid lock powered     -> bolt retracts, door opens
// unlocked = false -> relay OFF -> solenoid lock not powered -> bolt extended, door locked
void setLock(bool unlocked) {
  bool relayOn = unlocked;
  bool level = RELAY_ACTIVE_LOW ? !relayOn : relayOn;
  digitalWrite(RELAY_PIN, level ? HIGH : LOW);
}

// ---------- LCD screens ----------
void showIdle() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Enter Password");
  lcd.setCursor(0, 1);
  lcd.print("or Scan Card");
}

void showEntered() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Password:");
  lcd.setCursor(0, 1);
  for (byte i = 0; i < enteredLen; i++) lcd.print('*');
}

void clearEntered() {
  enteredLen = 0;
  entered[0] = '\0';
}

void grantAccess() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Access Granted");
  lcd.setCursor(0, 1);
  lcd.print("Door Unlocked");
  setLock(true);
  delay(UNLOCK_TIME_MS);
  setLock(false);
  clearEntered();
  showIdle();
}

void denyAccess() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Access Denied");
  lcd.setCursor(0, 1);
  lcd.print("Incorrect");
  delay(2000);
  clearEntered();
  showIdle();
}

// ---------- RFID ----------
bool isAuthorized(byte *uid, byte size) {
  if (size != 4) return false;
  for (byte c = 0; c < NUM_CARDS; c++) {
    bool match = true;
    for (byte i = 0; i < 4; i++) {
      if (uid[i] != AUTHORIZED_UIDS[c][i]) { match = false; break; }
    }
    if (match) return true;
  }
  return false;
}

void checkCard() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;

  // Print UID so you can add new cards to AUTHORIZED_UIDS
  Serial.print("Card UID:");
  for (byte i = 0; i < rfid.uid.size; i++) {
    Serial.print(rfid.uid.uidByte[i] < 0x10 ? " 0x0" : " 0x");
    Serial.print(rfid.uid.uidByte[i], HEX);
  }
  Serial.println();

  bool ok = isAuthorized(rfid.uid.uidByte, rfid.uid.size);
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  if (ok) grantAccess();
  else    denyAccess();
}

// ---------- Keypad ----------
void handleKey(char key) {
  if (key >= '0' && key <= '9') {
    if (enteredLen < MAX_LEN) {
      entered[enteredLen++] = key;
      entered[enteredLen] = '\0';
      showEntered();
    }
  } else if (key == '*') {          // clear
    clearEntered();
    showIdle();
  } else if (key == '#') {          // submit
    if (strcmp(entered, PASSWORD) == 0) grantAccess();
    else                                denyAccess();
  }
}

void setup() {
  Serial.begin(9600);

  // Keep the door locked from the very first moment
  digitalWrite(RELAY_PIN, RELAY_ACTIVE_LOW ? HIGH : LOW);
  pinMode(RELAY_PIN, OUTPUT);
  setLock(false);

  SPI.begin();
  rfid.PCD_Init();

  lcd.init();
  lcd.backlight();
  clearEntered();
  showIdle();
}

void loop() {
  char key = keypad.getKey();
  if (key) handleKey(key);

  checkCard();
}
