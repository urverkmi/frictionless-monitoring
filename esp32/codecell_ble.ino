/*
 * Code Cell C6 — BLE-controlled activate line
 *
 * Replaces the MicroLink app: the Raspberry Pi connects over BLE
 * (Nordic UART Service) and writes a single ASCII byte:
 *   "1" = ON
 *   "0" = OFF
 *
 * Advertised as "SNOOPY". Fire-and-forget — the Pi does not subscribe
 * to notifications. A TX characteristic is declared for completeness.
 *
 * Library: NimBLE-Arduino (h2zero/NimBLE-Arduino)
 */

#include <NimBLEDevice.h>

static const char *DEVICE_NAME = "EMMA";
static const char *SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const char *RX_CHAR_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
static const char *TX_CHAR_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";

const int ACTIVATE_PIN = 2;

NimBLECharacteristic *txChar = nullptr;

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *server, NimBLEConnInfo &info) override {
        Serial.println("[BLE] Connected");
    }
    void onDisconnect(NimBLEServer *server, NimBLEConnInfo &info, int reason) override {
        Serial.printf("[BLE] Disconnected (reason %d) — re-advertising\n", reason);
        NimBLEDevice::startAdvertising();
    }
};

class RxCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *chr, NimBLEConnInfo &info) override {
        std::string data = chr->getValue();
        if (data.empty()) return;

        char cmd = data[0];
        if (cmd == '1') {
            Serial.println("[BLE] RX: 1 (ON)");
            // TODO: define ON behavior for the real Code Cell payload.
            digitalWrite(ACTIVATE_PIN, HIGH);
        } else if (cmd == '0') {
            Serial.println("[BLE] RX: 0 (OFF)");
            // TODO: define OFF behavior for the real Code Cell payload.
            digitalWrite(ACTIVATE_PIN, LOW);
        }
    }
};

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("\n--- Code Cell C6 BLE activate ready ---");

    pinMode(ACTIVATE_PIN, OUTPUT);
    digitalWrite(ACTIVATE_PIN, LOW);

    NimBLEDevice::init(DEVICE_NAME);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    NimBLEServer *server = NimBLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());

    NimBLEService *service = server->createService(SERVICE_UUID);

    NimBLECharacteristic *rxChar = service->createCharacteristic(
        RX_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    rxChar->setCallbacks(new RxCallbacks());

    txChar = service->createCharacteristic(
        TX_CHAR_UUID,
        NIMBLE_PROPERTY::NOTIFY);

    service->start();

    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(SERVICE_UUID);
    adv->setName(DEVICE_NAME);
    adv->enableScanResponse(true);
    NimBLEDevice::startAdvertising();

    Serial.printf("[BLE] Advertising as \"%s\"\n", DEVICE_NAME);
}

void loop() {
    delay(1000);
}
