#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "SCServo.h"

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");

#define WIFI_SSID "ESP32-GRIP"
#define WIFI_PASS "Gubami_2023"
#define MAX_STA_CONN 1
#define TAG "ESP32_SERVER"

SMS_STS sms_sts;

// Константи
const int OPEN_POS = 2000;
const int CLOSE_POS = 3200;
const int BUTTON_PIN = 22;

// Створення UART-порту (GPIO4 - TX, GPIO5 - RX)
HardwareSerial SCSerial(1);

// FreeRTOS task handles
TaskHandle_t servoTaskHandle = NULL;

// Serve main HTML
esp_err_t root_get_handler(httpd_req_t *req)
{
    const uint8_t *html = index_html_start;
    size_t len = index_html_end - index_html_start;

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)html, len);
    return ESP_OK;
}

// Serve JSON status
esp_err_t status_handler(httpd_req_t *req)
{
    char resp[256];

    int rawCurrent = sms_sts.ReadCurrent(1);
    int position = sms_sts.ReadPos(1);
    int voltage = sms_sts.ReadVoltage(1);
    int torque = sms_sts.ReadLoad(1);
    int temperature = sms_sts.ReadTemper(1);

    snprintf(resp, sizeof(resp),
             R"({"current":%d,"position":%d,"voltage":%.1f,"torque":%d,"temperature":%d})",
             rawCurrent * 10, position, voltage / 10.0, torque, temperature);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_uri_t root_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_get_handler};
        httpd_register_uri_handler(server, &root_uri);

        httpd_uri_t status_uri = {
            .uri = "/api/status",
            .method = HTTP_GET,
            .handler = status_handler};
        httpd_register_uri_handler(server, &status_uri);
    }
    return server;
}

void wifi_init_softap()
{
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {};
    strcpy((char *)wifi_config.ap.ssid, WIFI_SSID); // SSID точки доступу
    wifi_config.ap.ssid_len = strlen(WIFI_SSID);
    wifi_config.ap.max_connection = MAX_STA_CONN;
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;       // Встановлення типу аутентифікації (WPA2-Personal)
    strcpy((char *)wifi_config.ap.password, WIFI_PASS); // Пароль для точки доступу
    wifi_config.ap.ssid_hidden = 0;                     // SSID буде видимим
    wifi_config.ap.beacon_interval = 100;               // Інтервал сигналу

    // Якщо пароль порожній, то точка доступу працюватиме без пароля
    if (strlen(WIFI_PASS) == 0)
    {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN; // Без пароля, відкритий доступ
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Access Point started. SSID: %s", WIFI_SSID);
}

// Function for servo control
void servoControlTask(void *parameter)
{
    int previousButtonState = digitalRead(BUTTON_PIN); // Initialize with default state (not pressed)

    while (true)
    {
        int buttonState = digitalRead(BUTTON_PIN);

        if (buttonState == HIGH)
        {
            if (buttonState != previousButtonState)
                printf("Button pressed!\n");
            sms_sts.WritePosEx(1, CLOSE_POS, 1500, 50);
        }
        else
        {
            if (buttonState != previousButtonState)
                printf("Button released!\n");
            sms_sts.WritePosEx(1, OPEN_POS, 1500, 50);
        }
        previousButtonState = buttonState; // Update the previous state

        vTaskDelay(100 / portTICK_PERIOD_MS); // Delay to prevent task hogging
    }
}

extern "C" void app_main(void)
{
    // Init NVS, network, WiFi
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    initArduino();
    SCSerial.begin(1000000, SERIAL_8N1, 5, 4);
    sms_sts.pSerial = &SCSerial;
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    wifi_init_softap();
    start_webserver();

    xTaskCreate(servoControlTask, "ServoControlTask", 2048, NULL, 1, NULL);
}
