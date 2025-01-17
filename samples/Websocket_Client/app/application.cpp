/* Websocket Client Demo
 * By hrsavla https://github.com/hrsavla
 * 08/08/2015
 * This is a simple demo of Websocket client
 * Client tries to connect to echo.websocket.org
 * It sents 25 messages then client connection is closed.
 * It reconnects and sends 25 messages and continues doing same.
 *
 * This demo shows connection, closing , reconnection methods of
 * websocket client.
 */
#include <SmingCore.h>
#include <Network/WebsocketClient.h>

#ifndef WIFI_SSID
#define WIFI_SSID "PutSsidHere" // Put your SSID and password here
#define WIFI_PWD "PutPasswordHere"
#endif

//Uncomment next line to enable websocket binary transfer test
//#define WS_BINARY

WebsocketClient wsClient;
Timer msgTimer;
Timer restartTimer;

// Number of messages to send
const unsigned MESSAGES_TO_SEND = 10;

// Interval (in seconds) between sending of messages
const unsigned MESSAGE_INTERVAL = 1;

// Time (in seconds) to wait before restarting client and sending another group of messages
const unsigned RESTART_PERIOD = 20;

unsigned msg_cnt = 0;

#ifdef ENABLE_SSL
DEFINE_FSTR_LOCAL(ws_Url, "wss://echo.websocket.org");
#else
DEFINE_FSTR_LOCAL(ws_Url, "ws://echo.websocket.org");
#endif /* ENABLE_SSL */

void wsMessageSent();

void wsConnected(WebsocketConnection& wsConnection)
{
	Serial.printf(_F("Start sending messages every %u second(s)...\r\n"), MESSAGE_INTERVAL);
	msgTimer.initializeMs(MESSAGE_INTERVAL * 1000, wsMessageSent);
	msgTimer.start();
}

void wsMessageReceived(WebsocketConnection& wsConnection, const String& message)
{
	Serial.printf(_F("WebSocket message received: %s\r\n"), message.c_str());
	Serial.printf(_F("Free Heap: %u\r\n"), system_get_free_heap_size());
}

void wsBinReceived(WebsocketConnection& wsConnection, uint8_t* data, size_t size)
{
	Serial.println(_F("WebSocket BINARY received"));
	for(uint8_t i = 0; i < size; i++) {
		Serial.printf("wsBin[%u] = 0x%02X\r\n", i, data[i]);
	}

	Serial.print(_F("Free Heap: "));
	Serial.println(system_get_free_heap_size());
}

void restart()
{
	Serial.println("restart...");

	msg_cnt = 0;
	wsClient.connect(String(ws_Url));
}

void wsDisconnected(WebsocketConnection& wsConnection)
{
	Serial.printf(_F("Restarting websocket client after %u seconds\r\n"), RESTART_PERIOD);
	msgTimer.setCallback(restart);
	msgTimer.setIntervalMs(RESTART_PERIOD * 1000);
	msgTimer.startOnce();
}

void wsMessageSent()
{
	if(!WifiStation.isConnected()) {
		// Check if Esp8266 is connected to router
		return;
	}

	if(msg_cnt > MESSAGES_TO_SEND) {
		Serial.println(_F("End Websocket client session"));
		msgTimer.stop();
		wsClient.close(); // clean disconnect.

		return;
	}

#ifndef WS_BINARY
	String message = F("Hello ") + String(msg_cnt++);
	Serial.print(_F("Sending websocket message: "));
	Serial.println(message);
	wsClient.sendString(message);
#else
	uint8_t buf[] = {0xF0, 0x00, 0xF0};
	buf[1] = msg_cnt++;
	Serial.println(_F("Sending websocket binary buffer"));
	for(uint8_t i = 0; i < 3; i++) {
		Serial.printf("wsBin[%u] = 0x%02X\r\n", i, buf[i]);
	}

	wsClient.sendBinary(buf, 3);
#endif
}

void STAGotIP(IpAddress ip, IpAddress mask, IpAddress gateway)
{
	Serial.print(_F("GOTIP - IP: "));
	Serial.print(ip);
	Serial.print(_F(", MASK: "));
	Serial.print(mask);
	Serial.print(_F(", GW: "));
	Serial.println(gateway);

	Serial.print(_F("Connecting to Websocket Server "));
	Serial.println(ws_Url);

	wsClient.setMessageHandler(wsMessageReceived);
	wsClient.setBinaryHandler(wsBinReceived);
	wsClient.setDisconnectionHandler(wsDisconnected);
	wsClient.setConnectionHandler(wsConnected);
	wsClient.setSslInitHandler([](Ssl::Session& session) { session.options.verifyLater = true; });
	wsClient.connect(String(ws_Url));
}

void STADisconnect(const String& ssid, MacAddress bssid, WifiDisconnectReason reason)
{
	Serial.print(_F("DISCONNECT - SSID: "));
	Serial.print(ssid);
	Serial.print(_F(", REASON: "));
	Serial.println(WifiEvents.getDisconnectReasonDesc(reason));
}

void init()
{
	Serial.begin(COM_SPEED_SERIAL);
	Serial.systemDebugOutput(true);
	WifiAccessPoint.enable(false);

	WifiStation.config(WIFI_SSID, WIFI_PWD);
	WifiStation.enable(true);

	WifiEvents.onStationGotIP(STAGotIP);
	WifiEvents.onStationDisconnect(STADisconnect);
}
