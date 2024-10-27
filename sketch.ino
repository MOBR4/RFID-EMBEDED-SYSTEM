#include <LiquidCrystal.h>
#include <DHT.h>
#include <ESP32Servo.h>
#include <TimeLib.h>  // Include the Time library

// Define constants
#define DHTPIN 26        // DHT22 data pin
#define DHTTYPE DHT22    // DHT 22 (AM2302)
#define BUZZER_PIN 23    // Pin for buzzer
#define LED_RED_PIN 2    // Pin for red LED
#define LED_GREEN_PIN 4  // Pin for green LED
#define SERVO_PIN 12     // Pin for servo motor
#define PROXIMITY_PIN 13 // Pin for proximity sensor

// LCD pins
const int LCD_RS = 19;
const int LCD_EN = 18;
const int LCD_D4 = 17;
const int LCD_D5 = 16;
const int LCD_D6 = 15;
const int LCD_D7 = 14;

// Initialize DHT sensor and LCD
DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal lcd(LCD_RS, LCD_EN, LCD_D4, LCD_D5, LCD_D6, LCD_D7);

// Servo object
Servo myServo;

// Access control variables
int accessGrantedCount = 0;
int accessDeniedCount = 0;
bool overheating = false;
bool proximityDetected = false;

struct User {
    String username;
    String rfidCode;
};

// Define permitted users
User permittedUsers[] = {
    {"Alice", "A1B2C3D4E5F6"},
    {"Bob", "B2C3D4E5F6A1"},
    {"Charlie", "C3D4E5F6A1B2"},
    {"David", "D4E5F6A1B2C3"},
    {"Eve", "E5F6A1B2C3D4"},
    {"Frank", "F6A1B2C3D4E5"},
    {"Grace", "G1B2C3D4E5F6"},
    {"Hannah", "H2C3D4E5F6A1"},
    {"Ian", "I3D4E5F6A1B2"},
    {"Jack", "J4E5F6A1B2C3"}
};

// Function to check if the RFID code is valid
bool isUserPermitted(String rfidInput) {
    for (int i = 0; i < sizeof(permittedUsers) / sizeof(permittedUsers[0]); i++) {
        if (permittedUsers[i].rfidCode == rfidInput) {
            return true; // User is permitted
        }
    }
    return false; // User is not permitted
}


void setup() {
    // Initialize Serial for debugging
    Serial.begin(115200);
    Serial.println("System starting...");

    // Initialize LCD and DHT sensor
    lcd.begin(20, 4);
    dht.begin();
    delay(2000);  // Give DHT sensor time to stabilize

    // Initialize output pins
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(LED_RED_PIN, OUTPUT);
    pinMode(LED_GREEN_PIN, OUTPUT);
    pinMode(PROXIMITY_PIN, INPUT);  // Set proximity pin as input
    myServo.attach(SERVO_PIN);  // Attach the servo to pin 12
    myServo.write(0);           // Start at 0 degrees (closed)
  
    // Set initial time (this should be set once, for example during initialization)
    setTime(9, 0, 0, 1, 1, 2024); // Set to 09:00:00 on January 1, 2024 (modify as needed)
}

void loop() {
    static unsigned long lastDHTread = 0;
    const unsigned long DHT_INTERVAL = 2000;  // Read temperature every 2 seconds
    const unsigned long CODE_TIMEOUT = 15000; // 15 seconds timeout for code entry
    static unsigned long lastCodeEntry = 0;

    // Update temperature continuously
    float temperature = dht.readTemperature();
    if (isnan(temperature)) {
        Serial.println("DHT sensor error");
        return;
    }

    // Check if temperature exceeds threshold
    if (temperature > 45) {
        // Trigger overheat warning and lockdown
        overheating = true;
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Warning:");
        lcd.setCursor(0, 1);
        lcd.print("Overheating!!!");
        tone(BUZZER_PIN, 1000); // Activate buzzer
        digitalWrite(LED_RED_PIN, HIGH); // Activate red LED
        myServo.write(0); // Close the door for safety
        delay(2000);
        
        // Wait for temperature to go below 45°C before resetting
        while (temperature > 45) {
            temperature = dht.readTemperature();
            delay(1000);
        }

        // Reset the warning state once temperature is safe
        overheating = false;
        noTone(BUZZER_PIN); // Turn off buzzer
        digitalWrite(LED_RED_PIN, LOW); // Turn off red LED
        lcd.clear();
    }

    // Handle proximity sensor input
    int proximityValue = digitalRead(PROXIMITY_PIN);
    if (proximityValue == HIGH && !overheating) {
        proximityDetected = true;
        lastCodeEntry = millis();  // Reset code entry timer

        // Check current time for working hours
        if (isWithinWorkingHours()) {
            // Display "Proximity Detected!" for 2 seconds
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Proximity Detected!");
            delay(2000);

            // Display "Welcome to Nuclear Access Control" for 3 seconds
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Welcome to Nuclear");
            lcd.setCursor(0, 1);
            lcd.print("Access Control");
            delay(3000);

            // Display access counts and prompt for RFID code
            displayAccessInfo(temperature);
        } else {
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Outside Working");
            lcd.setCursor(0, 1);
            lcd.print("Hours (9-18)");
            delay(3000);
            proximityDetected = false; // Reset proximity if outside working hours
        }
    }

    // If proximity sensor is triggered, process RFID input for multiple entries
    if (proximityDetected && !overheating && isWithinWorkingHours()) {
        // Check for RFID input and process it immediately
        if (Serial.available()) {
            String rfidInput = Serial.readStringUntil('\n');
            Serial.print("RFID Input: ");
            Serial.println(rfidInput);

            if (isUserPermitted(rfidInput)) {  // Replace with your actual RFID code
                accessGrantedCount++;
                digitalWrite(LED_GREEN_PIN, HIGH);
                digitalWrite(LED_RED_PIN, LOW);
                lcd.setCursor(0, 3);
                lcd.print("Access Granted!    ");
                myServo.write(90);  // Open the door
                delay(3000);        // Wait for 3 seconds
                myServo.write(0);   // Close the door
                delay(2000);        // Allow time for the servo to close
                digitalWrite(LED_GREEN_PIN, LOW);
            } else {
                accessDeniedCount++;
                digitalWrite(LED_RED_PIN, HIGH);
                digitalWrite(LED_GREEN_PIN, LOW);
                tone(BUZZER_PIN, 1000);
                lcd.setCursor(0, 3);
                lcd.print("Access Denied!     ");
                delay(2000);
                noTone(BUZZER_PIN);
                digitalWrite(LED_RED_PIN, LOW);
            }

            // Reset the timeout timer after each code entry
            lastCodeEntry = millis();
            
            // Immediately update the access info display
            displayAccessInfo(temperature);
        }
    }

    // Return to idle (health check) if no code entered within the timeout period
    if (proximityDetected && millis() - lastCodeEntry >= CODE_TIMEOUT && !Serial.available()) {
        proximityDetected = false;  // Exit code entry phase
    }

    // Display health check when idle and no proximity detected
    if (!proximityDetected && millis() - lastDHTread >= DHT_INTERVAL) {
        lastDHTread = millis();

        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("System Health:");
        lcd.setCursor(0, 1);
        lcd.print("Temperature: ");
        lcd.print(temperature, 1);
        lcd.print("C");
        delay(4000); // Display for 4 seconds before next check
    }

    delay(100);  // Small delay to prevent overwhelming the system
}

// Function to display access information
void displayAccessInfo(float temperature) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Granted: ");
    lcd.print(accessGrantedCount);
    lcd.print("    ");

    lcd.setCursor(0, 1);
    lcd.print("Denied: ");
    lcd.print(accessDeniedCount);
    lcd.print("    ");

    lcd.setCursor(0, 2);
    lcd.print("Temp: ");
    lcd.print(temperature, 1);
    lcd.print("C       ");
    
    lcd.setCursor(0, 3);
    lcd.print("Enter RFID Code:    ");
}

// Function to check if current time is within working hours
bool isWithinWorkingHours() {
    int currentHour = hour(); // Use the hour function from Time library
    return (currentHour >= 9 && currentHour < 18); // Working hours: 9 AM to 6 PM
}
