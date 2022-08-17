# 1 "C:\\Users\\Julien\\AppData\\Local\\Temp\\tmpz_ewjkr4"
#include <Arduino.h>
# 1 "C:/Users/Julien/Documents/Source/Repos/RFID_M0/Code_M0/src/feather_m0.ino"
# 23 "C:/Users/Julien/Documents/Source/Repos/RFID_M0/Code_M0/src/feather_m0.ino"
#include "Arduino.h"
#include "time.h"
#include "RTClib.h"
#include "SPI.h"
#include "SD.h"
#include "Servo.h"
#include "ArduinoJson.h"
#include "Adafruit_MAX31865.h"
#include "pt100rtd.h"
#include "SAMD21turboPWM.h"


#define PIN_VBAT A1
#define PIN_TEMP_CS 6
#define PIN_LED_RED 13
#define PIN_LED_SD 8
#define PIN_SD_CS 4
#define PIN_SD_CD 7
#define PIN_BUZZER_LED 9
#define PIN_PW_SWITCH A2
#define PIN_PW_SERVO A4
#define PIN_PW_3V 5
#define PIN_PW_RFID A5
#define PIN_PW_OFF A3
#define PIN_IR_1 10
#define PIN_IR_2 A0
#define PIN_IR_SEND 11
#define PIN_SERVO 12


#define RREF 4300.0
#define C2F(c) ((9 * c / 5) + 32)


struct Config {

 bool opt_IR_1 = false;
 bool opt_IR_2 = false;
 bool opt_temp_prec = true;
 bool opt_servo = false;

 int delay_loop = 10;
 const char* tag_type = "EM4102";
    int rfid_attempts = 10;
 int delay_tag_save = 1;
 int delay_temp = 10;
 bool mode_day_only = false;
 int start_time = 5;
 int stop_time = 23;
 int mode_capture = 1;
 int servo_bird_release_time = 10;
 const char* tag_1 = "01101728E6";
 const char* tag_2 = "01101728E6";
 const char* tag_3 = "01101728E6";
 const char* tag_4 = "01101728E6";
 const char* tag_5 = "01101728E6";
};


const char* filename_conf = "/config.txt";
Config config;
char filename_data[12];
File logfile;

Adafruit_MAX31865 Temp = Adafruit_MAX31865(PIN_TEMP_CS);
pt100rtd PT100 = pt100rtd();
RTC_DS3231 rtc;

DateTime time_compil, now, time_last_tag, time_last_door_closed, time_last_temp, time_file, time_last_voltage;
TimeSpan last_tag_diff;
int hours_current;
int now_ms;
bool rtc_error = false;

bool door_already_closed = false;
Servo Servo_control;
int servo_pos_opened = 10;
int servo_pos_closed = 170;
bool capture_order = false;

float measuredvbat = 0;
bool IR_1 = false, IR_1_previous = false;
bool IR_2 = false, IR_2_previous = false;
bool IR_state = false, IR_event = false;
String data, IR_name;
TurboPWM pwm;

int RFID_awake = config.rfid_attempts;
String trx, tag;
char cmd_read_tag[6];
char test[11];

bool acquisition = true;
bool tag_record = false, tag_event = false;
bool stop_event = false, start_event = false;

float Temperature = 0, Temperature_previous = 0;
float Vbat = 0, Vbat_previous = 0;
float V_LVD = 3;







void setup() {

 Serial.begin(9600);
 Serial.setTimeout(500);
 Serial1.begin(9600);
 Serial1.setTimeout(500);

 pinMode(PIN_LED_RED, OUTPUT);
 pinMode(PIN_LED_SD, OUTPUT);
 pinMode(PIN_IR_1, INPUT);
 pinMode(PIN_IR_2, INPUT);
 pinMode(PIN_PW_RFID, OUTPUT);
 pinMode(PIN_PW_3V, OUTPUT);
 pinMode(PIN_IR_SEND, OUTPUT);
 pinMode(PIN_BUZZER_LED, OUTPUT);
    pinMode(PIN_PW_OFF, OUTPUT);
 pinMode(PIN_VBAT, INPUT);
 pinMode(PIN_PW_SWITCH, INPUT);
 pinMode(PIN_PW_OFF, OUTPUT);
 pinMode(PIN_PW_SERVO, OUTPUT);
 digitalWrite(PIN_BUZZER_LED, HIGH);
 digitalWrite(PIN_PW_OFF, HIGH);
 digitalWrite(PIN_PW_RFID, HIGH);
 digitalWrite(PIN_PW_SERVO, LOW);
 digitalWrite(PIN_PW_3V, HIGH);
 pwm.setClockDivider(16, false);
 pwm.timer(2, 4, 20, true);
 pwm.analogWrite(PIN_IR_SEND, 500);

 Serial.print("Waiting for console opening");
 for (size_t r = 0; r < 20; r++) {
  Serial.print(".");
  delay(300);
 }
 const String stars = "***********************";
 Serial.println(" ");
 Serial.println(stars);
 Serial.println(stars);
 Serial.println(F("A MIBE TEAM PRODUCTION"));
 Serial.println(stars);
 Serial.println(F("Logging IR, RFID and RTD to log file with RTC timestamp"));
 Serial.println(stars);

 time_compil = DateTime(F(__DATE__), F(__TIME__));
 Serial.print(isoformat(time_compil, (int)0, ";"));
 Serial.println("\t<= Compilation date");
 Serial.println(stars);
 Serial.println(" ");
 Serial.println(F("-------SETUP-------"));

 delay(1000);


 if (!SD.begin(PIN_SD_CS)) {
  Serial.println("Error\n");
  Serial.println("SD Card init failed");
  error(2);
 }


 if (!rtc.begin()) {
  Serial.println("Error\n");
  Serial.println("Couldn't find RTC");
  delay(100);
  Serial.flush();
  abort();
 }


 now_ms = millis() % 1000;
 now = rtc.now();


 data = isoformat(now, now_ms, ";") + "System; " + "Start;" + "\n";
 if (now.unixtime() < time_compil.unixtime()) {
  rtc_error = true;
  data = isoformat(now, now_ms, ";") + "System; RTC has unknow error;" + "\n";
 }

 if (rtc.lostPower()) {
  rtc_error = true;
  data = isoformat(now, now_ms, ";") + "System; RTC lost power;" + "\n";
 }
 if (rtc_error) {
  rtc.adjust(time_compil);
  now = rtc.now();
  data = data + isoformat(now, now_ms, ";") + "System; RTC set time to compilation date; ";
 }
 else {
  data = data + isoformat(now, now_ms, ";") + "System; RTC is ok;" + "\n";
 }


 time_file = now;
 daily_data_file(filename_data, now);
 log_data(data, filename_data);

 time_last_tag = now;
 time_last_door_closed = now;
 time_last_temp = now;


 Serial.println(F("Loading SD card configuration..."));


 load_and_save_local_Configuration(Config(), config);


 printFile(filename_conf);


 if (config.opt_temp_prec == true) {
  Serial.println("MAX31865 PT100 Sensor Test using NIST resistance table.");
  Temp.begin(MAX31865_2WIRE);
 }


 if (config.opt_IR_1 == true) {
  IR_1_previous = digitalRead(PIN_IR_1);
 }

 if (config.opt_IR_2 == true) {
  IR_2_previous = digitalRead(PIN_IR_2);
 }

 String cmd_read = "@ru\r";
 if (String(config.tag_type) == "FDX") {
  cmd_read = "@rq\r";
 }
 cmd_read.toCharArray(cmd_read_tag, 6);


 if (config.mode_capture != 1)
 {
  Servo_control.attach(PIN_SERVO);
  Servo_control.write(servo_pos_opened);
  Serial.println(F("Init:\tServo door is opened"));
  delay(2000);
  digitalWrite(PIN_PW_SERVO, LOW);
  digitalWrite(PIN_PW_RFID, HIGH);
 }

 digitalWrite(PIN_BUZZER_LED, LOW);
 Serial.println(" ");
 Serial.println(F("-------LOOP-------"));
 delay(3000);
}


void loop() {







 now_ms = millis() % 1000;
 now = rtc.now();


 if (now.day() != time_file.day()) {
  time_file = now;
  daily_data_file(filename_data, now);
 }
# 324 "C:/Users/Julien/Documents/Source/Repos/RFID_M0/Code_M0/src/feather_m0.ino"
 if (true) {
# 441 "C:/Users/Julien/Documents/Source/Repos/RFID_M0/Code_M0/src/feather_m0.ino"
  if (config.opt_temp_prec == true) {
   uint16_t rtd, ohmsx100;
   uint32_t dummy;
   rtd = Temp.readRTD();



   dummy = ((uint32_t)(rtd << 1)) * 100 * ((uint32_t)floor(RREF));
   dummy >>= 16;
   ohmsx100 = (uint16_t)(dummy & 0xFFFF);







   Temperature = PT100.celsius(ohmsx100);
   checkFault();
   time_last_temp = rtc.now();
   now_ms = millis() % 1000;
   if (true) {
    Temperature_previous = Temperature;
    now_ms = millis() % 1000;
    data = isoformat(time_last_temp, now_ms, ";") + "Temperature; " + Temperature + "C° ;";
    log_data(data, filename_data);
   }

  }

 }
# 525 "C:/Users/Julien/Documents/Source/Repos/RFID_M0/Code_M0/src/feather_m0.ino"
 delay(config.delay_loop);
}


void blink(uint32_t Pin, int delay_ms, int blink_number) {
 for (uint8_t i = 0; i < blink_number; i++) {
  digitalWrite(Pin, HIGH);
  delay(delay_ms);
  digitalWrite(Pin, LOW);
  delay(delay_ms);
 }
}


void error(int error_number) {
 while (1) {
  uint8_t i;
  blink(LED_BUILTIN, 100, error_number);
  for (i = error_number; i < 10; i++) {
   delay(200);
  }
 }
}


String isoformat(DateTime t, int ms, String separator) {


 return String(t.year()) + "-" + String(t.month()) + "-" + String(t.day()) + separator + String(t.hour()) + ":" + String(t.minute()) + ":" + String(t.second()) + "." + String(ms) + ";";
}


String isoformat_date(DateTime t) {
 if (t.month() < 10 and t.day() < 10) {
  return String(t.year() - 2000) + "_0" + String(t.month()) + "_0" + String(t.day());
 }
 else if (t.month() >= 10 and t.day() >= 10) {
  return String(t.year() - 2000) + "_" + String(t.month()) + "_" + String(t.day());
 }
 else if (t.month() >= 10 and t.day() < 10) {
  return String(t.year() - 2000) + "_" + String(t.month()) + "_0" + String(t.day());
 }
 else {
  return String(t.year() - 2000) + "_0" + String(t.month()) + "_" + String(t.day());
 }
}


float get_voltage(uint32_t ulPin) {
 float measuredvbat = analogRead(ulPin);
 measuredvbat *= 2;
 measuredvbat *= 3.3;
 measuredvbat /= 1024;
 return measuredvbat;
}


void log_data(String data, const char* filename) {
 digitalWrite(PIN_LED_SD, HIGH);
 File log = SD.open(filename, FILE_WRITE);
 if (!log) {
  Serial.print("Couldnt create ");
  Serial.println(filename);
  Serial.println("error 3");
  error(3);
 }
 log.println(data);
 log.close();
 Serial.print("logged data:\n");
 Serial.println(data);
 digitalWrite(PIN_LED_SD, LOW);
}


void log_data_IR(RTC_DS3231 rtc, bool IR_state, String IR_name, const char* filename) {
 now_ms = millis() % 1000;
 data = isoformat(rtc.now(), now_ms, ";") + IR_name;
 if (IR_state == 1) {
  data += "broken beam ; ";
 }
 else {
  data += "beam restored ; ";
 }
 log_data(data, filename_data);
}


void daily_data_file(char* filename, DateTime t) {

 strcpy(filename, "01_01_00.TXT");
 char date[11];

 isoformat_date(t).toCharArray(date, 11);
 for (int i = 0; i < 8;) {
  filename[i + 0] = date[i];
  i++;
 }

 if (SD.exists(filename)) {
  Serial.print("The data file already exist\t");
 }
 else {
  Serial.print("Creating the data file\t");
 }
 Serial.println(filename);

 logfile = SD.open(filename, FILE_WRITE);
 if (!logfile) {
  Serial.print("Couldnt create\t");
  Serial.println(filename);
  Serial.println("error 3");
  error(3);
 }
 else {
  Serial.print("The data file was found or created with success\t");
  Serial.println(filename);
 }
 logfile.close();
}


void loadConfiguration(Config& config) {

 File file_c = SD.open(filename_conf);
 StaticJsonDocument<512> doc;
 DeserializationError error = deserializeJson(doc, file_c);
 if (error) {
  Serial.println(F("Failed to read file, using default configuration"));

 }


 config.opt_IR_1 = doc["opt_IR_1"];
 config.opt_IR_2 = doc["opt_IR_2"];
 config.opt_temp_prec = doc["opt_temp_prec"];
 config.opt_servo = doc["opt_servo"];
 config.mode_day_only = doc["mode_day_only"];
 config.mode_capture = doc["mode_capture"];
 config.start_time = doc["start_time"];
 config.stop_time = doc["stop_time"];
 config.delay_loop = doc["delay_loop"];
 config.tag_type = doc["tag_type"];
 config.rfid_attempts = doc["rfid_attempts"];
 config.delay_tag_save = doc["delay_tag_save"];
 config.delay_temp = doc["delay_temp"];
 config.servo_bird_release_time = doc["servo_bird_release_time"];
 config.tag_1 = doc["tag_1"];
 config.tag_2 = doc["tag_2"];
 config.tag_3 = doc["tag_3"];
 config.tag_4 = doc["tag_4"];
 config.tag_5 = doc["tag_5"];
 file_c.close();
}


void load_and_save_local_Configuration(struct Config, Config& config) {
 config.opt_IR_1 = Config().opt_IR_1;
 config.opt_IR_2 = Config().opt_IR_2;
 config.opt_temp_prec = Config().opt_temp_prec;
 config.opt_servo = Config().opt_servo;
 config.mode_day_only = Config().mode_day_only;
 config.mode_capture = Config().mode_capture;
 config.start_time = Config().start_time;
 config.stop_time = Config().stop_time;
 config.delay_loop = Config().delay_loop;
 config.tag_type = Config().tag_type;
 config.rfid_attempts = Config().rfid_attempts;
 config.delay_tag_save = Config().delay_tag_save;
 config.delay_temp = Config().delay_temp;
 config.servo_bird_release_time = Config().servo_bird_release_time;
 config.tag_1 = Config().tag_1;
 config.tag_2 = Config().tag_2;
 config.tag_3 = Config().tag_3;
 config.tag_4 = Config().tag_4;
 config.tag_5 = Config().tag_5;


 SD.remove(filename_conf);
 delay(5);
 File file = SD.open(filename_conf, FILE_WRITE);
 if (!file) {
  Serial.println(F("Failed to create file"));
 }

 StaticJsonDocument<512> doc;


 doc["opt_IR_1"] = config.opt_IR_1;
 doc["opt_IR_2"] = config.opt_IR_2;
 doc["opt_temp_prec"] = config.opt_temp_prec;
 doc["opt_servo"] = config.opt_servo;
 doc["mode_day_only"] = config.mode_day_only;
 doc["mode_capture"] = config.mode_capture;
 doc["start_time"] = config.start_time;
 doc["stop_time"] = config.stop_time;
 doc["delay_loop"] = config.delay_loop;
 doc["tag_type"] = config.tag_type;
 doc["rfid_attempts"] = config.rfid_attempts;
 doc["delay_tag_save"] = config.delay_tag_save;
 doc["delay_temp"] = config.delay_temp;
 doc["servo_bird_release_time"] = config.servo_bird_release_time;
 doc["tag_1"] = config.tag_1;
 doc["tag_2"] = config.tag_2;
 doc["tag_3"] = config.tag_3;
 doc["tag_4"] = config.tag_4;
 doc["tag_5"] = config.tag_5;

 if (serializeJson(doc, file) == 0) {
  Serial.println(F("Failed to write to config file"));
 }
 file.close();
}


void printFile(const char* filename) {

 File file = SD.open(filename);
 char letter;
 if (!file) {
  Serial.println(F("Failed to read file"));
  return;
 }
 else {
  Serial.print(F("Print file "));
  Serial.println(filename);
  Serial.print('\t');
 }

 while (file.available()) {
  letter = (char)file.read();
  if (letter == ',')
  {
   Serial.println(letter);
   Serial.print('\t');
  }
  else
  {
   Serial.print(letter);
  }
 }
 Serial.println();
 file.close();
}


void checkFault(void) {
 uint8_t fault = Temp.readFault();
 if (fault)
 {
  Serial.print("Fault 0x"); Serial.println(fault, HEX);
  if (fault & MAX31865_FAULT_HIGHTHRESH) {
   Serial.println("RTD High Threshold");
  }
  if (fault & MAX31865_FAULT_LOWTHRESH) {
   Serial.println("RTD Low Threshold");
  }
  if (fault & MAX31865_FAULT_REFINLOW) {
   Serial.println("REFIN- > 0.85 x Bias");
  }
  if (fault & MAX31865_FAULT_REFINHIGH) {
   Serial.println("REFIN- < 0.85 x Bias - FORCE- open");
  }
  if (fault & MAX31865_FAULT_RTDINLOW) {
   Serial.println("RTDIN- < 0.85 x Bias - FORCE- open");
  }
  if (fault & MAX31865_FAULT_OVUV) {
   Serial.println("Under/Over voltage");
  }
  Temp.clearFault();
 }
}

bool compare(const char* TAG_1, const char* TAG_2) {

 for (int i = 0; i < 10; i++) {
  if (!(TAG_1[i] == TAG_2[i])) {
   return false;
  }
 }
 return true;
}


unsigned int hex2int(char input) {
 if (input >= '0' && input <= '9') {
  return input - '0';
 }
 else {
  if (input >= 'A' && input <= 'F') {
   return input - 'A' + 10;
  }
  else {
   if (input >= 'a' && input <= 'f') {
    return input - 'a' + 10;
   }
   else {
    return '0';
   }
  }
 }
}


String longlong2String(unsigned long long int bigint) {
 String s = "";
 int digit;


 while (bigint > 0) {
  digit = (bigint % 10);
  s = String(digit) + s;
  bigint = bigint / 10;

 }

 return s;
}


String tag_hex_to_NIC(String src) {




 char hex[12];
 unsigned int a, z = 0;
 unsigned int codePays = 0;
 unsigned long long int codeNIC = 0;
 if (src.length() == 16)
 {

  src.toCharArray(hex, 13, 4);


  for (int i = 0; i < 3; i++) {

   a = hex2int(hex[i]);
   z = (z << 4) | a;


  }
  codePays = z >> 2;

  codeNIC = (3 & z);


  for (size_t i = 3; i < strlen(hex); i++) {

   a = hex2int(hex[i]);
   codeNIC = (codeNIC << 4) | a;

  }

  return (String(codePays) + "-" + longlong2String(codeNIC));
 }
 else return "err: tag hexa de mauvaise taille";

}


void shutDownButton(void) {
 bool stateButton = digitalRead(PIN_PW_SWITCH);
 int loops = 0;
 while (stateButton == true) {
  loops = loops + 1;
  digitalWrite(PIN_BUZZER_LED, HIGH);
  delay(100);
  digitalWrite(PIN_BUZZER_LED, LOW);
  delay(100);
  stateButton = digitalRead(PIN_PW_SWITCH);
  if (loops == 6) {
   digitalWrite(PIN_BUZZER_LED, HIGH);
   data = isoformat(rtc.now(), now_ms, ";") + "System; " + "Shut Down Button;";
   log_data(data, filename_data);
   delay(1000);
   digitalWrite(PIN_PW_OFF, LOW);
   while (1) {
    delay(10000);
   }
  }
 }
}
