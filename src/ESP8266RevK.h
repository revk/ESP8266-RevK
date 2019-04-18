// RevK platform
// This is a set of functions used in a number of projects by me, and a few friends
// It sets up WiFi, and ensures reconnect
// It sets ip MQTT, and ensures reconnect
// It provides a framework for publishing MQTT messages in a formal (similar to Tasmota)
// It allows commands to be accepted by MQTT and calls the app with them
// It manages EEPROM settings for itself and the app
//
// MQTT topic style is :-
// prefix/app/hostname/suffix
// Prefix can be set, and pre-defined are cmnd, setting, stat, tele, error
// Use setting to set a setting (including prefix[whatever])
// Predefined settings are
//
// hostname	The hostname
// otahost	OTA hostname
// wifissid	WiFi SSID
// wifipass	WiFi Password
// mqtthost	MQTT hostname
// mqttuser	MQTT username
// mqttpass	MQTT password
// mqttport	MQTT port number
// prefix[xx]	The prefixes, e.g. prefixcmnd
//
// Use cmnd to send commands
// Predefined commands are :-
// upgrade	Do OTA upgrade
// restart	Do a restart
//

#ifndef ESP8266RevK_H
#define ESP8266RevK_H

#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// Functions expected in the app (return true if OK)
boolean app_cmnd(const char*suffix, const byte *message, size_t len); // Called for incoming commands not already handled
boolean app_setting(const char *setting,const byte *value,size_t len);	// Called for settings from EEPROM (value is always null terminated at [len])

class ESP8266RevK : private PubSubClient {
 public:
   ESP8266RevK(const char*myappname="RevK");
   // Functions return true of "OK"
   boolean loop(void);	// Call in loop
   boolean stat(const char *suffix, const char *fmt=NULL, ...); // Publish stat
   boolean tele(const char *suffix, const char *fmt=NULL, ...); // Publish tele
   boolean error(const char *suffix, const char *fmt=NULL, ...); // Publish error
   boolean pub(const char *prefix, const char *suffix, const char *fmt=NULL, ...);	// Publish general
   boolean setting(const char *name,const char*value=NULL); // Apply a setting (gets written to EEPROM)
   boolean setting(const char *name,const byte*value=NULL,size_t len=0); // Apply a setting (gets written to EEPROM)
   boolean ota();	// Do upgrade

 private:
};

#endif
