#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <Wire.h>
#include <SPI.h>
#include <SPIFFS.h>
#include <ESPAsyncWebServer.h>
#include <functional>
#include <map>
#include <vector>
#include <si5351.h>
#include <Timer.h>
#include <cmath>

#define DEBUG // DEBUG MODE;
#define TCAADDR 0x70 // I2C address of TCA9548A;
#define SETVOL 0b0011

int8_t // pinout for ltc2636;
	pin_spi_cs = 19,
	pin_spi_sck = 18,
	pin_spi_mosi = 5,
	pin_spi_miso = 2;

int8_t // pinout for TCA9548A -> SI5351; (unchanged)
	pin_i2c_sda = 21,
	pin_i2c_cls = 22;

std::vector<float> frequency = {0,0,0,0,0,0}, voltage = {1000,500,700,800,0,0,0,0}, 
	frequency_dac = {1000,10,100,0,0,0,0,0}; // initial parameters;

std::vector<int> frequency_index = {0};

SPISettings settings(10000000, MSBFIRST, SPI_MODE0);

/* Sin generate */
uint32_t current_time;
float duration_control = 1;
bool state_control = true;

Timer timer_control(&current_time, &duration_control, &state_control);

/* WIFI SETTINGS */
struct wifi_net {

	const char* ssid = "esp32_dbd";
	const char* password = "qwerty123";
	const int channel = 1;
	const int ssid_hidden = 0;
	const int max_connection = 1;

} wifi_configuration;

const int port = 8080;
IPAddress local_ip(192, 168, 1, 1);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

#define HTML_OK 200

void initiate_ltc2636 () {

	pinMode(pin_spi_cs, OUTPUT);
	pinMode(pin_spi_sck, OUTPUT);
	pinMode(pin_spi_mosi, OUTPUT);
	pinMode(pin_spi_miso, INPUT);

	digitalWrite(pin_spi_cs, HIGH);
	digitalWrite(pin_spi_sck, LOW);
	digitalWrite(pin_spi_mosi, LOW);

	SPI.begin(pin_spi_sck, pin_spi_miso, pin_spi_mosi, pin_spi_cs);

}

void initiate_si5351 () {

	Wire.begin();

	Wire.beginTransmission(TCAADDR);
	Wire.write(1 << 0);
	Wire.endTransmission();
	si5351_Init(978);
	si5351_EnableOutputs((1<<0) | (1<<2));

	Wire.beginTransmission(TCAADDR);
	Wire.write(1 << 1);
	Wire.endTransmission();
	si5351_Init(978);
	si5351_EnableOutputs((1<<0) | (1<<2));
}

void set_frequency (uint8_t channel, float value) {

	uint8_t device_index;
	device_index = (channel > 2) ? 1 : 0;

	Wire.beginTransmission(TCAADDR);
	Wire.write(1 << device_index);
	Wire.endTransmission();

	if (channel == 0) {
		si5351_SetupCLK0(int(value*1e3), SI5351_DRIVE_STRENGTH_4MA);
		// si5351_EnableOutputs(1<<0);
	}
	if (channel == 2) {
		si5351_SetupCLK2(int(value*1e3), SI5351_DRIVE_STRENGTH_4MA);
		// si5351_EnableOutputs(1<<2);
	}
	if (channel == 3) {
		si5351_SetupCLK0(int(value*1e3), SI5351_DRIVE_STRENGTH_4MA);
		// si5351_EnableOutputs(1<<0);
	}
	if (channel == 5) {
		si5351_SetupCLK2(int(value*1e3), SI5351_DRIVE_STRENGTH_4MA);
		// si5351_EnableOutputs(1<<2);
	}

}

void set_voltage (uint8_t command, uint8_t address, float value) {
	SPI.beginTransaction(settings);
	digitalWrite(pin_spi_cs, LOW);
	SPI.transfer((command << 4) | address);
	SPI.transfer16(int(value) << 4);
	digitalWrite(pin_spi_cs, HIGH);
	SPI.endTransaction();
}

struct variables {
	std::vector<float>* value;
	std::function<void(float, int8_t)> function;
};

std::map<String, variables> parameters = {
	{"fm", {
		&frequency, 
		[] (float value, int8_t channel) {
				#ifdef DEBUG
					Serial.println("fm[" + String(channel) + "]=" + String(value));
				#endif
				set_frequency (channel, value);
			}
		}
	}, 
	{"dac", {
		&voltage, 
		[] (float value, int8_t channel) {
				#ifdef DEBUG
					Serial.println("dac[" + String(channel) + "]=" + String(value));
				#endif
				set_voltage(SETVOL, channel, value);
			}
		}
	},
	{"fdac", {
		&frequency_dac, 
		[] (float value, int8_t channel) {
				#ifdef DEBUG
					Serial.println("fdac[" + String(channel) + "]=" + String(value));
				#endif
				if (value == 0) {
					std::vector<int>::iterator position = std::find(frequency_index.begin(), frequency_index.end(), channel);
					if (position != frequency_index.end()) {
						frequency_index.erase(position);
					}
				}
				else {
					frequency_index.push_back(channel);
				}
			}
		}
	}
};

AsyncWebServer server(port);

struct WebFile {
    String url, path, type;
};

std::vector<String> pathes;

std::vector<WebFile> webfiles;

void getPathes(fs::FS &fs, const char * dirname, uint8_t levels){
    File root = fs.open(dirname);
    if(!root){
        return;
    }
    if(!root.isDirectory()){
        return;
    }
    File file = root.openNextFile();
    while(file){
        if(file.isDirectory()){
            if(levels){
                getPathes(fs, file.name(), levels - 1);
            }
        } else {
            pathes.push_back(file.name());
        }
        file = root.openNextFile();
    }
}

void initiateFileSystem () {
    SPIFFS.begin();
    getPathes(SPIFFS, "/", 0);
    WebFile temporary; 
    for (String path: pathes) {
        temporary.path = path;
        temporary.url = path;
        if (path.substring(path.lastIndexOf(".") + 1, path.length()) == "html") {
            if (path == "/index.html") {
                temporary.url = "/";
            } 
            temporary.type = "text/html";
        }
        if (path.substring(path.lastIndexOf(".") + 1, path.length()) == "css") {
            temporary.type = "text/css";
        }
        if (path.substring(path.lastIndexOf(".") + 1, path.length()) == "js") {
            temporary.type = "text/javascript";
        }
        webfiles.push_back(temporary);
    }
}

void initiateWebServer () {
	WiFi.mode(WIFI_AP);
	WiFi.softAP(wifi_configuration.ssid, wifi_configuration.password, wifi_configuration.channel, 
		wifi_configuration.ssid_hidden, wifi_configuration.max_connection);
	WiFi.softAPConfig(local_ip, gateway, subnet);
	#ifdef DEBUG
		Serial.printf("MAC address = %s\n", WiFi.softAPmacAddress().c_str());
	#endif
	WiFi.onEvent([] (WiFiEvent_t event, WiFiEventInfo_t info) {
		if (event == SYSTEM_EVENT_AP_STACONNECTED) {
			#ifdef DEBUG
				Serial.printf("Stations connected, number of soft-AP clients = %d\n", WiFi.softAPgetStationNum());
			#endif
		}
		if (event == SYSTEM_EVENT_AP_STADISCONNECTED) {
			#ifdef DEBUG
				Serial.printf("Stations disconnected, number of soft-AP clients = %d\n", WiFi.softAPgetStationNum());
			#endif
		}
	});

	initiateFileSystem();

    for (WebFile webfile: webfiles) {
        
        char url[webfile.url.length() + 1];

        webfile.url.toCharArray(url, webfile.url.length() + 1);

        if (webfile.url == "/index.html") {
            server.on("/", HTTP_GET, [webfile](AsyncWebServerRequest *request) {
                request->send(SPIFFS, webfile.path, String(), false);
            });  
        }
        else {
            server.on(url, HTTP_GET, [webfile](AsyncWebServerRequest *request) {
                request->send(SPIFFS, webfile.path, webfile.type);
            }); 
        }

    }

	server.on("/response", HTTP_GET, [](AsyncWebServerRequest *request) {		
		if (request->hasParam("json")) {
			DynamicJsonDocument json(1024);
			deserializeJson(json, request->getParam("json")->value());
			Serial.println("request=" + request->getParam("json")->value());
			JsonObject root = json.as<JsonObject>();
			for (JsonPair kv: root) {
				JsonObject root2 = kv.value().as<JsonObject>();
				String label = kv.key().c_str();
				JsonArray array_index = root2["index"];
				JsonArray array_value = root2["value"];
				for (int i = 0; i < array_value.size(); i++) {
					int8_t index = array_index[i].as<int>();
					float value = array_value[i].as<float>();
					std::vector<float>* vector = parameters[label].value;
					(*vector)[index] = value;
					parameters[label].function(value, index);
				}
			}
		}
		request->send(HTML_OK, "text/plain");
	});

	server.on("/request", HTTP_GET, [](AsyncWebServerRequest *request) {	
		DynamicJsonDocument json(2048);
		for (auto element: parameters) {
			std::vector<float> array = *(element.second.value);
			for (int8_t i = 0; i < array.size(); i++) {
				json[element.first][i] = array[i];
			}
		}
		String response;
		serializeJson(json, response);
		#ifdef DEBUG
			Serial.println(response);
		#endif
		request->send(HTML_OK, "application/json", response);
	});
	server.begin();
}

void initiate_parameters () {
	for (int i = 0; i < frequency.size(); i++) {set_frequency(i, frequency[i]);}
	for (int i = 0; i < voltage.size(); i++) {set_voltage(SETVOL, i, voltage[i]);}
}

void setup() {

    #ifdef DEBUG
		Serial.begin(115200);
	#endif

	initiateWebServer();

	initiate_ltc2636();

	initiate_si5351();

	initiate_parameters();

	timer_control.function = [] () {
		for (int i = 0; i < frequency_index.size(); i++) {
			set_voltage(SETVOL, i, voltage[frequency_index[i]] * 
				(1 - sin(2*3.14*frequency_dac[frequency_index[i]]*current_time*1e-6)));
		} 
	};

}

void loop() {
	current_time = micros();
	timer_control.update();
}