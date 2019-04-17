// RevK applicatioon framework
// See include file for more details

#ifdef ARDUINO_ESP8266_NODEMCU
#define BOARD "nodemcu"
#else
#define BOARD "generic"
#endif

#include <ESP8266RevK.h>
#include <ESP8266httpUpdate.h>

// Local functions
static void pub (const char *prefix, const char *suffix, const char *fmt, ...);

// App name set by constructor, expceted to be static string
static const char *appname = "RevK";    // System set from constructor as literal string
static int appnamelen = 4;      // May be truncated

// Settings used here
#define	settings		\
s(hostname,32,"")		\
s(otahost,128,"excalibur.bec.aa.net.uk")	\
s(wifissid,32,"Iot")		\
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

#define s(name,len,def) static char name[len]=def;
settings
#undef 	s
// Local variables
static WiFiClient mqttclient;
static PubSubClient mqtt;
static unsigned long mqttping = 0;
static unsigned long mqttretry = 0;
static unsigned long mqttbackoff = 100;

void
upgrade ()
{
   char url[200];
   snprintf (url, sizeof (url), "/%s.ino." BOARD ".bin", appnamelen, appname);
   pub (prefixstat, "upgrade", "Upgrading from http://%s/%s", otahost, url);
   delay (1000);                // Allow clean MQTT report/ack
   WiFiClient client;
   if (ESPhttpUpdate.update (client, otahost, 80, url))
      Serial.println (ESPhttpUpdate.getLastErrorString ());
   delay (1000);
}

static void
message (const char *topic, byte * payload, unsigned int len)
{
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
      {
         /* Do upgrade from web */
         upgrade ();
         return;
         /* Yeh, would not get here. */
      }
      if (app_cmnd (p, payload, len))
         return;
      pub (prefixerror, p, "Bad command");
      return;
   }
   l = strlen (prefixsetting);
   if (p && !strncasecmp (topic, prefixsetting, l) && topic[l] == '/')
   {
   }
}

ESP8266RevK::ESP8266RevK (const char *myappname)
{
   appname = strrchr (myappname, '/');
   if (appname)
      appname++;
   else
      appname = myappname;
   appnamelen = strlen (appname);
   if (appnamelen > 4 && !strcasecmp (appname + appnamelen - 4, ".ino"))
      appnamelen -= 4;
   if (!*hostname)
      snprintf (hostname, sizeof (hostname), "%06X", ESP.getChipId ());
   if (!*wifissid)
      strncpy (wifissid, "IoT", sizeof (wifissid));
   if (!*wifipass)
      strncpy (wifipass, "security", sizeof (wifipass));
   if (!*mqtthost)
      strncpy (mqtthost, "mqtt.iot", sizeof (mqtthost));
   WiFi.hostname (hostname);
   WiFi.mode (WIFI_STA);
   WiFi.begin (wifissid, wifipass);
   if (*mqtthost)
   {
      mqtt = PubSubClient ();
      mqtt.setClient (mqttclient);
      mqtt.setServer (mqtthost, atoi (mqttport));
      mqtt.setCallback (message);
   }
}

void
ESP8266RevK::loop ()
{
   /* MQTT reconnnect */
   if (*mqtthost && !mqtt.loop () && mqttretry < millis ())
   {
      char topic[101];
      snprintf (topic, sizeof (topic), "%s/%.*s/%s/LWT", prefixtele,appnamelen, appname, hostname);
      if (mqtt.connect (hostname, mqttuser, mqttpass, topic, MQTTQOS1, true, "Offline"))
      {
         /* Worked */
         mqttbackoff = 1000;
         mqtt.publish (topic, "Online", true);
         snprintf (topic, sizeof (topic), "+/%.*s/%s/#", appnamelen, appname, hostname);
         /* Specific device */
         mqtt.subscribe (topic);
         /* All devices */
         snprintf (topic, sizeof (topic), "+/%.*s/*/#", appnamelen, appname);
         mqtt.subscribe (topic);
      } else if (mqttbackoff < 300000)
         mqttbackoff *= 2;
      mqttretry = millis () + mqttbackoff;
   }
}

static void
pubap (const char *prefix, const char *suffix, const char *fmt, va_list ap)
{
   if (!*mqtthost)
      return;
   char temp[256] = { };
   if (fmt)
      vsnprintf (temp, sizeof (temp), fmt, ap);
   char topic[101];
   snprintf (topic, sizeof (topic), "%s/%.*s/%s/%s", prefix, appnamelen, appname, hostname, suffix);
   mqtt.publish (topic, temp);
}

static void
pub (const char *prefix, const char *suffix, const char *fmt, ...)
{
   va_list ap;
   va_start (ap, fmt);
   pubap (prefix, suffix, fmt, ap);
   va_end (ap);
}

void
ESP8266RevK::stat (const char *suffix, const char *fmt, ...)
{
   va_list ap;
   va_start (ap, fmt);
   pubap (prefixstat, suffix, fmt, ap);
   va_end (ap);
}

void
ESP8266RevK::tele (const char *suffix, const char *fmt, ...)
{
   va_list ap;
   va_start (ap, fmt);
   pubap (prefixtele, suffix, fmt, ap);
   va_end (ap);
}

void
ESP8266RevK::pub (const char *prefix, const char *suffix, const char *fmt, ...)
{
   va_list ap;
   va_start (ap, fmt);
   pubap (prefix, suffix, fmt, ap);
   va_end (ap);
}

void
ESP8266RevK::setting (const char *name, const char *value)
{                               // Set a setting
   // TODO
}

void
ESP8266RevK::ota ()
{
   upgrade ();
}
