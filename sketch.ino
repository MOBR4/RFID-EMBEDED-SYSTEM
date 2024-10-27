#define BLYNK_TEMPLATE_ID "TMPL6pHGpAtVl"
#define BLYNK_TEMPLATE_NAME "RFID Door System"
#define BLYNK_AUTH_TOKEN "hpgnPaPYhXK4dDt7AS_CyXHzxONuG1WM"

#include <ESP32Servo.h>
#include <LiquidCrystal.h>
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <EEPROM.h>
#include <time.h>
#include <SD.h>

// Pin Definitions
struct Pins {
    static const uint8_t PIR = 13;
    static const uint8_t DOOR = 25;
    static const uint8_t TEMP = 26; // Temperature sensor pin
    static const uint8_t RED_LED = 2;
    static const uint8_t GREEN_LED = 4;
    static const uint8_t BUZZER = 23;
    static const uint8_t SERVO = 12;
    static const uint8_t LCD_RS = 19;
    static const uint8_t LCD_EN = 18;
    static const uint8_t LCD_D4 = 17;
    static const uint8_t LCD_D5 = 16;
    static const uint8_t LCD_D6 = 15;
    static const uint8_t LCD_D7 = 14;
};

// Active hours for RFID reader
const int START_HOUR = 9;
const int END_HOUR = 18;

// NTP server settings
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;
const int daylightOffset_sec = 3600;

LiquidCrystal lcd(Pins::LCD_RS, Pins::LCD_EN, Pins::LCD_D4, Pins::LCD_D5, Pins::LCD_D6, Pins::LCD_D7);
Servo doorServo;

// Blynk Virtual Pins
#define VP_LOG V0
#define VP_COUNT V1
#define VP_TEMP V2
#define VP_DOOR V3
#define VP_ACCESS V4
#define VP_DENIED V5
#define VP_INVALID_ATTEMPTS V6

// EEPROM addresses
const uint16_t EEPROM_PEOPLE = 0;
const uint16_t EEPROM_ACCESS = 4;
const uint16_t EEPROM_DENIED = 8;

// RFID Users
const struct User {
    const char* id;
    const char* name;
    bool isAdmin;
} USERS[] = {
    {"E280689401A9", "ILYAS", true},
    {"E2000019060C", "RAYHAN", false},
    {"G46RD9V40F3A", "HAMADA", false},
    {"B71001B76894", "HATIM", false},
    {"1C81159073FD", "MOHAMED", true},
    {"B710D0186022", "NISTR", false}
};

class Security {
private:
    struct {
        uint16_t peopleNearDoor = 0;
        uint16_t peopleAccessed = 0;
        uint16_t accessDenied = 0;
        uint8_t invalidAttempts = 0;
        bool isDoorOpen = false;
        float temperature = 0;
        uint32_t pirLastDetectTime = 0;
        uint32_t tempLastReadTime = 0;
        uint32_t doorLastStateChangeTime = 0;
        char rfidBuffer[13] = {0};
        uint8_t rfidIndex = 0;
    } state;

    uint32_t lastLogTime = 0; // Time of the last PCAP log export

    void writeToEEPROM(uint16_t addr, uint16_t val) {
        EEPROM.put(addr, val);
        EEPROM.commit();
    }

    void displayOnLCD(uint8_t row, const String& msg, bool clear = true) {
        if (clear) lcd.setCursor(0, row);
        lcd.print(msg + String("                    ").substring(0, 20 - msg.length()));
    }

    void controlDoor(bool open) {
        doorServo.write(open ? 0 : 90);
        digitalWrite(Pins::GREEN_LED, open);
        state.isDoorOpen = open;
        updateBlynk();
    }

    void soundBuzzer(uint8_t beeps = 1) {
        for (uint8_t i = 0; i < beeps; i++) {
            digitalWrite(Pins::BUZZER, HIGH);
            delay(200);
            digitalWrite(Pins::BUZZER, LOW);
            if (i < beeps - 1) delay(200);
        }
    }

    void updateBlynk() {
        Blynk.virtualWrite(VP_COUNT, state.peopleNearDoor);
        Blynk.virtualWrite(VP_ACCESS, state.peopleAccessed);
        Blynk.virtualWrite(VP_DENIED, state.accessDenied);
        Blynk.virtualWrite(VP_TEMP, state.temperature); // Send temperature to Blynk
        Blynk.virtualWrite(VP_DOOR, state.isDoorOpen ? "Open" : "Closed");
        Blynk.virtualWrite(VP_INVALID_ATTEMPTS, state.invalidAttempts);
    }

    int getCurrentHour() {
        struct tm timeInfo;
        if (!getLocalTime(&timeInfo)) {
            Serial.println("Failed to obtain time");
            return -1;
        }
        return timeInfo.tm_hour;
    }

    void handleCard(const char* cardId) {
        int currentHour = getCurrentHour();

        if (currentHour >= START_HOUR && currentHour < END_HOUR) {
            bool valid = false;
            const char* name = nullptr;
            bool isAdmin = false;

            for (const User& user : USERS) {
                if (strcmp(cardId, user.id) == 0) {
                    valid = true;
                    name = user.name;
                    isAdmin = user.isAdmin;
                    break;
                }
            }

            if (valid && name) {
                state.peopleAccessed++;
                writeToEEPROM(EEPROM_ACCESS, state.peopleAccessed);
                
                displayOnLCD(2, " ACCESS GRANTED");
                displayOnLCD(3, String(" ") + name + (isAdmin ? " (ADMIN)" : " (USER)"));
                
                controlDoor(true);
                soundBuzzer(1);
                
                Blynk.logEvent("access", "Access: " + String(name));
                Blynk.virtualWrite(VP_LOG, "Access: " + String(name));
                
                delay(5000);
                controlDoor(false);
                state.invalidAttempts = 0;
            } else {
                state.accessDenied++;
                writeToEEPROM(EEPROM_DENIED, state.accessDenied);
                state.invalidAttempts++;

                displayOnLCD(2, " ACCESS DENIED");
                displayOnLCD(3, " INVALID CARD");
                
                digitalWrite(Pins::RED_LED, HIGH);
                soundBuzzer(3);
                
                delay(3000);
                digitalWrite(Pins::RED_LED, LOW);

                if (state.invalidAttempts >= 3) {
                    displayOnLCD(3, " ALERT: SECURITY!");
                    soundBuzzer(5);
                    Blynk.logEvent("security_alert", "3 Invalid Attempts Triggered Alarm");
                }
            }
            updateDisplay();
            updateBlynk();
        } else {
            displayOnLCD(2, " ACCESS DENIED");
            displayOnLCD(3, "Outside Working Hours");
            soundBuzzer(2);
            state.accessDenied++;
            writeToEEPROM(EEPROM_DENIED, state.accessDenied);
            updateBlynk();
        }
    }

    void exportPCAPLog() {
        if (SD.begin()) {
            File logFile = SD.open("/pcap_log.txt", FILE_WRITE);
            if (logFile) {
                logFile.println("Session Log:");
                logFile.println("Access Denied Count: " + String(state.accessDenied));
                logFile.println("Invalid Attempts: " + String(state.invalidAttempts));
                logFile.close();
                Serial.println("PCAP log saved.");
            } else {
                Serial.println("Failed to save PCAP log.");
            }
            SD.end();
        }
    }

public:
    void begin() {
        Serial.begin(9600);
        EEPROM.begin(512);

        pinMode(Pins::PIR, INPUT);
        pinMode(Pins::DOOR, INPUT_PULLUP);
        pinMode(Pins::TEMP, INPUT); // Initialize temperature sensor pin
        pinMode(Pins::RED_LED, OUTPUT);
        pinMode(Pins::GREEN_LED, OUTPUT);
        pinMode(Pins::BUZZER, OUTPUT);

        doorServo.attach(Pins::SERVO);
        controlDoor(false);

        lcd.begin(20, 4);
        updateDisplay();

        EEPROM.get(EEPROM_PEOPLE, state.peopleNearDoor);
        EEPROM.get(EEPROM_ACCESS, state.peopleAccessed);
        EEPROM.get(EEPROM_DENIED, state.accessDenied);

        WiFi.begin("Wokwi-GUEST", "");
        while (WiFi.status() != WL_CONNECTED) {
            delay(500);
            Serial.print(".");
        }
        Serial.println("WiFi connected.");

        Blynk.config(BLYNK_AUTH_TOKEN);

        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
        
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            Serial.printf("Current time: %02d:%02d:%02d\n", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        } else {
            Serial.println("Failed to obtain time.");
        }

        Serial.println("System Ready - Enter 12-digit RFID card number:");
    }

    void loop() {
        Blynk.run();

        // Check PIR sensor for movement
        if (digitalRead(Pins::PIR) == HIGH) {
            state.pirLastDetectTime = millis();
            state.peopleNearDoor++;
            writeToEEPROM(EEPROM_PEOPLE, state.peopleNearDoor);
            updateBlynk();
            updateDisplay();
        }

        // Check for RFID input
        if (Serial.available() > 0) {
            char c = Serial.read();
            if (c == '\n') {
                handleCard(state.rfidBuffer);
                memset(state.rfidBuffer, 0, sizeof(state.rfidBuffer));
                state.rfidIndex = 0;
            } else if (state.rfidIndex < sizeof(state.rfidBuffer) - 1) {
                state.rfidBuffer[state.rfidIndex++] = c;
            }
        }

        // Read temperature sensor
        if (millis() - state.tempLastReadTime >= 5000) { // Read every 5 seconds
            state.temperature = analogRead(Pins::TEMP) * (3.3 / 4095.0) * 100; // Convert to Celsius
            state.tempLastReadTime = millis();
            updateBlynk();
        }

        // Check for PCAP log export every 10 minutes
        if (millis() - lastLogTime >= 600) {
            exportPCAPLog();
            lastLogTime = millis();
        }
    }

    void updateDisplay() {
        lcd.clear();
        displayOnLCD(0, "People Near: " + String(state.peopleNearDoor));
        displayOnLCD(1, "Accessed: " + String(state.peopleAccessed));
        displayOnLCD(2, "Invalid: " + String(state.invalidAttempts));
        displayOnLCD(3, "Temp: " + String(state.temperature, 1) + "C");
    }
};

// Global instance of Security class
Security security;

void setup() {
    security.begin();
}

void loop() {
    security.loop();
}
