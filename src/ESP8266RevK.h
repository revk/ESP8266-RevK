/* RevK app framework
*/

#ifndef ESP8266RevK_H
#define ESP8266RevK_H

#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

class ESP8266RevK : private PubSubClient {
 public:
   ESP8266RevK(const char*appname="RevK", void (*rawcallback)(char*, uint8_t*, unsigned int)=NULL,void (*callback)(const char *prefix, const char*suffix, const byte *message, size_t len) = NULL);
   // Your rawcallback can just call message to do the callback and handle upgrades
   void message(const char* topic, byte* payload, unsigned int len);
   void loop(void);
   void pub(const char *prefix, const char *suffix, const char *fmt, ...);
   void (*callback)(const char *prefix, const char*suffix, const byte *message, size_t len);

   char appname[33];	// Name of app and part of topic
   char hostname[33];	// Unique name of host and part of hostname and topic with appname
   char wifissid[33];	// WiFi SSID
   char wifipass[33];	// WiFi Password
   char mqtthost[129];	// MQTT hostname/IP
   char mqttuser[33];	// MQTT usernmame
   char mqttpass[33];	// MQTT password
   char firmware[129];	// OTA Firmware Hostname

 private:
   WiFiClient mqttclient;
   PubSubClient mqtt;
   unsigned long mqttping = 0;
   unsigned long mqttretry = 0;
   unsigned long mqttbackoff = 100;
   unsigned short mqttport = 1883;

   void upgrade(const byte *message, size_t len);
};

#endif
