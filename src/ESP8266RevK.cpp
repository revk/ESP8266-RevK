// RevK applicatioon framework
// See include file for more details
//
// TODO :-
// Fallback SSID
// Fallback MQTT

//#define REVKDEBUG               // If defined, does serial debug at 74880

#ifdef REVKDEBUG
#define debug(...) Serial.printf(__VA_ARGS__)
#else
#define debug(...) do{}while(0)
#endif

#ifdef ARDUINO_ESP8266_NODEMCU
#define BOARD "nodemcu"
#else
#define BOARD "generic"
#endif

#include <ESP8266RevK.h>
#include <ESP8266httpUpdate.h>
#include <EEPROM.h>

// Local functions
static WiFiClientSecure myleclient ();
static boolean pub (const char *prefix, const char *suffix, const char *fmt, ...);
boolean savesettings ();
boolean applysetting (const char *name, const byte * value, size_t len);

// App name set by constructor, expceted to be static string
static const char *appname = "RevK";    // System set from constructor as literal string
static const char *appversion = NULL;   //  System set from constructor as literal string
static int appnamelen = 4;      // May be truncated
static boolean do_restart = false;      // Do a restart in main loop cleanly
static boolean do_upgrade = false;       // Do an OTA upgrade

// Settings used here
#define	OTAHOST			"excalibur.bec.aa.net.uk"       // Default OTA host
#define	MAXSETTINGS 1024        // EEPROM storage
#define	settings		\
s(hostname,32,"")		\
s(otahost,128,"")		\
s(wifissid,32,"IoT")		\
s(wifipass,32,"security")	\
s(mqtthost,128,"mqtt.iot")	\
s(mqttuser,32,"")		\
s(mqttpass,32,"")		\
s(mqttport,10,"1883")		\
s(prefixcmnd,10,"cmnd")		\
s(prefixstat,10,"stat")		\
s(prefixtele,10,"tele")		\
s(prefixerror,10,"error")	\
s(prefixsetting,10,"setting")	\

// LetsEncrypt IdenTrust DST Root CA X3 certificate valid until 20210930
// https://letsencrypt.org/certificates/
// Downloaded from https://www.identrust.com/support/downloads

#define TLS_CA_CERT_LENGTH 846  // Letsencrypt
#define TLS_CA_CERT { \
    0x30, 0x82, 0x03, 0x4a, 0x30, 0x82, 0x02, 0x32, 0xa0, 0x03, 0x02, 0x01, \
    0x02, 0x02, 0x10, 0x44, 0xaf, 0xb0, 0x80, 0xd6, 0xa3, 0x27, 0xba, 0x89, \
    0x30, 0x39, 0x86, 0x2e, 0xf8, 0x40, 0x6b, 0x30, 0x0d, 0x06, 0x09, 0x2a, \
    0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x05, 0x05, 0x00, 0x30, 0x3f, \
    0x31, 0x24, 0x30, 0x22, 0x06, 0x03, 0x55, 0x04, 0x0a, 0x13, 0x1b, 0x44, \
    0x69, 0x67, 0x69, 0x74, 0x61, 0x6c, 0x20, 0x53, 0x69, 0x67, 0x6e, 0x61, \
    0x74, 0x75, 0x72, 0x65, 0x20, 0x54, 0x72, 0x75, 0x73, 0x74, 0x20, 0x43, \
    0x6f, 0x2e, 0x31, 0x17, 0x30, 0x15, 0x06, 0x03, 0x55, 0x04, 0x03, 0x13, \
    0x0e, 0x44, 0x53, 0x54, 0x20, 0x52, 0x6f, 0x6f, 0x74, 0x20, 0x43, 0x41, \
    0x20, 0x58, 0x33, 0x30, 0x1e, 0x17, 0x0d, 0x30, 0x30, 0x30, 0x39, 0x33, \
    0x30, 0x32, 0x31, 0x31, 0x32, 0x31, 0x39, 0x5a, 0x17, 0x0d, 0x32, 0x31, \
    0x30, 0x39, 0x33, 0x30, 0x31, 0x34, 0x30, 0x31, 0x31, 0x35, 0x5a, 0x30, \
    0x3f, 0x31, 0x24, 0x30, 0x22, 0x06, 0x03, 0x55, 0x04, 0x0a, 0x13, 0x1b, \
    0x44, 0x69, 0x67, 0x69, 0x74, 0x61, 0x6c, 0x20, 0x53, 0x69, 0x67, 0x6e, \
    0x61, 0x74, 0x75, 0x72, 0x65, 0x20, 0x54, 0x72, 0x75, 0x73, 0x74, 0x20, \
    0x43, 0x6f, 0x2e, 0x31, 0x17, 0x30, 0x15, 0x06, 0x03, 0x55, 0x04, 0x03, \
    0x13, 0x0e, 0x44, 0x53, 0x54, 0x20, 0x52, 0x6f, 0x6f, 0x74, 0x20, 0x43, \
    0x41, 0x20, 0x58, 0x33, 0x30, 0x82, 0x01, 0x22, 0x30, 0x0d, 0x06, 0x09, \
    0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01, 0x05, 0x00, 0x03, \
    0x82, 0x01, 0x0f, 0x00, 0x30, 0x82, 0x01, 0x0a, 0x02, 0x82, 0x01, 0x01, \
    0x00, 0xdf, 0xaf, 0xe9, 0x97, 0x50, 0x08, 0x83, 0x57, 0xb4, 0xcc, 0x62, \
    0x65, 0xf6, 0x90, 0x82, 0xec, 0xc7, 0xd3, 0x2c, 0x6b, 0x30, 0xca, 0x5b, \
    0xec, 0xd9, 0xc3, 0x7d, 0xc7, 0x40, 0xc1, 0x18, 0x14, 0x8b, 0xe0, 0xe8, \
    0x33, 0x76, 0x49, 0x2a, 0xe3, 0x3f, 0x21, 0x49, 0x93, 0xac, 0x4e, 0x0e, \
    0xaf, 0x3e, 0x48, 0xcb, 0x65, 0xee, 0xfc, 0xd3, 0x21, 0x0f, 0x65, 0xd2, \
    0x2a, 0xd9, 0x32, 0x8f, 0x8c, 0xe5, 0xf7, 0x77, 0xb0, 0x12, 0x7b, 0xb5, \
    0x95, 0xc0, 0x89, 0xa3, 0xa9, 0xba, 0xed, 0x73, 0x2e, 0x7a, 0x0c, 0x06, \
    0x32, 0x83, 0xa2, 0x7e, 0x8a, 0x14, 0x30, 0xcd, 0x11, 0xa0, 0xe1, 0x2a, \
    0x38, 0xb9, 0x79, 0x0a, 0x31, 0xfd, 0x50, 0xbd, 0x80, 0x65, 0xdf, 0xb7, \
    0x51, 0x63, 0x83, 0xc8, 0xe2, 0x88, 0x61, 0xea, 0x4b, 0x61, 0x81, 0xec, \
    0x52, 0x6b, 0xb9, 0xa2, 0xe2, 0x4b, 0x1a, 0x28, 0x9f, 0x48, 0xa3, 0x9e, \
    0x0c, 0xda, 0x09, 0x8e, 0x3e, 0x17, 0x2e, 0x1e, 0xdd, 0x20, 0xdf, 0x5b, \
    0xc6, 0x2a, 0x8a, 0xab, 0x2e, 0xbd, 0x70, 0xad, 0xc5, 0x0b, 0x1a, 0x25, \
    0x90, 0x74, 0x72, 0xc5, 0x7b, 0x6a, 0xab, 0x34, 0xd6, 0x30, 0x89, 0xff, \
    0xe5, 0x68, 0x13, 0x7b, 0x54, 0x0b, 0xc8, 0xd6, 0xae, 0xec, 0x5a, 0x9c, \
    0x92, 0x1e, 0x3d, 0x64, 0xb3, 0x8c, 0xc6, 0xdf, 0xbf, 0xc9, 0x41, 0x70, \
    0xec, 0x16, 0x72, 0xd5, 0x26, 0xec, 0x38, 0x55, 0x39, 0x43, 0xd0, 0xfc, \
    0xfd, 0x18, 0x5c, 0x40, 0xf1, 0x97, 0xeb, 0xd5, 0x9a, 0x9b, 0x8d, 0x1d, \
    0xba, 0xda, 0x25, 0xb9, 0xc6, 0xd8, 0xdf, 0xc1, 0x15, 0x02, 0x3a, 0xab, \
    0xda, 0x6e, 0xf1, 0x3e, 0x2e, 0xf5, 0x5c, 0x08, 0x9c, 0x3c, 0xd6, 0x83, \
    0x69, 0xe4, 0x10, 0x9b, 0x19, 0x2a, 0xb6, 0x29, 0x57, 0xe3, 0xe5, 0x3d, \
    0x9b, 0x9f, 0xf0, 0x02, 0x5d, 0x02, 0x03, 0x01, 0x00, 0x01, 0xa3, 0x42, \
    0x30, 0x40, 0x30, 0x0f, 0x06, 0x03, 0x55, 0x1d, 0x13, 0x01, 0x01, 0xff, \
    0x04, 0x05, 0x30, 0x03, 0x01, 0x01, 0xff, 0x30, 0x0e, 0x06, 0x03, 0x55, \
    0x1d, 0x0f, 0x01, 0x01, 0xff, 0x04, 0x04, 0x03, 0x02, 0x01, 0x06, 0x30, \
    0x1d, 0x06, 0x03, 0x55, 0x1d, 0x0e, 0x04, 0x16, 0x04, 0x14, 0xc4, 0xa7, \
    0xb1, 0xa4, 0x7b, 0x2c, 0x71, 0xfa, 0xdb, 0xe1, 0x4b, 0x90, 0x75, 0xff, \
    0xc4, 0x15, 0x60, 0x85, 0x89, 0x10, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, \
    0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x05, 0x05, 0x00, 0x03, 0x82, 0x01, \
    0x01, 0x00, 0xa3, 0x1a, 0x2c, 0x9b, 0x17, 0x00, 0x5c, 0xa9, 0x1e, 0xee, \
    0x28, 0x66, 0x37, 0x3a, 0xbf, 0x83, 0xc7, 0x3f, 0x4b, 0xc3, 0x09, 0xa0, \
    0x95, 0x20, 0x5d, 0xe3, 0xd9, 0x59, 0x44, 0xd2, 0x3e, 0x0d, 0x3e, 0xbd, \
    0x8a, 0x4b, 0xa0, 0x74, 0x1f, 0xce, 0x10, 0x82, 0x9c, 0x74, 0x1a, 0x1d, \
    0x7e, 0x98, 0x1a, 0xdd, 0xcb, 0x13, 0x4b, 0xb3, 0x20, 0x44, 0xe4, 0x91, \
    0xe9, 0xcc, 0xfc, 0x7d, 0xa5, 0xdb, 0x6a, 0xe5, 0xfe, 0xe6, 0xfd, 0xe0, \
    0x4e, 0xdd, 0xb7, 0x00, 0x3a, 0xb5, 0x70, 0x49, 0xaf, 0xf2, 0xe5, 0xeb, \
    0x02, 0xf1, 0xd1, 0x02, 0x8b, 0x19, 0xcb, 0x94, 0x3a, 0x5e, 0x48, 0xc4, \
    0x18, 0x1e, 0x58, 0x19, 0x5f, 0x1e, 0x02, 0x5a, 0xf0, 0x0c, 0xf1, 0xb1, \
    0xad, 0xa9, 0xdc, 0x59, 0x86, 0x8b, 0x6e, 0xe9, 0x91, 0xf5, 0x86, 0xca, \
    0xfa, 0xb9, 0x66, 0x33, 0xaa, 0x59, 0x5b, 0xce, 0xe2, 0xa7, 0x16, 0x73, \
    0x47, 0xcb, 0x2b, 0xcc, 0x99, 0xb0, 0x37, 0x48, 0xcf, 0xe3, 0x56, 0x4b, \
    0xf5, 0xcf, 0x0f, 0x0c, 0x72, 0x32, 0x87, 0xc6, 0xf0, 0x44, 0xbb, 0x53, \
    0x72, 0x6d, 0x43, 0xf5, 0x26, 0x48, 0x9a, 0x52, 0x67, 0xb7, 0x58, 0xab, \
    0xfe, 0x67, 0x76, 0x71, 0x78, 0xdb, 0x0d, 0xa2, 0x56, 0x14, 0x13, 0x39, \
    0x24, 0x31, 0x85, 0xa2, 0xa8, 0x02, 0x5a, 0x30, 0x47, 0xe1, 0xdd, 0x50, \
    0x07, 0xbc, 0x02, 0x09, 0x90, 0x00, 0xeb, 0x64, 0x63, 0x60, 0x9b, 0x16, \
    0xbc, 0x88, 0xc9, 0x12, 0xe6, 0xd2, 0x7d, 0x91, 0x8b, 0xf9, 0x3d, 0x32, \
    0x8d, 0x65, 0xb4, 0xe9, 0x7c, 0xb1, 0x57, 0x76, 0xea, 0xc5, 0xb6, 0x28, \
    0x39, 0xbf, 0x15, 0x65, 0x1c, 0xc8, 0xf6, 0x77, 0x96, 0x6a, 0x0a, 0x8d, \
    0x77, 0x0b, 0xd8, 0x91, 0x0b, 0x04, 0x8e, 0x07, 0xdb, 0x29, 0xb6, 0x0a, \
    0xee, 0x9d, 0x82, 0x35, 0x35, 0x10 }


#define s(name,len,def) static char name[len+1]=def;
settings
#undef 	s
typedef struct setting_s setting_t;
struct setting_s
{
   setting_t *next;
   char *name;
   byte *value;
   size_t len;
};
static setting_t *set = NULL;   // The settings
static long settingsupdate = 0; // When to do a settings update (delay after settings changed, 0 if no change)

// Local variables
static WiFiClient mqttclient;
static PubSubClient mqtt;

boolean
savesettings ()
{
   if (!settingsupdate)
      return true;              // OK (not saved)
   EEPROM.begin (MAXSETTINGS);
   unsigned int addr = 0,
      i,
      l;
   EEPROM.write (addr++, 0);
   for (i = 0; i < appnamelen; i++)
      EEPROM.write (addr++, appname[i]);
   setting_t *s;
   for (s = set; s; s = s->next)
   {
      l = strlen (s->name);
      if (!l || l > 255 || s->len > 255)
      {
         debug ("Cannot save %s as bad length (%d)\n", s->name, s->len);
         continue;              // Cannot save
      }
      if (!s->value)
      {
         debug ("Cannot save %s (null)\n", s->name);
         continue;
      }
      EEPROM.write (addr++, l);
      for (i = 0; i < l; i++)
         EEPROM.write (addr++, s->name[i]);
      EEPROM.write (addr++, s->len);
      for (i = 0; i < s->len; i++)
         EEPROM.write (addr++, s->value[i]);
   }
   EEPROM.write (addr++, 0);    // End of settings
   EEPROM.write (0, appnamelen);        // Make settings valid
   debug ("Settings saved, used %d\n", addr);
   EEPROM.end ();
   settingsupdate = 0;
   return true;                 // Done
}

boolean
loadsettings ()
{
   debug ("Load settings\n");
   unsigned int addr = 0,
      i,
      l;
   EEPROM.begin (MAXSETTINGS);
   l = EEPROM.read (addr++);
   if (l != appnamelen)
   {
      debug ("EEPROM not set\n");
      EEPROM.end ();
      return false;             // Check app name
   }
   for (i = 0; i < l; i++)
      if (EEPROM.read (addr++) != appname[i])
      {
         debug ("EEPROM not set\n");
         EEPROM.end ();
         return false;
      }
   char name[33];
   byte value[257];
   while (1)
   {
      l = EEPROM.read (addr++);
      if (!l)
         break;
      if (l >= sizeof (name))
      {                         // Bad name, skip
         debug ("Bad name len to read (%d)\n", l);
         addr += l;             // Skip name
         l = EEPROM.read (addr++);
         addr += l;             // Skip value
         continue;
      }
      for (i = 0; i < l; i++)
         name[i] = EEPROM.read (addr++);
      name[i] = 0;
      l = EEPROM.read (addr++);
      debug ("Load %s (%d)\n", name, l);
      for (i = 0; i < l; i++)
         value[i] = EEPROM.read (addr++);
      value[i] = 0;
      applysetting (name, value, l);
   }
   EEPROM.end ();
   settingsupdate = 0;          // No need to save
   return true;
}

boolean
upgrade ()
{                               // Do OTA upgrade
   debug ("Upgrade\n");
   savesettings ();
   char url[200];
   snprintf (url, sizeof (url), "/%.*s.ino." BOARD ".bin", appnamelen, appname);
   char *host = otahost;
   if (!*host)
      host = OTAHOST;           // Default
   pub (prefixstat, "upgrade", "Upgrading from https://%s%s", host, url);
   delay (1000);                // Allow clean MQTT report/ack
   WiFiClientSecure client = myleclient ();;
   if (ESPhttpUpdate.update (client, host, 443, url))
      Serial.println (ESPhttpUpdate.getLastErrorString ());
   return false;                // Should not get here
}

boolean
localsetting (const char *name, const byte * value, size_t len)
{                               // Apply a local setting
#define s(n,l,d) if(!strcasecmp(name,#n)){if(len>l || (!value&&len))return false;memcpy(n,value,len);n[len]=0;return true;}
   settings
#undef s
      return false;
}

boolean
applysetting (const char *name, const byte * value, size_t len)
{                               // Apply a setting
   debug ("Apply %s %.*s (%d)\n", name, len, value, len);
   if (len > 255)
   {
      debug ("Setting %s too long (%d)\n", name, len);
      return false;             // Too big
   }
   if (!localsetting (name, value, len) && !app_setting (name, value, len))
      return false;             // Not a setting we know
   setting_t *s;
   for (s = set; s && strcasecmp (name, s->name); s = s->next);
   if (!s && (!value || !len))
      return true;              // Setting cleared but did not exist
   if (!s)
   {                            // Create new setting
      s = (setting_t *) malloc (sizeof (*s));
      s->name = strdup (name);
      s->value = (byte *) malloc (len);
      s->len = len;
      memcpy (s->value, value, len);
      s->next = set;
      set = s;
      settingsupdate = millis () + 1000;
      return true;
   }
   if (!len)
   {                            // Remove setting
      setting_t **ss = &set;
      while (*ss != s)
         ss = &(*ss)->next;
      *ss = s->next;
      free (s->name);
      if (s->value)
         free (s->value);
      free (s);
      return true;
   }
   if (s->len != len || memcpy (s->value, value, len))
   {                            // Change value
      if (s->value)
         free (s->value);
      s->value = (byte *) malloc (len);
      s->len = len;
      memcpy (s->value, value, len);
      settingsupdate = millis () + 1000;
      return true;
   }
   return true;                 // Found (not changed)
}

static void
message (const char *topic, byte * payload, unsigned int len)
{                               // Handle MQTT message
   debug ("MQTT msg %s %.*s (%d)\n", topic, len, payload, len);
   char *p = strchr (topic, '/');
   if (!p)
      return;
   // Find the suffix
   p = strrchr (p + 1, '/');
   if (p)
      p++;
   else
      p = NULL;
   int l;
   l = strlen (prefixcmnd);
   if (p && !strncasecmp (topic, prefixcmnd, l) && topic[l] == '/')
   {
      if (!strcasecmp (p, "upgrade"))
      {                         // OTA opgrade
         do_upgrade = true;
         return;
      }
      if (!strcasecmp (p, "restart"))
      {
         do_restart = true;
         return;
      }
      if (!app_cmnd (p, payload, len))
         pub (prefixerror, p, "Bad command");
      return;
   }
   l = strlen (prefixsetting);
   if (p && !strncasecmp (topic, prefixsetting, l) && topic[l] == '/')
   {
      if (applysetting (p, payload, len))
         pub (prefixstat, p, "Setting saved");
      else
         pub (prefixerror, p, "Bad setting");
      return;
   }
}

ESP8266RevK::ESP8266RevK (const char *myappname, const char *myappversion, const char *myotahost)
{
#ifdef REVKDEBUG
   Serial.begin (74880);
#endif
   appname = strrchr (myappname, '/');
   if (appname)
      appname++;
   else
      appname = myappname;
   appnamelen = strlen (appname);
   if (appnamelen > 4 && !strcasecmp (appname + appnamelen - 4, ".ino"))
      appnamelen -= 4;
   appversion = myappversion;
   debug ("Application start %.*s\n", appnamelen, appname);
   loadsettings ();
   if (!*otahost && otahost)
      strncpy (otahost, myotahost, sizeof (otahost));
   if (!*hostname)
      snprintf (hostname, sizeof (hostname), "%06X", ESP.getChipId ());
   if (!*wifissid)
      strncpy (wifissid, "IoT", sizeof (wifissid));
   if (!*wifipass)
      strncpy (wifipass, "security", sizeof (wifipass));
   if (!*mqtthost)
      strncpy (mqtthost, "mqtt.iot", sizeof (mqtthost));
   char host[100];
   snprintf (host, sizeof (host), "%.*s-%s", appnamelen, appname, hostname);
   WiFi.hostname (host);
   WiFi.mode (WIFI_STA);
   WiFi.begin (wifissid, wifipass);
   if (*mqtthost)
   {
      debug ("MQTT %s\n", mqtthost);
      mqtt = PubSubClient ();
      mqtt.setClient (mqttclient);
      mqtt.setServer (mqtthost, atoi (mqttport));
      mqtt.setCallback (message);
   }
   debug ("Init done\n");
}

boolean ESP8266RevK::loop ()
{
   long
      now = millis ();          // Use with care as wraps every 49 days - best used signed to allow for wrapping
   if (settingsupdate && settingsupdate < now)
      savesettings ();
   // MQTT reconnnect
   static long
      mqttretry = 0;            // Note, signed to allow for wrapping millis
   if (*mqtthost && !mqtt.loop () && (int) (mqttretry - now) < 0)
   {
      static long
         mqttbackoff = 100;
      debug ("MQTT check\n");
      char
         topic[101];
      snprintf (topic, sizeof (topic), "%s/%.*s/%s", prefixtele, appnamelen, appname, hostname);
      if (mqtt.connect (hostname, mqttuser, mqttpass, topic, MQTTQOS1, true, "Offline"))
      {
         debug ("MQTT ok\n");
         /* Worked */
         mqttbackoff = 1000;
         mqtt.publish (topic, appversion ? : "Online", true);   // LWT
         /* Specific device */
         snprintf (topic, sizeof (topic), "+/%.*s/%s/#", appnamelen, appname, hostname);
         mqtt.subscribe (topic);
         /* All devices */
         snprintf (topic, sizeof (topic), "+/%.*s/*/#", appnamelen, appname);
         mqtt.subscribe (topic);
      } else if (mqttbackoff < 300000)
         mqttbackoff *= 2;
      mqttretry = now + mqttbackoff;
   }
   if (do_restart)
   {
      savesettings ();
      ESP.restart ();
   }
   if (do_upgrade)
   {
      upgrade ();
      ESP.restart (); // Should not be needed
   }
   return true;                 // OK
}

static
   boolean
pubap (const char *prefix, const char *suffix, const char *fmt, va_list ap)
{
   if (!*mqtthost)
      return false;             // No MQTT
   char
   temp[256] = { };
   if (fmt)
      vsnprintf (temp, sizeof (temp), fmt, ap);
   char
      topic[101];
   snprintf (topic, sizeof (topic), "%s/%.*s/%s/%s", prefix, appnamelen, appname, hostname, suffix);
   return mqtt.publish (topic, temp);
}

static
   boolean
pub (const char *prefix, const char *suffix, const char *fmt, ...)
{
   va_list
      ap;
   va_start (ap, fmt);
   boolean
      ret = pubap (prefix, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

boolean ESP8266RevK::stat (const char *suffix, const char *fmt, ...)
{
   va_list
      ap;
   va_start (ap, fmt);
   boolean
      ret = pubap (prefixstat, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

boolean ESP8266RevK::tele (const char *suffix, const char *fmt, ...)
{
   va_list
      ap;
   va_start (ap, fmt);
   boolean
      ret = pubap (prefixtele, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

boolean ESP8266RevK::error (const char *suffix, const char *fmt, ...)
{
   va_list
      ap;
   va_start (ap, fmt);
   boolean
      ret = pubap (prefixerror, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

boolean ESP8266RevK::pub (const char *prefix, const char *suffix, const char *fmt, ...)
{
   va_list
      ap;
   va_start (ap, fmt);
   boolean
      ret = pubap (prefix, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

boolean ESP8266RevK::setting (const char *name, const char *value)
{
   return applysetting (name, (const byte *) value, strlen (value));
}

boolean
ESP8266RevK::setting (const char *name, const byte * value, size_t len)
{                               // Set a setting
   return applysetting (name, value, len);
}

boolean ESP8266RevK::ota ()
{
   do_upgrade = true;
}

boolean ESP8266RevK::restart ()
{
   do_restart = true;
}

static
   WiFiClientSecure
myleclient ()
{
   WiFiClientSecure
      client;
   unsigned char
      tls_ca_cert[] = TLS_CA_CERT;
   client.setCACert (tls_ca_cert, TLS_CA_CERT_LENGTH);
   return client;
}

WiFiClientSecure ESP8266RevK::leclient ()
{
   return myleclient ();
}
