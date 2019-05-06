// Minimal app that loads real app - used to save space

#include <ESP8266RevK.h>

// *** Set your own OTA server, SSID, password, and MQTT server here

// NULL app name stops it resetting settings
ESP8266RevK revk(NULL, __DATE__ " " __TIME__, "ota.revk.uk", "mqtt.revk.uk"); // Also SSID and password can be added

const char* app_setting(const char *tag, const byte *value, size_t len)
{ // Called for settings retrieved from EEPROM, return PSTR for tag if setting is OK
  return NULL; // Failed
}

boolean app_command(const char*tag, const byte *message, size_t len)
{ // Called for incoming MQTT messages, return true if message is OK
  return false; // Failed
}

void setup()
{
}

void loop()
{
  if(revk.loop() || millis()>60000) revk.ota(); // Load real app
}
