/*********
  Project --WEED--
  Author: Manoj Gudi & Purushottam 
  Link:
  

  Modifying base project of Rui Santos
*********/

#include <FS.h> //this needs to be first, or it all crashes and burns...
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          // https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h>          // https://github.com/bblanchon/ArduinoJson

#include <Arduino.h>
#include <SoftwareSerial.h>
#include <string.h>
#include <stdlib.h>

#include "LoraModules.h"
#include "NovaSDS011.h"

// Pin definition
#define LORA_RX D1
#define LORA_TX D2

#define SDS_TX D3
#define SDS_RX D4

#define MODE "debug"  


// Static bool variables used as flags
static char recv_buf[512];
static bool doesLoraExist   = false;
static bool hasLoraJoined = false;
static bool doesSDSExist = false;
static bool hasSDSInitialized = false;

// Static variables
static int led = 0;
static long loraConnectedSeconds = 0;
float p10, p25;
int error;


EspSoftwareSerial::UART LoraSerial;

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
void saveConfigCallback () {
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
String getParam(String name){
  String value;
  if(wifiManager.server->hasArg(name)) {
    value = wifiManager.server->arg(name);
  }
  return value;
}

/*
Logging module
*/
void debug(const char *output){
if (!(strcmp(MODE, "debug")))
  Serial.println(output);
}

/*
Function to send AT Commands and get response
*/
static int atSendCheckResponse(const char *p_ack, int timeout_ms, const char *p_cmd, ...) {
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
    return 0;
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

  if (startMillis >= timeout_ms){
    Serial.println("TIMEOUT");
    return 2;
  }
  
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
    led == !!data;
    if (led) {
      digitalWrite(LED_BUILTIN, LOW);
    } else {
      digitalWrite(LED_BUILTIN, HIGH);
    }
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
void bootLora(){
  if (atSendCheckResponse("+AT: OK", 100, "AT\r\n")) {
    atSendCheckResponse("+ID: AppEui", 1000, "AT+ID\r\n");
    atSendCheckResponse("+MODE: LWOTAA", 1000, "AT+MODE=LWOTAA\r\n");
    atSendCheckResponse("+DR: EU868", 1000, "AT+DR=EU868\r\n");
    atSendCheckResponse("+CH: NUM", 1000, "AT+CH=NUM,0-2\r\n");
    atSendCheckResponse("+KEY: APPKEY", 1000, "AT+KEY=APPKEY,\"2B7E151628AED2A6ABF7158809CF4F3C\"\r\n");
    atSendCheckResponse("+CLASS: C", 1000, "AT+CLASS=A\r\n");
    atSendCheckResponse("+PORT: 8", 1000, "AT+PORT=8\r\n");
    doesLoraExist = true;
    delay(200);
    Serial.println("LoRa found..");
  } else {
    doesLoraExist = false;    
    Serial.println("LoRa not found!");
  }
}

void joinLoraNetwork(){
  char stringBuffer[100];
  // atSendCheckResponse returns 0 if successfully joined
  int joinStatus = !(atSendCheckResponse("+JOIN: Network joined", 12000, "AT+JOIN\r\n"));
  if (joinStatus){
    hasLoraJoined = true;
    loraConnectedSeconds = (long) millis() / 1000;
    debug("LoRa joined network successfully.");  
  } else {
    hasLoraJoined = false;
    debug("LoRa could not join the network.");
    itoa(joinStatus, stringBuffer, 10);
    debug("Join return status: ");
    debug(stringBuffer);
  }
}


void setup() {
  Serial.begin(115200);
  
  sdsInstance.begin(SDS_RX, SDS_TX);
  delay(5000)  ;

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

  WiFiManagerParameter custom_field; // global param ( for non blocking w params )

  std::vector<const char *> menu = {"wifi","info","param","sep","restart","exit"};
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
  const char* custom_radio_str = "<br/><label for='customfieldid'>App Key for LoRa</label><br/><input type='radio' name='customfieldid' value='1' checked> One<br><input type='radio' name='customfieldid' value='2'> Two<br><input type='radio' name='customfieldid' value='3'> Three";
  
  new (&custom_field) WiFiManagerParameter(custom_radio_str); // custom html input
  
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

  // Initialize the output variables as outputs
  pinMode(atoi(output), OUTPUT);
  // Set outputs to LOW
  digitalWrite(atoi(output), LOW);;
  
  // LoRa Probe
  LoraSerial.begin(9600, SWSERIAL_8N1, LORA_RX, LORA_TX, false);
  Serial.print("Booting LoRa by probing pins (D1, D2).");
  bootLora();

  // SDS Probe
  SDS011Version version = sdsInstance.getVersionDate();  
  if (version.valid){
    doesSDSExist = true;
    String versionString = "sdsInstance Firmware Vesion:\nYear: " + String(version.year) + "\nMonth: " +
                   String(version.month) + "\nDay: " + String(version.day);
    debug(versionString);
  }

  // Configure only if SDS Exists
  if (doesSDSExist){
    if (sdsInstance.setWorkingMode(WorkingMode::work)) {
      debug("sdsInstance working mode \"Work\"");
      hasSDSInitialized = true;
    } else {
      debug("Couldn't set working mode");
    }

    // a duty cycle of 0 implies sds will work continuously
    if (sdsInstance.setDutyCycle(0)){
      hasSDSInitialized = hasSDSInitialized && true;
      Serial.println("sdsInstance Duty Cycle set to 5min");
    } else {
      debug("Couldn't set duty cycle on SDS");
    }
  }    

  server.begin();
}

void loop(){
  WiFiClient client = server.available();   // Listen for incoming clients

  if (client) {                             // If a new client connects,
    Serial.println("New Client.");          // print a message out in the serial port
    String currentLine = "";                // make a String to hold incoming data from the client
    while (client.connected()) {            // loop while the client's connected
      if (client.available()) {             // if there's bytes to read from the client,
        char c = client.read();             // read a byte, then
        Serial.write(c);                    // print it out the serial monitor
        header += c;
        if (c == '\n') {                    // if the byte is a newline character
          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {
            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            // and a content-type so the client knows what's coming, then a blank line:
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println("Connection: close");
            client.println();
            
            // turns the GPIOs on and off
            if (header.indexOf("GET /output/on") >= 0) {
              Serial.println("Output on");
              outputState = "on";
              digitalWrite(atoi(output), HIGH);
            } else if (header.indexOf("GET /output/off") >= 0) {
              Serial.println("Output off");
              outputState = "off";
              digitalWrite(atoi(output), LOW);
            }
            
            // Display the HTML web page
            client.println("<!DOCTYPE html><html>");
            client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
            client.println("<link rel=\"icon\" href=\"data:,\">");
            // CSS to style the on/off buttons 
            // Feel free to change the background-color and font-size attributes to fit your preferences
            client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
            client.println(".button { background-color: #195B6A; border: none; color: white; padding: 16px 40px;");
            client.println("text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}");
            client.println(".button2 {background-color: #77878A;}</style></head>");
            
            // Web Page Heading
            client.println("<body><h1>ESP8266 Web Server</h1>");
            
            // Display current state, and ON/OFF buttons for the defined GPIO  
            client.println("<p>Output - State " + outputState + "</p>");
            // If the outputState is off, it displays the ON button       
            if (outputState=="off") {
              client.println("<p><a href=\"/output/on\"><button class=\"button\">ON</button></a></p>");
            } else {
              client.println("<p><a href=\"/output/off\"><button class=\"button button2\">OFF</button></a></p>");
            }                  
            client.println("</body></html>");
            
            // The HTTP response ends with another blank line
            client.println();
            // Break out of the while loop
            break;
          } else { // if you got a newline, then clear currentLine
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
    Serial.println("Client disconnected.");
    Serial.println("");
  }
}
