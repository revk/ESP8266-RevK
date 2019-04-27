// Example application

#include <ESP8266RevK.h>

#define settings  \
  s(mysetting1);   \
  s(mysetting2);   \

#define s(n) const char *n=NULL
  settings
#undef s

  // Set the OTA host as you require, the rest is set based on filename and date/time
  ESP8266RevK revk(__FILE__, __DATE__ " " __TIME__, "ota.revk.uk");

  const char* app_setting(const char *tag, const byte *value, size_t len)
  { // Called for settings retrieved from EEPROM, return PSTR for tag if setting is OK
#define s(n) do{const char *t=PSTR(#n);if(!strcmp_P(tag,t)){n=(const char *)value;return t;}}while(0)
    settings
#undef s
    return NULL; // Failed
  }

  boolean app_command(const char*tag, const byte *message, size_t len)
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
