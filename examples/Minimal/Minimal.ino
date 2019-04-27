// Minimal app that loads real app - used to save space

#include <ESP8266RevK.h>

// NULL app name stops it resetting settings
// If you build, set your own OTA server
ESP8266RevK revk(__FILE__, __DATE__ " " __TIME__, "ota.revk.uk", "IoT", "insecure", "mqtt.revk.uk");

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
 revk.ota(); // Load real app
}

void loop()
{
  revk.loop();
}
