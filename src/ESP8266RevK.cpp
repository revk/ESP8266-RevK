// See include file for more details

#include <ESP8266RevK.h>

#ifdef ARDUINO_ESP8266_NODEMCU
#define BOARD "nodemcu"
#else
#define BOARD "generic"
#endif

#include <ESP8266httpUpdate.h>
#include <EEPROM.h>
#include "lecert.h"
extern "C"
{
#include "sntp.h"
}

              // Local functions
static void myclient (WiFiClient & client);
static void myclientTLS (WiFiClientSecure &, const byte * sha1 = NULL);
static boolean pub (boolean retain, const char *prefix, const char *suffix, const __FlashStringHelper * fmt, ...);
static boolean pub (const char *prefix, const char *suffix, const __FlashStringHelper * fmt, ...);
static boolean pub (const __FlashStringHelper * prefix, const __FlashStringHelper * suffix, const __FlashStringHelper * fmt, ...);
static boolean pub (boolean retain, const __FlashStringHelper * prefix, const __FlashStringHelper * suffix,
                    const __FlashStringHelper * fmt, ...);
boolean settings_save ();
boolean setting_apply (const char *name, const byte * value, size_t len);

// App name set by constructor, expceted to be static string
static const char *appname = NULL;      // System set from constructor as literal string
static const char *appversion = NULL;   // System set from constructor as literal string
static int appnamelen = 0;      // May be truncated
static long do_restart = false; // Do a restart in main loop cleanly
static long do_upgrade = false; // Do an OTA upgrade
static char mychipid[7];

// Some defaults
#define	OTAHOST			"ota.revk.uk"
#define MQTTHOST		"mqtt.revk.uk"
#define WIFISSID                "IoT"
#define WIFIPASS                "security"

#define	revk_settings	\
s(hostname);		\
s(otahost);		\
f(otasha1,20);		\
s(wifissid);		\
s(wifipass);		\
s(wifissid2);		\
s(wifipass2);		\
s(wifissid3);		\
s(wifipass3);		\
s(mqtthost);		\
s(mqtthost2);		\
f(mqttsha1,20);		\
s(mqttuser);		\
s(mqttpass);		\
s(mqttport);		\
s(ntphost);		\
s(prefixcommand);	\
s(prefixsetting);	\
s(prefixstate);		\
s(prefixevent);		\
s(prefixinfo);		\
s(prefixerror);		\

#define s(name) static const char *name=NULL
#define f(name,len) static const byte *name=NULL
revk_settings
#undef 	f
#undef 	s
typedef struct setting_s setting_t;
struct setting_s
{
   setting_t *next;
   const char *tag;             // PROGMEM
   byte len;
   const byte value[0];
};
#define	MAXEEPROM 1024          // EEPROM storage
const char eepromsig[] = "RevK";
static setting_t *set = NULL;   // The settings
static unsigned int setlen = sizeof (eepromsig) + 1;
static long settingsupdate = 0; // When to do a settings update (delay after settings changed, 0 if no change)

      // Local variables
static WiFiClientSecure mqttclientsecure;
static WiFiClient mqttclient;
static PubSubClient mqtt;
static boolean mqttbackup = false;

static const char *
strdup_P (const char *p)
{
   const char *m = (const char *) malloc (strlen_P (p) + 1);
   strcpy_P ((char *) m, p);
   return m;
}

#define PCPY(x) strdup_P(PSTR(x))

void
settings_reset ()
{                               // Invalidate settings
   EEPROM.begin (MAXEEPROM);
   EEPROM.write (0, 0);
   EEPROM.end ();
}

boolean
settings_save ()
{
   if (!settingsupdate)
      return true;              // OK(not saved)
   if (!appnamelen)
      return false;             // No app name, minimal load to load app
   EEPROM.begin (MAXEEPROM);
   unsigned int addr = 0,
      i,
      l;
   EEPROM.write (addr++, 0);
   for (i = 0; i < sizeof (eepromsig) - 1; i++)
      EEPROM.write (addr++, eepromsig[i]);
   EEPROM.write (addr++, appnamelen);
   for (i = 0; i < appnamelen; i++)
      EEPROM.write (addr++, appname[i]);
   setting_t *s;
   for (s = set; s; s = s->next)
   {
      l = strlen_P (s->tag);
      char temp[50];
      if (!l || l > sizeof (temp) - 1 || s->len > 255)
      {
         debugf ("Cannot save %S as bad length (%d)", s->tag, s->len);
         continue;              // Cannot save
      }
      strcpy_P (temp, s->tag);
      EEPROM.write (addr++, l);
      for (i = 0; i < l; i++)
         EEPROM.write (addr++, temp[i]);
      EEPROM.write (addr++, s->len);
      for (i = 0; i < s->len; i++)
         EEPROM.write (addr++, ((char *) s->value)[i]);
   }
   EEPROM.write (addr++, 0);    // End of settings
   EEPROM.write (0, sizeof (eepromsig) - 1);    // Make settings valid
   debugf ("Settings saved, used %d/%d bytes", addr, MAXEEPROM);
   EEPROM.end ();
   settingsupdate = 0;
   return true;                 // Done
}

boolean
loadsettings ()
{
   debug ("Load settings");
   unsigned int addr = 0,
      i,
      l;
   EEPROM.begin (MAXEEPROM);
   i = 0;
   l = EEPROM.read (addr++);
   if (l == sizeof (eepromsig) - 1)
      for (i = 0; i < sizeof (eepromsig) - 1 && EEPROM.read (addr++) == eepromsig[i]; i++);
   if (!i || i != sizeof (eepromsig) - 1)
   {
      settingsupdate = (millis ()? : 1);        // Save settings
      debug ("EEPROM not set");
      EEPROM.end ();
      return false;             // Check app name
   }
   i = 0;
   l = EEPROM.read (addr++);
   if (!appnamelen)
      addr += l;                // Skip
   else
   {
      if (l == appnamelen)
         for (i = 0; i < appnamelen && EEPROM.read (addr++) == appname[i]; i++);
      if (!i || i != appnamelen)
      {
         if (appnamelen)
            settingsupdate = (millis ()? : 1);  // Save settings
         debug ("EEPROM different app");
         EEPROM.end ();
         return false;          // Check app name
      }
   }
   char name[33];
   byte value[257];
   boolean bad = false;
   while (1)
   {
      l = EEPROM.read (addr++);
      if (!l)
         break;
      if (l >= sizeof (name))
      {                         // Bad name, skip
         debugf ("Bad name len to read (%d)", l);
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
      if (!setting_apply (name, value, l) && appnamelen)
         bad = true;
   }
   EEPROM.end ();
   if (!bad)
      settingsupdate = 0;       // No need to save
   do_restart = 0;              // Not changed key settings
   debug ("Loaded settings");
   return true;
}

static int
doupdate (char *url)
{
   int ret = 0;
   pub (true, prefixstate, NULL, F ("0 OTA http://%s%s"), otahost, url);
   delay (100);
   mqtt.disconnect ();
   delay (100);
   int ok = 0;
   if (otasha1)
   {
      WiFiClientSecure client;
      myclientTLS (client, otasha1);
      ok = ESPhttpUpdate.update (client, String (otahost), 443, String (url));
   } else
   {
      WiFiClientSecure client;
      myclientTLS (client);
      ok = ESPhttpUpdate.update (client, String (otahost), 443, String (url));
   }
   mqtt.connect (hostname, mqttbackup ? NULL : mqttuser, mqttbackup ? NULL : mqttpass);
   if (ok)
      pub (true, prefixstate, NULL, F ("0 OTA Reboot"));
   else
      pub (true, prefixstate, NULL, F ("0 OTA Error %s"), ESPhttpUpdate.getLastErrorString ().c_str ());
   return ok ? 0 : ESPhttpUpdate.getLastError ();
}

boolean
upgrade (int appnamelen, const char *appname)
{                               // Do OTA upgrade
   settings_save ();
   char url[200];
   {
      int p = 0,
         e = sizeof (url) - 1;
      url[p++] = '/';
      if (appnamelen)
         p += snprintf_P (url + p, e - p, PSTR ("%.*s"), appnamelen, appname);
      else
      {                         // Check flash for saved app name
         EEPROM.begin (MAXEEPROM);
         int addr = 0;
         int l = EEPROM.read (addr++),
            i;
         if (l)
         {
            addr += l;
            l = EEPROM.read (addr++);
            if (l)
               for (i = 0; i < l; i++)
                  if (p < e)
                     url[p++] = EEPROM.read (addr++);
         }
         EEPROM.end ();
      }
      if (p < e)
         p += snprintf_P (url + p, e - p, PSTR ("%S"), PSTR (".ino." BOARD ".bin"));
      url[p] = 0;
   }
   ESPhttpUpdate.rebootOnUpdate (false);
   int er = doupdate (url);
   if (er == HTTP_UE_TOO_LESS_SPACE)
   {                            // try smaller which should then be able to load the real code.
      snprintf_P (url, sizeof (url), PSTR ("Minimal..ino." BOARD ".bin"));
      er = doupdate (url);
   } else if (er)
      er = doupdate (url);      // Do again, it seems TLS can take too long for the header check, WTF, but session is cached to should work second time
   mqtt.disconnect ();
   delay (100);
   //ESP.restart ();              // Boot
   ESP.reset ();                // Boot hard
   return false;                // Should not get here
}

const char *
localsetting (const char *name, const byte * value, size_t len)
{                               // Apply a local setting (return PROGMEM tag)
#define s(n) do{const char*t=PSTR(#n);if(!strcmp_P(name,t)){n=(const char*)value;return t;}}while(0)
#define f(n,l) do{const char*t=PSTR(#n);if(!strcmp_P(name,t)){if(len&&len!=l)return NULL;n=value;return t;}}while(0)
   revk_settings
#undef f
#undef s
      return NULL;
}

#define isxchar(c) ((c)>='0'&&(c)<='9'||(c)>='a'&&(c)<='f'||(c)>='A'&&(c)<='F')

boolean
setting_apply (const char *tag, const byte * value, size_t len)
{                               // Apply a setting
   if (!tag)
   {
      debug ("Cannot handle null tag");
      return false;
   }
   if (len > 255)
   {
      debugf ("Setting %s too long (%d)", tag, len);
      return false;             // Too big
   }
   // New setting
   setting_t *news = NULL;      // New setting
   const byte *val = NULL;      // Value in new setting
   if (tag[0] == '0' && tag[1] == 'x')
   {                            // Convert from Hex
      tag += 2;                 // Strip 0x
      if (len)
      {                         // Has a value
         const byte *e = value + len;
         const byte *i = value;
         size_t n = 0;
         while (i < e)
         {
            if (!isxchar (*i))
               break;
            i++;
            if (i < e && isxchar (*i))
               i++;
            while (i < e && (*i == ' ' || *i == ':'))
               i++;
            n++;
         }
         len = n;
         news = (setting_t *) malloc (sizeof (*news) + len + 1);
         val = news->value;
         i = value;
         const byte *o = val;
         while (i < e)
         {
            if (!isxchar (*i))
               break;
            byte v = (*i & 0xF) + (*i >= 'A' ? 9 : 0);
            i++;
            if (i < e && isxchar (*i))
            {
               v = (v << 4) + (*i & 0xF) + (*i >= 'A' ? 9 : 0);
               i++;
            }
            while (i < e && (*i == ' ' || *i == ':'))
               i++;
            *(byte *) o++ = v;
         }
         *(byte *) o = 0;
         news->len = len;
      }
   } else if (len)
   {                            // Create new setting with new value
      news = (setting_t *) malloc (sizeof (*news) + len + 1);
      val = news->value;
      memcpy ((void *) val, (void *) value, len);
      ((char *) val)[len] = 0;
      news->len = len;
   }
   debugf ("Apply %s %.*s (%d)", tag, len, val, len);
   const char *newtag = NULL;
   // Existing setting
   setting_t **ss = &set;
   while (*ss && strcmp_P (tag, (*ss)->tag))
      ss = &(*ss)->next;
   if (!*ss && !news)
      return true;              // No new value and no existing value
   if (*ss && news && (*ss)->len == len && !memcmp ((*ss)->value, val, len))
   {
      free (news);
      return true;              // Value has not changed
   }
   unsigned int newlen = setlen;
   if (*ss)
      newlen -= strlen (tag) + 1 + (*ss)->len + 1;
   if (news)
      newlen += strlen (tag) + 1 + len + 1;
   if (newlen > MAXEEPROM)
   {
      debugf ("Settings would take too much space %d/%d", newlen, MAXEEPROM);
      if (news)
         free (news);
      return false;             // Not a setting we know
   }
   if (!(newtag = localsetting (tag, val, len)) && !(newtag = app_setting (tag, val, len)))
   {                            // Setting not accepted
      debugf ("Bad setting %s", tag);
      if (news)
         free (news);
      return false;             // Not a setting we know
   }
   setlen = newlen;
   if (*ss)
   {                            // Delete setting
      setting_t *s = *ss;
      *ss = s->next;
      free (s);
   }
   if (news)
   {                            // Add new setting
      news->tag = newtag;
      news->next = set;
      set = news;
   }
   settingsupdate = ((millis () + 1000) ? : 1);
   if (settingsupdate && !strcasecmp_P (tag, PSTR ("hostname")))
      do_restart = true;
   return true;                 // Found(not changed)
}

static void
message (const char *topic, byte * payload, unsigned int len)
{                               // Handle MQTT message
   debugf ("MQTT msg %s %.*s (%d)", topic, len, payload, len);
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
   l = strlen (prefixcommand);
   if (p && !strncasecmp (topic, prefixcommand, l) && topic[l] == '/')
   {
      if (!strcasecmp_P (p, PSTR ("upgrade")))
      {                         // OTA upgrade
         if (len)
            upgrade (len, (const char *) payload);      // App specific - special case
         else
            do_upgrade = (millis ()? : 1);
         return;
      }
      if (!strcasecmp_P (p, PSTR ("restart")))
      {
         do_restart = (millis ()? : 1);
         return;
      }
      if (!strcasecmp_P (p, PSTR ("factory")) && len == appnamelen + 6 && !memcmp (mychipid, payload, 6)
          && !memcmp (appname, payload + 6, appnamelen))
      {                         // Factory reset
         settings_reset ();
         do_restart = millis ();
         return;
      }
      if (!app_command (p, payload, len))
         pub (prefixerror, p, F ("Bad command"));
      return;
   }
   l = strlen (prefixsetting);
   if (p && !strncasecmp (topic, prefixsetting, l) && topic[l] == '/')
   {
      if (!setting_apply (p, payload, len))
         pub (prefixerror, p, F ("Bad setting"));
      return;
   }
}

void
preinit ()
{
#ifndef REVKDEBUG
   Serial.end ();               // If needed, then begin will be used
   pinMode (1, INPUT);
#endif
}

ESP8266RevK::ESP8266RevK (const char *myappname, const char *myappversion, const char *myotahost, const char *mymqtthost,
                          const char *mywifissid, const char *mywifipass)
{
   snprintf_P (mychipid, sizeof (mychipid), PSTR ("%06X"), ESP.getChipId ());
   chipid = mychipid;
#ifdef REVKDEBUG
   Serial.begin (115200);
#endif
   debug ("RevK start");
   if (myappname)
   {                            // Fudge appname - Strip leading whatever / or whatever \ (windows)
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
   setlen += appnamelen + 1;
   debugf ("RevK app name set %d", appnamelen);
   debugf ("Application start %.*s %s", appnamelen, appname, appversion);
   loadsettings ();
   // Override defaults
   if (!otahost && myotahost)
      otahost = myotahost;
   if (!wifissid && mywifissid)
      wifissid = mywifissid;
   if (!wifipass && mywifipass)
      wifipass = mywifipass;
   if (!mqtthost && mymqtthost)
      mqtthost = mymqtthost;
   if (!mqtthost2 && mymqtthost)
      mqtthost2 = mymqtthost;
   if (!hostname)
      hostname = chipid;
   // My defaults
#ifdef	WIFISSID
   if (!wifissid)
      wifissid = PCPY (WIFISSID);
#endif
#ifdef	WIFIPASS
   if (!wifipass)
      wifipass = PCPY (WIFIPASS);
#endif
#ifdef OTAHOST
   if (!otahost)
      otahost = PCPY (OTAHOST);
#endif
#ifdef MQTTHOST
   if (!mqtthost)
      mqtthost = PCPY (MQTTHOST);
   if (!mqtthost2)
      mqtthost2 = PCPY (MQTTHOST);
#endif
   if (!prefixcommand)
      prefixcommand = PCPY ("command");
   if (!prefixsetting)
      prefixsetting = PCPY ("setting");
   if (!prefixstate)
      prefixstate = PCPY ("state");
   if (!prefixerror)
      prefixerror = PCPY ("error");
   if (!prefixinfo)
      prefixinfo = PCPY ("info");
   if (!prefixevent)
      prefixevent = PCPY ("event");
   char host[100];
   snprintf_P (host, sizeof (host), PSTR ("%.*s-%s"), appnamelen, appname, hostname);
   debugf ("WiFi %s", host);
   WiFi.mode (WIFI_STA);
   wifi_station_set_hostname (host);
   WiFi.setAutoConnect (true);  // On start
   WiFi.setAutoReconnect (true);        // On loss
   if (wifissid)
      WiFi.begin (wifissid, wifipass);
   if (mqtthost)
   {
      if (mqttsha1)
      {
         debugf ("MQTT secure %s", mqtthost);
         myclientTLS (mqttclientsecure, mqttsha1);
         mqtt.setClient (mqttclientsecure);
      } else
      {
         debugf ("MQTT insecure %s", mqtthost);
         myclient (mqttclient);
         mqtt.setClient (mqttclient);
      }
      mqtt.setCallback (message);
      mqtt.setServer (mqtthost, mqttport ? atoi (mqttport) : mqttsha1 ? 8883 : 1883);
   }
   sntp_set_timezone (0);       // UTC please
   if (ntphost)
      sntp_setservername (0, (char *) ntphost);
   debug ("RevK init done");
}

boolean ESP8266RevK::loop ()
{
   unsigned long
      now = millis ();          // Use with care as wraps every 49 days
   if (do_restart && (int) (do_restart - now) < 0)
   {
      settings_save ();
      pub (true, prefixstate, NULL, F ("0 Restart"));
      mqtt.disconnect ();
      delay (100);
      ESP.reset ();
      return false;             // Uh
   }
   if (do_upgrade && (int) (do_upgrade - now) < 0)
   {
      upgrade (appnamelen, appname);
      return false;             // Uh
   }
   // Save settings
   if (settingsupdate && (int) (settingsupdate - now) < 0)
      settings_save ();
   // WiFi reconnect
   static long
      sntpbackoff = 100;
   static long
      sntptry = sntpbackoff;
   static long
      wifiscan = (wifissid ? 10000 : 1);        // Start a scan at 10 seconds
   static int
      wifilast = -1;            // Last used, don't try again right away
   static int
   wifibias[20] = { };
   static boolean
      wifiscanned = false;
   if (wificonnected)
   {                            // Connected
      if (WiFi.status () != WL_CONNECTED)
      {
         debug ("WiFi disconnected");
         wificonnected = false;
         mqtt.disconnect ();
         wifiscan = ((now + 10000) ? : 1);      // Rescan wifi if not back in 10 seconds
      }
   } else
   {                            // Not connected
      if (WiFi.status () == WL_CONNECTED)
      {
         debug ("WiFi connected");
         wificonnected = true;
         sntpbackoff = 100;
         sntptry = now;
         wifiscan = 0;
         wifiscanned = false;
      }
   }
   if (wifiscan)
   {
      if ((int) (wifiscan - now) < 0)
      {                         // Do a scan
         wifiscan = 0;
         if (!wificonnected)
         {
            debug ("WiFi scan start");
            WiFi.scanNetworks (true, true);
            wifiscanned = true;
         }
      }
   } else if (wifiscanned)
   {                            // Check scan done
      int
         n = WiFi.scanComplete ();
      if (n >= 0)
      {                         // Networks found
         wifiscanned = false;
         debugf ("WiFi scan found %d", n);
         if (!wifissid && !wifissid2 && !wifissid3)
         {                      // Pick any open WiFi
            int
               i,
               v,
               r = 0,
               b = n;
            static char
               s[33];
            for (i = 0; i < n; i++)
            {
               v = WiFi.RSSI (i);
               if (i < sizeof (wifibias) / sizeof (*wifibias))
                  v -= wifibias[i];     // Assuming in same order but does not matter much, just creates a bias
               const char *
                  ssid = WiFi.SSID (i).c_str ();
               if (*ssid && WiFi.encryptionType (i) == ENC_TYPE_NONE && (!r || v > r))
               {                // Find strongest
                  b = i;
                  r = v;
                  strncpy (s, ssid, sizeof (s));
               }
            }
            if (b < n)
            {
               wifilast = b;
               if (b < sizeof (wifibias) / sizeof (*wifibias))
                  wifibias[b]++;        // Bias against picking again
               debugf ("WiFi using any open [%s]", s);
               WiFi.begin (s, NULL, WiFi.channel (b), WiFi.BSSID (b));
            }
            wifiscan = ((now + 10000) ? : 1);   // Again (cancels if connected)
         } else if (!wificonnected)
         {                      // Pick from configured WiFi
            int
               i,
               v,
               r = 0,
               b = n;
            const char *
               s = NULL,
               *p = NULL;
            for (i = 0; i < n; i++)
            {
               v = WiFi.RSSI (i);
               if (i < sizeof (wifibias) / sizeof (*wifibias))
                  v -= wifibias[i];     // Assuming in same order but does not matter much, just creates a bias
               const char *
                  ssid = WiFi.SSID (i).c_str ();
               if (*ssid && (!r || v > r))
               {                // Stronger, consider it
                  if (wifissid && !strcmp (wifissid, ssid)
                      && (WiFi.encryptionType (i) == ENC_TYPE_NONE ? !wifipass : wifipass != NULL))
                  {
                     b = i;
                     r = v;
                     s = wifissid;
                     p = wifipass;
                  } else if (wifissid2 && !strcmp (wifissid2, ssid)
                             && (WiFi.encryptionType (i) == ENC_TYPE_NONE ? !wifipass2 : wifipass2 != NULL))
                  {
                     b = i;
                     r = v;
                     s = wifissid2;
                     p = wifipass2;
                  } else if (wifissid3 && !strcmp (wifissid3, ssid)
                             && (WiFi.encryptionType (i) == ENC_TYPE_NONE ? !wifipass3 : wifipass3 != NULL))
                  {
                     b = i;
                     r = v;
                     s = wifissid3;
                     p = wifipass3;
                  }
               }
            }
            if (b < n)
            {
               wifilast = b;
               if (b < sizeof (wifibias) / sizeof (*wifibias))
                  wifibias[b]++;        // Bias against picking again
               debugf ("WiFi using [%s]", s);
               WiFi.begin (s, p, WiFi.channel (b), WiFi.BSSID (b));
            }
            wifiscan = ((now + 10000) ? : 1);   // Again (cancels if connected)
         }
         WiFi.scanDelete ();
      }
   }
   // More aggressive SNTP
   if (wificonnected && time (NULL) < 86400 && (int) (sntptry - now) < 0)
   {
      if (sntpbackoff > 100)
         debug ("Poked SNTP again");
      sntptry = now + sntpbackoff;
      if (sntpbackoff < 300000)
         sntpbackoff *= 2;
      sntp_stop ();
      sntp_init ();
   }
   // MQTT reconnnect
   static long
      mqttbackoff = 100;
   static long
      mqttretry = mqttbackoff;
   // Note, signed to allow for wrapping millis
   if (mqtthost && !mqtt.loop () && (!mqttretry || (int) (mqttretry - now) < 0) && wificonnected)
   {
      char
         topic[101];
      snprintf_P (topic, sizeof (topic), PSTR ("%s/%.*s/%s"), prefixstate, appnamelen, appname, hostname);
      const char *
         host = mqttbackup ? mqtthost2 : mqtthost;
      if (mqtt.connect (hostname, mqttbackup ? NULL : mqttuser, mqttbackup ? NULL : mqttpass, topic, MQTTQOS1, true, "0 Fail"))
      {
         // Worked
         mqttretry = 0;
         mqttbackoff = 1000;
         // Specific device
         snprintf_P (topic, sizeof (topic), PSTR ("%s/%.*s/%s/#"), prefixcommand, appnamelen, appname, hostname);
         mqtt.subscribe (topic);
         snprintf_P (topic, sizeof (topic), PSTR ("%s/%.*s/%s/#"), prefixsetting, appnamelen, appname, hostname);
         mqtt.subscribe (topic);
         // All devices
         snprintf_P (topic, sizeof (topic), PSTR ("%s/%.*s/*/#"), prefixcommand, appnamelen, appname);
         mqtt.subscribe (topic);
         snprintf_P (topic, sizeof (topic), PSTR ("%s/%.*s/*/#"), prefixsetting, appnamelen, appname);
         mqtt.subscribe (topic);
         mqttconnected = true;
         pub (true, prefixstate, NULL, F ("1"));
         pub (prefixinfo, NULL, F ("Ver %s, up %d.%03d, flash %dKiB, RSSI %d"), appversion, now / 1000, now % 1000,
              ESP.getFlashChipRealSize () / 1024, WiFi.RSSI ());
         app_command ("connect", (const byte *) host, strlen ((char *) host));
         debugf ("MQTT connected %s", host);
      } else
      {
         if (mqttconnected)
         {
            mqttconnected = false;
            app_command ("disconnect", (const byte *) host, strlen ((char *) host));
            debugf ("MQTT disconnected %s", host);
         }
         if (mqttbackoff < 30000)
         {                      // Not connected to MQTT
            mqttbackoff *= 2;
            mqttretry = ((now + mqttbackoff) ? : 1);
         } else
         {
            if (mqtthost2 && strcmp (mqtthost, mqtthost2))
            {
               debug ("MQTT swap");
               mqttbackup = !mqttbackup;
               myclient (mqttclient);
               if (mqttbackup)
                  mqtt.setServer (mqtthost2, 1883);
               else
                  mqtt.setServer (mqtthost, mqttport ? atoi (mqttport) : mqttsha1 ? 8883 : 1883);
               mqttretry = 0;
               mqttbackoff = 1000;
            }
            if (!mqttbackup)
               WiFi.disconnect ();      // Retry at wifi level
         }
         return false;
      }
   }
   return wificonnected;
}

static
   boolean
pubap (boolean retain, const __FlashStringHelper * prefix, const __FlashStringHelper * suffix,
       const __FlashStringHelper * fmt, va_list ap)
{
   if (!mqtthost || !hostname)
      return false;             // No MQTT
   char
      temp[128] = {
   };
   if (fmt)
      vsnprintf_P (temp, sizeof (temp), (PGM_P) fmt, ap);
   char
      topic[101];
   if (suffix)
      snprintf_P (topic, sizeof (topic), PSTR ("%S/%.*s/%s/%S"), (PGM_P) prefix, appnamelen, appname, hostname, (PGM_P) suffix);
   else
      snprintf_P (topic, sizeof (topic), PSTR ("%S/%.*s/%s"), (PGM_P) prefix, appnamelen, appname, hostname);
   return mqtt.publish (topic, temp, retain);
}

static
   boolean
pubap (boolean retain, const char *prefix, const __FlashStringHelper * suffix, const __FlashStringHelper * fmt, va_list ap)
{
   if (!mqtthost || !hostname)
      return false;             // No MQTT
   char
      temp[128] = {
   };
   if (fmt)
      vsnprintf_P (temp, sizeof (temp), (PGM_P) fmt, ap);
   char
      topic[101];
   if (suffix)
      snprintf_P (topic, sizeof (topic), PSTR ("%s/%.*s/%s/%S"), prefix, appnamelen, appname, hostname, (PGM_P) suffix);
   else
      snprintf_P (topic, sizeof (topic), PSTR ("%s/%.*s/%s"), prefix, appnamelen, appname, hostname);
   return mqtt.publish (topic, temp, retain);
}

static
   boolean
pubap (boolean retain, const char *prefix, const char *suffix, const __FlashStringHelper * fmt, va_list ap)
{
   if (!mqtthost || !hostname)
      return false;             // No MQTT
   char
      temp[128] = {
   };
   if (fmt)
      vsnprintf_P (temp, sizeof (temp), (PGM_P) fmt, ap);
   char
      topic[101];
   if (suffix)
      snprintf_P (topic, sizeof (topic), PSTR ("%s/%.*s/%s/%s"), prefix, appnamelen, appname, hostname, suffix);
   else
      snprintf_P (topic, sizeof (topic), PSTR ("%s/%.*s/%s"), prefix, appnamelen, appname, hostname);
   return mqtt.publish (topic, temp, retain);
}

static
   boolean
pub (const char *prefix, const char *suffix, const __FlashStringHelper * fmt, ...)
{
   va_list
      ap;
   va_start (ap, fmt);
   boolean
      ret = pubap (false, prefix, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

static
   boolean
pub (boolean retain, const char *prefix, const char *suffix, const __FlashStringHelper * fmt, ...)
{
   va_list
      ap;
   va_start (ap, fmt);
   boolean
      ret = pubap (retain, prefix, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

static
   boolean
pub (const __FlashStringHelper * prefix, const __FlashStringHelper * suffix, const __FlashStringHelper * fmt, ...)
{
   va_list
      ap;
   va_start (ap, fmt);
   boolean
      ret = pubap (false, prefix, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

static
   boolean
pub (boolean retain, const __FlashStringHelper * prefix, const __FlashStringHelper * suffix, const __FlashStringHelper * fmt, ...)
{
   va_list
      ap;
   va_start (ap, fmt);
   boolean
      ret = pubap (retain, prefix, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

boolean ESP8266RevK::state (const __FlashStringHelper * suffix, const __FlashStringHelper * fmt, ...)
{
   va_list
      ap;
   va_start (ap, fmt);
   boolean
      ret = pubap (true, prefixstate, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

boolean ESP8266RevK::state (const char *suffix, const __FlashStringHelper * fmt, ...)
{
   va_list
      ap;
   va_start (ap, fmt);
   boolean
      ret = pubap (true, prefixstate, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

boolean ESP8266RevK::event (const __FlashStringHelper * suffix, const __FlashStringHelper * fmt, ...)
{
   va_list
      ap;
   va_start (ap, fmt);
   boolean
      ret = pubap (false, prefixevent, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

boolean ESP8266RevK::event (const char *suffix, const __FlashStringHelper * fmt, ...)
{
   va_list
      ap;
   va_start (ap, fmt);
   boolean
      ret = pubap (false, prefixevent, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

boolean ESP8266RevK::info (const __FlashStringHelper * suffix, const __FlashStringHelper * fmt, ...)
{
   va_list
      ap;
   va_start (ap, fmt);
   boolean
      ret = pubap (false, prefixinfo, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

boolean ESP8266RevK::info (const char *suffix, const __FlashStringHelper * fmt, ...)
{
   va_list
      ap;
   va_start (ap, fmt);
   boolean
      ret = pubap (false, prefixinfo, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

boolean ESP8266RevK::error (const __FlashStringHelper * suffix, const __FlashStringHelper * fmt, ...)
{
   va_list
      ap;
   va_start (ap, fmt);
   boolean
      ret = pubap (false, prefixerror, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

boolean ESP8266RevK::error (const char *suffix, const __FlashStringHelper * fmt, ...)
{
   va_list
      ap;
   va_start (ap, fmt);
   boolean
      ret = pubap (false, prefixerror, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

boolean ESP8266RevK::pub (const char *prefix, const char *suffix, const __FlashStringHelper * fmt, ...)
{
   va_list
      ap;
   va_start (ap, fmt);
   boolean
      ret = pubap (false, prefix, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

boolean
   ESP8266RevK::pub (const __FlashStringHelper * prefix, const __FlashStringHelper * suffix, const __FlashStringHelper * fmt, ...)
{
   va_list ap;
   va_start (ap, fmt);
   boolean ret = pubap (false, prefix, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

boolean
ESP8266RevK::pub (boolean retain, const char *prefix, const char *suffix, const __FlashStringHelper * fmt, ...)
{
   va_list ap;
   va_start (ap, fmt);
   boolean ret = pubap (retain, prefix, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

boolean
   ESP8266RevK::pub (boolean retain, const __FlashStringHelper * prefix, const __FlashStringHelper * suffix,
                     const __FlashStringHelper * fmt, ...)
{
   va_list ap;
   va_start (ap, fmt);
   boolean ret = pubap (retain, prefix, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

boolean ESP8266RevK::setting (const __FlashStringHelper * tag, const char *value)
{
   char
      temp[50];
   strncpy_P (temp, (PGM_P) tag, sizeof (temp));
   return setting_apply (temp, (const byte *) value, strlen (value));
}

boolean ESP8266RevK::setting (const __FlashStringHelper * tag, const byte * value, size_t len)
{
   // Set a setting
   char
      temp[50];
   strncpy_P (temp, (PGM_P) tag, sizeof (temp));
   return setting_apply (temp, value, len);
}

boolean ESP8266RevK::ota (int delay)
{
   if (delay < 0)
      do_upgrade = 0;
   else
      do_upgrade = ((millis () + delay) ? : 1);
}

boolean ESP8266RevK::restart (int delay)
{
   if (delay < 0)
      do_restart = 0;
   else
      do_restart = ((millis () + delay) ? : 1);
}

static void
myclient (WiFiClient & client)
{
}

static void
myclientTLS (WiFiClientSecure & client, const byte * sha1)
{
   if (sha1)
      client.setFingerprint (sha1);
   else
   {
      unsigned char
         tls_ca_cert[] = TLS_CA_CERT;
      client.setCACert (tls_ca_cert, TLS_CA_CERT_LENGTH);
   }
   static
      BearSSL::Session
      sess;
   client.setSession (&sess);
}

void
ESP8266RevK::clientTLS (WiFiClientSecure & client, const byte * sha1)
{
   return myclientTLS (client, sha1);
}

void
ESP8266RevK::sleep (unsigned long s)
{                               // Total sleep for a while
   if (!s)
      return;                   // Duh
   debugf ("Sleeping for %d seconds, good night...", s);
   pub (true, prefixstate, NULL, F ("0 Sleep"));
   delay (100);
   mqtt.disconnect ();
   delay (100);
   wifi_station_disconnect ();
   delay (100);
   ESP.deepSleep (s * 1000000);
   debug ("WTF");
   // Goes back to reset at this point - connect GPIO16 to RST
}

#define s(n) const char * ESP8266RevK::get_##n(){return n;}
#define f(n,b) const byte * ESP8266RevK::get_##n(){return n;}
revk_settings
#undef f
#undef s
