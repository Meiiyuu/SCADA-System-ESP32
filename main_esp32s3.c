////////// ESP 32 S3 //////////////
//////////  Nextion /////////
// Nodo encargado de recibir y visualizar la información en la pantalla NEXTION para los operadores del almacén.

// --------------------- LIBRERIAS ---------------------
// Librerias estandar
#include <stdio.h>
#include <string.h>

// Drivers de hardware
#include "driver/uart.h"

// Logging y temporizacion
#include "esp_log.h"
#include "esp_timer.h"

// FreeRTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// WiFi y red
#include "esp_wifi.h"
#include "esp_sntp.h"
#include "time.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"

// --------------------- CONFIGURACION UART ---------------------
// UART utilizada para la pantalla Nextion
#define UART_NEXTION UART_NUM_2
#define NEXTION_TX 17
#define NEXTION_RX 16

// UART utilizada para comunicacion con sensores
#define UART_SENSOR UART_NUM_1
#define SENSOR_TX 5
#define SENSOR_RX 4

// Tamaño del buffer de recepcion UART
#define BUF_SIZE 1024

/* ============================================================
   FUNCIÓN: initUART
   DESCRIPCIÓN:
   Inicializa una UART con los parámetros básicos:
   - Baudrate
   - Pines TX y RX
   - Configuración estándar 
   ============================================================ */
void initUART(int uart_num, int tx, int rx, int baud) {
    uart_config_t config = {
        .baud_rate = baud,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(uart_num, &config);
    uart_set_pin(uart_num, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(uart_num, BUF_SIZE, 0, 0, NULL, 0);
}

/* ============================================================
   FUNCIÓN: sendText
   DESCRIPCIÓN:
   Envía texto a un objeto de la pantalla Nextion.
   FORMATO:
   objeto.txt="valor"
   IMPORTANTE:
   Nextion requiere terminar cada comando con 0xFF 0xFF 0xFF
   ============================================================ */
void sendText(const char *obj, const char *val) {
    char cmd[100];

    // Construcción del comando
    sprintf(cmd, "%s.txt=\"%s\"", obj, val);

    // Envío por UART
    uart_write_bytes(UART_NEXTION, cmd, strlen(cmd));

    // Secuencia de finalización obligatoria
    uint8_t end[3] = {0xFF, 0xFF, 0xFF};
    uart_write_bytes(UART_NEXTION, (const char *)end, 3);
}

// --------------------- CONFIGURACIÓN WiFi ---------------------
#define WIFI_SSID "Sofi"
#define WIFI_PASS "hola1234"

/* ============================================================
   FUNCIÓN: wifi_event_handler
   DESCRIPCIÓN:
   Maneja eventos del WiFi:
   - Inicio → intenta conectar
   - Desconexión → reconecta automáticamente
   - Obtención de IP → muestra dirección asignada
   ============================================================ */
void wifi_event_handler(void* arg, esp_event_base_t event_base,
                        int32_t event_id, void* event_data) {

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    }

    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI("WIFI", "Reconectando...");
        esp_wifi_connect();
    }

    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI("WIFI", "Conectado! IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

/* ============================================================
   FUNCIÓN: initWiFi
   DESCRIPCIÓN:
   Inicializa el WiFi en modo estación (STA) y establece
   la conexión con la red configurada.
   ============================================================ */
void initWiFi() {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    // Registro de eventos
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);

    esp_wifi_set_mode(WIFI_MODE_STA);

    // Configuración de credenciales
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS
        },
    };

    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
}

// --------------------- CONFIGURACIÓN SNTP ---------------------
/* ============================================================
   FUNCIÓN: initSNTP
   DESCRIPCIÓN:
   Inicializa el cliente SNTP para sincronizar la hora
   desde un servidor NTP (pool.ntp.org).
   ============================================================ */
void initSNTP() {
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
}

/* ============================================================
   FUNCIÓN: getDateTime
   DESCRIPCIÓN:
   Obtiene la fecha y hora actuales del sistema y las
   formatea en cadenas de texto.
   ============================================================ */
void getDateTime(char *dateStr, char *timeStr) {
    time_t now;
    struct tm timeinfo;

    time(&now);
    localtime_r(&now, &timeinfo);

    strftime(timeStr, 16, "%H:%M:%S", &timeinfo);
    strftime(dateStr, 16, "%d/%m/%Y", &timeinfo);
}

/* ============================================================
   FUNCIÓN PRINCIPAL
   ============================================================ */
void app_main(void)
{
    // Inicialización de UARTs
    initUART(UART_NEXTION, NEXTION_TX, NEXTION_RX, 9600);
    initUART(UART_SENSOR, SENSOR_TX, SENSOR_RX, 115200);

    // Buffers y variables
    uint8_t data[128];
    char timer_val[16];
    char date_val[16];

    // Inicialización de memoria NVS (necesaria para WiFi)
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Inicialización de red y tiempo
    initWiFi();
    initSNTP();

    // Configuración de zona horaria (Colombia UTC-5)
    setenv("TZ", "COT-5", 1);
    tzset();

    // Variable para detectar pérdida de datos del sensor
    int64_t last_data_time = esp_timer_get_time();

    while (1)
    {
        // --------------------- LECTURA DE SENSORES ---------------------
        int len = uart_read_bytes(UART_SENSOR, data, sizeof(data)-1, pdMS_TO_TICKS(100));

        if (len > 0)
        {
            last_data_time = esp_timer_get_time();

            data[len] = '\0';
            ESP_LOGI("UART", "Recibido: %s", data);

            // Variables de sensores
            float t = 0, h = 0, p = 0;
            int co2 = 0, tvoc = 0;

            // Parseo del string recibido
            int parsed = sscanf((char*)data, "T:%f,H:%f,P:%f,CO2:%d,TVOC:%d",
                                &t, &h, &p, &co2, &tvoc);

            // Validación de datos
            if (parsed != 5) {
                ESP_LOGW("UART", "Datos incompletos o corruptos");
                t = h = p = 0;
                co2 = tvoc = 0;
            }

            // Conversión a strings
            char t_str[10], h_str[10], p_str[10], co2_str[10];

            sprintf(t_str, "%.2fC", t);
            sprintf(h_str, "%.2f%%", h);
            sprintf(p_str, "%.2fhPa", p);
            sprintf(co2_str, "%dppm", co2);

            // --------------------- CLASIFICACIÓN DE CALIDAD ---------------------
            char state_temp[10], state_hum[10], state_pres[10], state_gas[10];

            // Temperatura
            if (t >= 20 && t <= 25) strcpy(state_temp, "GOOD");
            else if ((t >= 16 && t < 20) || (t > 25 && t <= 30)) strcpy(state_temp, "OK");
            else strcpy(state_temp, "BAD");

            // Humedad
            if (h >= 40 && h <= 60) strcpy(state_hum, "GOOD");
            else if ((h >= 30 && h < 40) || (h > 60 && h <= 70)) strcpy(state_hum, "OK");
            else strcpy(state_hum, "BAD");

            // Presión
            if (p >= 1000 && p <= 1020) strcpy(state_pres, "GOOD");
            else if ((p >= 980 && p < 1000) || (p > 1020 && p <= 1040)) strcpy(state_pres, "OK");
            else strcpy(state_pres, "BAD");

            // CO2
            if (co2 < 800) strcpy(state_gas, "GOOD");
            else if (co2 <= 1200) strcpy(state_gas, "OK");
            else strcpy(state_gas, "BAD");

            // Envío a pantalla Nextion
            sendText("temp", t_str);
            sendText("humidity", h_str);
            sendText("pressure", p_str);
            sendText("gas", co2_str);

            sendText("state_temp", state_temp);
            sendText("state_hum", state_hum);
            sendText("state_pres", state_pres);
            sendText("state_gas", state_gas);
        }

        // --------------------- DETECCIÓN DE FALLO DE SENSOR ---------------------
        int64_t now = esp_timer_get_time();

        if ((now - last_data_time) > 2000000) {
            ESP_LOGW("UART", "Sensor desconectado o sin datos");

            sendText("temp", "0C");
            sendText("humidity", "0%");
            sendText("pressure", "0hPa");
            sendText("gas", "0ppm");

            sendText("state_temp", "BAD");
            sendText("state_hum", "BAD");
            sendText("state_pres", "BAD");
            sendText("state_gas", "BAD");
        }

        // --------------------- LECTURA DE EVENTOS NEXTION ---------------------
        int len_nextion = uart_read_bytes(UART_NEXTION, data, sizeof(data), pdMS_TO_TICKS(10));

        if (len_nextion > 0) {
            printf("NEXTION HEX: ");
            for (int i = 0; i < len_nextion; i++) {
                printf("%02X ", data[i]);
            }
            printf("\n");

            // Evento: botón presionado
            if (data[0] == 0x65 && data[1] == 0x00) {
                ESP_LOGI("EVENTO", "BOTON PRESIONADO EN NEXTION");

                // Notificación al sistema de sensores
                char msg[] = "BTN:PRESSED";
                uart_write_bytes(UART_SENSOR, msg, strlen(msg));
            }
        }

        // --------------------- ACTUALIZACIÓN DE FECHA Y HORA ---------------------
        getDateTime(date_val, timer_val);
        sendText("timer", timer_val);
        sendText("date", date_val);

        // Delay del ciclo principal
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}