/*
An example to log sdsInstance data using LoRa interface
Uses 1 native serial, 2 software serial, need to improve fetching data from sdsInstance
XXX
Don't forget to patch Print.h and sdsInstance library
*/


#include <Arduino.h>
#include <SoftwareSerial.h>
#include <string.h>
#include <stdlib.h>

#include "NovaSDS011.h"
#include "LoraModules.h"

// Pin definition
#define LORA_RX D1
#define LORA_TX D2

#define SDS_TX D3
#define SDS_RX D4

#define MODE "DEBUG"  

EspSoftwareSerial::UART LoraSerial;

static char recv_buf[512];
static bool doesLoraExist   = false;
static bool hasLoraJoined = false;
static int led = 0;
static long loraConnectedSeconds = 0;
float p10, p25;
int sdsErr;
NovaSDS011 sdsInstance;
/*
Logging module
*/
void DEBUG(const char *output){
if (!(strcmp(MODE, "DEBUG")))
…}

/*
Function to send AT Commands and get response
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
  if (at_send_check_response("+AT: OK", 100, "AT\r\n")) {
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

void joinLoraNetwork(){
  char stringBuffer[100];
  // at_send_check_response returns 0 if successfully joined
  int joinStatus = !(at_send_check_response("+JOIN: Network joined", 12000, "AT+JOIN\r\n"));
  if (joinStatus){
    hasLoraJoined = true;
    loraConnectedSeconds = (long) millis() / 1000;
    DEBUG("LoRa joined network successfully.");  
  } else {
    hasLoraJoined = false;
    DEBUG("LoRa could not join the network.");
    itoa(joinStatus, stringBuffer, 10);
    DEBUG("Join return status: ");
    DEBUG(stringBuffer);
  }
    


}


void setup() {
  sdsInstance.begin(SDS_RX, SDS_TX);
  delay(5000)  ;
  Serial.begin(9600);
  LoraSerial.begin(9600, SWSERIAL_8N1, LORA_RX, LORA_TX, false); 

  //testDataReportingMode();
  //testDataWorkingMode();
  //testDataDutyCycle();
  //testSetDeviceID(0xAAAA);

  if (sdsInstance.setWorkingMode(WorkingMode::work))
  {
    Serial.println("sdsInstance working mode \"Work\"");
  }
  else
  {
    Serial.println("FAIL: Unable to set working mode \"Work\"");
  }

  sdsInstance.begin(SDS_RX, SDS_TX);
  SDS011Version version = sdsInstance.getVersionDate();

  if (version.valid)
  {
    Serial.println("sdsInstance Firmware Vesion:\nYear: " + String(version.year) + "\nMonth: " +
                   String(version.month) + "\nDay: " + String(version.day));
  }
  else
  {
    Serial.println("FAIL: Unable to obtain Software Version");
  }

  if (sdsInstance.setDutyCycle(0))
  {
    Serial.println("sdsInstance Duty Cycle set to 5min");
  }
  else
  {
    Serial.println("FAIL: Unable to set Duty Cycle");
  }


  DEBUG("Booting LoRa by probing pins (D1, D2).");
  bootLora();
  delay(1000);
  joinLoraNetwork();
  
}

void loop() {

  float p25, p10;
  char cmd[256];
  String cmdString = "";

  // Send message from LORA if SDS value was read correctly
  if (sdsInstance.queryData(p25, p10) == QuerryError::no_error)
  {
    Serial.println(String(millis() / 1000) + "s:PM2.5=" + String(p25) + ", PM10=" + String(p10));
  
  /*
    // Try connecting to LoRa if LoRa is found, and LoRa has not joined and TODO: loraConnectedSeconds > 1 hour (rejoin logic)
    if ((doesLoraExist) && (!hasLoraJoined))
      
    else
      DEBUG("Do nothing");
    */

    cmdString = String("AT+CMSG=")+ String(p25) +"A"+  String(p10) +String("\r\n");
    cmdString.toCharArray(cmd, sizeof(cmd));

    int ret = at_send_check_response("Done", 10000, cmd);
    // sleep for 150 seconds
    delay(150000);

    }
    
}

/*
For quick reference
ID: DevAddr, 42:XX:XX:XX
ID: DevEui, 2C:F7:F1:20:42:00:1E:AD
ID: AppEui, 80:00:00:00:00:00:00:06
*/
