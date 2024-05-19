/*Written by Devin Headrick

*/
#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define PIN_NUM_MISO 2  // D0
#define PIN_NUM_MOSI 15 // D3
#define PIN_NUM_CLK 14  // SCK
#define PIN_NUM_CS 13   // CMD

#define MOUNT_POINT "/sdcard"
static const char *TAG = "esp32_sd_spi";

#define EXAMPLE_MAX_CHAR_SIZE 64
#define QUEUE_LENGTH 256             // Number of bytes
#define QUEUE_ITEM_SIZE sizeof(char) // Each item is one byte
#define MEASURE_INTERVAL 1000        // Measure interval in milliseconds
#define MS_BETWEEN_DATA_GENERATION 10

static QueueHandle_t data_queue;
static FILE *file = NULL;

// Task to generate data and place it onto the queue
static void data_generator_task(void *param)
{
    char data = 'A'; // Starting character

    while (1)
    {
        // Place data onto the queue
        if (xQueueSend(data_queue, &data, portMAX_DELAY) != pdPASS)
        {
            ESP_LOGE(TAG, "Failed to send data to queue");
        }
        else
        {
            ESP_LOGI(TAG, "Data generated: %c", data);
        }

        // Generate next character
        data = (data == 'Z') ? 'A' : data + 1; // Cycle through 'A' to 'Z'

        // Delay for a bit to simulate data generation interval
        vTaskDelay(pdMS_TO_TICKS(MS_BETWEEN_DATA_GENERATION)); // Adjust this to control the data generation rate
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting SD card write program...\n");

    esp_err_t ret;

    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024};

    sdmmc_card_t *card;
    const char mount_point[] = MOUNT_POINT;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT(); // default freq at 20MHz

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to init the spi bus\n");
        return;
    }

    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host.slot;

    ESP_LOGI(TAG, "Mounting filesystem");
    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE(TAG, "Failed to mount filesystem. ");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                          "Make sure SD card lines have pull-up resistors in place.",
                     esp_err_to_name(ret));
        }
        return;
    }

    ESP_LOGI(TAG, "Filesystem mounted");

    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, card);

    // Now use POSIX and C standard library functions to work with files thanks to VFS FAT FS mounting.

    // TODO - Create file, write to data for specific duration of time

    // Create a FreeRTOS queue for byte data
    data_queue = xQueueCreate(QUEUE_LENGTH, QUEUE_ITEM_SIZE);
    if (data_queue == NULL)
    {
        ESP_LOGE(TAG, "Failed to create queue");
        return;
    }

    xTaskCreatePinnedToCore(data_generator_task, "data_generator_task", 4096, NULL, 5, NULL, 1); // Run on core 1

    esp_vfs_fat_sdcard_unmount(mount_point, card);
    ESP_LOGI(TAG, "Card unmounted");

    // deinitialize the bus after all devices are removed
    spi_bus_free(host.slot);
}
