// Minimal app that loads real app - used to save space

#include <ESP8266RevK.h>

// NULL app name stops it resetting settings
// If you build, set your own OTA server
ESP8266RevK revk(NULL, __DATE__ " " __TIME__,"ota.revk.uk");

boolean app_setting(const char *setting, const byte *value, size_t len)
{ // Called for settings retrieved from EEPROM, return true if setting is OK
  return false; // Failed
}

boolean app_cmnd(const char*suffix, const byte *message, size_t len)
{ // Called for incoming MQTT messages, return true if message is OK
  return false; // Failed
}

void setup()
{
 revk.upgrade(); // Load real app
}

void loop()
{
  revk.loop();
}
