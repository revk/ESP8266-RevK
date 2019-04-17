// RevK applicatioon framework
// See include file for more details
//
// TODO :-
// Fallback SSID
// Fallback MQTT
// MQTT TLS
// OTA TLS

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
static boolean pub (const char *prefix, const char *suffix, const char *fmt, ...);
boolean savesettings ();
boolean applysetting (const char *name, const byte * value, size_t len);

// App name set by constructor, expceted to be static string
static const char *appname = "RevK";    // System set from constructor as literal string
static int appnamelen = 4;      // May be truncated

// Settings used here
#define	MAXSETTINGS 1024
#define	settings		\
s(hostname,32,"")		\
s(otahost,128,"excalibur.bec.aa.net.uk")	\
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
static unsigned long mqttping = 0;
static unsigned long mqttretry = 0;
static unsigned long mqttbackoff = 100;

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
debug("Bad name len to read (%d)\n",l);
         addr += l;             // Skip name
         l = EEPROM.read (addr++);
         addr += l;             // Skip value
         continue;
      }
      for (i = 0; i < l; i++)
         name[i] = EEPROM.read (addr++);
      name[i] = 0;
      l = EEPROM.read (addr++);
      debug ("Load %s (%d)\n", name,l);
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
   rst_info *myResetInfo = ESP.getResetInfoPtr ();
   if (myResetInfo->reason == REASON_EXT_SYS_RST)
   {                            // Cannot flash if serial loaded for some reason
      pub (prefixerror, "upgrade", "Please restart first");
      return false;
   }
   debug ("Upgrade\n");
   // TODO add TLS option
   savesettings ();
   char url[200];
   snprintf (url, sizeof (url), "/%.*s.ino." BOARD ".bin", appnamelen, appname);
   pub (prefixstat, "upgrade", "Upgrading from http://%s/%s", otahost, url);
   delay (1000);                // Allow clean MQTT report/ack
   WiFiClient client;
   if (ESPhttpUpdate.update (client, otahost, 80, url))
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
         if (upgrade ())
            return;
      }
      if (!strcasecmp (p, "restart"))
      {
         savesettings ();
         ESP.restart ();
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

ESP8266RevK::ESP8266RevK (const char *myappname)
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
   debug ("Application start %.*s\n", appnamelen, appname);
   loadsettings ();
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

boolean
ESP8266RevK::loop ()
{
   long now = millis ();
   if (settingsupdate && settingsupdate < now)
      savesettings ();
   /* MQTT reconnnect */
   if (*mqtthost && !mqtt.loop () && mqttretry < now)
   {
      debug ("MQTT check\n");
      char topic[101];
      snprintf (topic, sizeof (topic), "%s/%.*s/%s/LWT", prefixtele, appnamelen, appname, hostname);
      if (mqtt.connect (hostname, mqttuser, mqttpass, topic, MQTTQOS1, true, "Offline"))
      {
         debug ("MQTT ok\n");
         /* Worked */
         mqttbackoff = 1000;
         mqtt.publish (topic, "Online", true);  // LWT
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
   // TODO reboot before mills() wrap?
   return true;                 // OK
}

static boolean
pubap (const char *prefix, const char *suffix, const char *fmt, va_list ap)
{
   if (!*mqtthost)
      return false;             // No MQTT
   char temp[256] = { };
   if (fmt)
      vsnprintf (temp, sizeof (temp), fmt, ap);
   char topic[101];
   snprintf (topic, sizeof (topic), "%s/%.*s/%s/%s", prefix, appnamelen, appname, hostname, suffix);
   return mqtt.publish (topic, temp);
}

static boolean
pub (const char *prefix, const char *suffix, const char *fmt, ...)
{
   va_list ap;
   va_start (ap, fmt);
   boolean ret = pubap (prefix, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

boolean
ESP8266RevK::stat (const char *suffix, const char *fmt, ...)
{
   va_list ap;
   va_start (ap, fmt);
   boolean ret = pubap (prefixstat, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

boolean
ESP8266RevK::tele (const char *suffix, const char *fmt, ...)
{
   va_list ap;
   va_start (ap, fmt);
   boolean ret = pubap (prefixtele, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

boolean
ESP8266RevK::pub (const char *prefix, const char *suffix, const char *fmt, ...)
{
   va_list ap;
   va_start (ap, fmt);
   boolean ret = -pubap (prefix, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

boolean
ESP8266RevK::setting (const char *name, const char *value)
{
   return applysetting (name, (const byte *) value, strlen (value));
}

boolean ESP8266RevK::setting (const char *name, const byte * value, size_t len)
{                               // Set a setting
   return applysetting (name, value, len);
}

boolean
ESP8266RevK::ota ()
{
   return upgrade ();
}
