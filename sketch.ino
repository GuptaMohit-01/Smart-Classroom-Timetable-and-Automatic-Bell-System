#include "secrets.h"  // Must come before Blynk library
#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <Wire.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>
#include <BlynkSimpleEsp32.h>

#define SDA_PIN 21
#define SCL_PIN 22
#define BUZZER_PIN 18

RTC_DS1307 rtc;
LiquidCrystal_I2C lcd(0x27, 16, 2);
BlynkTimer timer;

enum BellType {
  ASSEMBLY,
  FIRST_PERIOD,
  REGULAR_PERIOD,
  LUNCH_BREAK,
  DISMISSAL
};

struct BellEvent {
  int hour;
  int minute;
  const char* name;
  BellType type;
};

BellEvent timetable[] = {
  { 8, 45, "Assembly",       ASSEMBLY },
  { 9,  0, "First Period",   FIRST_PERIOD },
  { 9, 50, "Regular Period", REGULAR_PERIOD },
  {10, 40, "Regular Period", REGULAR_PERIOD },
  {11, 30, "Regular Period", REGULAR_PERIOD },
  {12, 20, "Lunch Break",    LUNCH_BREAK },
  {13,  0, "Regular Period", REGULAR_PERIOD },
  {13, 50, "Regular Period", REGULAR_PERIOD },
  {14, 40, "Regular Period", REGULAR_PERIOD },
  {15, 30, "Dismissal Bell", DISMISSAL }
};

const int EVENT_COUNT =
  sizeof(timetable) / sizeof(timetable[0]);

uint32_t lastTriggeredDate[EVENT_COUNT] = {0};

bool scheduleEnabled = true;
bool isRinging = false;

unsigned long lastReconnectAttempt = 0;

String getBellStatus() {
  if (isRinging) return "Ringing";
  if (!scheduleEnabled) return "Disabled";
  return "Idle";
}

void publishBellStatus() {
  if (Blynk.connected()) {
    Blynk.virtualWrite(V9, getBellStatus());
  }
}

void beep(int frequency, int duration) {
  tone(BUZZER_PIN, frequency, duration);
  delay(duration);
  noTone(BUZZER_PIN);
  delay(150);
}

void playBell(BellType type, const char* eventName) {
  isRinging = true;

  Serial.print("BELL EVENT: ");
  Serial.println(eventName);

  publishBellStatus();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(eventName);
  lcd.setCursor(0, 1);
  lcd.print("Bell is ringing");

  switch (type) {
    case ASSEMBLY:
      beep(800, 180);
      beep(1000, 180);
      beep(1200, 180);
      break;

    case FIRST_PERIOD:
      beep(1500, 900);
      break;

    case REGULAR_PERIOD:
      beep(1000, 300);
      beep(1000, 300);
      break;

    case LUNCH_BREAK:
      beep(600, 600);
      beep(600, 600);
      break;

    case DISMISSAL:
      beep(1400, 350);
      beep(1000, 350);
      beep(700, 700);
      break;
  }

  isRinging = false;
  publishBellStatus();

  Serial.println("Bell event completed.");
}

void triggerManualBell(BellType type,
                       const char* name,
                       int virtualPin) {
  // Reset the dashboard switch so each press is a new command.
  if (Blynk.connected()) {
    Blynk.virtualWrite(virtualPin, 0);
  }

  playBell(type, name);
}

BLYNK_CONNECTED() {
  Serial.println("Blynk Cloud connected!");
  Blynk.syncVirtual(V3);
  publishBellStatus();
}

BLYNK_WRITE(V3) {
  scheduleEnabled = (param.asInt() == 1);

  Serial.print("Automatic schedule: ");
  Serial.println(scheduleEnabled ? "ENABLED" : "DISABLED");

  publishBellStatus();
}

BLYNK_WRITE(V4) {
  if (param.asInt() == 1) {
    triggerManualBell(ASSEMBLY, "Assembly", V4);
  }
}

BLYNK_WRITE(V5) {
  if (param.asInt() == 1) {
    triggerManualBell(FIRST_PERIOD, "First Period", V5);
  }
}

BLYNK_WRITE(V6) {
  if (param.asInt() == 1) {
    triggerManualBell(REGULAR_PERIOD, "Regular Period", V6);
  }
}

BLYNK_WRITE(V7) {
  if (param.asInt() == 1) {
    triggerManualBell(LUNCH_BREAK, "Lunch Break", V7);
  }
}

BLYNK_WRITE(V8) {
  if (param.asInt() == 1) {
    triggerManualBell(DISMISSAL, "Dismissal Bell", V8);
  }
}

void printTwoDigits(int number) {
  if (number < 10) lcd.print('0');
  lcd.print(number);
}

void displayClock(const DateTime& now) {
  lcd.setCursor(0, 0);
  lcd.print("Time: ");
  printTwoDigits(now.hour());
  lcd.print(':');
  printTwoDigits(now.minute());
  lcd.print(':');
  printTwoDigits(now.second());
  lcd.print("  ");

  lcd.setCursor(0, 1);
  lcd.print("Date: ");
  printTwoDigits(now.day());
  lcd.print('/');
  printTwoDigits(now.month());
  lcd.print('/');
  lcd.print(now.year());
  lcd.print(" ");
}

int findCurrentEvent(const DateTime& now) {
  int currentMinutes = now.hour() * 60 + now.minute();
  int result = -1;

  for (int i = 0; i < EVENT_COUNT; i++) {
    int eventMinutes =
      timetable[i].hour * 60 + timetable[i].minute;

    if (eventMinutes <= currentMinutes) {
      result = i;
    } else {
      break;
    }
  }

  return result;
}

int findNextEvent(const DateTime& now) {
  int currentMinutes = now.hour() * 60 + now.minute();

  for (int i = 0; i < EVENT_COUNT; i++) {
    int eventMinutes =
      timetable[i].hour * 60 + timetable[i].minute;

    if (eventMinutes > currentMinutes) {
      return i;
    }
  }

  // All today's events have passed; next is tomorrow's assembly.
  return 0;
}

void publishDashboard() {
  if (!Blynk.connected()) return;

  DateTime now = rtc.now();

  char timeBuffer[12];
  snprintf(timeBuffer, sizeof(timeBuffer),
           "%02d:%02d:%02d",
           now.hour(), now.minute(), now.second());

  Blynk.virtualWrite(V0, timeBuffer);

  int currentIndex = findCurrentEvent(now);

  if (currentIndex < 0) {
    Blynk.virtualWrite(V1, "Before Assembly");
  } else if (currentIndex == EVENT_COUNT - 1) {
    Blynk.virtualWrite(V1, "Classes Ended");
  } else {
    Blynk.virtualWrite(V1, timetable[currentIndex].name);
  }

  int nextIndex = findNextEvent(now);
  bool tomorrow =
    (now.hour() * 60 + now.minute()) >=
    (timetable[EVENT_COUNT - 1].hour * 60 +
     timetable[EVENT_COUNT - 1].minute);

  char nextBuffer[48];
  snprintf(nextBuffer, sizeof(nextBuffer),
           "%s%s %02d:%02d",
           tomorrow ? "Tomorrow: " : "",
           timetable[nextIndex].name,
           timetable[nextIndex].hour,
           timetable[nextIndex].minute);

  Blynk.virtualWrite(V2, nextBuffer);
  Blynk.virtualWrite(V9, getBellStatus());
}

void checkAutomaticSchedule() {
  if (!scheduleEnabled || isRinging) return;

  DateTime now = rtc.now();

  uint32_t todayKey =
    (uint32_t)now.year() * 10000UL +
    (uint32_t)now.month() * 100UL +
    (uint32_t)now.day();

  for (int i = 0; i < EVENT_COUNT; i++) {
    if (now.hour() == timetable[i].hour &&
        now.minute() == timetable[i].minute &&
        lastTriggeredDate[i] != todayKey) {

      // Mark before playing to prevent repeated triggers.
      lastTriggeredDate[i] = todayKey;
      playBell(timetable[i].type, timetable[i].name);
      break;
    }
  }
}

void setup() {
  Serial.begin(115200);

  Wire.begin(SDA_PIN, SCL_PIN);
  pinMode(BUZZER_PIN, OUTPUT);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Classroom Bell");
  lcd.setCursor(0, 1);
  lcd.print("Connecting...");

  if (!rtc.begin()) {
    Serial.println("ERROR: RTC not detected!");
    lcd.clear();
    lcd.print("RTC not found");

    while (true) {
      delay(10);
    }
  }

  if (!rtc.isrunning()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  WiFi.begin("Wokwi-GUEST", "", 6);

  Serial.print("Connecting to WiFi");

  unsigned long wifiStart = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - wifiStart < 15000) {
    delay(250);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected!");
    Blynk.config(BLYNK_AUTH_TOKEN);

    if (Blynk.connect(10000)) {
      Serial.println("Blynk connection successful!");
    } else {
      Serial.println("Blynk not connected yet; retrying.");
    }
  } else {
    Serial.println("WiFi connection timed out.");
  }

  timer.setInterval(2000L, publishDashboard);

  Serial.println("Smart Classroom Bell ready.");
  lcd.clear();
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    if (Blynk.connected()) {
      Blynk.run();
    } else if (millis() - lastReconnectAttempt >= 5000) {
      lastReconnectAttempt = millis();
      Serial.println("Retrying Blynk connection...");
      Blynk.connect(1000);
    }
  }

  timer.run();

  static unsigned long lastScheduleCheck = 0;

  if (millis() - lastScheduleCheck >= 200) {
    lastScheduleCheck = millis();
    checkAutomaticSchedule();

    if (!isRinging) {
      displayClock(rtc.now());
    }
  }

  delay(10);
}
