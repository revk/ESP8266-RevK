// See include file for more details
//
// TODO: -
// Fallback MQTT

#include <ESP8266RevK.h>
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

#include <ESP8266WiFiMulti.h>
#include <ESP8266httpUpdate.h>
#include <ESP8266TrueRandom.h>
#include <EEPROM.h>
#include "lecert.h"
extern "C"
{
#include "sntp.h"
}

              // Local functions
static void myclient (WiFiClient &);
static void myclientTLS (WiFiClientSecure &, byte * sha1 = NULL);
static boolean pub (const char *prefix, const char *suffix, const char *fmt, ...);
boolean savesettings ();
boolean applysetting (const char *name, const byte * value, size_t len);

// App name set by constructor, expceted to be static string
static const char *appname = "RevK";    // System set from constructor as literal string
static const char *appversion = NULL;   // System set from constructor as literal string
static int appnamelen = 4;      // May be truncated
static boolean do_restart = false;      // Do a restart in main loop cleanly
static boolean do_upgrade = false;      // Do an OTA upgrade
static boolean otausetls = true;        // Use TLS for OTA (only set in constructor)
static WiFiClientSecure mqttclientsecure;
static WiFiClient mqttclient;

   // Settings used here
#define	OTAHOST			"excalibur.bec.aa.net.uk"       // Default OTA host
#define	MAXSETTINGS 1024        // EEPROM storage
#define	settings		\
s(hostname,32,"")		\
s(otahost,128,"")		\
f(otasha1,20)			\
s(wifissid,32,"IoT")		\
s(wifipass,32,"security")	\
s(wifissid2,32,"")		\
s(wifipass2,32,"")		\
s(wifissid3,32,"")		\
s(wifipass3,32,"")		\
s(mqtthost,128,"mqtt.iot")	\
f(mqttsha1,20)			\
s(mqttuser,32,"")		\
s(mqttpass,32,"")		\
s(mqttport,10,"1883")		\
s(ntphost,127,"")		\
s(prefixcmnd,10,"cmnd")		\
s(prefixstat,10,"stat")		\
s(prefixtele,10,"tele")		\
s(prefixerror,10,"error")	\
s(prefixsetting,10,"setting")	\

#define s(name,len,def) static char name[len+1]=def;
#define f(name,len) static byte name[len]={};boolean name##_set=false;
settings
#undef 	f
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
static ESP8266WiFiMulti WiFiMulti;
static PubSubClient mqtt;

boolean
savesettings ()
{
   if (!settingsupdate)
      return true;              // OK(not saved)
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
         addr += l;
         //Skip name
         l = EEPROM.read (addr++);
         addr += l;
         //Skip value
         continue;
      }
      for (i = 0; i < l; i++)
         name[i] = EEPROM.read (addr++);
      name[i] = 0;
      l = EEPROM.read (addr++);
      for (i = 0; i < l; i++)
         value[i] = EEPROM.read (addr++);
      value[i] = 0;
      applysetting (name, value, l);
   }
   EEPROM.end ();
   settingsupdate = 0;          // No need to save
   do_restart = 0;              // Not changed key settings
   debug ("Loaded settings\n");
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
   ESPhttpUpdate.rebootOnUpdate (false);        // We 'll do the reboot
   if (otasha1_set)
   {
      debug ("Upgrade secure %s\n", host);
      delay (100);
      WiFiClientSecure client;
      myclientTLS (client, otasha1);
      if (ESPhttpUpdate.update (client, String (host), 443, String (url)))
         Serial.println (ESPhttpUpdate.getLastErrorString ());
   } else if (otausetls)
   {
      debug ("Upgrade secure (LE) %s\n", host);
      delay (100);
      WiFiClientSecure client;
      myclientTLS (client);
      ESPhttpUpdate.update (client, String (host), 443, String (url));
   } else
   {
      debug ("Upgrade insecure %s\n", host);
      delay (100);
      WiFiClient client;
      myclient (client);
      ESPhttpUpdate.update (client, String (host), 80, String (url));
   }
   debug ("Upgrade done: %s\n", ESPhttpUpdate.getLastErrorString ().c_str ());
   delay (100);
   ESP.restart ();              // Boot
   return false;                // Should not get here
}

boolean
localsetting (const char *name, const byte * value, size_t len)
{                               // Apply a local setting
#define s(n,l,d) if(!strcasecmp(name,#n)){if(len>l || (!value&&len))return false;memcpy(n,value,len);n[len]=0;return true;}
#define f(n,l) if(!strcasecmp(name,#n)){if(len==0){n##_set=false;return true;}else if(len==l){memcpy(n,value,len);n##_set=true;return true;}}
   settings
#undef f
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
   } else if (!len)
   {                            // Remove setting
      setting_t **ss = &set;
      while (*ss != s)
         ss = &(*ss)->next;
      *ss = s->next;
      free (s->name);
      if (s->value)
         free (s->value);
      free (s);
      settingsupdate = millis () + 1000;
   } else if (s->len != len || memcpy (s->value, value, len))
   {                            // Change value
      if (s->value)
         free (s->value);
      s->value = (byte *) malloc (len);
      s->len = len;
      memcpy (s->value, value, len);
      settingsupdate = millis () + 1000;
   } else
      return false;             // ?
   if (settingsupdate && !strcasecmp (name, "hostname"))
      do_restart = true;
   return true;                 // Found(not changed)
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

ESP8266RevK::ESP8266RevK (const char *myappname, const char *myappversion, const char *myotahost, boolean usetls)
{
#ifdef REVKDEBUG
   Serial.begin (115200);
#endif
   {
      // Fudge appname - strip training.whatever.Strip leading whatever / or whatever \ (windows)
      int i,
        l = strlen (myappname);
      for (i = l; i && myappname[i - 1] != '.' && myappname[i - 1] != '/' && myappname[i - 1] != '\\'; i--);
      if (i && myappname[i - 1] == '.')
         l = --i;
      for (; i && myappname[i - 1] != '/' && myappname[i - 1] != '\\'; i--);
      appname = myappname + i;
      appnamelen = l - i;
   }
   appversion = myappversion;
   debug ("Application start %.*s %s\n", appnamelen, appname, appversion);
   loadsettings ();
   otausetls = usetls;
   if (!*otahost && myotahost)
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
   debug ("WiFi %s\n", host);
   WiFi.hostname (host);
   WiFi.mode (WIFI_STA);
   if (*wifissid2 || *wifissid3)
   {
      WiFiMulti.addAP (wifissid, wifipass);
      if (*wifissid2)
         WiFiMulti.addAP (wifissid2, wifipass2);
      if (*wifissid3)
         WiFiMulti.addAP (wifissid3, wifipass3);
      WiFiMulti.run ();
      // See if we can connect
   } else
   {
      WiFi.setAutoConnect (true);
      WiFi.setAutoReconnect (true);
      WiFi.begin (wifissid, wifipass);
   }
   if (*mqtthost)
   {
      mqtt = PubSubClient ();
      if (mqttsha1_set)
      {
         debug ("MQTT secure %s\n", mqtthost);
         myclientTLS (mqttclientsecure, mqttsha1);
         mqtt.setClient (mqttclientsecure);
      } else
      {
         debug ("MQTT insecure %s\n", mqtthost);
         //myclient (mqttclient);
         mqtt.setClient (mqttclient);
      }
      mqtt.setServer (mqtthost, atoi (mqttport));
      mqtt.setCallback (message);
   }
   sntp_set_timezone (0);       // UTC please
   if (*ntphost)
      sntp_setservername (0, ntphost);
   debug ("RevK init done\n");
}

boolean ESP8266RevK::loop ()
{
   long
      now = millis ();          // Use with care as wraps every 49 days - best used signed to allow for wrapping
   if (do_restart)
   {
      savesettings ();
      pub (prefixstat, "restart", "Restarting");
      mqtt.disconnect ();
      ESP.restart ();
      return false;             // Uh
   }
   if (do_upgrade)
   {
      pub (prefixstat, "upgrade", "OTA upgrade %s", otahost);
      mqtt.disconnect ();
      upgrade ();
      return false;             // Uh
   }
   // More aggressive SNTP
   static long
      sntpbackoff = 100;
   static long
      sntptry = 0;
   if (time (NULL) < 86400 && (int) (sntptry - now) < 0)
   {
      sntptry = now + sntpbackoff;
      if (sntpbackoff < 300000)
         sntpbackoff *= 2;
      sntp_stop ();
      sntp_init ();
   }
   // Save settings
   if (settingsupdate && settingsupdate < now)
      savesettings ();
   // WiFi
   if ((*wifissid2 || *wifissid3) ? (WiFiMulti.run () != WL_CONNECTED) : (!WiFi.isConnected ()))
   {
      sntpbackoff = 100;
      sntptry = now;
      return false;             // No wifi, not a lot more we can do
   }
   // MQTT reconnnect
   static long
      mqttretry = 0;
   // Note, signed to allow for wrapping millis
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
         // Worked
         mqttbackoff = 1000;
         mqtt.publish (topic, appversion ? : "Online", true);   // LWT
         // Specific device
         snprintf (topic, sizeof (topic), "+/%.*s/%s/#", appnamelen, appname, hostname);
         mqtt.subscribe (topic);
         // All devices
         snprintf (topic, sizeof (topic), "+/%.*s/*/#", appnamelen, appname);
         mqtt.subscribe (topic);
      } else if (mqttbackoff < 300000)
         mqttbackoff *= 2;
      mqttretry = now + mqttbackoff;
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
      temp[256] = {
   };
   if (fmt)
      vsnprintf (temp, sizeof (temp), fmt, ap);
   char
      topic[101];
   if (suffix)
      snprintf (topic, sizeof (topic), "%s/%.*s/%s/%s", prefix, appnamelen, appname, hostname, suffix);
   else
      snprintf (topic, sizeof (topic), "%s/%.*s/%s", prefix, appnamelen, appname, hostname);
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
ESP8266RevK::error (const char *suffix, const char *fmt, ...)
{
   va_list ap;
   va_start (ap, fmt);
   boolean ret = pubap (prefixerror, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

boolean
ESP8266RevK::pub (const char *prefix, const char *suffix, const char *fmt, ...)
{
   va_list ap;
   va_start (ap, fmt);
   boolean ret = pubap (prefix, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

boolean
ESP8266RevK::setting (const char *name, const char *value)
{
   return applysetting (name, (const byte *) value, strlen (value));
}

boolean
ESP8266RevK::setting (const char *name, const byte * value, size_t len)
{
   // Set a setting
   return applysetting (name, value, len);
}

boolean
ESP8266RevK::ota ()
{
   do_upgrade = true;
}

boolean
ESP8266RevK::restart ()
{
   do_restart = true;
}

static void
myclient (WiFiClient & client)
{
   client.setLocalPortStart (60000 + ESP8266TrueRandom.random (5000));
}

static void
myclientTLS (WiFiClientSecure & client, byte * sha1)
{
   client.setLocalPortStart (60000 + ESP8266TrueRandom.random (5000));
   if (sha1)
      client.setFingerprint (sha1);
   else
   {
      unsigned char tls_ca_cert[] = TLS_CA_CERT;
      client.setCACert (tls_ca_cert, TLS_CA_CERT_LENGTH);
   }
   static BearSSL::Session sess;
   client.setSession (&sess);
}

void
ESP8266RevK::client (WiFiClient & client)
{
   return myclient (client);
}

void
ESP8266RevK::clientTLS (WiFiClientSecure & client, byte * sha1)
{
   return myclientTLS (client, sha1);
}
