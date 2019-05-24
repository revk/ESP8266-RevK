// See include file for more details

#include <ESPRevK.h>

#ifdef ARDUINO_ESP8266_NODEMCU
#define BOARD "nodemcu"
#else
#define BOARD "generic"
#endif

#include <ESP8266httpUpdate.h>
#include <EEPROM.h>
extern "C"
{
#include "sntp.h"
}

//#define GRATARP       10000 // Send gratuitous ARP periodically (no, does not actually help stay on WiFi, FFS)
#ifdef	GRATARP
#include "lwip/etharp.h"
#endif

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
static long do_restart = 0;     // Do a restart in main loop cleanly
static long do_upgrade = 0;     // Do an OTA upgrade
static char mychipid[7];

// Some defaults
#define	OTAHOST			"ota.revk.uk"
#define MQTTHOST		"mqtt.iot"
#define WIFISSID                "IoT"
#define WIFIPASS                "security"
#define       WIFISCANRATE            300

#define s(name) static const char *name=NULL
#define n(name,def) static int name=def;
#define f(name,len) static const byte *name=NULL
revk_settings
#undef 	f
#undef 	s
#undef n
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
WiFiClient mqttclient;
WiFiClientSecure mqttclientsecure;
PubSubClient mqtt;
boolean mqttbackup = false;
long mqttbackoff = 100;
long mqttretry = mqttbackoff;
int mqttcount = 0;

static const char *
strdup_P (const char *p)
{
   const char *m = (const char *) malloc (strlen_P (p) + 1);
   strcpy_P ((char *) m, p);
   return m;
}

static int wificount = 0;       // Count of connects
static volatile int wifidiscause = -1;  // Last disconnect cause

static const char *thisssid = NULL;     // Last creds used
static const char *lastssid = NULL;     // Last creds used
static const char *thispass = NULL;
static const char *lastpass = NULL;
static const byte *thisbssid = NULL;
static const byte *lastbssid = NULL;
static byte thischan = 0;
static byte lastchan = 0;
static boolean thisbssidfixed = false;
static boolean lastbssidfixed = false;

static const byte copybssid[6] = { };

static WiFiEventHandler wifidisconnecthandler = NULL;
static void
wifidisconnect (const WiFiEventStationModeDisconnected & event)
{                               // Event handler
   wifidiscause = event.reason;
}

static void
wifitry (const char *ssid, const char *passphrase, int32_t channel, const uint8_t * bssid)
{                               // try a connection and confirm if it worked
   if (bssid)
      debugf ("WiFi try %s%s %d %02X:%02X:%02X:%02X:%02X:%02X", ssid, passphrase ? "" : " (open)", channel, bssid[0], bssid[1],
              bssid[2], bssid[3], bssid[4], bssid[5]);
   else
      debugf ("WiFi try %s%s %d", ssid, passphrase ? "" : " (open)", channel);
   uint8_t status;
   status = WiFi.status ();
   if (status != WL_DISCONNECTED && status != WL_IDLE_STATUS)
   {                            // Should not really happen, we should only be here if disconnected
      debug ("WiFi force disconnect?");
      WiFi.disconnect ();
      delay (100);
   }
   thisssid = ssid;
   thispass = passphrase;
   thischan = channel;
   if (bssid)
   {
      thisbssidfixed = true;
      memcpy ((void *) copybssid, (void *) bssid, sizeof (copybssid));
      thisbssid = copybssid;
   }
   wifidiscause = 0;            // Stays 0 until we fail or later disconnect
   WiFi.begin (thisssid, thispass, thischan, thisbssid, true);
}

static boolean
wificonnect ()
{                               // Try and reconnect
   static int wifiseq = 0;
   if (!wifidiscause)
   {                            // We are trying, or connected
      if (!WiFi.isConnected ())
         return false;          // Still trying
      if (!thischan)
         thischan = WiFi.channel ();
      if (!thisbssid)
      {
         memcpy ((void *) copybssid, (void *) WiFi.BSSID (), sizeof (copybssid));
         thisbssid = copybssid;
      }
      lastssid = thisssid;
      lastpass = thispass;
      lastchan = thischan;
      lastbssid = thisbssid;
      lastbssidfixed = thisbssidfixed;
      debugf ("WiFi connected %s %d %02X:%02X:%02X:%02X:%02X:%02X RSSI %d", lastssid, lastchan, lastbssid[0], lastbssid[1],
              lastbssid[2], lastbssid[3], lastbssid[4], lastbssid[5], WiFi.RSSI ());
      wifiseq = 0;
      return true;
   }
   // Let's try and connect
   wificount++;                 // Connect attempt count
   if (wifiseq >= 3 && wifissid3)
      wifitry (wifissid3, wifipass3, wifichan3, wifibssid3);
   else if (wifiseq >= 2 && wifissid2)
      wifitry (wifissid2, wifipass2, wifichan2, wifibssid2);
   else if (wifiseq >= 1 && wifissid)
      wifitry (wifissid, wifipass, wifichan, wifibssid);
   else if (lastssid)
      wifitry (lastssid, lastpass, lastchan, lastbssid);
   wifiseq++;
   if (wifiseq == 4)
      wifiseq = 0;
   return false;
}

static void
wifiscan ()
{                               // Check for stronger signal if we are not locked to a bssid
   static long scannext = 1000;
   if (lastbssidfixed)
      return;                   // Fixed BSSID
   if (scannext && (int) (scannext - millis ()) >= 0)
      return;                   // Waiting
   if (scannext)
   {                            // Start a scan
      //debug ("WiFi scan");
      scannext = 0;
      WiFi.scanNetworks (true, false, 0, (uint8 *) lastssid);
      return;
   }
   int n = WiFi.scanComplete ();
   if (n < 0)
      return;
   debugf ("WiFi scan found %d", n);
   scannext = (millis () + WIFISCANRATE * 1000 ? : 1);  // Next scan
   int best = -1,
      bestrssi = 0;
   while (n--)
   {                            // Check for better signal
      if (best < 0 || WiFi.RSSI (n) > bestrssi)
      {
         bestrssi = WiFi.RSSI (n);
         best = n;
      }
   }
   if (best >= 0 && bestrssi > WiFi.RSSI () && memcmp ((void *) lastbssid, (void *) WiFi.BSSID (best), 6))
   {                            // Switch to better wifi
      lastchan = WiFi.channel (best);
      memcpy ((void *) (lastbssid = copybssid), (void *) WiFi.BSSID (best), sizeof (copybssid));
      if (best >= 0)
         debugf ("WiFi better %d %02X:%02X:%02X:%02X:%02X:%02X RSSI %d", lastchan, lastbssid[0], lastbssid[1], lastbssid[2],
                 lastbssid[3], lastbssid[4], lastbssid[5], bestrssi);
      WiFi.disconnect ();
   }
   WiFi.scanDelete ();
}


static long wifidown = 0;
static boolean
domqttopen (boolean silent = false)
{
   const char *host = (mqttbackup ? mqtthost2 : mqtthost);
   if (!host)
      return false;
   mqttcount++;
   if (mqttbackup)
   {
      debugf ("MQTT backup insecure %s", mqtthost2);
      myclient (mqttclient);
      mqtt.setClient (mqttclient);
      mqtt.setServer (mqtthost2, 1883);
   } else
   {
      if (mqttsha1)
      {
         debugf ("MQTT main secure %s", mqtthost);
         myclientTLS (mqttclientsecure, mqttsha1);
         mqtt.setClient (mqttclientsecure);
         mqtt.setServer (mqtthost, mqttport ? atoi (mqttport) : 8883);
      } else
      {
         debugf ("MQTT main insecure %s", mqtthost);
         myclient (mqttclient);
         mqtt.setClient (mqttclient);
         mqtt.setServer (mqtthost, mqttport ? atoi (mqttport) : 1883);
      }
   }
   char topic[101];
   snprintf_P (topic, sizeof (topic), PSTR ("%s/%.*s/%s"), prefixstate, appnamelen, appname, hostname);
   if (!mqtt.connect (hostname, mqttbackup ? NULL : mqttuser, mqttbackup ? NULL : mqttpass, topic, MQTTQOS1, true, "0 Fail"))
      return false;
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
   debugf ("MQTT connected %s", host);
   if (silent)
      return true;
   pub (true, prefixstate, NULL, F ("1 %s"), appversion);
   unsigned int now = millis ();
   pub (prefixinfo, NULL,
        F ("Up %d.%03d, flash %dKiB, W%d M%d, WiFi %s %d %02X:%02X:%02X:%02X:%02X:%02X RSSI %d Down %d.%03d"),
        now / 1000, now % 1000, ESP.getFlashChipRealSize () / 1024, wificount, mqttcount, lastssid, lastchan,
        lastbssid[0], lastbssid[1], lastbssid[2], lastbssid[3], lastbssid[4], lastbssid[5], WiFi.RSSI (),
        wifidown / 1000, wifidown % 1000);
   app_command ("connect", (const byte *) host, strlen ((char *) host));
   return true;
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
   debugf ("Do update %s", url);
   int ret = 0;
   if (mqtt.connected ())
   {
      pub (true, prefixstate, NULL, F ("0 OTA https://%s%s"), otahost, url);
      mqtt.disconnect ();
      delay (100);
   }
   debugf ("OTA https://%s%s", otahost, url);
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
   debugf ("OTA done %s", ESPhttpUpdate.getLastErrorString ().c_str ());
   if (domqttopen (true))
   {
      if (ok)
         pub (true, prefixstate, NULL, F ("0 OTA Reboot"));
      else
         pub (true, prefixstate, NULL, F ("0 OTA Error %s"), ESPhttpUpdate.getLastErrorString ().c_str ());
   }
   return ok ? 0 : ESPhttpUpdate.getLastError ();
}

boolean
upgrade (int appnamelen, const char *appname)
{                               // Do OTA upgrade
   debug ("Upgrade start");
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
   if (mqtt.connected ())
   {
      mqtt.disconnect ();
      delay (100);
   }
   WiFi.disconnect ();
   delay (100);
   ESP.restart ();              // Boot
   ESP.reset ();                // Boot hard (?)
   return false;                // Should not get here
}

const char *
localsetting (const char *name, const byte * value, size_t len)
{                               // Apply a local setting (return PROGMEM tag)
   if (mqtthost && !strncmp_P (name, PSTR ("mqtt"), 4) && mqtt.connected ())
   {
      pub (true, prefixstate, NULL, F ("0 Config change"));
      mqtt.disconnect ();
      delay (100);
   }
#define s(n) do{const char*t=PSTR(#n);if(!strcmp_P(name,t)){n=(const char*)value;return t;}}while(0)
#define n(n,d) do{const char*t=PSTR(#n);if(!strcmp_P(name,t)){n=(len?atoi((const char*)value):d);return t;}}while(0)
#define f(n,l) do{const char*t=PSTR(#n);if(!strcmp_P(name,t)){if(len&&len!=l)return NULL;n=value;return t;}}while(0)
   revk_settings
#undef f
#undef s
#undef n
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
   const char *newtag = NULL;
   // Existing setting
   setting_t **ss = &set;
   while (*ss && strcmp_P (tag, (*ss)->tag))
      ss = &(*ss)->next;
   if (!*ss && !news)
   {
      debugf ("Non setting: %s", tag);
      return true;              // No new value and no existing value
   }
   if (*ss && news && (*ss)->len == len && !memcmp ((*ss)->value, val, len))
   {                            // Same
      debugf ("Unchanged setting: %s %.*s (%d)", tag, len, val, len);
      free (news);
      return true;              // Value has not changed
   }
   debugf ("Setting: %s %.*s (%d)", tag, len, val, len);
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
      debugf ("Bad setting: %s", tag);
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
      do_restart = (millis ()? : 1);
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
         do_restart = (millis ()? : 1);
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

ESPRevK::ESPRevK (const char *myappname, const char *myappversion, const char *myotahost, const char *mymqtthost,
                  const char *mywifissid, const char *mywifipass)
{
#ifdef REVKDEBUG
   Serial.begin (115200);
#endif
   debugf ("App: %s", myappname);
   debugf ("Ver: %s", myappversion);
   snprintf_P (mychipid, sizeof (mychipid), PSTR ("%06X"), ESP.getChipId ());
   chipid = mychipid;
   debugf ("ID: %s", chipid);
   if (myappversion)
      strncpy (appver, myappversion, sizeof (appver));
   if (myappversion && strlen (myappversion) == 20 && !isdigit (*myappversion))
   {                            // Messing with version, horrid
      // We are assuming app version is __DATE__" "__TIME__, e.g. "May 13 2019 07:35:27"
      int m = 0,
         d = atoi (myappversion + 4);
      if (myappversion[0] == 'J')
      {
         if (myappversion[1] == 'a')
            m = 1;
         else if (myappversion[1] == 'u')
         {
            if (myappversion[2] == 'n')
               m = 6;
            else if (myappversion[2] == 'l')
               m = 7;
         }
      } else if (myappversion[0] == 'F')
         m = 2;
      else if (myappversion[0] == 'M')
      {
         if (myappversion[1] == 'a')
         {
            if (myappversion[2] == 'r')
               m = 3;
            else if (myappversion[2] == 'y')
               m = 5;
         }
      } else if (myappversion[0] == 'A')
      {
         if (myappversion[1] == 'p')
            m = 4;
         else if (myappversion[1] == 'u')
            m = 8;
      } else if (myappversion[0] == 'S')
         m = 9;
      else if (myappversion[0] == 'O')
         m = 10;
      else if (myappversion[0] == 'N')
         m = 11;
      else if (myappversion[0] == 'D')
         m = 12;
      snprintf (appver, sizeof (appver), "%.4s-%02d-%02d %.8s", myappversion + 7, m, d, myappversion + 12);
      appversion = appver;
   }
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
   setlen += appnamelen + 1;
   debugf ("Application start %.*s %s", appnamelen, appname, appversion);
   loadsettings ();
   // Override defaults
   if (!otahost && myotahost)
      otahost = myotahost;
   if (!wifissid && !wifipass && mywifipass)
      wifipass = mywifipass;
   if (!wifissid && mywifissid)
      wifissid = mywifissid;
   if (!mqtthost && mymqtthost)
      mqtthost = mymqtthost;
   if (!hostname)
      hostname = chipid;
   // My defaults
#ifdef	WIFIPASS
   if (!wifissid && !wifipass)
      wifipass = PCPY (WIFIPASS);
#endif
#ifdef	WIFISSID
   if (!wifissid)
      wifissid = PCPY (WIFISSID);
#endif
#ifdef OTAHOST
   if (!otahost || !*otahost)
      otahost = PCPY (OTAHOST);
#endif
#ifdef MQTTHOST
   if (!mqtthost)
      mqtthost = PCPY (MQTTHOST);
#endif
   if (mqtthost && !*mqtthost)
      mqtthost = NULL;
   if (mqtthost2 && !*mqtthost2)
      mqtthost2 = NULL;
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
   WiFi.setAutoConnect (false); // On start
   WiFi.setAutoReconnect (false);       // On loss (we connect)
   wifidisconnecthandler = WiFi.onStationModeDisconnected (wifidisconnect);
   sntp_set_timezone (0);       // UTC please
   if (ntphost)
      sntp_setservername (0, (char *) ntphost);
   WiFi.setSleepMode (WIFI_NONE_SLEEP); // We assume we have no power issues
   mqtt.setCallback (message);
   debug ("RevK init done");
}

ESPRevK::ESPRevK (const char *myappname, const __FlashStringHelper * myappversion, const char *myotahost,
                  const char *mymqtthost, const char *mywifissid, const char *mywifipass)
{
   char temp[21];
   *temp = 0;
   if (myappversion)
      strncpy_P (temp, (PGM_P) myappversion, sizeof (temp));
   ESPRevK (myappname, temp, myotahost, mymqtthost, mywifissid, mywifipass);
}

boolean
ESPRevK::loop ()
{
   unsigned long now = (millis ()? : 1);        // Use with care as wraps every 49 days
   if (do_restart && (int) (do_restart - now) <= 0)
   {
      debug ("Restart");
      settings_save ();
      if (mqtt.connected ())
      {
         pub (true, prefixstate, NULL, F ("0 Restart"));
         mqtt.disconnect ();
         delay (100);
      }
      if (!WiFi.isConnected ())
         ESP.reset ();          // Brutal
      WiFi.disconnect ();
      delay (100);
      ESP.restart ();
      return false;             // Uh
   }
   if (do_upgrade && (int) (do_upgrade - now) <= 0)
   {
      upgrade (appnamelen, appname);
      return false;             // Uh
   }
   // Save settings
   if (settingsupdate && (int) (settingsupdate - now) <= 0)
      settings_save ();
#ifdef GRATARP
   static long kickarp = 0;
   if ((int) (kickarp - now) <= 0)
   {
      kickarp = now + GRATARP;
      netif *n = netif_list;
      while (n)
      {
         etharp_gratuitous (n);
         n = n->next;
      }
   }
#endif
   // WiFi reconnect
   static long sntpbackoff = 100;
   static long sntptry = sntpbackoff;
   static long wifiok = 0;
   static int wifibias[20] = { };
   if (wificonnected)
   {                            // Connected
      if (wifidiscause && !(wificonnected = wificonnect ()))
         debug ("WiFi disconnected");
      else
      {                         // All is well
         wifiscan ();
      }
   } else if ((wificonnected = wificonnect ()))
   {
      wifidown = now - wifiok;
      // debug ("WiFi connected"); // Logged in wifi connect
      sntpbackoff = 100;
      sntptry = now;
   }
   if (WiFi.isConnected ())
      wifiok = now;
   if (wifireset && !do_restart && (int) (now - wifiok) > wifireset * 1000)
      do_restart = now;         // No wifi, restart
   static long mqttok = 0;
   if (mqttconnected)
      mqttok = now;
   if (mqttreset && !do_restart && (int) (now - mqttok) > mqttreset * 1000)
      do_restart = now;         // No mqtt, restart
   // More aggressive SNTP
   if (wificonnected && time (NULL) < 86400 && (int) (sntptry - now) <= 0)
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
   if (mqtthost)
   {                            // We are doing MQTT
      if (!mqtt.loop ())
      {                         // Not working
         const char *host = mqttbackup ? mqtthost2 : mqtthost;
         if ((!mqttretry || (int) (mqttretry - now) <= 0) && wificonnected)
         {                      // Try reconnect
            if (domqttopen ())
               mqttconnected = true;
            else
            {                   // Failed reconnect
               if (mqttconnected)
               {                // No longer connected 
                  mqttconnected = false;
                  app_command ("disconnect", (const byte *) host, strlen ((char *) host));
                  debugf ("MQTT failed reconnect %s", host);
               }
               if (mqttbackoff < 30000)
               {                // Not connected to MQTT
                  mqttbackoff *= 2;
                  mqttretry = ((now + mqttbackoff) ? : 1);
               } else
               {
                  if (mqtthost2 && (mqttsha1 || strcmp (mqtthost, mqtthost2)))
                  {
                     mqttbackup = !mqttbackup;
                     mqttretry = 0;
                     mqttbackoff = 1000;
                  }
                  if (!mqttbackup && (wifissid2 || wifissid3))
                     WiFi.disconnect ();        // Retry at wifi level
               }
               return false;
            }
         } else if (mqttconnected)
         {                      // No longer connected
            mqttconnected = false;
            app_command ("disconnect", (const byte *) host, strlen ((char *) host));
            debugf ("MQTT disconnected %s", host);
            return false;
         }
      }
   } else if (mqttconnected)
   {                            // Uh? config change or something
      mqttconnected = false;
      app_command ("disconnect", NULL, 0);
      debug ("MQTT disconnected");
   }
#ifdef	REVKDEBUG
   static long ticker = 0;
   if ((int) (ticker - now) <= 0)
   {
      ticker = (now + 10000) / 10000 * 10000;
      //debug ("Tick");
      debugf ("Now=%d W%d%S%S", now, wifidiscause, wificonnected ? PSTR (" WiFi") : PSTR (""),
              mqttconnected ? PSTR (" MQTT") : PSTR (""));
   }
#endif
   return wificonnected;
}

static boolean
pubap (boolean retain, const __FlashStringHelper * prefix, const __FlashStringHelper * suffix,
       const __FlashStringHelper * fmt, va_list ap)
{
   if (!mqtt.connected () || !hostname)
      return false;             // No MQTT
   char temp[200] = {
   };
   if (fmt)
      vsnprintf_P (temp, sizeof (temp), (PGM_P) fmt, ap);
   char topic[101];
   if (suffix)
      snprintf_P (topic, sizeof (topic), PSTR ("%S/%.*s/%s/%S"), (PGM_P) prefix, appnamelen, appname, hostname, (PGM_P) suffix);
   else
      snprintf_P (topic, sizeof (topic), PSTR ("%S/%.*s/%s"), (PGM_P) prefix, appnamelen, appname, hostname);
   return mqtt.publish (topic, temp, retain);
}

static boolean
pubap (boolean retain, const char *prefix, const __FlashStringHelper * suffix, const __FlashStringHelper * fmt, va_list ap)
{
   if (!mqtt.connected () || !hostname)
      return false;             // No MQTT
   char temp[200] = {
   };
   if (fmt)
      vsnprintf_P (temp, sizeof (temp), (PGM_P) fmt, ap);
   char topic[101];
   if (suffix)
      snprintf_P (topic, sizeof (topic), PSTR ("%s/%.*s/%s/%S"), prefix, appnamelen, appname, hostname, (PGM_P) suffix);
   else
      snprintf_P (topic, sizeof (topic), PSTR ("%s/%.*s/%s"), prefix, appnamelen, appname, hostname);
   return mqtt.publish (topic, temp, retain);
}

static boolean
pubap (boolean retain, const char *prefix, const char *suffix, const __FlashStringHelper * fmt, va_list ap)
{
   if (!mqtt.connected () || !hostname)
      return false;             // No MQTT
   char temp[200] = {
   };
   if (fmt)
      vsnprintf_P (temp, sizeof (temp), (PGM_P) fmt, ap);
   char topic[101];
   if (suffix)
      snprintf_P (topic, sizeof (topic), PSTR ("%s/%.*s/%s/%s"), prefix, appnamelen, appname, hostname, suffix);
   else
      snprintf_P (topic, sizeof (topic), PSTR ("%s/%.*s/%s"), prefix, appnamelen, appname, hostname);
   return mqtt.publish (topic, temp, retain);
}

static boolean
pubap (boolean retain, const char *prefix, const __FlashStringHelper * suffix, unsigned int len, const byte * data)
{
   if (!mqtt.connected () || !hostname)
      return false;             // No MQTT
   char topic[101];
   if (suffix)
      snprintf_P (topic, sizeof (topic), PSTR ("%s/%.*s/%s/%s"), prefix, appnamelen, appname, hostname, suffix);
   else
      snprintf_P (topic, sizeof (topic), PSTR ("%s/%.*s/%s"), prefix, appnamelen, appname, hostname);
   return mqtt.publish (topic, data, len, retain);
}

static boolean
pub (const char *prefix, const char *suffix, const __FlashStringHelper * fmt, ...)
{
   va_list ap;
   va_start (ap, fmt);
   boolean ret = pubap (false, prefix, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

static boolean
pub (boolean retain, const char *prefix, const char *suffix, const __FlashStringHelper * fmt, ...)
{
   va_list ap;
   va_start (ap, fmt);
   boolean ret = pubap (retain, prefix, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

static boolean
pub (const __FlashStringHelper * prefix, const __FlashStringHelper * suffix, const __FlashStringHelper * fmt, ...)
{
   va_list ap;
   va_start (ap, fmt);
   boolean ret = pubap (false, prefix, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

static boolean
pub (boolean retain, const __FlashStringHelper * prefix, const __FlashStringHelper * suffix, const __FlashStringHelper * fmt, ...)
{
   va_list ap;
   va_start (ap, fmt);
   boolean ret = pubap (retain, prefix, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

boolean
ESPRevK::state (const __FlashStringHelper * suffix, const __FlashStringHelper * fmt, ...)
{
   va_list ap;
   va_start (ap, fmt);
   boolean ret = pubap (true, prefixstate, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

boolean
ESPRevK::state (const char *suffix, const __FlashStringHelper * fmt, ...)
{
   va_list ap;
   va_start (ap, fmt);
   boolean ret = pubap (true, prefixstate, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

boolean
ESPRevK::state (const __FlashStringHelper * suffix, unsigned int len, const byte * data)
{
   return pubap (true, prefixstate, suffix, len, data);
}

boolean
ESPRevK::event (const __FlashStringHelper * suffix, const __FlashStringHelper * fmt, ...)
{
   va_list ap;
   va_start (ap, fmt);
   boolean ret = pubap (false, prefixevent, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

boolean
ESPRevK::event (const char *suffix, const __FlashStringHelper * fmt, ...)
{
   va_list ap;
   va_start (ap, fmt);
   boolean ret = pubap (false, prefixevent, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

boolean
ESPRevK::event (const __FlashStringHelper * suffix, unsigned int len, const byte * data)
{
   return pubap (false, prefixevent, suffix, len, data);
}

boolean
ESPRevK::info (const __FlashStringHelper * suffix, const __FlashStringHelper * fmt, ...)
{
   va_list ap;
   va_start (ap, fmt);
   boolean ret = pubap (false, prefixinfo, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

boolean
ESPRevK::info (const char *suffix, const __FlashStringHelper * fmt, ...)
{
   va_list ap;
   va_start (ap, fmt);
   boolean ret = pubap (false, prefixinfo, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

boolean
ESPRevK::info (const __FlashStringHelper * suffix, unsigned int len, const byte * data)
{
   return pubap (false, prefixinfo, suffix, len, data);
}

boolean
ESPRevK::error (const __FlashStringHelper * suffix, const __FlashStringHelper * fmt, ...)
{
   va_list ap;
   va_start (ap, fmt);
   boolean ret = pubap (false, prefixerror, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

boolean
ESPRevK::error (const char *suffix, const __FlashStringHelper * fmt, ...)
{
   va_list ap;
   va_start (ap, fmt);
   boolean ret = pubap (false, prefixerror, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

boolean
ESPRevK::error (const __FlashStringHelper * suffix, unsigned int len, const byte * data)
{
   boolean ret = pubap (false, prefixerror, suffix, len, data);
   return ret;
}

boolean
ESPRevK::pub (const char *prefix, const char *suffix, const __FlashStringHelper * fmt, ...)
{
   va_list ap;
   va_start (ap, fmt);
   boolean ret = pubap (false, prefix, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

boolean
ESPRevK::pub (const __FlashStringHelper * prefix, const __FlashStringHelper * suffix, const __FlashStringHelper * fmt, ...)
{
   va_list ap;
   va_start (ap, fmt);
   boolean ret = pubap (false, prefix, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

boolean
ESPRevK::pub (boolean retain, const char *prefix, const char *suffix, const __FlashStringHelper * fmt, ...)
{
   va_list ap;
   va_start (ap, fmt);
   boolean ret = pubap (retain, prefix, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

boolean
   ESPRevK::pub (boolean retain, const __FlashStringHelper * prefix, const __FlashStringHelper * suffix,
                 const __FlashStringHelper * fmt, ...)
{
   va_list ap;
   va_start (ap, fmt);
   boolean ret = pubap (retain, prefix, suffix, fmt, ap);
   va_end (ap);
   return ret;
}

boolean
ESPRevK::setting (const __FlashStringHelper * tag, const char *value)
{
   char temp[50];
   strncpy_P (temp, (PGM_P) tag, sizeof (temp));
   return setting_apply (temp, (const byte *) value, strlen (value));
}

boolean
ESPRevK::setting (const __FlashStringHelper * tag, const byte * value, size_t len)
{
   // Set a setting
   char temp[50];
   strncpy_P (temp, (PGM_P) tag, sizeof (temp));
   return setting_apply (temp, value, len);
}

boolean
ESPRevK::ota (int delay)
{
   debugf ("App OTA request %d", delay);
   if (delay < 0)
      do_upgrade = 0;
   else
      do_upgrade = ((millis () + delay) ? : 1);
}

boolean
ESPRevK::restart (int delay)
{
   debugf ("App restart request", delay);
   if (delay < 0)
      do_restart = 0;
   else
      do_restart = ((millis () + delay) ? : 1);
}

static void
myclient (WiFiClient & client)
{
}

#include "lecert.h"
static void
myclientTLS (WiFiClientSecure & client, const byte * sha1)
{
   if (sha1)
      client.setFingerprint (sha1);
   else
      client.setCACert_P (LECert, sizeof (LECert));
   static BearSSL::Session sess;
   client.setSession (&sess);
}

void
ESPRevK::clientTLS (WiFiClientSecure & client, const byte * sha1)
{
   return myclientTLS (client, sha1);
}

void
ESPRevK::sleep (unsigned long s)
{                               // Total sleep for a while
   if (!s)
      return;                   // Duh
   debugf ("Sleeping for %d seconds, good night...", s);
   if (mqtt.connected ())
   {
      pub (true, prefixstate, NULL, F ("0 Sleep"));
      mqtt.disconnect ();
      delay (100);
   }
   wifi_station_disconnect ();
   delay (100);
   ESP.deepSleep (s * 1000000);
   debug ("WTF");
   // Goes back to reset at this point - connect GPIO16 to RST
}

void
ESPRevK::mqttclose (const __FlashStringHelper * reason)
{
   debugf ("MQTT close %S", (PGM_P) reason);
   if (!mqttconnected)
      return;
   if (reason)
      pub (true, prefixstate, NULL, F ("0 %S"), reason);
   else
      pub (true, prefixstate, NULL, F ("0"));
   mqtt.disconnect ();
   delay (100);
   mqttconnected = false;
   mqttretry = 0;
}

boolean
ESPRevK::mqttopen (boolean silent)
{
   debug ("MQTT open");
   if (mqttconnected)
      return true;
   if (!domqttopen (silent))
      return false;
   mqttconnected = true;
   return true;
}

#define s(n) const char * ESPRevK::get_##n(){return n;}
#define f(n,b) const byte * ESPRevK::get_##n(){return n;}
#define n(n,d) int ESPRevK::get_##n(){return n;}
revk_settings
#undef f
#undef n
#undef s
