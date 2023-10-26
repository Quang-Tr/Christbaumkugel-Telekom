#include <Adafruit_NeoPixel.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <esp_wifi.h>  // turn off to save energy
#include <set> // somehow must include <set>, can't just use std::set like std::map

#define LED_PIN 14
#define LED_COUNT 8
#define BRIGHTNESS 100
#define LONG_DELAY 250
#define SHORT_DELAY 10
#define SERVICE_UUID "f54ac316-a0b1-41ac-8916-ba0807dc3a4a"
#define CHAR_CTRL_UUID "eaa80fc3-1054-4f2c-9b3e-b662971698c9"
#define CHAR_MODE_UUID "90706889-893c-430f-ae9c-c9f98f10fec2"
BLEDevice device;
BLEServer* pServer;
BLEService* pService;
BLECharacteristic* pCharCtrl; // on off
BLECharacteristic* pCharMode; // change LED mode
BLEAdvertising* pAdvertising;

Adafruit_NeoPixel stripe(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
std::map<std::string, uint32_t> colors = {
  {"red", stripe.Color(255, 0, 0)},
  {"orange", stripe.Color(255, 127, 0)},
  {"yellow", stripe.Color(255, 255, 0)},
  {"green", stripe.Color(0, 255, 0)},
  {"blue", stripe.Color(0, 0, 255)},
  {"indigo", stripe.Color(75, 0, 130)},
  {"violet", stripe.Color(148, 0, 211)},
  {"white", stripe.Color(255, 255, 255)}
};

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    Serial.println("Client connected");
  }
  void onDisconnect(BLEServer* pServer) {
    Serial.println("Client disconnected");
  }
};

class CharacteristicCallbacks : public BLECharacteristicCallbacks {
  std::set<std::string> ctrlCmds = {"on", "off"};
  std::set<std::string> modeCmds = [colors] {
    std::set<std::string> cmds = {"allcolors", "bounce", "rainbow", "idle"};
    for (auto const& kvPair : colors) {
      cmds.insert(kvPair.first);
      cmds.insert("train," + kvPair.first);
    }
    return cmds;
  } (); // immediately invoked function expression (IIFE)

  void onRead(BLECharacteristic* pCharacteristic) {
    std::string name = pCharacteristic->getUUID().toString();
    std::string value = pCharacteristic->getValue();
    if (name == CHAR_CTRL_UUID & !ctrlCmds.count(value)) {
      pCharacteristic->setValue("off");
    } else if (name == CHAR_MODE_UUID & !modeCmds.count(value)) {
      pCharacteristic->setValue("idle");
    }
  }

  void onWrite(BLECharacteristic* pCharacteristic) {
    std::string name, error;
    std::string value = pCharacteristic->getValue();
    if (pCharacteristic->getUUID().toString() == CHAR_CTRL_UUID) {
      name = "Ctrl ";
      if (!ctrlCmds.count(value)) {
        error = " not supported, default to \"off\"";
      }
    } else {
      name = "Mode ";
      if (!modeCmds.count(value)) {
        error = " not supported, default to \"idle\"";
      }
    }
    Serial.println((name + value + error).c_str());
  }
};

void setup() {
  Serial.begin(115200);
  device.init("bauble");
  pServer = device.createServer();
  pService = pServer->createService(SERVICE_UUID);
  pCharCtrl = pService->createCharacteristic(CHAR_CTRL_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_READ);
  pCharMode = pService->createCharacteristic(CHAR_MODE_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_READ);
  pCharCtrl->setValue("on"); // more verbose than bool
  pCharMode->setValue("idle"); // on nRF app change to UTF-8

  pServer->setCallbacks(new ServerCallbacks());
  pCharCtrl->setCallbacks(new CharacteristicCallbacks());
  pCharMode->setCallbacks(new CharacteristicCallbacks());

  pService->start();
  pAdvertising = device.getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMaxPreferred(0x12);
  device.startAdvertising();

  stripe.begin();
  stripe.show(); // all LEDs off
}

void loop() {
  if (pCharCtrl->getValue() == "on") {
    std::string mode = pCharMode->getValue();

    if (colors.count(mode)) { // color's name matches
      fill1Color(colors[mode]);
    }

    else if (mode == "allcolors") {
      for (auto const& kvPair : colors) {
        fill1Color(kvPair.second);
        delay(LONG_DELAY);
      }
    }

    else if (mode == "bounce") {
      uint32_t color;
      auto set1Pixel = [&color] (uint8_t pos) { // lambda expression
        if (pos == 0 | pos == LED_COUNT - 1) { // change color at both ends
          color = stripe.Color(rand() % 256, rand() % 256, rand() % 256);
        }
        stripe.clear();
        stripe.setPixelColor(pos, color);
        stripe.setBrightness(BRIGHTNESS);
        stripe.show();
        delay(LONG_DELAY);
      };
      for (uint8_t pos = 0; pos < LED_COUNT - 1; pos++) {
        set1Pixel(pos);
      }
      for (uint8_t pos = LED_COUNT - 1; pos > 0; pos--) {
        set1Pixel(pos);
      }
    }

    else if (mode.substr(0, 5) == "train") { // e.g., train,white no whitespace
      std::string color = mode.substr(6); // after comma to the end
      if (!colors.count(color)) {
        color = "white";
      }
      uint8_t bitPattern = 0b00110011; // for 8 LEDs
      for (uint8_t shift = 0; shift < 4; shift++) { // 00110011, 10011001, 11001100, 01100110
        bitPattern = (bitPattern >> 1) | (bitPattern << 7); // shift right with wrap around
        colorBitPattern(bitPattern, colors[color]);
        delay(LONG_DELAY);
      }
    }

    else if (mode == "rainbow") { // canon example strandtest in Adafruit NeoPixel
      for (long firstPixelHue = 0; firstPixelHue < 3 * 65536; firstPixelHue += 256) { // color wheel rolls over at 65536
        stripe.rainbow(firstPixelHue); // stripe.rainbow(firstPixelHue, 1, 255, 255, true);
        stripe.show();
        delay(SHORT_DELAY);
      }
    }

    else { // Mode idle
      for (uint8_t brightness = 0; brightness < 255; brightness++) { // 255 prevents uint8_t overflow
        stripe.fill(colors["white"], 0, LED_COUNT);
        stripe.setBrightness(stripe.gamma8(stripe.sine8(brightness))); // perceptially smooth
        stripe.show();
        delay(SHORT_DELAY);
      }
    }
  }

  stripe.clear(); // Ctrl off, can be interrupt to turn off during animation
  stripe.show();
}

void fill1Color(uint32_t color) {
  stripe.fill(color, 0, LED_COUNT);
  stripe.setBrightness(BRIGHTNESS);
  stripe.show();
}

void colorBitPattern(uint8_t bitPattern, uint32_t color) {
  stripe.clear();
  for (uint8_t pos = 0; pos < 8; pos++) {
    if (bitPattern & 1) { // last bit
      stripe.setPixelColor(pos, color);
    }
    bitPattern = bitPattern >> 1;
  }
  stripe.setBrightness(BRIGHTNESS);
  stripe.show();
}