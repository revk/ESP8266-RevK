// Example application

#include <ESPRevK.h>

#define app_settings  \
  s(mysetting1);   \
  s(mysetting2);   \

#define s(n) const char *n=NULL
  app_settings
#undef s

  // *** Set your own OTA server, SSID, password, and MQTT server here

  ESPRevK revk(__FILE__, __DATE__ " " __TIME__, "ota.revk.uk", "mqtt.revk.uk"); // also SSID, password can be added

  const char* app_setting(const char *tag, const byte *value, size_t len)
  { // Called for settings retrieved from EEPROM, return PSTR for tag if setting is OK
#define s(n) do{const char *t=PSTR(#n);if(!strcmp_P(tag,t)){n=(const char *)value;return t;}}while(0)
    app_settings
#undef s
    return NULL; // Failed
  }

  boolean app_command(const char*tag, const byte *message, size_t len)
  { // Called for incoming MQTT messages, return true if message is OK
    return false; // Failed
  }

  void setup()
  { // Your set up here as usual
	Serial.begin(115200);
	Serial.printf_P(PSTR("Setting 1 is %s\n"),mysetting1?:"[unset]");
	Serial.printf_P(PSTR("Setting 2 is %s\n"),mysetting2?:"[unset]");
  }

  void loop()
  {
    revk.loop();
    // Your loop her as usual
  }
