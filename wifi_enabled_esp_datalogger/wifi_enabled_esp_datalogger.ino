#include <FS.h>  //this needs to be first, or it all crashes and burns...
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>  // https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h>
#include <Wire.h>
#include "Adafruit_BME280.h"
#include "Adafruit_SGP30.h"
#include "Adafruit_Sensor.h"
#include "NovaSDS011.h"

#define MODE "debug"
#define SEALEVELPRESSURE_HPA (1013.25)

#define SDS_TX D5
#define SDS_RX D6

#define LORA_RX D8
#define LORA_TX D7

#define BMSG_SDA D2
#define BMSG_SCL D1

#define DELIMITER ","

NovaSDS011 sdsInstance;
Adafruit_SGP30 sgp;
Adafruit_BME280 bme;
EspSoftwareSerial::UART LoraSerial;

// address of the digital sensors
const uint8_t bme280_addr1 = 118;
const uint8_t bme280_addr2 = 119;
const uint8_t sgp30_addr = 88;

// Initialize Lora associated variables
static char recv_buf[512];
static bool doesLoraExist = false;
static bool hasLoraJoined = false;
static long loraConnectedSeconds = 0;

// static variable used as flags
static bool doesSDSExist = false;
static bool hasSDSInitialized = false;

// Replace with your network credentials
const char *ssid = "Galaxy M119718";
const char *password = "qgol1210";

// temoprary variable
uint8_t error, address;

// sensor discovered
int sensorDiscovered = -1;

/*
---------------------------------------------------------------------
                    LoRa methods
---------------------------------------------------------------------
*/
/*
Function to send AT Commands and get response
Returns 0 if p_ack string was successfully returned by the LoRa module in response to the p_cmd (AT Command)
*/
static int at_send_check_response(const char *p_ack, int timeout_ms, const char *p_cmd, ...) {
  int ch;
  int num = 0;
  int index = 0;
  int startMillis = 0;
  va_list args;

  memset(recv_buf, 0, sizeof(recv_buf));
  va_start(args, p_cmd);
  LoraSerial.printf(p_cmd, args);
  Serial.printf(p_cmd, args);
  va_end(args);
  delay(200);
  startMillis = millis();

  if (p_ack == NULL) {
    DEBUG("Didn't get any acknowledgement from LoRa");
    return -1;
  }

  do {
    while (LoraSerial.available() > 0) {
      ch = LoraSerial.read();
      recv_buf[index++] = ch;
      Serial.print((char)ch);
      delay(2);
    }

    if (strstr(recv_buf, p_ack) != NULL)
      return 1;

  } while (millis() - startMillis < timeout_ms);

  if (startMillis >= timeout_ms) {
    DEBUG("Response timeout from LoRa");
    return -2;
  }

  DEBUG("Strange error from LoRa");
  return 0;
}

/*
Method to recieve Lora phrase and print it to Serial
*/
static void recv_prase(char *p_msg) {
  if (p_msg == NULL)
    return;

  char *p_start = NULL;
  int data = 0;
  int rssi = 0;
  int snr = 0;

  p_start = strstr(p_msg, "RX");
  if (p_start && (1 == sscanf(p_start, "RX: \"%d\"\r\n", &data))) {
    Serial.print("Recevied Here: ");
    Serial.println(data);
  }

  p_start = strstr(p_msg, "RSSI");
  if (p_start && (1 == sscanf(p_start, "RSSI %d,", &rssi))) {
    Serial.print("RSSI ");
    Serial.println(rssi);
  }
  p_start = strstr(p_msg, "SNR");
  if (p_start && (1 == sscanf(p_start, "SNR %d", &snr))) {
    Serial.print("SNR ");
    Serial.println(snr);
  }
}


/*
Tries probing if LoRa exists
*/
void bootLora() {
  if (at_send_check_response("+AT: OK", 100, "AT\r\n") >= 0) {
    at_send_check_response("+ID: AppEui", 1000, "AT+ID\r\n");
    at_send_check_response("+MODE: LWOTAA", 1000, "AT+MODE=LWOTAA\r\n");
    at_send_check_response("+DR: EU868", 1000, "AT+DR=EU868\r\n");
    at_send_check_response("+CH: NUM", 1000, "AT+CH=NUM,0-2\r\n");
    at_send_check_response("+KEY: APPKEY", 1000, "AT+KEY=APPKEY,\"2B7E151628AED2A6ABF7158809CF4F3C\"\r\n");
    at_send_check_response("+CLASS: C", 1000, "AT+CLASS=A\r\n");
    at_send_check_response("+PORT: 8", 1000, "AT+PORT=8\r\n");
    // Change this to "QUIET" in Production
    at_send_check_response("+LOG: DEBUG", 1000, "AT+LOG=DEBUG");
    // Check the Max length of the message allowed
    //at_send_check_response("+LEN:", 1000, "AT+LW=LEN");


    doesLoraExist = true;
    delay(200);
    Serial.println("LoRa found..");
  } else {
    doesLoraExist = false;
    Serial.println("LoRa not found!");
  }
}

void joinLoraNetwork() {
  char stringBuffer[100];
  // at_send_check_response returns 1 if successfully joined
  int joinStatus = (at_send_check_response("+JOIN:", 12000, "AT+JOIN\r\n"));
  if (joinStatus >= 0) {
    hasLoraJoined = true;
    loraConnectedSeconds = (long)millis() / 1000;
    DEBUG("LoRa joined network successfully.");
  } else {
    hasLoraJoined = false;
    DEBUG("LoRa could not join the network.");
    itoa(joinStatus, stringBuffer, 10);
    DEBUG("Join return status: ");
    DEBUG(stringBuffer);
  }
}



/*
--------------------------wifimanager starts here ----------------------------------------------
*/
// WiFiManager Global instance
WiFiManager wifiManager;

// Set web server port number to 80
WiFiServer server(80);

// Variable to store the HTTP request
String header;

// Auxiliar variables to store the current output state
String outputState = "off";

// Assign output variables to GPIO pins
char output[2] = "5";

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback() {
  Serial.println("Should save config");
  shouldSaveConfig = true;

  Serial.println("saving config");
  DynamicJsonDocument json(1024);
  json["output"] = output;
  json["customFieldID"] = getParam("customfieldid");
  Serial.print("CustomfieldID: ");
  Serial.println(getParam("customfieldid"));

  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println("failed to open config file for writing");
  }

  serializeJson(json, Serial);
  serializeJson(json, configFile);
  configFile.close();
  //end save
}

//read parameter from server, for customhmtl input
String getParam(String name) {
  String value;
  if (wifiManager.server->hasArg(name)) {
    value = wifiManager.server->arg(name);
  }
  return value;
}
/*
------------------------------------------------------------------------
*/
/*
Logging module
*/
void DEBUG(const char *output){
if (!(strcmp(MODE, "DEBUG")))
  Serial.println(output);
}

void DEBUG(String output){
if (!(strcmp(MODE, "DEBUG")))
  Serial.println(output);
}

void ERR(const char *output){
  Serial.println(output);
}


void setup() {
  Serial.begin(115200);

  // Setup LoRa serial
  LoraSerial.begin(9600, SWSERIAL_8N1, LORA_RX, LORA_TX, false);
  bootLora();
  delay(1000);
  joinLoraNetwork();

  /*
  ---------------------------------wifi manager setup------------------------------
   */
  //clean FS, for testing
  //SPIFFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);

        // DynamicJsonBuffer jsonBuffer;
        DynamicJsonDocument json(1024);
        DeserializationError error = deserializeJson(json, buf.get());

        // JsonObject& json = jsonBuffer.parseObject(buf.get());
        //json.printTo(Serial);
        serializeJson(json, Serial);
        if (!error) {
          Serial.println("\nparsed json");
          strcpy(output, json["output"]);
        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read

  WiFiManagerParameter custom_output("output", "output", output, 2);

  WiFiManagerParameter custom_field;  // global param ( for non blocking w params )

  std::vector<const char *> menu = { "wifi", "info", "param", "sep", "restart", "exit" };
  wifiManager.setMenu(menu);
  // set dark theme
  wifiManager.setClass("invert");

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  // set custom ip for portal
  //wifiManager.setAPConfig(IPAddress(10,0,1,1), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

  //add all your parameters here
  wifiManager.addParameter(&custom_output);
  strcpy(output, custom_output.getValue());

  // Label to get App Key for LoRa
  WiFiManagerParameter loraAppKeyInputBox("", "Enter your string here", "default string", 50);


  // test custom html(radio)
  const char *custom_radio_str = "<br/><label for='customfieldid'>App Key for LoRa</label><br/><input type='radio' name='customfieldid' value='1' checked> One<br><input type='radio' name='customfieldid' value='2'> Two<br><input type='radio' name='customfieldid' value='3'> Three";

  new (&custom_field) WiFiManagerParameter(custom_radio_str);  // custom html input

  wifiManager.addParameter(&custom_field);
  wifiManager.setSaveParamsCallback(saveConfigCallback);

  // Uncomment and run it once, if you want to erase all the stored information
  //wifiManager.resetSettings();

  //set minimu quality of signal so it ignores AP's under that quality
  //defaults to 8%
  //wifiManager.setMinimumSignalQuality();

  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  //wifiManager.setTimeout(120);

  // fetches ssid and pass from eeprom and tries to connect
  // if it does not connect it starts an access point with the specified name
  // here  "AutoConnectAP"
  // and goes into a blocking loop awaiting configuration
  wifiManager.autoConnect("AutoConnectAP");
  // or use this for auto generated name ESP + ChipID
  //wifiManager.autoConnect();

  // if you get here you have connected to the WiFi
  Serial.println("Connected.");



  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonDocument json(1024);
    json["output"] = output;
    json["customFieldID"] = getParam("customfieldid");
    Serial.print("CustomfieldID: ");
    Serial.println(getParam("customfieldid"));

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    serializeJson(json, Serial);
    serializeJson(json, configFile);
    configFile.close();
    //end save
  }

  /*
  --------------------looking for the BME and SGP sensor----------------------------
  */
  Wire.begin(BMSG_SDA, BMSG_SCL);
  int devices = 0;
  DEBUG("Scanning...");
  for (address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("Device found at address 0x");
      if (address < 16)
        Serial.print("0");
      Serial.print(address, HEX);
      devices++;
      break;
    }
  }

  if (devices != 0) {
    if (bme280_addr1 == address || bme280_addr2 == address) {
      DEBUG("BME280 Barometer sensor connected");
      // initializing the BME280 sensor
      if (!bme.begin(0x76)) {  // BME280 sensor address
        DEBUG("Could not find a valid BME280 sensor, check wiring!");
        while (1)
          ;
      }
    } else if (sgp30_addr == address) {
      DEBUG("SGP30 VOC and eCO2 gas sensor connected");
      Serial.print("SGP30 serial #");
      if (!sgp.begin()) {
        DEBUG("Sensor not found :(");
        while (1)
          ;
      }
      DEBUG("Found");

      // Initialize the sensor
      if (!sgp.IAQinit()) {
        DEBUG("Failed to initialize sensor!");
        while (1)
          ;
      }

      // Wait for the sensor to stabilize
      DEBUG("SGP30 is warming up for 60 seconds...");
      delay(60000);
      DEBUG("Initialization complete!");
    } else {
      DEBUG("Unknown device connected!!!");
    }
  } else {
    DEBUG("No device connected!!!");
  }


  /*
--------------------------------connecting the SDS module------------------------
*/

  Serial.println("going in SDS011");
  sdsInstance.begin(SDS_RX, SDS_TX);
  Serial.println("going in SDS011, BEGIN");

  // SDS Probe
  SDS011Version version = sdsInstance.getVersionDate();
  String versionString = "sdsInstance Firmware Vesion:\nYear: " + String(version.year) + "\nMonth: " + String(version.month) + "\nDay: " + String(version.day);

  Serial.println(versionString);
  if (version.valid) {
    Serial.println("going in SDS011, IN VERSION");

    doesSDSExist = true;
    String versionString = "sdsInstance Firmware Vesion:\nYear: " + String(version.year) + "\nMonth: " + String(version.month) + "\nDay: " + String(version.day);
    Serial.println(versionString);
    Serial.println("going in SDS011, OUT VERSION");
  }

  // Configure only if SDS Exists
  if (doesSDSExist) {
    if (sdsInstance.setWorkingMode(WorkingMode::work)) {
      Serial.println("going in SDS011, IN WORKING");

      DEBUG("sdsInstance working mode \"Work\"");
      hasSDSInitialized = true;
    } else {
      Serial.println("going in SDS011, NO WOKRING");

      DEBUG("Couldn't set working mode");
    }

    // a duty cycle of 0 implies sds will work continuously
    if (sdsInstance.setDutyCycle(0)) {
      Serial.println("going in SDS011, SET DUTY");

      hasSDSInitialized = hasSDSInitialized && true;
      Serial.println("sdsInstance Duty Cycle set to 5min");
    } else {
      Serial.println("going in SDS011, no DUTY");

      DEBUG("Couldn't set duty cycle on SDS");
    }
  }
  Serial.println("going in SDS011, DELAY START");

  delay(5000);
  server.begin();
}

void loop() {
  // SDS011 data printing
  float p25, p10;
  char cmd[256];
  String cmdString = "";

  WiFiClient client = server.available();  // Listen for incoming clients

  if (client) {                   // If a new client connects,
    DEBUG("New Client.");         // print a message out in the serial port
    String currentLine = "";      // make a String to hold incoming data from the client
    while (client.connected()) {  // loop while the client's connected
      if (client.available()) {   // if there's bytes to read from the client,
        char c = client.read();   // read a byte, then
        Serial.write(c);          // print it out the serial monitor
        header += c;
        if (c == '\n') {  // if the byte is a newline character
          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {
            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            // and a content-type so the client knows what's coming, then a blank line:
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println("Connection: close");
            client.println();

            // Display the HTML web page
            client.println("<!DOCTYPE html><html>");
            client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
            client.println("<link rel=\"icon\" href=\"data:,\">");
            // CSS to style the table
            client.println("<style>body { text-align: center; font-family: \"Trebuchet MS\", Arial;}");
            client.println("table { border-collapse: collapse; width:35%; margin-left:auto; margin-right:auto; }");
            client.println("th { padding: 12px; background-color: #0043af; color: white; }");
            client.println("tr { border: 1px solid #ddd; padding: 12px; }");
            client.println("tr:hover { background-color: #bcbcbc; }");
            client.println("td { border: none; padding: 12px; }");
            client.println(".sensor { color:white; font-weight: bold; background-color: #bcbcbc; padding: 1px; }</style></head>");

            // Web Page Heading
            if (sdsInstance.queryData(p25, p10) == QuerryError::no_error) {
              DEBUG("in SDS ");
              client.println("<body><h1>ESP8266 with SDS011</h1>");
              client.println("<table><tr><th>MEASUREMENT</th><th>VALUE</th></tr>");
              client.println("<tr><td>PM2.5</td><td><span class=\"sensor\">");
              client.println(p25);
              client.println("</span></td></tr>");
              client.println("<tr><td>PM10</td><td><span class=\"sensor\">");
              client.println(p10);
              client.println("</span></td></tr></table>");

              if ((doesLoraExist) && (!hasLoraJoined)){
                cmdString = String("AT+CMSG=")+ String(p25) +"A"+  String(p10) +String("\r\n");
                cmdString.toCharArray(cmd, sizeof(cmd));
                int ret = at_send_check_response("Done", 10000, cmd);
            }
              else
                DEBUG("Do nothing");

            }
            // BME280 or SGP30 data printing
            if (bme280_addr1 == address || bme280_addr2 == address) {
              client.println("<h1>ESP8266 with BME280</h1>");
              client.println("<table><tr><th>MEASUREMENT</th><th>VALUE</th></tr>");
              client.println("<tr><td>Temp. Celsius</td><td><span class=\"sensor\">");
              client.println(bme.readTemperature());
              client.println(" *C</span></td></tr>");
              client.println("<tr><td>Temp. Fahrenheit</td><td><span class=\"sensor\">");
              client.println(1.8 * bme.readTemperature() + 32);
              client.println(" *F</span></td></tr>");
              client.println("<tr><td>Pressure</td><td><span class=\"sensor\">");
              client.println(bme.readPressure() / 100.0F);
              client.println(" hPa</span></td></tr>");
              client.println("<tr><td>Approx. Altitude</td><td><span class=\"sensor\">");
              client.println(bme.readAltitude(SEALEVELPRESSURE_HPA));
              client.println(" m</span></td></tr>");
              client.println("<tr><td>Humidity</td><td><span class=\"sensor\">");
              client.println(bme.readHumidity());
              client.println(" %</span></td></tr></table>");
              client.println("</body></html>");
            } else if (!sgp30_addr == address) {
              // taking the measure form the sensor
              if (!sgp.IAQmeasure()) {
                DEBUG("Failed to measure air quality!");
                return;
              }
              // Retrieve the TVOC and eCO2 values
              uint16_t TVOC = sgp.TVOC;
              uint16_t eCO2 = sgp.eCO2;

              client.println("<h1>ESP8266 with SGP30</h1>");
              client.println("<table><tr><th>MEASUREMENT</th><th>VALUE</th></tr>");
              client.println("<tr><td>TOVC</td><td><span class=\"sensor\">");
              client.println(TVOC);
              client.println(" ppb</span></td></tr>");
              client.println("<tr><td>eCO2</td><td><span class=\"sensor\">");
              client.println(eCO2);
              client.println(" ppm</span></td></tr></table>");
              client.println("</body></html>");
            }

            // The HTTP response ` with another blank line
            client.println();
            // Break out of the while loop
            break;
          } else {  // if you got a newline, then clear currentLine
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }
      }
    }
    // Clear the header variable
    header = "";
    // Close the connection
    client.stop();
    DEBUG("Client disconnected.");
    DEBUG("");
  }
}
