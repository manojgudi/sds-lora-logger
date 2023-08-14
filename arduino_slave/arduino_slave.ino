#include <Wire.h>
#include "Adafruit_BME280.h"
#include "Adafruit_SGP30.h"
#include "Adafruit_Sensor.h"
#include "NovaSDS011.h"

#define MODE "debug"  
#define SEALEVELPRESSURE_HPA (1013.25)

#define SDS_TX 5
#define SDS_RX 6

NovaSDS011 sdsInstance;
Adafruit_SGP30 sgp;
Adafruit_BME280 bme;

// address of the digital sensors
const uint8_t bme280_addr1 = 118;
const uint8_t bme280_addr2 = 119;
const uint8_t sgp30_addr = 88;

// static variable used as flags
static bool doesSDSExist = false;
static bool hasSDSInitialized = false;
static bool doesBMEexist = false;
static bool doesSGPexist = false;

// variable to store readings from the sensor
float sensor_input[8];

// functions
// Logging module
void debug(const char* output){
  if (!(strcmp(MODE, "debug"))){
    Serial.println(output);
  }  
}

void debug(String output){
  if (!(strcmp(MODE, "debug"))){
    Serial.println(output);
  }  
}

void setup() {
  Serial.begin(9600);
  while(!Serial){
    delay(1);
  }
  Wire.begin(8);                // join i2c bus with address #8
  Wire.onRequest(requestEvent); // register event
  Wire.onReceive(receiveEvent); // register event
  /*
  -------------------------------BME280----------------------------------
  */
  if (!bme.begin(0x76)) {  // BME280 sensor address
    debug("Could not find a valid BME280 sensor, check wiring!");
  }
  else{
    doesBMEexist = true;
    debug("BME280 connected...");
  }
  /*
  ------------------------------SDS011-----------------------------------
  */
  sdsInstance.begin(SDS_RX, SDS_TX);
  // SDS Probe
  SDS011Version version = sdsInstance.getVersionDate();
  if (version.valid){
    doesSDSExist = true;
    String versionString = "sdsInstance Firmware Vesion:\nYear: " + String(version.year) + "\nMonth: " + String(version.month) + "\nDay: " + String(version.day);
    debug(versionString);
  }

  // Configure only if SDS Exists
  if (doesSDSExist){
    if (sdsInstance.setWorkingMode(WorkingMode::work)) {
      debug("sdsInstance working mode \"Work\"");
      hasSDSInitialized = true;
    }
    else {
      debug("Couldn't set working mode");
    }
    // a duty cycle of 0 implies sds will work continuously
    if (sdsInstance.setDutyCycle(0)){
      hasSDSInitialized = hasSDSInitialized && true;
      Serial.println("sdsInstance Duty Cycle set to 5min");
    } else {
      debug("Couldn't set duty cycle on SDS");
    }
    delay(5000);
  }
  /*
  -------------------------------SPG58---------------------------------
  */
  if (!sgp.begin()) {
    debug("SGP30 Sensor not found :("); 
  }
  else{
    debug("SGP30 sensor connected ... ");
    doesSGPexist = true;
    debug("Found");
    // Initialize the sensor
    if (!sgp.IAQinit()) {
      debug("Failed to initialize sensor!");
      while (1);
    }
    // Wait for the sensor to stabilize
    debug("SGP30 is warming up for 60 seconds...");
    delay(60000);
    debug("Initialization complete!");
  }
}

void loop() {
  /*
  ------------------------Reading BME280------------------------------
  */
  if(doesBMEexist == true){
    sensor_input[0] = bme.readTemperature();
    sensor_input[1] = bme.readHumidity();
    sensor_input[2] = bme.readPressure();
    sensor_input[3] = bme.readAltitude(SEALEVELPRESSURE_HPA);
    debug("\nBME280 values :");
    String BME280value = "Temperature : " + String(sensor_input[0]) + " *C, Humidity : " + String(sensor_input[1]) + " % \nPressure : " + String(sensor_input[2] / 100.0F) + " hPa, and Approx. Altitude : " + String(sensor_input[3]) + " m.";
    debug(BME280value);
  }
  /*
  ---------------------------Reading SDS011-------------------------
  */
  if(doesSDSExist == true && hasSDSInitialized == true){
    if (sdsInstance.queryData(sensor_input[4], sensor_input[5]) == QuerryError::no_error){
      debug("\nSDS011 values :");
      String SDS011value = "PM2.5 : " + String(sensor_input[4]) + " PM10 : " + String(sensor_input[5]);
      debug(SDS011value);
    }
  }
  /*
  --------------------------Reading SGP30--------------------------------
  */
  if(doesSGPexist == true){
    if (!sgp.IAQmeasure()) {
      debug("Failed to measure air quality!");
      return;
    }
    //reading values
    sensor_input[6] = sgp.TVOC; 
    sensor_input[7] = sgp.eCO2;
    debug("\nSPG30 values :");
    String SGP30value = "TVOC : " + String(sensor_input[6]) + " ppb, and eCO2 : " + String(sensor_input[7]) + " ppm.";
    debug(SGP30value);
  }
  delay(5000);
}

// function that executes whenever data is received from master
void receiveEvent() {
  while (Wire.available()) { // loop through all but the last
    char c = Wire.read();       // receive byte as a character
    Serial.print(c);         // print the character
  }
}

// function that executes whenever data is requested by master
void requestEvent() {
  for (int i = 0; i < 8; i++){
    byte* byteValue = (byte*)&sensor_input[i];
    Wire.write(byteValue, sizeof(sensor_input[i])); 
  }
}
