/*
An example to log SDS011 data using LoRa interface
Uses 1 native serial, 2 software serial, need to improve fetching data from SDS011
XXX
Don't forget to patch Print.h and SDS011 library
*/

#include "SDS011.h"
#include <Arduino.h>
#include <SoftwareSerial.h>

SoftwareSerial Serial1(10, 11); // RX, TX

static char recv_buf[512];
static bool is_exist = false;
static bool is_join = false;
static int led = 0;
float p10, p25;
int error;
SDS011 my_sds;

static int at_send_check_response(char *p_ack, int timeout_ms, char *p_cmd, ...){
  int ch;
  int num = 0;
  int index = 0;
  int startMillis = 0;
  va_list args;

  memset(recv_buf, 0, sizeof(recv_buf));
  va_start(args, p_cmd);
  Serial1.printf(p_cmd, args);
  Serial.printf(p_cmd, args);

  va_end(args);
  startMillis = millis();


  if (p_ack == NULL){
    return 0;
  }

  do {
    while(Serial1.available() > 0){               
    ch = Serial1.read();
    recv_buf[index++] = ch;
    Serial.print((char) ch);
    delay(2);
  }

  if (strstr(recv_buf, p_ack) != NULL)
    return 1;

  } while(millis() - startMillis < timeout_ms);
  
  return 0;
}

static void recv_prase(char *p_msg){
  if (p_msg == NULL)
    return;

      
char *p_start = NULL;
int data = 0;
int rssi = 0;
int snr = 0;

p_start = strstr(p_msg, "RX");
if (p_start && (1 == sscanf(p_start, "RX: \"%d\"\r\n", &data))){
  Serial.println(data);
  led == !!data;
  if (led)  {
    digitalWrite(LED_BUILTIN, LOW);
  } else {
    digitalWrite(LED_BUILTIN, HIGH);
  }
    
  }
}


void setup() {
  Serial.begin(9600);
  Serial1.begin(9600);

pinMode(LED_BUILTIN, OUTPUT);
digitalWrite(LED_BUILTIN, HIGH);

Serial.print("E5 LORAWAN TEST\r\n");

if (at_send_check_response("+AT: OK", 100, "AT\r\n")){
        Serial.print("Sent AT commands");
        at_send_check_response("+ID: AppEui", 1000, "AT+ID\r\n");
        at_send_check_response("+MODE: LWOTAA", 1000, "AT+MODE=LWOTAA\r\n");
        at_send_check_response("+DR: EU868", 1000, "AT+DR=EU868\r\n");
        at_send_check_response("+CH: NUM", 1000, "AT+CH=NUM,0-2\r\n");
        at_send_check_response("+KEY: APPKEY", 1000, "AT+KEY=APPKEY,\"2B7E151628AED2A6ABF7158809CF4F3C\"\r\n");
        at_send_check_response("+CLASS: C", 1000, "AT+CLASS=A\r\n");
        at_send_check_response("+PORT: 8", 1000, "AT+PORT=8\r\n");
        is_exist = true;
        is_join = true;
        delay(200);
} else {
  Serial.print("No E5 module found");
  is_exist = false;
}

}

void loop() {

  // Magic of the universe lies in 42
  Serial.println("Start reading from SDS");
  // Can we remove the ugly delays? TODO
  my_sds.begin(6, 7);
  delay(1000);
  error = my_sds.read(&p25, &p10);
  
  // SDS needs to be shutdown for LoRa to function properly
  my_sds.end();
  delay(1000);

  if (!error) {
    Serial.println("P2.5: " + String(p25));
    Serial.println("P10:  " + String(p10));
      
  }else{
    Serial.print("Error ");
    Serial.println(error);
  }

  if (is_exist){
    int ret = 0;
    if (is_join){    
      ret = at_send_check_response("+JOIN: Network joined", 12000, "AT+JOIN\r\n");    
      if (ret) {
        is_join = false;      
      } else {
        at_send_check_response("+ID: AppEui", 1000, "AT+ID\r\n");
        delay(5000);
      }
    } else {
      char cmd[128];
      sprintf(cmd, "AT+CMSGHEX=\"%04X\"\r\n", (int)temp);
      ret = at_send_check_response("Done", 5000, cmd);
      if (ret){
        recv_prase(recv_buf);
      } else{
        Serial.print("Send failed!\r\n\r\n");
      }
        delay(5000);    
    } 
  } else {
    delay(1000);
  }

  
}

/*
For quick reference
ID: DevAddr, 42:XX:XX:XX
ID: DevEui, 2C:F7:F1:20:42:00:1E:AD
ID: AppEui, 80:00:00:00:00:00:00:06
*/
