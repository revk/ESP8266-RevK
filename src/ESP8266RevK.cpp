/*
 * This is a wrapper for a number of common applications It provides the
 * basic common aspects - connnection to WiFi and MQTT
 * 
 * TODO :- Add functions for app to set these and store in EEPROM
 * Add fall back SSID
 * Add fall back MQTT Add option for TLS MQTT
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

void		ESP8266RevK:: upgrade(const byte * message, size_t len)
{
	char		url       [200];
	snprintf(url, sizeof(url), "/%s.ino." BOARD ".bin", appname);
	pub("stat", "upgrade", "Upgrade " __TIME__ " http://%s%s", firmware, url);
	WiFiClient	client;
	if (ESPhttpUpdate.update(client, firmware, 80, url)) {
		Serial.println(ESPhttpUpdate.getLastErrorString());
		pub("error", "upgrade", "%s", ESPhttpUpdate.getLastErrorString().c_str());
	}
}

void		ESP8266RevK::message(const char *topic, byte * payload, unsigned int len)
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
	callback(prefix, p, payload, len);
}

ESP8266RevK::ESP8266RevK(const char *appname, void (*rawcallback)(char*, uint8_t*, unsigned int),void (*callback) (const char *prefix, const char *suffix, const byte * message, size_t len))
{
	if (appname && *appname)
		strncpy(this->appname, appname, sizeof(this->appname));
	this->callback = callback;
	if (!*hostname)
		snprintf(hostname, sizeof(hostname), "%06X", ESP.getChipId());
	if (!*wifissid)
		strncpy(wifissid, "IoT", sizeof(wifissid));
	if (!*wifipass)
		strncpy(wifipass, "security", sizeof(wifipass));
	if (!*mqtthost)
		strncpy(mqtthost, "mqtt.iot", sizeof(mqtthost));
	if (!*firmware)
		strncpy(firmware, "excalibur.bec.aa.net.uk", sizeof(firmware));
	char		host      [129];
	snprintf(host, sizeof(host), "%s-%s", appname, hostname);
	WiFi.hostname(host);
	WiFi.mode(WIFI_STA);
	WiFi.begin(wifissid, wifipass);
	mqtt=PubSubClient();
	mqtt.setClient(mqttclient);
	mqtt.setServer(mqtthost, mqttport);
	mqtt.setCallback(rawcallback);
}

void		ESP8266RevK::loop()
{
	/* MQTT reconnnect */
	if (!mqtt.loop() && mqttretry < millis()) {
		if (mqtt.connect(hostname, mqttuser, mqttpass)) {
			/* Worked */
			pub("stat", "boot", "Running " __TIME__);
			mqttbackoff = 1000;
			char		sub       [101];
			snprintf(sub, sizeof(sub), "+/%s/%s/#", appname, hostname);
			/* Specific device */
			mqtt.subscribe(sub);
			/* All devices */
			snprintf(sub, sizeof(sub), "+/%s/*/#", appname);
			mqtt.subscribe(sub);
		} else if (mqttbackoff < 300000)
			mqttbackoff *= 2;
		mqttretry = millis() + mqttbackoff;
		/*Serial.printf("MQTT connnect %s\n",mqtthost);*/
	}
}

void		ESP8266RevK::pub(const char *prefix, const char *suffix, const char *fmt,...)
{
	char		temp      [256];
	va_list		ap;
	va_start(ap, fmt);
	vsnprintf(temp, sizeof(temp), fmt, ap);
	va_end(ap);
	char		topic     [101];
	snprintf(topic, sizeof(topic), "%s/%s/%s/%s", prefix, appname, hostname, suffix);
	mqtt.publish(topic, temp);
}
