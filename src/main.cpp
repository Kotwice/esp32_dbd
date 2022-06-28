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

#define DEBUG
#define C3 3
#define TCAADDR 0x70



#define HTML_OK 200
// #define json_append(object, json) for (auto& element: object) {json[element->first] = *(element->second.value);}

// SI5351
#define SI5351_ADDRESS_0 0x60
#define SI5351_ADDRESS_1 0x61

// Si5351 si5351(0x60);

int16_t SI5351_ADDRESS_ARRAY[6] = {SI5351_ADDRESS_0, SI5351_ADDRESS_0, SI5351_ADDRESS_0, 
	SI5351_ADDRESS_1, SI5351_ADDRESS_1, SI5351_ADDRESS_1,};

void initiate_si5351 () {
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

// void initialte_si5153 () {
// 	// bool i2c_found;
// 	// i2c_found = si5351.init(SI5351_CRYSTAL_LOAD_8PF, 0, 0);
// 	// if(!i2c_found) {Serial.println("Device SI5153 not found on I2C bus!");}
// 	// // si5351.output_enable(SI5351_CLK0, 1); 
// 	// // si5351.output_enable(SI5351_CLK1, 0);
// 	// // si5351.output_enable(SI5351_CLK2, 0);
// 	bool i2c_found;

//   // Start serial and initialize the Si5351

//   i2c_found = si5351.init(SI5351_CRYSTAL_LOAD_8PF, 0, 0);
//   if(!i2c_found)
//   {
//     Serial.println("Device not found on I2C bus!");
//   }

//   // Set CLK0 to output 14 MHz
//   si5351.set_freq(1400000000ULL, SI5351_CLK0);

//   // Set CLK1 to output 175 MHz
//   si5351.set_ms_source(SI5351_CLK1, SI5351_PLLB);
//   si5351.set_freq_manual(17500000000ULL, 70000000000ULL, SI5351_CLK1);

//   // Query a status update and wait a bit to let the Si5351 populate the
//   // status flags correctly.
//   si5351.update_status();
//   delay(500);
// }

void set_frequency (uint8_t channel, float value) {

	uint8_t device_index;
	device_index = (channel > 2) ? 1 : 0;

	Wire.beginTransmission(TCAADDR);
	Wire.write(1 << device_index);
	Wire.endTransmission();

	// Wire.beginTransmission(SI5351_ADDRESS_ARRAY[channel]);

	// 	Wire.write(3);
	// 	Wire.write(1 << channel);

	// uint8_t error = Wire.endTransmission(true);

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

	// if (channel == 0) {
	// 	si5351.set_freq(int(value*1e3), SI5351_CLK0);
	// }
	// if (channel == 1) {
	// 	si5351.set_freq(int(value*1e3), SI5351_CLK1);	
	// }
	// if (channel == 2) {
	// 	si5351.set_freq(int(value*1e3), SI5351_CLK2);	
	// }

}

int8_t 
	pin_spi_cs = 19,
	pin_spi_sck = 18,
	pin_spi_mosi = 5,
	pin_spi_miso = 2;

SPISettings settings(2000000, MSBFIRST, SPI_MODE0);

void set_voltage (uint8_t command, uint8_t address, float value) {
	SPI.beginTransaction(settings);
	digitalWrite(pin_spi_cs, LOW);
	SPI.transfer((command << 4) | address);
	SPI.transfer16(int(value) << 4);
	digitalWrite(pin_spi_cs, HIGH);
	SPI.endTransaction();
}

std::vector<float> frequency = {10,10,10,10,10,10}, voltage = {1,1,1,1,1,1,1,1};

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
				set_voltage(0b0011, channel, value);
			}
		}
	}
};

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

AsyncWebServer server(port);

struct WebFile {
    String url, path, type;
};

std::vector<String> pathes;

std::vector<WebFile> webfiles;

void get_pathes(fs::FS &fs, const char * dirname, uint8_t levels){
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
                get_pathes(fs, file.name(), levels - 1);
            }
        } else {
            pathes.push_back(file.name());
        }
        file = root.openNextFile();
    }
}

void initiate_file_system () {
    SPIFFS.begin();
    get_pathes(SPIFFS, "/", 0);
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

	initiate_file_system();

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

void setup() {

	Wire.begin();

    #ifdef DEBUG
		Serial.begin(115200);
	#endif
	
	pinMode(pin_spi_cs, OUTPUT);
	pinMode(pin_spi_sck, OUTPUT);
	pinMode(pin_spi_mosi, OUTPUT);
	pinMode(pin_spi_miso, INPUT);

	digitalWrite(pin_spi_cs, HIGH);
	digitalWrite(pin_spi_sck, LOW);
	digitalWrite(pin_spi_mosi, LOW);

	SPI.begin(pin_spi_sck, pin_spi_miso, pin_spi_mosi, pin_spi_cs);

	initiateWebServer();

	// initialte_si5153();
	// si5351.set_freq(1400000000ULL, SI5351_CLK0);
	// si5351.update_status();
	initiate_si5351();

}

void loop() {
//   // Read the Status Register and print it every 10 seconds
//   si5351.update_status();
//   Serial.print("SYS_INIT: ");
//   Serial.print(si5351.dev_status.SYS_INIT);
//   Serial.print("  LOL_A: ");
//   Serial.print(si5351.dev_status.LOL_A);
//   Serial.print("  LOL_B: ");
//   Serial.print(si5351.dev_status.LOL_B);
//   Serial.print("  LOS: ");
//   Serial.print(si5351.dev_status.LOS);
//   Serial.print("  REVID: ");
//   Serial.println(si5351.dev_status.REVID);

//   delay(10000);
}