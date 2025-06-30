#include <Arduino.h>

static String ID = "HC001";
static String VERSION = "1.3";

typedef struct {
  int pinA;
  int pinB;
  int id;
  int status;
}FaderMux;

typedef struct {
  int wiperPin;
  FaderMux faders[2];
}FaderGroup;

// Input buffer
const uint8_t BUFFER_SIZE = 64;
char inputBuffer[BUFFER_SIZE];
uint8_t bufferIndex = 0;

FaderGroup faderGroups[5];
uint16_t status[5][2][2];
size_t groupCount;
void setup() {
  
  faderGroups[0].wiperPin = A0;
  faderGroups[0].faders[0].pinA = 2;
  faderGroups[0].faders[0].pinB = 3;
  faderGroups[0].faders[0].id = 1;
  faderGroups[0].faders[1].pinA = 4;
  faderGroups[0].faders[1].pinB = 5;
  faderGroups[0].faders[1].id = 2;
  
  faderGroups[1].wiperPin = A1;
  faderGroups[1].faders[0].id = 3;
  faderGroups[1].faders[0].pinA = 6;
  faderGroups[1].faders[0].pinB = 7;
  
  faderGroups[2].wiperPin = A2;
  faderGroups[2].faders[0].id = 4;
  faderGroups[2].faders[0].pinA = 8;
  faderGroups[2].faders[0].pinB = 9;
  
  faderGroups[3].wiperPin = A3;
  faderGroups[3].faders[0].id = 5;
  faderGroups[3].faders[0].pinA = 14;
  faderGroups[3].faders[0].pinB = 15;
  
  groupCount = sizeof(faderGroups) / sizeof(faderGroups[0]);

  Serial.begin(115200);

  Serial.println("# HyperControl v" + VERSION + " ID=" + ID);
  Serial.println("# Type HELP for available commands");

}

void loop() {
  readFaders();
  readSerialInput();
  printStatus();
  delay(20);
}

// --- Read and process serial input ---
void readSerialInput() {
  while (Serial.available()) {
    char c = Serial.read();

    if (c == '\n') {
      inputBuffer[bufferIndex] = '\0';
      processCommand(inputBuffer);
      bufferIndex = 0;
    } else if (bufferIndex < BUFFER_SIZE - 1) {
      inputBuffer[bufferIndex++] = c;
    }
  }
}

// --- Read fader values ---
void readFaders() {
  //size_t groupCount = sizeof(faderGroups) / sizeof(faderGroups[0]);
  for (size_t group = 0; group < groupCount; group++) {
    size_t muxCount = sizeof(faderGroups[group].faders) / sizeof(faderGroups[group].faders[0]);
    for (size_t mux = 0; mux < muxCount; mux++) {
      if (faderGroups[group].faders[mux].id != 0) {
        if (faderGroups[group].faders[mux].pinA >= 2) {
          size_t not_mux = 1 - mux;
          pinMode(faderGroups[group].faders[not_mux].pinA, INPUT);
          pinMode(faderGroups[group].faders[not_mux].pinB, INPUT);
          pinMode(faderGroups[group].faders[mux].pinA, OUTPUT);
          pinMode(faderGroups[group].faders[mux].pinB, OUTPUT);
          digitalWrite(faderGroups[group].faders[mux].pinA, LOW);
          digitalWrite(faderGroups[group].faders[mux].pinB, HIGH);
        }
        delayMicroseconds(100);

        status[group][mux][0] = faderGroups[group].faders[mux].id;
        status[group][mux][1] = analogRead(faderGroups[group].wiperPin);
      }
    }
  }
}


// --- Command processing ---
void processCommand(const char *cmd) {
  if (strncmp(cmd, "HELP", 4) == 0) {
    printHelp();
  }
  else if (strncmp(cmd, "STATUS", 6) == 0) {
    printStatus();
  }
  else {
    printHelp();
  }
}

// --- STATUS ---
void printStatus() {

  Serial.print("{\"id\":\"");
  Serial.print(ID);
  Serial.print("\", \"version\":\"");
  Serial.print(VERSION);
  Serial.print("\", \"spec\":{");
  Serial.print("\"faderCount\":");
  Serial.print(groupCount);
  Serial.print(",\"min\":0,\"max\":1023},");
  Serial.print("\"faders\":[");

  bool first = true;
  for (size_t group = 0; group < groupCount; group++) {
    size_t muxCount = sizeof(status[0]) / sizeof(status[0][0]);
    for (size_t mux = 0; mux < muxCount; mux++) {
      if (status[group][mux][0] != 0) {
        if (!first) {
          Serial.print(",");
        }
        first = false;

        Serial.print("{\"id\":");
        Serial.print(status[group][mux][0]);
        Serial.print(",\"value\":");
        Serial.print(status[group][mux][1]);
        Serial.print("}");
      }
    }
  }
  Serial.println("]}");
}

// --- HELP ---
void printHelp() {
  Serial.println("# Available commands:");
  Serial.println("HELP                # Show this help");
  Serial.println("STATUS          # Show current state");
}
