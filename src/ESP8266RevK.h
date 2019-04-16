/* RevK app framework
 * Your app needs some functions that this calls
 */

#ifndef ESP8266RevK_H
#define ESP8266RevK_H

#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// Functions expected in the app
void app_mqtt(const char *prefix, const char*suffix, const byte *message, size_t len); // Called for incoming messages
void app_setting(const char *setting,const char *value);	// Called for settings from EEPROM

class ESP8266RevK : private PubSubClient {
 public:
   ESP8266RevK(const char*myappname="RevK");
   void loop(void);
   void pub(const char *prefix, const char *suffix, const char *fmt=NULL, ...);
   void start_setting();	// Store settings in EEPROM
   void add_setting(const char *name,const char*value);
   void end_setting();

   char hostname[33];	// Unique ID of host (default is chipid)
   char wifissid[33];	// WiFi SSID
   char wifipass[33];	// WiFi Password
   char mqtthost[129];	// MQTT hostname/IP
   char mqttuser[33];	// MQTT usernmame
   char mqttpass[33];	// MQTT password

 private:
   WiFiClient mqttclient;
   PubSubClient mqtt;
   unsigned long mqttping = 0;
   unsigned long mqttretry = 0;
   unsigned long mqttbackoff = 100;
   unsigned short mqttport = 1883;
};

#endif
