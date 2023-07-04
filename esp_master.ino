#include <FS.h>
#include <WiFiClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>  
#include <ArduinoJson.h>
#include <SoftwareSerial.h>
#include <Wire.h>

#define ESP_SDA D2
#define ESP_SCL D1

#define LORA_RX D6
#define LORA_TX D7

#define MODE "debug"

//the pins A4 and A5 are the SDA and SCL lines
int query = 0;
EspSoftwareSerial::UART LoraSerial;


/*
--------------------------wifimanager starts here ----------------------------------------------
*/
// WiFiManager Global instance  
WiFiManager wifiManager;

// Set web server port number to 80
WiFiServer server(80);

// Variable to store the HTTP request
String header;

// Assign output variables to GPIO pins
char LoRa_key[32];

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;

    Serial.println("saving config");
    DynamicJsonDocument json(1024);
    json["LoRa_key"] = LoRa_key;
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
------------------------------------------------------------------------
*/

/*
---------------------------------------------------------------------
                    LoRa methods
---------------------------------------------------------------------
*/
// Initialize Lora associated variables
static char recv_buf[512];
static bool doesLoraExist = false;
static bool hasLoraJoined = false;
static long loraConnectedSeconds = 0;

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
    at_send_check_response("+LOG: DEBUG", 1000, "AT+LOG=DEBUG");

    doesLoraExist = true;
    delay(1000);
    Serial.println("LoRa found..");
  } else {
    doesLoraExist = false;
        delay(1000);

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



// functions
//Logging module
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
  Wire.begin(ESP_SDA, ESP_SCL); // join i2c bus (address optional for master)
  Serial.begin(9600);
  while(!Serial){
    delay(1);
  }

  // Setup LoRa serial
  LoraSerial.begin(9600, SWSERIAL_8N1, LORA_RX, LORA_TX, false);
  bootLora();
  delay(1000);
  joinLoraNetwork();

  /*
  ---------------------------------wifi manager setup------------------------------
   */  
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
        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read
  
  WiFiManagerParameter custom_field; // global param ( for non blocking w params )

  std::vector<const char *> menu = {"wifi","info","param","sep","restart","exit"};
  wifiManager.setMenu(menu);
  // set dark theme
  wifiManager.setClass("invert");
  
  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  
  // test custom html(radio)
  const char* custom_radio_str = "<br/><label for='customfieldid'>App Key for LoRa</label><br/><input type='radio' name='customfieldid' value='1' checked> One<br><input type='radio' name='customfieldid' value='2'> Two<br><input type='radio' name='customfieldid' value='3'> Three";
  

  new (&custom_field) WiFiManagerParameter(custom_radio_str); // custom html input
  
  wifiManager.addParameter(&custom_field);
  wifiManager.setSaveParamsCallback(saveConfigCallback);
  
  // fetches ssid and pass from eeprom and tries to connect
  // if it does not connect it starts an access point with the specified name
  // here  "AutoConnectAP"
  // and goes into a blocking loop awaiting configuration
  wifiManager.autoConnect("AutoConnectAP");
  
  // if you get here you have connected to the WiFi
  Serial.println("Connected.");
  /*
  ---------------------------------end wifi manager---------------------------
  */
  debug("server started");
  server.begin();
  delay(6500);
}

void loop() {
  /*
  ---------------------------taking data from slave-----------------------------------
  */
  Wire.beginTransmission(8); // transmit to device #8
  Wire.write("Master value :  ");         // sends these bytes
  Wire.write(query);              // sends one byte
  Wire.endTransmission();    // stop transmitting

  unsigned long int times = 0;
  float value[8] = {0,0,0,0,0,0,0,0};

  Wire.requestFrom(8, 8*sizeof(float));    // request 1 byte from slave device #8
  while (Wire.available()) { // slave may send less than requested
    Serial.println();
    for(int i = 0; i < 8; i++){

      byte* byteValue = (byte*)&value[i];
      for (int j = 0; j < sizeof(value[i]); j++) {
        byteValue[j] = Wire.read();
      }

      Serial.print("Slave value ," + String(i) + ", : ");
      Serial.println(value[i]);         // print the character
    }
    break;
  }

  /*
  --------------------------trying to send payload via LoRa-------------------------------
  */

  if ((!(value[4])) && (!(value[5])) && (!(value[0])) && (!(value[1]))){
    debug("cannot send payload because values are zero ... ");
  }
  else{
    if(doesLoraExist){
      char cmd[256];
      String cmdString = "";
      cmdString = String("AT+CMSG=")+ String(value[4]) + "A" + String(value[5]) + "A" + String(value[0]) + "A" + String(value[1]) + "A"  +String("\r\n");
      cmdString.toCharArray(cmd, sizeof(cmd));
      int ret = at_send_check_response("Done", 30000, cmd);
      debug("ret = " + String(ret));
      delay(25000);
    }

    /*
    ------------------------------trying to send payload via Wifi--------------------------------
    */
    else if(WiFi.status() == WL_CONNECTED){
      WiFiClient client1;
      HTTPClient http;
      String serverName = "http://user.ackl.io:12001/module/";
      http.begin(client1, serverName);
      http.addHeader("Content-Type", "application/json");
      String request_payload = "{\"api_key\" : \"OBAMA_ROCK$\",\r\n\"sensordatavalues\": [\r\n{\r\n\"value\": \""+ String(value[4]) +"\",\r\n\"value_type\": \"P2\"\r\n},\r\n{\r\n\"value\": \""+ String(value[5]) +"\",\r\n\"value_type\": \"P1\"\r\n},\r\n{\r\n\"value\":\""+ String(value[0]) +"\",\r\n\"value_type\": \"temperature\"\r\n},\r\n{\r\n\"value\":\""+ String(value[1]) +"\",\r\n\"value_type\": \"humidity\"\r\n}\r\n],\r\n\"software_version\": \"test-imt-v0.1\"}";
      int httpResponseCode = http.POST(request_payload);
      if(httpResponseCode != 200){
        debug("Payload not sent!!!");
      }
      String xyz = "HTTP response"+ String(httpResponseCode);
      debug(request_payload);
      http.end();
      delay(25000);
    }

    /*
    ------------------both WiFi and LoRa are not working-------------------------------
    */

    else{
      debug("Cannot send payload both LoRa and Wifi are not connected!!!!");
    }
  }
  /*
  -----------------------------sending values on the webpage-----------------------
  */

  WiFiClient client = server.available();   // Listen for incoming clients
  if (client) {                             // If a new client connects,
    debug("New Client.");          // print a message out in the serial port
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
            client.println(".sensor { color:white; font-weight: bold; background-color: #bcbcbc; padding: 1px; }</style></head><body>");

            // Web Page Heading  
            // SDS011 data printing
            // looking for the sensor and taking data
            if(true){
                client.println("<h1>ESP8266 with SDS011</h1>");
                client.println("<table><tr><th>MEASUREMENT</th><th>VALUE</th></tr>");
                client.println("<tr><td>PM2.5</td><td><span class=\"sensor\">");
                client.println(value[4]);
                client.println("</span></td></tr>");  
                client.println("<tr><td>PM10</td><td><span class=\"sensor\">");
                client.println(value[5]);
                client.println("</span></td></tr></table>"); 
              
            }
            // BME280 or SGP30 data printing 
            if(true){
              client.println("<h1>ESP8266 with BME280</h1>");
              client.println("<table><tr><th>MEASUREMENT</th><th>VALUE</th></tr>");
              client.println("<tr><td>Temp. Celsius</td><td><span class=\"sensor\">");
              client.println(value[0]);
              client.println(" *C</span></td></tr>");  
              client.println("<tr><td>Temp. Fahrenheit</td><td><span class=\"sensor\">");
              client.println(1.8 * value[0] + 32);
              client.println(" *F</span></td></tr>");       
              client.println("<tr><td>Pressure</td><td><span class=\"sensor\">");
              client.println(value[2] / 100.0F);
              client.println(" hPa</span></td></tr>");
              client.println("<tr><td>Approx. Altitude</td><td><span class=\"sensor\">");
              client.println(value[3]);
              client.println(" m</span></td></tr>"); 
              client.println("<tr><td>Humidity</td><td><span class=\"sensor\">");
              client.println(value[1]);
              client.println(" %</span></td></tr></table>"); 
              }
            // checking for SGP30 sensor
            if(true){
              client.println("<h1>ESP8266 with SGP30</h1>");
              client.println("<table><tr><th>MEASUREMENT</th><th>VALUE</th></tr>");
              client.println("<tr><td>TOVC</td><td><span class=\"sensor\">");
              client.println(value[6]);
              client.println(" ppb</span></td></tr>");  
              client.println("<tr><td>eCO2</td><td><span class=\"sensor\">");
              client.println(value[7]);
              client.println(" ppm</span></td></tr></table>");  
            }
            client.println("</body></html>");               
            // The HTTP response ` with another blank line
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
    debug("Client disconnected.");
    debug("");
  }
  delay(5000);
}

