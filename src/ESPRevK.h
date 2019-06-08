// RevK platform

//#define REVKDEBUG	Serial              // If defined, does serial debug at 74880

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

#ifdef REVKDEBUG
#define debugf(fmt,...) do{REVKDEBUG.printf_P(PSTR(fmt "\n"),__VA_ARGS__);REVKDEBUG.flush();}while(0)
#define debug(str) do{REVKDEBUG.printf(PSTR("%S\n"),PSTR(str));REVKDEBUG.flush();}while(0)
#else
#define debugf(...) do{}while(0)
#define debug(...) do{}while(0)
#endif

#ifndef ESPRevK_H
#define ESPRevK_H

#define revk_settings   \
s(hostname);            \
s(otahost);             \
f(otasha1,20);          \
n(wifireset,300);	\
s(wifissid);            \
f(wifibssid,6);         \
n(wifichan,0);          \
s(wifipass);            \
s(wifissid2);           \
f(wifibssid2,6);        \
n(wifichan2,0);         \
s(wifipass2);           \
s(wifissid3);           \
f(wifibssid3,6);        \
n(wifichan3,0);         \
s(wifipass3);           \
n(mqttreset,0);		\
s(mqtthost);            \
s(mqtthost2);           \
f(mqttsha1,20);         \
s(mqttuser);            \
s(mqttpass);            \
s(mqttport);            \
s(ntphost);             \
s(prefixcommand);       \
s(prefixsetting);       \
s(prefixstate);         \
s(prefixevent);         \
s(prefixinfo);          \
s(prefixerror);         \

#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// Functions expected in the app (return true if OK)
boolean app_command(const char*tag, const byte *message, size_t len); // Called for incoming commands not already handled
const char * app_setting(const char *tag,const byte *value,size_t len);	// Called for settings from EEPROM 
// value is NULL, or malloc'd with NULL added and not freed until next app setting with same tag
// Return is PROGMEM pointer to the setting name if setting is accepted, or NULL if not accepted

class ESPRevK 
{
 public:
   ESPRevK(const char*myappname="RevK",const char *myappversion=NULL,
		   const char *myotahost=NULL,
		   const char *mymqtthost=NULL,
		   const char *mywifissid=NULL,
		   const char *mywifipass=NULL);
   ESPRevK(const char*myappname="RevK",const __FlashStringHelper *myappversion=NULL,
		   const char *myotahost=NULL,
		   const char *mymqtthost=NULL,
		   const char *mywifissid=NULL,
		   const char *mywifipass=NULL);
   void  clientTLS(WiFiClientSecure&,const byte *sha1=NULL);	// Secure TLS client (LE cert if no sha1)
   // Functions return true of "OK"
   boolean loop(void);	// Call in loop, returns false if wifi not connected
   boolean state(const __FlashStringHelper *tag, const __FlashStringHelper *fmt=NULL, ...); // Publish stat
   boolean state(const char *tag, const __FlashStringHelper *fmt=NULL, ...); // Publish stat
   boolean state(const __FlashStringHelper *tag, unsigned int len, const byte *data);
   boolean event(const __FlashStringHelper *tag, const __FlashStringHelper *fmt=NULL, ...); // Publish tele
   boolean event(const char *tag, const __FlashStringHelper *fmt=NULL, ...); // Publish tele
   boolean event(const __FlashStringHelper *tag, unsigned int len, const byte *data);
   boolean error(const __FlashStringHelper *tag, const __FlashStringHelper *fmt=NULL, ...); // Publish error
   boolean error(const char *tag, const __FlashStringHelper *fmt=NULL, ...); // Publish error
   boolean error(const __FlashStringHelper *tag, unsigned int len, const byte *data);
   boolean info(const __FlashStringHelper *tag, const __FlashStringHelper *fmt=NULL, ...); // Publish error
   boolean info(const char *tag, const __FlashStringHelper *fmt=NULL, ...); // Publish error
   boolean info(const __FlashStringHelper *tag, unsigned int len, const byte *data);
   boolean pub (const char * prefix, const char * suffix, const __FlashStringHelper * fmt, ...); // Publish general
   boolean pub(const __FlashStringHelper *prefix, const __FlashStringHelper *tag, const __FlashStringHelper *fmt=NULL, ...);	// Publish general
   boolean pub(boolean retain, const __FlashStringHelper *prefix, const __FlashStringHelper *tag,  const __FlashStringHelper *fmt=NULL, ...);	// Publish general (with retain)
   boolean pub(boolean retain, const char *prefix, const char *tag,  const __FlashStringHelper *fmt=NULL, ...);	// Publish general (with retain)
   boolean setting(const __FlashStringHelper *tag,const char*value); // Apply a setting (gets written to EEPROM)
   boolean setting(const __FlashStringHelper *tag,const byte*value=NULL,size_t len=0); // Apply a setting (gets written to EEPROM)
   boolean ota(int delay=0);	// Do upgrade
   boolean restart(int delay=0);	// Save settings and restart
   void sleep(unsigned long s);	// Got to sleep
   void mqttclose(const __FlashStringHelper *reason=NULL); // Close (will typically reopen on next loop)
   boolean mqttopen(boolean silent=false); // Re-open MQTT

#define s(n) const char *get_##n(); // return setting
#define f(n,b) const byte *get_##n(); // return setting
#define n(n,d) int get_##n(); // return setting
   revk_settings
#undef s
#undef n
#undef f

   boolean wificonnected=false;
   boolean mqttconnected=false;
   const char *chipid;
   char appver[20];
 private:
};

#endif
