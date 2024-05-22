/*
Written by Devin Headrick
*/
#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include "esp_timer.h"
#include <rom/ets_sys.h> //For ets_delay_us()

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define PIN_NUM_MISO 2  // D0
#define PIN_NUM_MOSI 15 // D3
#define PIN_NUM_CLK 14  // SCK
#define PIN_NUM_CS 13   // CMD

#define MOUNT_POINT "/sdcard"
const char *file_path = MOUNT_POINT "/new_data.txt";

static const char *TAG = "esp32_sd_spi";

#define DATA_CHUNK_SIZE 16384 // Size of a 'chunk' of data (bytes) for each write operation

#define QUEUE_LENGTH 2
#define QUEUE_ITEM_SIZE sizeof(char)
#define MEASURE_INTERVAL_MS 5000 // Measure interval in milliseconds
#define DATA_GEN_DELAY_US 1

static QueueHandle_t data_queue;
//static FILE *file = NULL;
static uint32_t total_bytes_written = 0;

typedef struct
{
    char data[DATA_CHUNK_SIZE];
} data_chunk_t;

// Task to generate data as bytes and place them onto the queue
static void data_generator_task(void *param)
{
    data_chunk_t chunk;

    // Create a 'chunk' of data of a particular size
    for (int i = 0; i < DATA_CHUNK_SIZE; i++)
    {
        chunk.data[i] = 'A' + (i % 26); // Cycle between ASCII capital letters
    }

    while (1)
    {
        // Place data chunk onto the queue
        if (xQueueSend(data_queue, &chunk, portMAX_DELAY) != pdPASS)
        {
            ESP_LOGE(TAG, "Failed to send data to queue");
        }
        else
        {
            // ESP_LOGI(TAG, "Data generated: %c", data);
        }
        // Delay for a bit to simulate data generation interval
        //ets_delay_us(DATA_GEN_DELAY_US); // busy wait delay here in micro seconds instead of millisecond non-blocking delay
    }
}

static esp_err_t write_data_to_file(const char *path, char *data)
{
    // Open the file
    FILE *file = fopen(path, "a");
    if (file == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for appending");
        return ESP_FAIL;
    }
    // Write the data
    if (fprintf(file, data) < 0)
    {
        ESP_LOGE(TAG, "Failed to write data to the file");
        return ESP_FAIL;
    }
    // TODO - try writing data using fwrite as we do not need formatting provided by fprintf with the raw byte data

    fclose(file);

    return ESP_OK;
}

// Task to write data from the queue to the SD card
static void sd_card_writer_task(void *param)
{
    data_chunk_t chunk;
    int64_t start_time_us = esp_timer_get_time();
    int64_t current_time_us = esp_timer_get_time();
    int64_t elapsed_time_us = 0;

    while (1)
    {
        // Wait for data to arrive on the queue
        if (xQueueReceive(data_queue, &chunk, portMAX_DELAY) == pdPASS)
        {
            if(write_data_to_file(file_path, &chunk.data) != ESP_OK){
                ESP_LOGI(TAG, "Error writing data chunk to sd card");
            }

            total_bytes_written += DATA_CHUNK_SIZE;

            current_time_us = esp_timer_get_time();
            elapsed_time_us = current_time_us - start_time_us;

            if (elapsed_time_us >= MEASURE_INTERVAL_MS * 1000)
            {
                float write_speed_Bps = (total_bytes_written) / (elapsed_time_us / 1000000.0); // in bits per second
                // float write_speed_mbps = write_speed_bps / 1000000.0; // convert to Mbps

                ESP_LOGI(TAG, "Write Speed: %.5f Bytes per second ", write_speed_Bps);

                // Reset counters
                total_bytes_written = 0;
                start_time_us = esp_timer_get_time();
            }
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting SD card write program...\n");

    esp_err_t ret;

    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 6,
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
    data_queue = xQueueCreate(QUEUE_LENGTH, sizeof(data_chunk_t));
    if (data_queue == NULL)
    {
        ESP_LOGE(TAG, "Failed to create queue");
        return;
    }

    // Open file for writing data and keep it open
    // const char *file_path = MOUNT_POINT "/new_data.txt";
    // ESP_LOGI(TAG, "Opening file %s", file_path);
    // file = fopen(file_path, "a");
    // if (file == NULL)
    // {
    //     ESP_LOGE(TAG, "Failed to open file for writing");
    //     return;
    // }

    // if (file != NULL)
    // {
    //     setvbuf(file, NULL, _IOFBF, 4096); // Set a larger buffer size
    // }

    //TODO - check if data file exists already - if yes then wipe it , otherwise ignore (write fxn opens for appending)

    xTaskCreatePinnedToCore(sd_card_writer_task, "sd_card_writer_task", 65536, NULL, 5, NULL, 0); // Run on core 0
    xTaskCreatePinnedToCore(data_generator_task, "data_generator_task", 65536, NULL, 5, NULL, 1); // Run on core 1

    // esp_vfs_fat_sdcard_unmount(mount_point, card);
    // ESP_LOGI(TAG, "Card unmounted");

    //// deinitialize the bus after all devices are removed
    // spi_bus_free(host.slot);
}
