#include "SDS011.h"
#include <Arduino.h>
#include <SoftwareSerial.h>
#include "LoRaCredentials.h"

SoftwareSerial Serial1(10, 11);  // RX, TX

static char recv_buf[512];
static bool is_exist = false;
static bool is_join = false;
static int led = 0;
float p10, p25;
int error;

static int at_send_check_response(char *p_ack, int timeout_ms, char *p_cmd, ...) {
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
  delay(200);
  startMillis = millis();

  if (p_ack == NULL) {
    return 0;
  }

  do {
    while (Serial1.available() > 0) {
      ch = Serial1.read();
      recv_buf[index++] = ch;
      Serial.print((char)ch);
      delay(2);
    }

    if (strstr(recv_buf, p_ack) != NULL)
      return 1;

  } while (millis() - startMillis < timeout_ms);

  if (startMillis >= timeout_ms)
    Serial.println("TIMEOUT");

  return 0;
}

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




void setup() {
  Serial.begin(9600);
  while (!Serial) {
    ;  // wait for serial port to connect. Needed for native USB port only
  }
  Serial1.begin(9600);

  Serial.print("E5 LORAWAN TEST\r\n");

  if (at_send_check_response("+AT: OK", 100, "AT\r\n")) {
    Serial.print("Sent AT commands");
    at_send_check_response("+ID: AppEui", 1000, "AT+ID\r\n");
    at_send_check_response("+MODE: LWOTAA", 1000, "AT+MODE=LWOTAA\r\n");
    at_send_check_response("+DR: EU868", 1000, "AT+DR=EU868\r\n");
    at_send_check_response("+KEY: APPKEY", 1000, "AT+KEY=APPKEY,\"2B7E151628AED2A6ABF7158809CF4F3C\"\r\n");
    at_send_check_response("+CLASS: C", 1000, "AT+CLASS=A\r\n");
    at_send_check_response("+PORT: 8", 1000, "AT+PORT=1\r\n");

    is_exist = true;
    is_join = true;
    delay(1000);
  } else {
    Serial.print("No E5 module found");
    is_exist = false;
  }

  at_send_check_response("+AT JOIN", 12000, "AT+JOIN=FORCE\r\n");
  Serial.println("Start the loop");
}

void loop() {
  if (Serial.available()) {
    Serial1.write(Serial.read());
  }
  // run over and over
  if (Serial1.available()) {
    Serial.write(Serial1.read());
  }
  
}
