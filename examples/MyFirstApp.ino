// Example application

#include <ESP8266RevK.h>

// Set the OTA host as you require, the rest is set based on filename and date/time
ESP8266RevK revk(__FILE__, __DATE__ " " __TIME__,"ota.revk.uk");

boolean app_setting(const char *setting, const byte *value, size_t len)
{ // Called for settings retrieved from EEPROM, return true if setting is OK
  return false; // Failed
}

boolean app_cmnd(const char*suffix, const byte *message, size_t len)
{ // Called for incoming MQTT messages, return true if message is OK
  return false; // Failed
}

void setup()
{ // Your set up here as usual

}

void loop()
{
  revk.loop();
  // Your loop her as usual
}
