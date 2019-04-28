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
// hostname	The hostname	(default is chip ID)
// otahost	OTA hostname	(always TLS using Let's Encrypt)
// wifissid	WiFi SSID	(default for set up is IoT)
// wifipass	WiFi Password	(default for set up is security)
// mqtthost	MQTT hostname	(assumed to be local and so non TLS)
// mqttuser	MQTT username	(default is empty)
// mqttpass	MQTT password	(default is empty)
// mqttport	MQTT port number (default is 1883)
// prefix[xx]	The prefixes, e.g. prefixcmnd
//
// Note that wifissid2, and wifissid3 (and wifipass2/wifipass3) can be defined.
// If any are defined then WiFiMulti is used which only tries non-hidden SSIDs
// To use with hidden SSID, *only* set wifissid/wifipass
//
// Use cmnd to send commands
// Predefined commands are :-
// upgrade	Do OTA upgrade from otahost via HTTPS
// restart	Do a restart (saving settings first)
//

//#define REVKDEBUG               // If defined, does serial debug at 74880

#ifndef ESP8266RevK_H
#define ESP8266RevK_H

#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// Functions expected in the app (return true if OK)
boolean app_command(const char*tag, const byte *message, size_t len); // Called for incoming commands not already handled
const char * app_setting(const char *tag,const byte *value,size_t len);	// Called for settings from EEPROM 
// value is NULL, or malloc'd with NULL added and not freed until next app setting with same tag
// Return is PROGMEM pointer to the setting name if setting is accepted, or NULL if not accepted

class ESP8266RevK : private PubSubClient {
 public:
   ESP8266RevK(const char*myappname="RevK",const char *myappversion=NULL,
		   const char *myotahost=NULL,
		   const char *mywifissid=NULL,
		   const char *mywifipass=NULL,
		   const char *mymqtthost=NULL);
   void  clientTLS(WiFiClientSecure&,const byte *sha1=NULL);	// Secure TLS client (LE cert if no sha1)
   // Functions return true of "OK"
   boolean loop(void);	// Call in loop, returns false if wifi not connected
   boolean state(const __FlashStringHelper *tag, const __FlashStringHelper *fmt=NULL, ...); // Publish stat
   boolean event(const __FlashStringHelper *tag, const __FlashStringHelper *fmt=NULL, ...); // Publish tele
   boolean error(const __FlashStringHelper *tag, const __FlashStringHelper *fmt=NULL, ...); // Publish error
   boolean info(const __FlashStringHelper *tag, const __FlashStringHelper *fmt=NULL, ...); // Publish error
   boolean pub (const char * prefix, const char * suffix, const __FlashStringHelper * fmt, ...); // Publish general
   boolean pub(const __FlashStringHelper *prefix, const __FlashStringHelper *tag, const __FlashStringHelper *fmt=NULL, ...);	// Publish general
   boolean pub(boolean retain, const __FlashStringHelper *prefix, const __FlashStringHelper *tag,  const __FlashStringHelper *fmt=NULL, ...);	// Publish general (with retain)
   boolean pub(boolean retain, const char *prefix, const char *tag,  const __FlashStringHelper *fmt=NULL, ...);	// Publish general (with retain)
   boolean setting(const __FlashStringHelper *tag,const char*value=NULL); // Apply a setting (gets written to EEPROM)
   boolean setting(const __FlashStringHelper *tag,const byte*value=NULL,size_t len=0); // Apply a setting (gets written to EEPROM)
   boolean ota(int delay=0);	// Do upgrade
   boolean restart(int delay=0);	// Save settings and restart

   boolean wificonnected=false;
   boolean mqttconnected=false;
 private:
};

#endif
