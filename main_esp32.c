/////////////////// ESP 32 ////////////////
// Nodo encargado de adquirir datos de sensores ambientales (BME280 y CCS811)
// --------------------- LIBRERÍAS ---------------------
// Librerías estándar
#include <stdio.h>
#include <string.h>

// FreeRTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Logging
#include "esp_log.h"

// Drivers de hardware
#include "driver/uart.h"
#include "driver/gpio.h"

// Sensor BME280 (temperatura, humedad, presión)
#include "bme280.h"

// Sensor CCS811 (calidad de aire: eCO2 y TVOC)
#include "i2cdev.h"
#include "ccs811.h"

// --------------------- CONFIGURACIÓN I2C ---------------------
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_SDA 22
#define I2C_MASTER_SCL 21
#define I2C_MASTER_FREQ_HZ 100000

// Dirección I2C del BME280
#define BME280_I2C_ADDR 0x77  

// --------------------- CONFIGURACIÓN UART ---------------------
#define UART_PORT UART_NUM_1
#define TXD_PIN GPIO_NUM_5
#define RXD_PIN GPIO_NUM_4

// --------------------- CONFIGURACIÓN LED ---------------------
#define LED_GPIO GPIO_NUM_2

// Tag para logs
static const char *TAG = "ESP_SENSORES";

/* ============================================================
   FUNCIONES DE INTERFAZ I2C PARA BME280
   Estas funciones permiten adaptar el driver del BME280
   al sistema I2C del ESP32
   ============================================================ */

/* Delay en microsegundos requerido por el driver */
void user_delay_us(uint32_t period, void *intf_ptr)
{
    vTaskDelay(pdMS_TO_TICKS(period / 1000));
}

/* Escritura en registros I2C */
int8_t user_i2c_write(uint8_t reg_addr, const uint8_t *reg_data,
                      uint32_t len, void *intf_ptr)
{
    i2c_dev_t *dev = (i2c_dev_t *)intf_ptr;

    I2C_DEV_TAKE_MUTEX(dev);
    I2C_DEV_CHECK(dev, i2c_dev_write_reg(dev, reg_addr, reg_data, len));
    I2C_DEV_GIVE_MUTEX(dev);

    return BME280_OK;
}

/* Lectura de registros I2C */
int8_t user_i2c_read(uint8_t reg_addr, uint8_t *reg_data,
                     uint32_t len, void *intf_ptr)
{
    i2c_dev_t *dev = (i2c_dev_t *)intf_ptr;

    I2C_DEV_TAKE_MUTEX(dev);
    I2C_DEV_CHECK(dev, i2c_dev_read_reg(dev, reg_addr, reg_data, len));
    I2C_DEV_GIVE_MUTEX(dev);

    return BME280_OK;
}

/* ============================================================
   FUNCIÓN PRINCIPAL
   ============================================================ */
void app_main(void)
{
    ESP_LOGI(TAG, "Inicializando sensores...");

    // --------------------- INICIALIZACIÓN UART ---------------------
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    uart_driver_install(UART_PORT, 1024, 0, 0, NULL, 0);
    uart_param_config(UART_PORT, &uart_config);
    uart_set_pin(UART_PORT, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    // --------------------- INICIALIZACIÓN I2C ---------------------
    ESP_ERROR_CHECK(i2cdev_init());

    // --------------------- CONFIGURACIÓN LED ---------------------
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };

    gpio_config(&io_conf);

    // LED apagado inicialmente
    gpio_set_level(LED_GPIO, 0);

    /* ============================================================
       INICIALIZACIÓN SENSOR CCS811 (CALIDAD DE AIRE)
       ============================================================ */
    ccs811_dev_t ccs811_dev;
    memset(&ccs811_dev, 0, sizeof(ccs811_dev_t));

    // Espera de arranque del sensor
    vTaskDelay(pdMS_TO_TICKS(250));

    ESP_ERROR_CHECK(ccs811_init_desc(&ccs811_dev, CCS811_I2C_ADDRESS_2,
                                     I2C_MASTER_NUM, I2C_MASTER_SDA, I2C_MASTER_SCL));

    // Activación de resistencias pull-up
    ccs811_dev.i2c_dev.cfg.sda_pullup_en = true;
    ccs811_dev.i2c_dev.cfg.scl_pullup_en = true;

    // Inicialización y modo de medición (cada 1 segundo)
    ccs811_init(&ccs811_dev);
    ccs811_set_mode(&ccs811_dev, CCS811_MODE_1S);

    /* ============================================================
       INICIALIZACIÓN SENSOR BME280
       ============================================================ */
    i2c_dev_t bme280_i2c_dev;
    memset(&bme280_i2c_dev, 0, sizeof(i2c_dev_t));

    // Configuración del bus I2C
    bme280_i2c_dev.port = I2C_MASTER_NUM;
    bme280_i2c_dev.addr = BME280_I2C_ADDR;
    bme280_i2c_dev.cfg.sda_io_num = I2C_MASTER_SDA;
    bme280_i2c_dev.cfg.scl_io_num = I2C_MASTER_SCL;
    bme280_i2c_dev.cfg.master.clk_speed = I2C_MASTER_FREQ_HZ;
    bme280_i2c_dev.cfg.sda_pullup_en = true;
    bme280_i2c_dev.cfg.scl_pullup_en = true;

    i2c_dev_create_mutex(&bme280_i2c_dev);

    struct bme280_dev dev;
    dev.intf = BME280_I2C_INTF;
    dev.intf_ptr = &bme280_i2c_dev;
    dev.read = user_i2c_read;
    dev.write = user_i2c_write;
    dev.delay_us = user_delay_us;

    bme280_init(&dev);

    // Configuración del sensor
    struct bme280_settings settings;

    settings.osr_h = BME280_OVERSAMPLING_1X;
    settings.osr_p = BME280_OVERSAMPLING_16X;
    settings.osr_t = BME280_OVERSAMPLING_2X;
    settings.filter = BME280_FILTER_COEFF_16;
    settings.standby_time = BME280_STANDBY_TIME_62_5_MS;

    uint8_t settings_sel = BME280_SEL_OSR_PRESS |
                           BME280_SEL_OSR_TEMP |
                           BME280_SEL_OSR_HUM |
                           BME280_SEL_FILTER |
                           BME280_SEL_STANDBY;

    bme280_set_sensor_settings(settings_sel, &settings, &dev);
    bme280_set_sensor_mode(BME280_POWERMODE_NORMAL, &dev);

    // Variables de medición
    struct bme280_data bme_data;
    uint16_t eco2, tvoc;

    /* ============================================================
       BUCLE PRINCIPAL
       ============================================================ */
    while (1)
    {
        // --------------------- LECTURA DE SENSORES ---------------------
        bme280_get_sensor_data(BME280_ALL, &bme_data, &dev);

        // Lectura del CCS811
        if (ccs811_get_results(&ccs811_dev, &tvoc, &eco2, NULL, NULL) != ESP_OK) {
            eco2 = 0;
            tvoc = 0;
        }

        // --------------------- FORMATO DE DATOS ---------------------
        char msg[128];

        sprintf(msg, "T:%.2f,H:%.2f,P:%.2f,CO2:%d,TVOC:%d\n",
                bme_data.temperature,
                bme_data.humidity,
                bme_data.pressure / 100.0,  // Conversión a hPa
                eco2,
                tvoc);

        // --------------------- ENVÍO POR UART ---------------------
        uart_write_bytes(UART_PORT, msg, strlen(msg));
        printf("Enviado: %s", msg);

        // --------------------- RECEPCIÓN DE COMANDOS ---------------------
        uint8_t data[128];
        int len = uart_read_bytes(UART_PORT, data, sizeof(data)-1, pdMS_TO_TICKS(100));

        if (len > 0) {
            data[len] = '\0';

            ESP_LOGI("UART", "Recibido: %s", data);

            // Acción al recibir evento de botón desde la otra ESP
            if (strstr((char*)data, "BTN:PRESSED")) {
                ESP_LOGI("ACCION", "Botón presionado en la otra ESP");

                // Encender LED temporalmente
                gpio_set_level(LED_GPIO, 1);
                vTaskDelay(pdMS_TO_TICKS(500));
                gpio_set_level(LED_GPIO, 0);
            }
        }

        // Periodo de muestreo (2 segundos)
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}