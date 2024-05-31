/*
Simple program to mock the FPGA data source, which acts as an SPI master
and send a 24bit packet every 8 micro seconds.
*/
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#include <rom/ets_sys.h> // for microsecond delay

#define GPIO_MOSI 12
#define GPIO_MISO 13
#define GPIO_SCLK 15
#define GPIO_CS 14

///Data is a single float in 24 bits 
#define FPGA_SOURCE_CLOCK_SPEED 6000000

static const char *TAG = "esp32_sd_spi";

void app_main(void)
{
    esp_err_t ret;
    spi_device_handle_t handle;

    // Configuration for the SPI bus
    spi_bus_config_t buscfg = {
        .mosi_io_num = GPIO_MOSI,
        .miso_io_num = GPIO_MISO,
        .sclk_io_num = GPIO_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1};

    // Configuration for the SPI device on the other side of the bus
    spi_device_interface_config_t devcfg = {
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
        .clock_speed_hz = FPGA_SOURCE_CLOCK_SPEED,
        .duty_cycle_pos = 128, // 50% duty cycle
        .mode = 0,
        .spics_io_num = GPIO_CS,
        .cs_ena_posttrans = 3, // Keep the CS low 3 cycles after transaction, to stop slave from missing the last bit when CS has less propagation delay than CLK
        .queue_size = 3};

    char sendbuf[3] = {0xAA, 0xAA, 0xAA};
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));

    // Init HSPI bus
    ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    assert(ret == ESP_OK);

    // Init device configurations for recevier
    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &handle);
    assert(ret == ESP_OK);

    while(1){
        
        t.length = sizeof(sendbuf) * 8;
        t.tx_buffer = sendbuf;
        
        ret = spi_device_transmit(handle, &t); 
        //ets_delay_us(10);
        //ESP_LOGI(TAG, "Sent: %s \n", sendbuf); 
        //vTaskDelay(1 / portTICK_PERIOD_MS); 
    }
}

