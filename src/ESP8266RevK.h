// RevK platform
// This is a set of functions used in a number of projects by me, and a few friends
// It sets up WiFi, and ensures reconnect
// It sets ip MQTT, and ensures reconnect
// It provides a framework for publishing MQTT messages in a formal (similar to Tasmota)
// It allows commands to be accepted by MQTT and calls the app with them
// It manages EEPROM settings for itself and the app
//

#ifndef ESP8266RevK_H
#define ESP8266RevK_H

#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// Functions expected in the app (return true if OK)
boolean app_cmnd(const char*suffix, const byte *message, size_t len); // Called for incoming commands not already handled
boolean app_setting(const char *setting,const char *value);	// Called for settings from EEPROM

class ESP8266RevK : private PubSubClient {
 public:
   ESP8266RevK(const char*myappname="RevK");
   void loop(void);	// Call in loop
   void stat(const char *suffix, const char *fmt=NULL, ...); // Publish stat
   void tele(const char *suffix, const char *fmt=NULL, ...); // Publish tele
   void pub(const char *prefix, const char *suffix, const char *fmt=NULL, ...);	// Publish general
   void setting(const char *name,const char*value); // Apply a setting (gets written to EEPROM)
   void ota();	// Do upgrade

 private:
};

#endif
