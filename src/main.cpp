/*
 Name:		Feather_M0.ino
 Created:	2020/11/18
 Author:	jcourtec
*/

/*
* Hardware configuration
****************************************
* Processor	Feather M0 Adalogger board
* RTC x 1 DS3231
* Sensors
*	Infra-Red board
*		x 2	standard device
*	Temperature probe
*		x 1	PT100 wih MAX31865 digitilazer
*	RFID
*		x 1 Tectus TLB-30-SER
* Servomotor x 1 Hitec HS-53 #TODO
*/

//Libraries
#include "Arduino.h"
#include "time.h"
#include "RTClib.h"
#include "SPI.h"
#include "SD.h"
#include "Servo.h"
#include "ArduinoJson.h"
#include "Adafruit_MAX31865.h"
#include "pt100rtd.h" // for feather M0, in this .h replace #include <pgmspace.h> by #include <avr\pgmspace.h>
#include "SAMD21turboPWM.h"
#include "FlashStorage.h"

// Adafruit board pin mapping
#define PIN_VBAT		A1                 // analog input for battery voltage measurement
#define PIN_PW_SW		A2				   // input for power switch
#define PIN_PW_EN  		A3             	   // output for power relay low battery
#define PIN_TEMP_CS		6                  // input for RTD sensor
#define PIN_LED_SD		8	               // input/output for SD card
#define PIN_SD_CS		4	               // input/output for SD card
#define PIN_SD_CD		7	               // input/output for SD card
#define PIN_BUZZER_LED 	9                  // output for buzzer or led
#define PIN_PW_SERVO  	A4	               // output for power relay of servomotor
#define PIN_PW_3V       5                  // output for power relay of IRs and RTD
#define PIN_PW_RFID     A5                 // output for power relay of RFID
#define PIN_IR_1		A0                 // input for IR sensor 1
#define PIN_IR_2	    10             	   // input for IR sensor 2
#define PIN_IR_SEND     11                 // output pwm 36kHz for IR sensor
#define PIN_SERVO		12	               // output pwm for signal servo pin

// output for CS temperature with MAX31865
#define RREF            430.0               // resistance reference for RTD
#define C2F(c)          ((9 * c / 5) + 32)  // temperature conversion function, celcius to fahrenheit

// Electrical characteristics of the power board
#define LIION_BATTERY

#ifdef LIION_BATTERY
	#define POWER_BOARD_BATT_RATIO   2			// Ratio of the voltage divider : 2 for a 5V-board - 6 for a 12V-board
	#define BATTERY_MIN_VOLTAGE      3			// Low voltage disconnect in volts : 3 for LiPo battery - 12 for a 12V lead battery
	#define BATTERY_MAX_VOLTAGE     4.2         // Battery voltage when 100% full : 4.2 for LiPo battery - 13.5 for a 12V lead battery
#else
	#define POWER_BOARD_BATT_RATIO   6			// Ratio of the voltage divider : 2 for a 5V-board - 6 for a 12V-board
	#define BATTERY_MIN_VOLTAGE      12			// Low voltage disconnect in volts : 3 for LiPo battery - 12 for a 12V lead battery
	#define BATTERY_MAX_VOLTAGE     13.5        // Battery voltage when 100% full : 4.2 for LiPo battery - 13.5 for a 12V lead battery
#endif

// Characteristics of the servo
#define SERVO_POS_OPENED         132        // Opened position for servo in degrees
#define SERVO_POS_CLOSED         65         // Closed position for servo in degrees

//---- Temperature measurment with thermistor --------//
//#define THERMISTOR_SECURITY

#ifdef THERMISTOR_SECURITY
	#define SERIESRESISTOR 10000 				// the value of the NTC resistor
	#define THERMISTORNOMINAL 10000 			// resistance at 25 degrees C
	#define TEMPERATURENOMINAL 25 				// temp. for nominal resistance (almost always 25 C)
	#define NUMSAMPLES 5 						// how many samples to take and average, more takes longer  but is smoother
	#define BCOEFFICIENT 3950 					// The beta coefficient of the thermistor (usually 3000-4000)
	#define THERMISTORPIN A4  					// What pin to connect the sensor to

	int samples[NUMSAMPLES];
#endif

// User parameters
struct Config {
	// Hardware options
	bool opt_IR_1 = true;               // use infrared sensor 1
	bool opt_IR_2 = true;               // use infrared sensor 2
	bool opt_temp_prec = false;         // use temperature recording
	// Modes and key parameters
	int delay_loop = 10;			    // loop delay in ms (time to sleep between checking sensors) RFID timeout is always adding to this delay_loop
	String tag_type = "FDX";            // TAG supported "FDX" / "EM4102"
    int rfid_attempts = 10;             // how many times the RFID will try to read TAG after IR event
	int delay_tag_save = 1; 	        // time in seconds to save a tag sitting on the antenna
	int delay_temp = 60;				// period in seconds to record temperature
	bool mode_time_period = false;   	// activation only between start_time and stop_time hours 
	int start_time = 5;			        // in day only mode, hour to start the device, value 0 to 23
	int stop_time = 23;			        // in day only mode, hour to stop the device, 1 to 24
	int mode_capture = 1;               // 1 no captures, 2 capture all, 3 capture specific tags, 4 capture non tagged
	int release_time = 10;   			// time in seconds for a release after capture
	const char* tag_1 = "01101728E6";   // tags for mode capture 3
	const char* tag_2 = "01101728E6";   // tags for mode capture 3
	const char* tag_3 = "01101728E6";   // tags for mode capture 3
	const char* tag_4 = "01101728E6";   // tags for mode capture 3
	const char* tag_5 = "01101728E6";   // tags for mode capture 3
};

// Assembly description
struct Assembly {
	String uid_mainboard 	= "$uid_mainboard$";
	String uid_powerboard 	= "$uid_powerboard$";
	String uid_tectus 		= "$uid_tectus$";
	String uid_rfid_sensor 	= "$uid_rfid_sensor$";
	String uid_software 	= "$uid_software$";
	String uid_experiment 	= "$uid_experiment$";
};

struct Average_value {
    float array[3];
    float mean_value;
    float mean_value_previous;
    float threshold;
};


//Variables declaration
const char* filename_conf = "/config.cfg";                     // config file
const char* filename_assembly = "/assembly.cfg";               // Assembly description file
Config config;                                                 // global configuration object
Assembly assembly;
char filename_data[13];                                        // NB Files names are limited to 8 characters : 8 charac + '.TXT' + \0
File logfile;

Adafruit_MAX31865 Temp = Adafruit_MAX31865(PIN_TEMP_CS);       // Temperature MAX31865
pt100rtd PT100 = pt100rtd();                                   // Init the Pt100 table lookup module
const float temp_max = 50;									   // Temperature Thermistor
Average_value Temperature = {{0,0,0}, 0, 0, 0.02};		       // Temperature : Current and previous raw values, current and previous mean value	

RTC_DS3231 rtc;                                                // Real Time Clock
bool rtc_error = false;										   // RTC lost power or wrong date
FlashStorage(rtc_updated, bool);							   // Flag to know if the RTC was already updated - Automatically reset during upload
DateTime time_compil, now, time_start, time_last_tag, time_last_door_closed, time_last_temp, time_file, time_last_voltage, time_off_user_buzzer, time_last_up;
TimeSpan last_tag_diff;
int now_ms;													   // Current millisecond from millis()
bool user_buzzer_on;									  	   // User signal activation BUZZER or LED
TimeSpan delay_user_buzzer = TimeSpan(300);

Servo Servo_control;		                                   // Servo object
bool capture_order = false;                                    // Command to close the door
bool door_already_closed = true;                               // Status of the door

bool IR_1 = false, IR_1_previous = false;                      // IR_1 tracker variables
bool IR_2 = false, IR_2_previous = false;                      // IR_2 tracker variables
bool IR_state = false, IR_event = false;                       // IR events variables
TurboPWM pwm;												   // PWM used for infrared emitter

int RFID_awake = config.rfid_attempts;						   // Number of remaining attempts to read the RFID
String trx, trx_previous, tag;								   // Current and previous output of the RID reader
char cmd_read_tag[6];               						   // RFID reading command
char tag_to_compare[11];

bool acquisition = true;									   // Status of the acquisition
bool tag_record = false, tag_event = false;                    // Flag to kow if a tag was detected and need to be saved
bool search_tag = false;                                       // Flag to know if we are currently looking for a tag after an IR event (used for capture mode #4)
bool battery_condition = true;							       // Condition to run acquisition based on battery voltage
bool time_period_condition = true;						       // Condition to run acquisition based on time period (if mode_time_period = true)
String data;

Average_value Vbat = {{0,0,0}, 0, 0, 0.1};					   // Battery voltage : Current and previous raw values, current and previous mean value	

// Functions
/*
  We have so many functions, you'll find them at the end of the file
*/
void blink(uint32_t Pin, int delay_ms, int blink_number);
void error(int error_number);
String isoformat(DateTime t, int ms, String separator);
String isoformat_date(DateTime t);
float get_voltage(uint32_t ulPin);
void log_data(String data, const char* filename);
void log_data_IR(RTC_DS3231 rtc, bool IR_state, String IR_name, const char* filename);
String get_component(String data_type);
void daily_data_file(char* filename, DateTime now);
void loadConfiguration(Config& config);
void load_and_save_local_Configuration();
void loadAssembly(Assembly& assembly);
void create_config_file();
void create_assembly_file();
void printFile(const char* filename);
void checkFault(void);
bool compare(const char* TAG_1, const char* TAG_2);
unsigned int hex2int(char input);
String longlong2String(unsigned long long int bigint);
String tag_hex_to_NIC(String src);
void switchButtonMgmt(void);
void closeDoor(bool door_cmd_closed, String reason = "");
bool moving_average(struct Average_value *s, float updated_value);
#ifdef THERMISTOR_SECURITY
float readThermistorTemperature();
#endif

// The setup function runs once when you press reset or power the board
void setup() {
	// Start serial connection
	Serial.begin(9600);
	Serial.setTimeout(500);
	Serial1.begin(9600);
	Serial1.setTimeout(500);
	// Configure the IO
	pinMode(PIN_LED_SD, OUTPUT);
	pinMode(PIN_IR_1, INPUT);
	pinMode(PIN_IR_2, INPUT);
	pinMode(PIN_VBAT, INPUT);
	pinMode(PIN_PW_SW, INPUT);	
	pinMode(PIN_PW_RFID, OUTPUT);
	pinMode(PIN_PW_3V, OUTPUT);
	pinMode(PIN_IR_SEND, OUTPUT);
	pinMode(PIN_BUZZER_LED, OUTPUT);
    pinMode(PIN_PW_EN, OUTPUT);
#ifndef THERMISTOR_SECURITY
	pinMode(PIN_PW_SERVO, OUTPUT);
	digitalWrite(PIN_PW_SERVO, LOW);
#else
	pinMode(THERMISTORPIN, INPUT);
#endif
	digitalWrite(PIN_PW_EN, HIGH);
	digitalWrite(PIN_PW_RFID, HIGH);
	digitalWrite(PIN_PW_3V, LOW);
	digitalWrite(PIN_BUZZER_LED, HIGH);
	pwm.setClockDivider(16, false);		// Main clock divided by 16 => 3MHz
	pwm.timer(2, 4, 20, true);			// Use timer 2 for pin PIN_IR_SEND, divide clock by 4, resolution 20, single-slope PWM

	// RTC initialisation
	if (!rtc.begin()) {
		Serial.println("Error\n");
		Serial.println("Couldn't find RTC");
		delay(100);
		Serial.flush();
		error(4);
	}

	time_start = rtc.now();
	time_compil = DateTime(F(__DATE__), F(__TIME__));

	// If the flash memory is reset, a new code was uploaded --> update RTC
	if (not rtc_updated.read()){
		rtc.adjust(time_compil);
	}

	Serial.print("Waiting for console opening");
	for (size_t r = 0; r < 20; r++) {
		Serial.print(".");
		delay(300); // wait for console opening
	}
	const String stars = "***********************";
	Serial.println(" ");
	Serial.println(stars);
	Serial.println(stars);
	Serial.println(F("A MIBE TEAM PRODUCTION"));
	Serial.println(stars);
	Serial.println(F("Logging IR, RFID and RTD to log file with RTC timestamp"));
	Serial.println(stars);
	// Print the date and time of program launch
	Serial.print(isoformat(time_compil, (int)0, ";"));
	Serial.println("\t<= Compilation date");
	Serial.println(stars);
	Serial.println(" ");
	Serial.println(F("-------SETUP-------"));

	delay(1000);

	// See if the card is present and can be initialized:
	if (!SD.begin(PIN_SD_CS)) {
		Serial.println("Error\n");
		Serial.println("SD Card init failed");
		error(2);
	}

	now_ms = millis() % 1000;
	now = rtc.now();

	// load the assembly description
	loadAssembly(assembly);

	// Log system start-up
	data = isoformat(time_start, now_ms, ";") + get_component("System") + "Start;\n";

	// If the flash memory is reset, a new code was uploaded and the RTC update was just done (previously in the code)
	if (not rtc_updated.read()){
		rtc_updated.write(true);
		data = data + isoformat(time_start, now_ms, ";") + get_component("System") + "RTC set time to compilation date;";
	}
	else {
		// Check for RTC errors : wrong date or power loss
		if (rtc.lostPower()) {
			rtc_error = true;
			data = data + isoformat(now, now_ms, ";") + get_component("System") + "RTC lost power;";
		}
		else if (now.unixtime() < time_compil.unixtime()) {
			rtc_error = true;
			data = data + isoformat(now, now_ms, ";") + get_component("System") + "RTC has unknow error;";
		}
		else {
			data = data + isoformat(now, now_ms, ";") + get_component("System") + "RTC is ok;";
		}
	}

	// Creating a new file at setup
	time_file = now;
	daily_data_file(filename_data, now);
	log_data(data, filename_data);

	if (rtc_error) {
		error(4);
	}

	time_last_tag = now;
	time_last_door_closed = now;
	time_last_temp = now;
	time_last_up = now;
	time_off_user_buzzer = now + delay_user_buzzer;

	// use this line to load configuration from SD config.cfg file
	Serial.println(F("Loading SD card configuration..."));
	loadConfiguration(config);
	// use this line instead to load configuration from struct object defined in the program
	//load_and_save_local_Configuration();

	// Dump config & assembly files
	printFile(filename_conf);
	printFile(filename_assembly);

	// Activation of 3.3V only if at least one IR sensor active or temp sensor active
	if (config.opt_IR_1 == true or config.opt_IR_2 == true or config.opt_temp_prec == true){
		digitalWrite(PIN_PW_3V, HIGH);
	}

	// Activation of PWM only if at least one IR sensor is active
	if (config.opt_IR_1 == true or config.opt_IR_2 == true){
		pwm.analogWrite(PIN_IR_SEND, 500);  // PWM frequency is now around 36KHz, dutycycle is 500 / 1000 * 100% = 50%
	}

	// initialization of the temperature sensor
	if (config.opt_temp_prec == true) {
		Serial.println("MAX31865 PT100 Sensor Test using NIST resistance table.");
		Temp.begin(MAX31865_2WIRE);  // set to 2WIRE
	}

	// Small delay to let time to IR receiver to start
	delay(50);

	// initialization of IR variables
	if (config.opt_IR_1 == true) {
		IR_1_previous = digitalRead(PIN_IR_1);
	}

	if (config.opt_IR_2 == true) {
		IR_2_previous = digitalRead(PIN_IR_2);
	}

	// Loading RFID settings
	String cmd_read = "@ru\r";
	if (config.tag_type == "FDX") {
		cmd_read = "@rq\r";
	}
	cmd_read.toCharArray(cmd_read_tag, 6);

	// Initialization of the servo motor : Door is opened by default
#ifndef THERMISTOR_SECURITY
	Servo_control.attach(PIN_SERVO);
	closeDoor(false, "Init");
#endif

	//Initialization battery voltage & log data
	moving_average(&Vbat, get_voltage(PIN_VBAT));
	delay(100);
	moving_average(&Vbat, get_voltage(PIN_VBAT));
	delay(100);
	moving_average(&Vbat, get_voltage(PIN_VBAT));
	data = isoformat(rtc.now(), now_ms, ";") + get_component("Vbat") + String(Vbat.mean_value) + "V;";
	log_data(data, filename_data);

	user_buzzer_on = true;
	digitalWrite(PIN_BUZZER_LED, LOW);
	Serial.println(" ");
	Serial.println(F("-------LOOP-------"));

	// 3 second delay to let time for user to release switch button : avoid to call switchButtonMgmt function after power-on
	delay(3000);
}

// the loop function runs over and over again until power down or reset
void loop() {
	// !!!!!!!!!!!!!!!!!!!!!!!!!!!
	// TODO Time offset !
	// !!
	// Set an offset when the RTC second change happen else you could get 40.914s , 40.230s, 40.734s, 41.238s as real time
	// !!!!!!!!!!!!!!!!!!!!!!!!!!!

	// Refresh time for the loop
	now_ms = millis() % 1000;
	now = rtc.now();
	
	// Creating a new file each day
	if (now.day() != time_file.day()) {
		time_file = now;
		daily_data_file(filename_data, now);
	}

	// Disable user signal - Led_ext or Buzzer - after a delay # 300s
	if (user_buzzer_on) { 				// then check time
		if (now.unixtime() > time_off_user_buzzer.unixtime()) {
			user_buzzer_on = false;
		}
	}


	// Check if acquisition should run according to day only mode
	if (config.mode_time_period == true) {
		int hours_current = now.hour();
		time_period_condition = false;
		// If the time period is defined on the same day
		if (config.start_time < config.stop_time) {
			// Current hour is in the range
			if ((hours_current >= config.start_time) and (hours_current < config.stop_time)) {
				time_period_condition = true;
			}
		}
		// Else, it means the time period includes a change of day (night mode)
		else {
			// Current hour is in the range
			if ((hours_current >= config.start_time) or (hours_current < config.stop_time))	{
				time_period_condition = true;
			}
		}
	}


	// Stop acquisition if currently running and conditions are not satisfied
	if (not(battery_condition and time_period_condition) and acquisition) {
		// Security : Open the door if closed
		closeDoor(false, "Security");
		acquisition = false;
		digitalWrite(PIN_PW_3V, LOW);
		digitalWrite(PIN_PW_RFID, LOW);
		data = isoformat(rtc.now(), now_ms, ";") + get_component("System") + "Sleep mode;";
		log_data(data, filename_data);
	}


	// Start acquisition if currently sleeping and conditions are satisfied
	if (battery_condition and time_period_condition and (!acquisition)) {
		acquisition = true;
		digitalWrite(PIN_PW_3V, HIGH);
		digitalWrite(PIN_PW_RFID, HIGH);
		data = isoformat(rtc.now(), now_ms, ";") + get_component("System") + "Wake up mode;";
		log_data(data, filename_data);
	}


	// While acquisition is running
	if (acquisition == true) {
		IR_event = false;

		// Event on infrared sensor 1
		if (config.opt_IR_1 == true) {
			// record infrared beam events
			IR_1 = digitalRead(PIN_IR_1);
			if (IR_1 != IR_1_previous) {
				IR_1_previous = IR_1;
				IR_event = true;
				if ((IR_1 == 1) and not search_tag) {search_tag = true;}
				log_data_IR(rtc, IR_1, "IR 1", filename_data);
				RFID_awake = config.rfid_attempts;
			}
		}

		// Event on infrared sensor 2
		if (config.opt_IR_2 == true) {
			IR_2 = digitalRead(PIN_IR_2);
			if (IR_2 != IR_2_previous) {
				IR_2_previous = IR_2;
				IR_event = true;
				if ((IR_2 == 1) and not search_tag) {search_tag = true;}
				log_data_IR(rtc, IR_2, "IR 2", filename_data);
				RFID_awake = config.rfid_attempts;
			}
		}

		// Read RFID a certain number of attemps base on external events (IR, ..)
		if (RFID_awake > 0) {
			Serial1.write(cmd_read_tag);
			delay(10);
			trx = Serial1.readStringUntil('\r');
			delay(10);
			trx.trim();
			tag_event = false;
			if (trx.indexOf("-") < 0) { //  if '-' not in trx:
				tag_event = true;
				search_tag = false;
				trx.replace("+ ", "");
				trx.replace("ru", "");
				if (trx.length() >= 5) {	// avoid tagless file names
					if (trx == trx_previous) {	// avoid repeated records
						last_tag_diff = rtc.now() - time_last_tag;
						if (last_tag_diff.seconds() >= config.delay_tag_save) {
							tag_record = true;
						}
					}
					else {
						tag_record = true;
					}
				}
			}

			// Record a pit tag
			if (tag_record == true) {
				if (user_buzzer_on) {
					digitalWrite(PIN_BUZZER_LED, HIGH);
				}

				time_last_tag = rtc.now();
				now_ms = millis() % 1000;

				if (config.tag_type == "FDX") {
					tag = tag_hex_to_NIC(trx);
				}
				else {
					tag = trx;
				}
				data = isoformat(time_last_tag, now_ms, ";") + get_component("A0") + tag + ";";
				log_data(data, filename_data);
				tag_record = false;
				trx_previous = trx;
				digitalWrite(PIN_BUZZER_LED, LOW);
			}

			if (config.opt_IR_1 or config.opt_IR_2) {
				RFID_awake--;
			}

		}

		// Action with the servo - Capture
		if (config.mode_capture != 1) {
			// Mode 2 : Capture if one infrared event detected
			if (config.mode_capture == 2 and IR_event == true) {
				capture_order = true;
			}
			// Mode 3 : Capture if the detected tag matches the ones defined by user
			if (config.mode_capture == 3 and tag_event == true) {
				tag.toCharArray(tag_to_compare, 11);
				capture_order = compare(tag_to_compare, config.tag_1) or compare(tag_to_compare, config.tag_2) or compare(tag_to_compare, config.tag_3) or compare(tag_to_compare, config.tag_4) or compare(tag_to_compare, config.tag_5);
			}
			// Mode 4 : Capture if infrared event with no succeed to read RFID tag
			if (config.mode_capture == 4 and search_tag and (RFID_awake == 0)) {
				capture_order = true;
				search_tag = false;
			}
			// Control the door
			if (capture_order == true)
			{
				closeDoor(true);
				capture_order = false;
			}
		}

		// Temperature
		if (config.opt_temp_prec == true and ((time_last_temp.unixtime() + config.delay_temp) <= now.unixtime())) {

			float current_temperature;

#ifdef THERMISTOR_SECURITY

			current_temperature = readThermistorTemperature();

#else
			uint16_t rtd, ohmsx100;
			uint32_t dummy;
			rtd = Temp.readRTD();

			// Use uint16_t (ohms * 100) since it matches data type in lookup table.
			dummy = ((uint32_t)(rtd << 1)) * 100 * ((uint32_t)floor(RREF));
			dummy >>= 16;
			ohmsx100 = (uint16_t)(dummy & 0xFFFF);

			// or use exact ohms floating point value.
			//ohms = (float)(ohmsx100 / 100) + ((float)(ohmsx100 % 100) / 100.0);
			//Serial.print("rtd: 0x"); Serial.print(rtd, HEX);
			//Serial.print(", ohms: "); Serial.println(ohms, 2);

			// LookUp Table method
			current_temperature = PT100.celsius(ohmsx100);
			checkFault();
#endif

			time_last_temp = rtc.now();
			now_ms = millis() % 1000;
			if (moving_average(&Temperature, current_temperature)) {
				now_ms = millis() % 1000;
				data = isoformat(time_last_temp, now_ms, ";") + get_component("Temperature") + String(Temperature.mean_value) + "C°;";
				log_data(data, filename_data);
			}


			// Power off 
			if (Temperature.mean_value > temp_max) {
				// Security : Open the door if closed
				closeDoor(false, "Security");
				data = isoformat(rtc.now(), now_ms, ";") + get_component("System") + "Shutdown : High temp;";
				log_data(data, filename_data);
				blink(PIN_BUZZER_LED, 100, 6);
				digitalWrite(PIN_PW_EN, LOW);
			}
		}

	}


	// SECURITY : Open door if closed for a certain time 
	if (((time_last_door_closed.unixtime() + config.release_time) <= now.unixtime()) and door_already_closed) {
		closeDoor(false, "Release time");
	}


	// Check battery voltage every 2 seconds
	if ((time_last_voltage.unixtime() + 2) <= now.unixtime()) {

		time_last_voltage = now;
		
		// Voltage measurement
		if (moving_average(&Vbat, get_voltage(PIN_VBAT))) {
			now_ms = millis() % 1000;
			data = isoformat(rtc.now(), now_ms, ";") + get_component("Vbat") + String(Vbat.mean_value) + "V;";
			log_data(data, filename_data);
		}
		// Force sleep mode if battery voltage is too low
		if ((Vbat.mean_value <= (BATTERY_MIN_VOLTAGE + 0.1)) and battery_condition) {
			data = isoformat(rtc.now(), now_ms, ";") + get_component("Vbat") + String(Vbat.mean_value) + "V;";
			data = isoformat(rtc.now(), now_ms, ";") + get_component("Vbat") + "Power saving;";
			log_data(data, filename_data);
			battery_condition = false;
		}
		// Ask for wake up if battery voltage reaches acceptable voltage
		if ((Vbat.mean_value >= (BATTERY_MIN_VOLTAGE + 0.2)) and not battery_condition) {
			data = isoformat(rtc.now(), now_ms, ";") + get_component("Vbat") + String(Vbat.mean_value) + "V;";
			data = isoformat(rtc.now(), now_ms, ";") + get_component("Vbat") + "Battery restored;";
			log_data(data, filename_data);
			battery_condition = true;
		}
		// Power off to protect battery
		if (Vbat.mean_value <= BATTERY_MIN_VOLTAGE) {
			// Security : Open the door if closed
			closeDoor(false, "Security");
			data = isoformat(rtc.now(), now_ms, ";") + get_component("Vbat") + String(Vbat.mean_value) + "V;";
			data = isoformat(rtc.now(), now_ms, ";") + get_component("System") + "Shutdown : Battery Low;";
			log_data(data, filename_data);
			blink(PIN_BUZZER_LED, 100, 6);
			digitalWrite(PIN_PW_EN, LOW);
		}
	}
	
	// Write a periodic message to indicate the system is still alive
	if ((time_last_up.unixtime() + 3600) <= now.unixtime()) {
		data = isoformat(rtc.now(), now_ms, ";") + get_component("System") + "Up;";
		log_data(data, filename_data);
		time_last_up = rtc.now();
	}

	// Check general switch
	switchButtonMgmt();
	delay(config.delay_loop);
}


// Blink function
void blink(uint32_t Pin, int delay_ms, int blink_number) {
	for (uint8_t i = 0; i < blink_number; i++) {
		digitalWrite(Pin, HIGH);
		delay(delay_ms);
		digitalWrite(Pin, LOW);
		delay(delay_ms);
	}
}

// Blink out an error code
void error(int error_number) {
	// Security : Open the door if closed
	closeDoor(false, "Security");
	// External led activation for error-code
	while (1) {
		blink(PIN_BUZZER_LED, 200, error_number);
		for (uint8_t i = error_number; i < 10; i++) {
			delay(200);
		}
	}
}

// Isoformat a date and timestamp without millisecond
String isoformat(DateTime t, int ms, String separator) {
	// '2019-05-10T09:08:53.155'
	// for millisecond attachInterrupt pin on the RTC and use interrupt routine
	return String(t.year()) + "-" + String(t.month()) + "-" + String(t.day()) + separator + String(t.hour()) + ":" + String(t.minute()) + ":" + String(t.second()) + "." + String(ms) + ";";
}

// Isoformat a date without millisecond
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

// Get battery voltage
float get_voltage(uint32_t ulPin) {
	float measuredvbat = analogRead(ulPin);
	measuredvbat *= POWER_BOARD_BATT_RATIO;     // We divided by POWER_BOARD_BATT_RATIO, so multiply back
	measuredvbat *= 3.3;  						// Multiply by 3.3V, our reference voltage
	measuredvbat /= 1023; 						// convert to voltage
	return measuredvbat;
}

// Log data to file
void log_data(String data, const char* filename) {
	digitalWrite(PIN_LED_SD, HIGH);
	File log = SD.open(filename, FILE_WRITE);
	if (!log) {
		Serial.print("Couldnt open ");
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

// Log data according to IR state
void log_data_IR(RTC_DS3231 rtc, bool IR_state, String IR_name, const char* filename) {
	now_ms = millis() % 1000;
	data = isoformat(rtc.now(), now_ms, ";") + get_component(IR_name);
	if (IR_state == 1) {
		data += "broken beam;";
	}
	else {
		data += "beam restored;";
	}
	log_data(data, filename);
}

// Returns the component uid, component type and data type in a comma-delimited string according to data_type value
String get_component(String data_type){
	String data;
	if ((data_type == "System") or (data_type == "Temperature") or (data_type == "Vbat")  or (data_type == "Door")) {
		data = assembly.uid_mainboard + ";inv_mainboard;" + data_type + ";"; 
	}
	else {
		 data = assembly.uid_rfid_sensor + ";inv_rfid_sensor;" + data_type + ";";
	}
	return data;
}

// Create a file each day
void daily_data_file(char* filename, DateTime now) {
	char date[11];
	bool write_header = false;
	// NB Files names are limited to 8 charaters
	strcpy(filename, "01_01_00.TXT"); // beware that filenames cannot exceed 8 or 9 characters
	// Get isoformat date for filename
	isoformat_date(now).toCharArray(date, 11);
	for (int i = 0; i < 8;) {
		filename[i + 0] = date[i];
		i++;
	}
	// Check if the file exists
	if (SD.exists(filename)) {
		Serial.print("The data file already exists\t");
	}
	else {
		Serial.print("Creating the data file\t");
		write_header = true;
	}
	Serial.println(filename);

	File logfile = SD.open(filename, FILE_WRITE);
	if (!logfile) {
		Serial.print("Couldnt create\t");
		Serial.println(filename);
		Serial.println("error 3");
		error(3);
	}
	else {
		Serial.print("The data file was found or created with success\t");
		Serial.println(filename);
		// If it's a new file, write the header
		if (write_header){
			String data = isoformat(now, now_ms, ";") + assembly.uid_mainboard + ";inv_mainboard;inv_experiment;" + assembly.uid_experiment + ";";
			logfile.println(data);
		}
	}
	logfile.close();
}

// Loads the configuration from a file
void loadConfiguration(Config& config) {
	// from https://arduinojson.org/
	File file_c = SD.open(filename_conf);
	// If open with success, read it
	if (file_c) {
		StaticJsonDocument<768> doc;
		DeserializationError error = deserializeJson(doc, file_c);
		if (error) {
			Serial.println(F("Failed to read config file, using default configuration"));
			// If error, use the values already loaded in memory and quit the function
			// Maybe your StaticJsonDocument<SIZE> doc is too small - Check on https://arduinojson.org/v6/assistant/#/step1
		}
		else{
			// Copy values from the JsonDocument to the Config
			config.opt_IR_1 = doc["opt_IR_1"];
			config.opt_IR_2 = doc["opt_IR_2"];
			config.opt_temp_prec = doc["opt_temp_prec"];
			config.mode_time_period = doc["mode_time_period"];
			config.mode_capture = doc["mode_capture"];
			config.start_time = doc["start_time"];
			config.stop_time = doc["stop_time"];
			config.delay_loop = doc["delay_loop"];
			config.tag_type = doc["tag_type"].as<String>();
			config.rfid_attempts = doc["rfid_attempts"];
			config.delay_tag_save = doc["delay_tag_save"];
			config.delay_temp = doc["delay_temp"];
			config.release_time = doc["release_time"];
			config.tag_1 = doc["tag_1"];
			config.tag_2 = doc["tag_2"];
			config.tag_3 = doc["tag_3"];
			config.tag_4 = doc["tag_4"];
			config.tag_5 = doc["tag_5"];
			file_c.close();
		}
	}
	// Else, create a config file with the values already loaded in memory
	else{
		Serial.println(F("Failed to find config file, create file with default configuration"));
		create_config_file();
	}
}

// Save local configuration into file
void load_and_save_local_Configuration() {

	//removing the old file and creating a new one
	SD.remove(filename_conf);
	delay(5);
	create_config_file();

}

// Load assembly description from datafile
void loadAssembly(Assembly& assembly){
	File file_c = SD.open(filename_assembly);
	// If open with success, read it
	if (file_c) {
		StaticJsonDocument<768> doc;
		DeserializationError error = deserializeJson(doc, file_c);
		if (error) {
			Serial.println(F("Failed to read assembly file, using default configuration"));
			// If error, use the values already loaded in memory and quit the function
			// Maybe your StaticJsonDocument<SIZE> doc is too small - Check on https://arduinojson.org/v6/assistant/#/step1
		}
		else{
			// Copy values from the JsonDocument to the Config
			assembly.uid_mainboard 		= doc["uid_mainboard"].as<String>();
			assembly.uid_powerboard 	= doc["uid_powerboard"].as<String>();
			assembly.uid_tectus 		= doc["uid_tectus"].as<String>();
			assembly.uid_rfid_sensor 	= doc["uid_rfid_sensor"].as<String>();
			assembly.uid_software 		= doc["uid_software"].as<String>();
			assembly.uid_experiment 	= doc["uid_experiment"].as<String>();
			file_c.close();
		}
	}
	// Else, create an assembly file with the values already loaded in memory
	else{
		Serial.println(F("Failed to find assembly file, create file with default configuration"));
		create_assembly_file();
	}
}


// Create a configuration file from memory values
void create_config_file() {
	// Try to open the file
	File file = SD.open(filename_conf, FILE_WRITE);
	if (!file) {
		Serial.println(F("Failed to create file"));
	}
	else{
		StaticJsonDocument<768> doc;
		// Set the values in the document
		doc["opt_IR_1"] = config.opt_IR_1;
		doc["opt_IR_2"] = config.opt_IR_2;
		doc["opt_temp_prec"] = config.opt_temp_prec;
		doc["mode_time_period"] = config.mode_time_period;
		doc["mode_capture"] = config.mode_capture;
		doc["start_time"] = config.start_time;
		doc["stop_time"] = config.stop_time;
		doc["delay_loop"] = config.delay_loop;
		doc["tag_type"] = config.tag_type;
		doc["rfid_attempts"] = config.rfid_attempts;
		doc["delay_tag_save"] = config.delay_tag_save;
		doc["delay_temp"] = config.delay_temp;
		doc["release_time"] = config.release_time;
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
}

// Create a assembly file from memory values
void create_assembly_file() {
	// Try to open the file
	File file = SD.open(filename_assembly, FILE_WRITE);
	if (!file) {
		Serial.println(F("Failed to create file"));
	}
	else{
		StaticJsonDocument<768> doc;
		// Set the values in the document
		doc["uid_mainboard"] 	= assembly.uid_mainboard;
		doc["uid_powerboard"] 	= assembly.uid_powerboard;
		doc["uid_tectus"] 		= assembly.uid_tectus;
		doc["uid_rfid_sensor"] 	= assembly.uid_rfid_sensor;
		doc["uid_software"] 	= assembly.uid_software;
		doc["uid_experiment"] 	= assembly.uid_experiment;

		if (serializeJson(doc, file) == 0) {
			Serial.println(F("Failed to write to config file"));
		}
		file.close();
	}
}

// Prints the content of a file to the Serial
void printFile(const char* filename) {
	// from https://arduinojson.org/
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
	// Extract each characters by one by one
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

// Check and print any faults
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

// Compare only 10 last characters of the TAG
bool compare(const char* TAG_1, const char* TAG_2) {
	uint8_t TAG_1_len = strlen(TAG_1);
	uint8_t TAG_2_len = strlen(TAG_2);
	uint8_t nb_digit_to_compare = min(TAG_1_len, TAG_2_len);
	nb_digit_to_compare = min(10, nb_digit_to_compare);
	
	for (int i = 0; i < nb_digit_to_compare; i++) {
		if (!(TAG_1[TAG_1_len - 1 - i] == TAG_2[TAG_2_len - 1 - i])) {
			return false;
		}
	}
	return true;
}

// Convert char hexadecimal to int number
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

// Conversion long long int number to a String
String longlong2String(unsigned long long int bigint) {
  String s = "";
  int digit;

  //for each decimal digit of the long long bigint, extract it and convert it in string
  while (bigint > 0) {
    digit = (bigint % 10);		// get the last decimal digit of bigint
    s = String(digit) + s;		// convert the digit in string and add it to output
    bigint = bigint / 10;		// truncate the last decimal digit

  }

  return s;
}

// Conversion Tag FDX from Hexadecimal to National Identification Code(NIC)
String tag_hex_to_NIC(String src) {
  /*
  input : String of tag hexadecimal number. exemple: [ scr = "80003EB533A9F1FA" ]
  output : String of tag converted with countryCode - CodeNIC [ "250-228500042234" ]
  */
  char hex[12];
  unsigned int a, z = 0;
  unsigned int codePays = 0;        //country code 3 decimals / 10bits [ 250 ]
  unsigned long long int codeNIC = 0;   // National Identificatin Code (NIC) on 12decimals / 38bits [ 228500042234 ]
  if (src.length() == 16)         //FDX tag length is 16 hex [ 0x80003EB533A9F1FA ]
  {
    /*keep only the last 12 hexa digit that contain the Codes [0x3EB533A9F1FA] and convert it to char array*/
    src.toCharArray(hex, 13, 4);

    //Conversion of the 3 first hex digit to get the codePays
    for (int i = 0; i < 3; i++) {
                      // get hex digit [ for i = 2; hexL[i] = 0xB ]
      a = hex2int(hex[i]);      //conversion to int [ a = 11 = 0b1011 ]
      z = (z << 4) | a;       // add the 4 bits of the hexa digit at the end of z
                      // [ z = 62 = 0b 0011 1110  -> becomes -> z = 1003 = 0b 0011 1110 1011]

    }
    codePays = z >> 2;          // supress last 2 bits of z to get code Pays [ codePays = 0b 0011 1110 10 = 250]

    codeNIC = (3 & z);          //get the last 2 bits to begin codeNIC [ codeNIC = 0b 11 ]

    //Conversion of the rest of hex digit to get the codeNIC
    for (size_t i = 3; i < strlen(hex); i++) {
		                  // get hex digit [ for i = 3; hexL[i] = 0x5 ]
      a = hex2int(hex[i]);      //conversion to int [ a = 5 = 0b0101 ]
      codeNIC = (codeNIC << 4) | a; // add the 4 bits of the hexa digit at the end of codeNIC
                      // [ codeNIC = 0b 11  -> becomes -> codeNIC = 0b 11 0101]
    }

    return (String(codePays) + "-" + longlong2String(codeNIC));
  }
  else return "misread tag";

}

// Check switch button
void switchButtonMgmt(void) {
	bool stateButton = digitalRead(PIN_PW_SW);
	int loops = 0;
	while (stateButton == true) {
		loops = loops + 1;
		digitalWrite(PIN_BUZZER_LED, HIGH);
		delay(100);
		digitalWrite(PIN_BUZZER_LED, LOW);
		delay(100);
		stateButton = digitalRead(PIN_PW_SW);
		// It switch button maintained at least 1.2 second, consider it's a shutdown request
		if (loops == 6) {
			digitalWrite(PIN_BUZZER_LED, HIGH);
			data = isoformat(rtc.now(), now_ms, ";") + get_component("System") + "Shutdown : User;";
			log_data(data, filename_data);
			delay(1000);
			digitalWrite(PIN_PW_EN, LOW);
			while (1) {
				delay(10000);
			}
		}
	}
	// If the button is released before shutdown, consider it's a battery check
	if (loops > 0){
		moving_average(&Vbat, get_voltage(PIN_VBAT));
		data = isoformat(rtc.now(), now_ms, ";") + get_component("Vbat") + "Battery check by user;";
		log_data(data, filename_data);
		data = isoformat(rtc.now(), now_ms, ";") + get_component("Vbat") + String(Vbat.mean_value) + "V;";
		log_data(data, filename_data);

		delay(1000); // Delay to let time for user to understand the battery indication is starting
		// Blink 1 time per 20% of battery available + Saturation between 1 and 5 blinks
		uint8_t SOC = 1;
		if (Vbat.mean_value >= BATTERY_MAX_VOLTAGE) {
			SOC = 5;
		}
		else if (Vbat.mean_value > BATTERY_MIN_VOLTAGE) {
			SOC = (Vbat.mean_value - BATTERY_MIN_VOLTAGE) / (BATTERY_MAX_VOLTAGE - BATTERY_MIN_VOLTAGE) * 5;
		}		
		blink(PIN_BUZZER_LED, 200, SOC + 1);
	}
}

#ifdef THERMISTOR_SECURITY
float readThermistorTemperature() {
	float temp_reading;
	float steinhart;
	temp_reading = analogRead(THERMISTORPIN);
	// convert the value to resistance
	temp_reading = (1023 / temp_reading) - 1;     // (1023/ADC - 1)
	temp_reading = SERIESRESISTOR / temp_reading;  // 10K / (1023/ADC - 1)
	// convert the resistance to temperature in Celcus
	steinhart = temp_reading / THERMISTORNOMINAL;     // (R/Ro)
	steinhart = log(steinhart);                  // ln(R/Ro)
	steinhart /= BCOEFFICIENT;                   // 1/B * ln(R/Ro)
	steinhart += 1.0 / (TEMPERATURENOMINAL + 273.15); // + (1/To)
	steinhart = 1.0 / steinhart;                 // Invert
	steinhart -= 273.15;                         // convert absolute temp to C
	return steinhart;
}
#endif


// Management of the door
void closeDoor(bool door_cmd_closed, String reason){
#ifndef THERMISTOR_SECURITY
	if (door_cmd_closed != door_already_closed) {
		digitalWrite(PIN_PW_SERVO, HIGH);
		// Close the door
		if (door_cmd_closed) {
			Servo_control.write(SERVO_POS_CLOSED);
			time_last_door_closed = rtc.now();
			door_already_closed = true;
			Serial.println("Door closed");
			data = isoformat(rtc.now(), now_ms, ";") + get_component("Door") + "Closed;";
			log_data(data, filename_data);
		}
		// Else open the door
		else {
			Servo_control.write(SERVO_POS_OPENED);
			door_already_closed = false;
			Serial.println("Door opened");
			data = isoformat(rtc.now(), now_ms, ";") + get_component("Door") + "Open : " + reason + ";";
			log_data(data, filename_data);
		}
		delay(2000);
		digitalWrite(PIN_PW_SERVO, LOW);
	}
#endif
}

// Average value filtering
bool moving_average(struct Average_value *s, float updated_value) {

	// Update values
	s->array[2] = s->array[1];
	s->array[1] = s->array[0];
	s->array[0] = updated_value;

    // Calculation of the new moving average
    s->mean_value = (s->array[0] + s->array[1] + s->array[2]) / 3.0;
	
    // Comparison with the previous moving average and threshold
    if (fabs(s->mean_value - s->mean_value_previous) <= s->threshold) {
        return false;
    } else {
        // Update the values in the structure
        s->mean_value_previous = s->mean_value;
        return true;
    }
}