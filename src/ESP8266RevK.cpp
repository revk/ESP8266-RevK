/*
 * This is a wrapper for a number of common applications It provides the
 * basic common aspects - connnection to WiFi and MQTT
 * 
 * TODO :-
 * Add functions for app to set these and store in EEPROM
 * Add fall back SSID
 * Add fall back MQTT Add option for TLS MQTT
 * Online/offline (will)
 * 
 * There are a number of default / key settings which can be overridden in the
 * info.h file
 */

#ifdef ARDUINO_ESP8266_NODEMCU
#define BOARD "nodemcu"
#else
#define BOARD "generic"
#endif

#include <ESP8266RevK.h>
#include <ESP8266httpUpdate.h>

// Local functions
static void pub (const char *prefix, const char *suffix, const char *fmt, ...);

// Statics rather than in class - the mqtt callback was a problem, and really this is system wide stuff
static const char *appname = NULL;      // System set from constructor as literal string

// Settings that can be changed
static char firmware[129] = "excalibur.bec.aa.net.uk";  // Host from which we load new code

static char hostname[33] = { }; // Unique ID of host (default is chipid)

static char wifissid[33] = "IoT";       // WiFi SSID
static char wifipass[33] = "security";  // WiFi Password
static char mqtthost[129] = "mqtt.iot"; // MQTT hostname/IP
static char mqttuser[33] = "";  // MQTT usernmame
static char mqttpass[33] = "";  // MQTT password

// Local variables
static WiFiClient mqttclient;
static PubSubClient mqtt;
static unsigned long mqttping = 0;
static unsigned long mqttretry = 0;
static unsigned long mqttbackoff = 100;
static unsigned short mqttport = 1883;

void
upgrade (const byte * message, size_t len)
{
   char url[200];
   snprintf (url, sizeof (url), "/%s.ino." BOARD ".bin", appname);
   pub ("stat", "upgrade", "Upgrading from http://%s/%s", firmware, url);
   delay (1000);                // Allow clean MQTT report/ack
   WiFiClient client;
   if (ESPhttpUpdate.update (client, firmware, 80, url))
      Serial.println (ESPhttpUpdate.getLastErrorString ());
   delay (1000);
}

static void
message (const char *topic, byte * payload, unsigned int len)
{
   char *p = strchr (topic, '/');
   if (!p)
      return;
   char *prefix = (char *) alloca (p - topic + 1);
   strncpy (prefix, topic, p - topic);
   prefix[p - topic] = 0;
   p = strrchr (p + 1, '/');
   if (p)
      p++;
   else
      p = NULL;
   if (!strcmp (prefix, "cmnd") && p)
   {
      if (!strcmp (p, "upgrade"))
      {
         /* Do upgrade from web */
         upgrade (payload, len);
         return;
         /* Yeh, would not get here. */
      }
      if (app_cmnd (p, payload, len))
         return;
      pub ("stat", "error", "Unknown command");
      return;
   }
}

ESP8266RevK::ESP8266RevK (const char *myappname)
{
   appname = myappname;
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
      mqtt.setServer (mqtthost, mqttport);
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
      snprintf (topic, sizeof (topic), "tele/%s/%s/LWT", appname, hostname);
      if (mqtt.connect (hostname, mqttuser, mqttpass, topic, MQTTQOS1, true, "Offline"))
      {
         /* Worked */
         mqttbackoff = 1000;
         mqtt.publish (topic, "Online", true);
         snprintf (topic, sizeof (topic), "+/%s/%s/#", appname, hostname);
         /* Specific device */
         mqtt.subscribe (topic);
         /* All devices */
         snprintf (topic, sizeof (topic), "+/%s/*/#", appname);
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
   snprintf (topic, sizeof (topic), "%s/%s/%s/%s", prefix, appname, hostname, suffix);
   mqtt.publish (topic, temp);
}

static void
pub (const char *prefix, const char *suffix, const char *fmt, ...)
{
   va_list ap;
   va_start (ap, fmt);
   pub (prefix, suffix, fmt, ap);
   va_end (ap);
}

void
ESP8266RevK::stat (const char *suffix, const char *fmt, ...)
{
   va_list ap;
   va_start (ap, fmt);
   pub ("stat", suffix, fmt, ap);
   va_end (ap);
}

void
ESP8266RevK::tele (const char *suffix, const char *fmt, ...)
{
   va_list ap;
   va_start (ap, fmt);
   pub ("tele", suffix, fmt, ap);
   va_end (ap);
}

void
ESP8266RevK::pub (const char *prefix, const char *suffix, const char *fmt, ...)
{
   va_list ap;
   va_start (ap, fmt);
   pub (prefix, suffix, fmt, ap);
   va_end (ap);
}


void
setting (const char *name, const char *value)
{
   // TODO
}
