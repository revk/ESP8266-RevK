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

static const char *appname=NULL;
static const char *firmware="excalibur.bec.aa.net.uk";

void	upgrade(const byte * message, size_t len)
{
	char		url       [200];
	snprintf(url, sizeof(url), "/%s.ino." BOARD ".bin", appname);
	WiFiClient	client;
	if (ESPhttpUpdate.update(client, firmware, 80, url)) {
		Serial.println(ESPhttpUpdate.getLastErrorString());
	}
	delay(1000);
}

static void		message(const char *topic, byte * payload, unsigned int len)
{
	char           *p = strchr(topic, '/');
	if (!p)
		return;
	char           *prefix = (char *)alloca(p - topic + 1);
	strncpy(prefix, topic, p - topic);
	prefix[p - topic] = 0;
	p = strrchr(p + 1, '/');
	if (p)
		p++;
	else
		p = NULL;
	if (!strcmp(prefix, "cmnd") && p && !strcmp(p, "upgrade")) {
		/* Do upgrade from web */
		upgrade(payload, len);
		return;
		/* Yeh, would not get here. */
	}
	app_mqtt(prefix,p,payload,len);
}

ESP8266RevK::ESP8266RevK(const char *myappname)
{
	appname=myappname;
	if (!*hostname)
		snprintf(hostname, sizeof(hostname), "%06X", ESP.getChipId());
	if (!*wifissid)
		strncpy(wifissid, "IoT", sizeof(wifissid));
	if (!*wifipass)
		strncpy(wifipass, "security", sizeof(wifipass));
	if (!*mqtthost)
		strncpy(mqtthost, "mqtt.iot", sizeof(mqtthost));
	WiFi.hostname(hostname);
	WiFi.mode(WIFI_STA);
	WiFi.begin(wifissid, wifipass);
	if(*mqtthost)
	{
		mqtt=PubSubClient();
		mqtt.setClient(mqttclient);
		mqtt.setServer(mqtthost, mqttport);
		mqtt.setCallback(message);
	}
}

void		ESP8266RevK::loop()
{
	/* MQTT reconnnect */
	if (*mqtthost&&!mqtt.loop() && mqttretry < millis()) {
		char topic[101];
		snprintf(topic, sizeof(topic), "tele/%s/%s/LWT", appname, hostname);
		if (mqtt.connect(hostname, mqttuser, mqttpass,topic,MQTTQOS1,true,"Offline")) {
			/* Worked */
			mqttbackoff = 1000;
			mqtt.publish(topic,"Online",true);
			snprintf(topic, sizeof(topic), "+/%s/%s/#", appname, hostname);
			/* Specific device */
			mqtt.subscribe(topic);
			/* All devices */
			snprintf(topic, sizeof(topic), "+/%s/*/#", appname);
			mqtt.subscribe(topic);
		} else if (mqttbackoff < 300000)
			mqttbackoff *= 2;
		mqttretry = millis() + mqttbackoff;
		/*Serial.printf("MQTT connnect %s\n",mqtthost);*/
	}
}

void		ESP8266RevK::pub(const char *prefix, const char *suffix, const char *fmt,...)
{
	if(!*mqtthost)return;
	char		temp      [256]={};
	if(fmt)
	{
		va_list		ap;
		va_start(ap, fmt);
		vsnprintf(temp, sizeof(temp), fmt, ap);
		va_end(ap);
	}
	char		topic     [101];
	snprintf(topic, sizeof(topic), "%s/%s/%s/%s", prefix, appname, hostname, suffix);
	mqtt.publish(topic, temp);
}

void start_setting()
{        // Store settings in EEPROM
 // TODO
}

void add_setting(const char *name,const char*value)
{
 // TODO
}

void end_setting()
{
 // TODO
}
