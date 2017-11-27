#include "ArCOM.h"
#include <SPI.h>
#include <Ethernet.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

ArCOM USBCOM(Serial); // Creates an ArCOM object called USBCOM, wrapping Serial (for Teensy 3.6)
ArCOM Serial1COM(Serial1); // Creates an ArCOM object called Serial1COM, wrapping Serial1

#define FirmwareVersion 1
const uint32_t MessageBufSize = 32000;

// Module setup
char moduleName[] = "Ethernet"; // Name of module for manual override UI and state machine assembler

// Ethernet
#define port 11258

// IO lines
#define WIZ850_CS_PIN 10
#define WIZ850_RESET_PIN 9
#define OLED_RESET 15  // Connect RST to pin 9
#define OLED_DC    16  // Connect DC to pin 8
#define OLED_CS    14 // Connect CS to pin 10
#define DISP_BUTTON_PIN 2

// Display config
uint32_t oLED_Timeout = 5000; // in ms
uint16_t debounceTime = 500; // in ms
boolean screenSaverMode = false;
boolean statusDrawn = false; // True if status screen has been drawn once

// Vars
IPAddress ip(0, 0, 0, 0); // Stores IP (assigned with DHCP, or via Bpod relay for manual address)
byte mac[] = {0x00, 0xAA, 0xBB, 0xCC, 0xDE, 0x02};
elapsedMillis timeOnline; // Time since board got an IP address
elapsedMillis timeSinceSystemUpdate; // Time since last oLED display update
elapsedMillis timeSinceDisplayOn; // Time since oLED display was enabled (on boot, after button press)
elapsedMillis timeSinceButtonPress; // used for debounce
boolean displayActive = false; // Whether the oLED display is showing status (off if false, to save screen)
EthernetServer server(port);
EthernetClient client;
boolean initialized = false;
boolean connected2Client = false;
byte opByte = 0;
uint32_t nMessageBytes = 0;
byte MessageBuffer[MessageBufSize] = {0};
uint32_t nBytesTransferred = 0;
uint32_t nBytes2Relay = 0;
byte newByte = 0;
uint32_t nFullReads = 0;
uint32_t partialReadSize = 0;

// For millis to H:M:S conversion
uint32_t allSeconds = 0;
uint32_t secsRemaining = 0;
uint32_t runHours = 0;
uint32_t runMinutes = 0;
uint32_t runSeconds = 0;
char timeStr[10] = {0};

union {
    byte byteArray[4];
    uint16_t uint16;
    uint32_t uint32;
    int8_t int8;
    int16_t int16;
    int32_t int32;
} typeBuffer;

Adafruit_SSD1306 display(OLED_DC, OLED_RESET, OLED_CS);
#if (SSD1306_LCDHEIGHT != 64)
#error("Height incorrect, please fix Adafruit_SSD1306.h!");
#endif

void setup() {
  display.begin(SSD1306_SWITCHCAPVCC); // Initialize the OLED
  display.clearDisplay(); // Clear the display's internal memory
  updateDisplay(0); // 1 = connecting
  Serial1.begin(2625000); // 2625000
  pinMode(WIZ850_RESET_PIN, OUTPUT);
  digitalWrite(WIZ850_RESET_PIN, LOW);    // begin reset the WIZ850io
  pinMode(WIZ850_CS_PIN, OUTPUT);
  digitalWrite(WIZ850_CS_PIN, HIGH);  // de-select WIZ850io
  digitalWrite(WIZ850_RESET_PIN, HIGH);   // end reset pulse
  pinMode(DISP_BUTTON_PIN, INPUT_PULLUP);
  if (Ethernet.begin(mac) == 1) {
    initialized = true;
    ip = Ethernet.localIP();
  }
  server.begin(); // Start the Ethernet server
  // Print IP and status to OLED
  for (byte i = 0; i < 3; i++) {
    // print the value of each byte of the IP address:
    Serial.print(ip[i], DEC);
    Serial.print(".");
  }
  Serial.print(ip[3], DEC);
  Serial.println(" ");
  timeOnline = 0; timeSinceDisplayOn = 0; displayActive = true;
  updateDisplay(1); // 1 = ip display
}

void loop() {
   if (!connected2Client) {
    client = server.available();
   }
   //if (client) {
      if (client.available() > 0) {
        if (!connected2Client) {
          connected2Client = true;
          statusDrawn = false;
        }
        opByte = client.read();
        switch (opByte) {
          case 'H':
            client.write(1);
          break;
          case 'R':
            client.read(typeBuffer.byteArray, 4);
            nBytes2Relay = typeBuffer.uint32;
            nFullReads = (unsigned long)(floor((double)nBytes2Relay/(double)MessageBufSize));
            for (int i = 0; i < nFullReads; i++) {
              while(client.available() == 0) {}
              client.read(MessageBuffer, MessageBufSize);
              Serial1COM.writeByteArray(MessageBuffer, MessageBufSize);
            }
            partialReadSize = nBytes2Relay-(nFullReads*MessageBufSize);
            if (partialReadSize > 0) {
              while(client.available() == 0) {}
              client.read(MessageBuffer, partialReadSize);
              Serial1COM.writeByteArray(MessageBuffer, partialReadSize);
            }         
            nBytesTransferred += nBytes2Relay; 
          break;
        }
      }
   //}
   if (Serial1COM.available() > 0) {
    opByte = Serial1COM.readByte();
    switch (opByte) {
      case 255:
        returnModuleInfo();
      break;
      case 'R': // Relay message to client
        nBytes2Relay = Serial1COM.readUint32();
        nFullReads = (unsigned long)(floor((double)nBytes2Relay/(double)MessageBufSize));
        for (int i = 0; i < nFullReads; i++) {
          while(Serial1COM.available() == 0) {}
          Serial1COM.readByteArray(MessageBuffer, MessageBufSize);
          client.write(MessageBuffer, MessageBufSize);
        }
        partialReadSize = nBytes2Relay-(nFullReads*MessageBufSize);
        if (partialReadSize > 0) {
          while(Serial1COM.available() == 0) {}
          Serial1COM.readByteArray(MessageBuffer, partialReadSize);
          client.write(MessageBuffer, partialReadSize);
        }         
        nBytesTransferred += nBytes2Relay; 
      break;
    }
   }
   if (timeSinceSystemUpdate > 1000) {
      Ethernet.maintain();
      if (displayActive) {
        if (!client.connected()) {
           timeOnline = 0; 
           if (connected2Client == true) {
             connected2Client = false; 
             statusDrawn = false;
             nBytesTransferred = 0;
           } else {
             client = server.available();
             if (client) {
              connected2Client = true;
              statusDrawn = false;
             }
           }
        }
        updateDisplay(1);
      }
      timeSinceSystemUpdate = 0;
   }
   if (displayActive) {
     if (screenSaverMode == true) {
       if (timeSinceDisplayOn > oLED_Timeout) {
        setDisplayActive(false);
       }
     }
     if ((digitalRead(DISP_BUTTON_PIN) == LOW) && (timeSinceButtonPress > debounceTime)) {
        timeSinceButtonPress = 0;
        setDisplayActive(false);
     }
   } else {
     if ((digitalRead(DISP_BUTTON_PIN) == LOW) && (timeSinceButtonPress > debounceTime)) {
        timeSinceButtonPress = 0;
        setDisplayActive(true);
     }
   }
}

void updateDisplay(byte op) {
  switch (op) {
    case 0:
      display.clearDisplay();
      display.setTextSize(2);
      display.setTextColor(WHITE);
      display.setCursor(5,25);
      display.println("Connecting");
      display.display();
    break;
    case 1:
      if (statusDrawn == false) {
        display.clearDisplay();
        display.setTextSize(0);
        display.setTextColor(WHITE);
        display.setCursor(0,0);
        if (connected2Client) {
          display.println("Status: Connected");
        } else {
          display.println("Status: Idle");
        }
        display.setCursor(0,14);
        display.print("IP:");
        display.print(ip[0], DEC);
        display.print(".");
        display.print(ip[1], DEC);
        display.print(".");
        display.print(ip[2], DEC);
        display.print(".");
        display.print(ip[3], DEC);
        display.setCursor(0,28);
        display.print("Port:");
        display.print(port);
        display.setTextColor(WHITE);
        display.setCursor(0,42);
        display.print("Cxn UpTime: ");
        display.setCursor(0,56);
        display.print("Bytes:");
        statusDrawn = true;
      }
      clearText(70,42,9); // yPos,xPos,nCharacters
      display.setCursor(70,42);
      allSeconds=timeOnline/1000;
      runHours= allSeconds/3600;
      secsRemaining=allSeconds%3600;
      runMinutes=secsRemaining/60;
      runSeconds=secsRemaining%60;
      sprintf(timeStr, "%03d:%02d:%02d",runHours, runMinutes, runSeconds);
      display.print(timeStr);
      clearText(40,56,16);
      display.setCursor(40,56);
      display.print(nBytesTransferred);
      display.display();
    break;
  }
}

void setDisplayActive(boolean status) {
  if (status == true) {
    statusDrawn = false;
    updateDisplay(1);
    displayActive = true;
    timeSinceDisplayOn = 0;
  } else {
    display.clearDisplay();
    display.display();
    displayActive = false;
  }
}

void clearText(byte yPos, byte xPos, byte nChar) {
    display.setCursor(yPos,xPos);
    display.setTextColor(BLACK);
    for (int i = 0; i < nChar; i++) { // Clear last time
      display.write(218);
    }
    display.display();
    display.setTextColor(WHITE);
}

void returnModuleInfo() {
  Serial1COM.writeByte(65); // Acknowledge
  Serial1COM.writeUint32(FirmwareVersion); // 4-byte firmware version
  Serial1COM.writeByte(sizeof(moduleName)-1); // Length of module name
  Serial1COM.writeCharArray(moduleName, sizeof(moduleName)-1); // Module name
  Serial1COM.writeByte(0); // 1 if more info follows, 0 if not
}
