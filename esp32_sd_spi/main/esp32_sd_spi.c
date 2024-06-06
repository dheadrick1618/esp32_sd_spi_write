/*
Written by Devin Headrick

This program writes to a new directory every powercycle by increasing the appended number 
in the directory name. A file will be created and appended to for a mininum hardcoded number 
of times, then a new file is created and appended etc. 

We can use POSIX and C standard library functions to work with files thanks to VFS FAT FS mounting.

Note:
-To change the pinout for a different board, un-comment the desired define in config.h
-SD cards typically have a 'sector size' of 512 bytes. This is the smallest unit of data that can be written to the card.
-DMA is used by the SPI reader to read 4092 bytes into a buffer from the fpga data source at a time.

TODO - For each new file to write, check if there is space on the SD card for the new file (with margin)
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
#include <math.h>
#include <dirent.h>
#include "driver/spi_slave.h"
#include "config.h" //For different boards with different pinouts

#define FPGA_SPI_HOST VSPI_HOST

#define MOUNT_POINT "/sdcard"
// #define DATA_CHUNK_SIZE 32768 // Size of a 'chunk' of data (bytes) for each write operation
// #define DATA_CHUNK_SIZE 16 // debug so things fit on screen
#define DATA_CHUNK_SIZE 512
//#define BUFFER_SIZE 4092 // known to "work" in getting some data to sdcard
#define BUFFER_SIZE 4 // saves memory, proper calculation for number of times

#define QUEUE_LENGTH 1
#define QUEUE_ITEM_SIZE sizeof(data_chunk_t)
#define MEASURE_INTERVAL_MS 5000 // Measure interval in milliseconds
#define DATA_GEN_DELAY_US 1
//#define NUM_WRITES_PER_FILE_MAX 30
#define NUM_WRITES_PER_FILE_MAX 1
// Recall 4 GB is not 4^9 Bytes, rather 4 *1024 * 1024 * 1024 bytes
#define SD_CARD_SIZE_BYTES 4194304 // Assume 4 GB MAX (given we are using Single Level Cell [SLC] type mem for flight hardyness)
static QueueHandle_t data_queue;
static uint32_t total_bytes_written = 0;

static const char *TAG = "esp32_sd_spi";

char new_dir_name[32];
char file_path[128];

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
            taskYIELD();
        }
        // ets_delay_us(DATA_GEN_DELAY_US); // busy wait delay here in micro seconds instead of millisecond non-blocking delay to simulate data input read
    }
}

static void read_spi_task(void *param)
{
    data_chunk_t chunk;                     // The struct being placed onto queue for sd writer task
    memset(chunk.data, 0, DATA_CHUNK_SIZE); // Clear the data chunk for the first iteration

    esp_err_t ret;

    int recv_buf_size = BUFFER_SIZE; 
    int num_fpga_packets_per_chunk = DATA_CHUNK_SIZE / recv_buf_size; 

    // int recv_buf_size = num_fpga_packets_per_chunk*2; // Double buffer size for overflow prevention margin
    WORD_ALIGNED_ATTR char recvbuf[BUFFER_SIZE];
    memset(recvbuf, 0, recv_buf_size);
    spi_slave_transaction_t t;
    memset(&t, 0, sizeof(t));

    // Read 24 bit packets and fill a buffer until you have size 'Data chunk' then write that to queue
    while (1)
    {
        uint8_t packet_received_count = 0;
        // Receive SPI packets until we have enough for a data_chunk_t
        while (packet_received_count < num_fpga_packets_per_chunk)
        {
            memset(recvbuf, 0x00, recv_buf_size); //Clear receive buffer before each transaction //remove

            t.length = (recv_buf_size) * 8; //length of a single transaction in bits
            t.tx_buffer = NULL;
            t.rx_buffer = recvbuf;
            
            /* This call enables the SPI slave interface to send/receive to the sendbuf and recvbuf. The transaction is
            initialized by the SPI master, however, so it will not actually happen until the master starts a hardware transaction
            by pulling CS low and pulsing the clock etc. In this specific example, we use the handshake line, pulled up by the
            .post_setup_cb callback that is called as soon as a transaction is ready, to let the master know it is free to transfer
            data.
            */
           /**/
            ret = spi_slave_transmit(FPGA_SPI_HOST, &t, portMAX_DELAY);
            //printf("%02X \n", *recvbuf); // for debug
            if (ret != ESP_OK)
            {
                ESP_LOGI(TAG, "Spi slave recv error ");
                break;
            }
            /**/

            /* 
            // According to documentation, this is 2x faster than spi_slave_transmit
            ret = spi_slave_queue_trans(FPGA_SPI_HOST, &t, portMAX_DELAY); 
            if (ret != ESP_OK)
            {
                ESP_LOGI(TAG, "Spi trans error ");
                break;
            }
            ret = spi_slave_get_trans_result(FPGA_SPI_HOST, &t, portMAX_DELAY);
            if (ret != ESP_OK)
            {
                ESP_LOGI(TAG, "Spi trans_result error ");
                break;
            }
            */

            // spi_slave_transmit does not return until the master has done a transmission, so by here we have sent our data and
            // received data from the master.

            /**/
            chunk.data[packet_received_count * 4 + 0] = *(recvbuf + 0);
            chunk.data[packet_received_count * 4 + 1] = *(recvbuf + 1);
            chunk.data[packet_received_count * 4 + 2] = *(recvbuf + 2);
            chunk.data[packet_received_count * 4 + 3] = *(recvbuf + 3);
            packet_received_count++;
            /**/


            //printf("%d\n", packet_received_count); // added with the 4 passes to chunk.data
        }
        //printf("Data chunk built! \n");
        /*
        for (int i = 0; i < 16; i++)
        {
            printf("%02X ", chunk.data[i]);
        }
        printf("\n");
        */

        /**/
        // Place data chunk onto the queue
        if (xQueueSend(data_queue, &chunk, portMAX_DELAY) != pdPASS)
        {
            ESP_LOGE(TAG, "Failed to send data to queue");
        }
        else
        {
            // ESP_LOGI(TAG, "Data chunk written to task: ");
        }
        /**/

        memset(chunk.data, 0, DATA_CHUNK_SIZE); // Clear the data chunk for the next iteration // remove
    }
}

static int write_data_to_file(const char *path, char *data)
{
    int written_bytes = 0;
    FILE *file = fopen(path, "a");
    if (file == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for appending");
        return -1;
    }
    // Set to NULL buffer (dynamically determined buffer size), Full Buffering, with larger stream buffer size
    setvbuf(file, NULL, _IOFBF, 32 * 1024);

    written_bytes = fwrite(data, sizeof(char), DATA_CHUNK_SIZE, file); // Perform file write
    if (written_bytes < 0)
    {
        ESP_LOGE(TAG, "Failed to write data to the file");
        return -1;
    }
    fclose(file);
    return written_bytes;
}

/// @brief Get the number associated with the previously written directory (largest appended num)
/// NOTE the FAT FS seems to write dir names as uppercase, so we want to write dirs with captial letters
/// @return the appended file num of the previous directory used for writing
int get_previous_written_dir_num()
{
    DIR *dir = opendir(MOUNT_POINT);
    if (!dir)
    {
        ESP_LOGE(TAG, "Failed to open directory");
        return -1;
    }

    int max_index = -1;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        ESP_LOGI(TAG, "Found entry: %s", entry->d_name);
        if (entry->d_type == DT_DIR)
        {
            int index;
            if (sscanf(entry->d_name, "DIR%d", &index) == 1)
            {
                if (index > max_index)
                {
                    max_index = index;
                }
            }
            else
            {
                ESP_LOGI(TAG, "Directory name does not match format: %s", entry->d_name);
            }
        }
    }
    closedir(dir);

    return max_index;
}

/// @brief Responsible for writing data from a queue (from the receiver core) onto an SD card as fast as possible
static void sd_card_writer_task()
{
    data_chunk_t chunk;
    int64_t start_time_us = esp_timer_get_time();
    int64_t elapsed_time_us = 0;

    int file_count = 1;
    int num_writes_per_file = 0;

    while (1)
    {
        // Wait for data to arrive on the queue
        if (xQueueReceive(data_queue, &chunk, portMAX_DELAY) == pdPASS)
        {
            if (num_writes_per_file < NUM_WRITES_PER_FILE_MAX)
            {
                int bytes_written_this_write = (uint32_t)write_data_to_file(file_path, chunk.data);

                if (bytes_written_this_write > 0)
                {
                    total_bytes_written += bytes_written_this_write;
                    num_writes_per_file += 1;
                }
            }
            else
            {
                // Incremement the file name by 1
                sprintf(file_path, "%s/FILE_%d.txt", new_dir_name, file_count);
                file_count++;
                num_writes_per_file = 0;
                // ESP_LOGI(TAG, "FILE_%d has been written max times. Now writing to FILE_%d", file_count - 1, file_count);
            }
        }
        //-------------------------------------------------------------------
        elapsed_time_us = esp_timer_get_time() - start_time_us;
        if (elapsed_time_us >= MEASURE_INTERVAL_MS * 1000)
        {
            float write_speed_Bps = (total_bytes_written) / (elapsed_time_us / 1000000.0); // in bits per second
            ESP_LOGI(TAG, "Write Speed: %.5f Bytes per second ", write_speed_Bps);
            // Reset timer counters
            total_bytes_written = 0;
            start_time_us = esp_timer_get_time();
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting SD card write program...\n");

    esp_err_t ret;

    // --------------------------------- SD CARD SPI CONFIG ---------------------------------

    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 64 * 1024};

    sdmmc_card_t *card;
    const char mount_point[] = MOUNT_POINT;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT(); // default freq at 20MHz

    spi_bus_config_t sd_spi_bus_cfg = {
        .mosi_io_num = PIN_SD_MOSI,
        .miso_io_num = PIN_SD_MISO,
        .sclk_io_num = PIN_SD_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = BUFFER_SIZE, // recieve_buf_size in the sd_card_writer_task()
    };

    ret = spi_bus_initialize(host.slot, &sd_spi_bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to init the spi bus. Restarting ESP32 to re-attempt sd card spi bus init.");
        esp_restart();
    }

    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_SD_CS;
    slot_config.host_id = host.slot;

    ESP_LOGI(TAG, "Mounting filesystem");
    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE(TAG, "Failed to mount filesystem. Restarting ESP32 to attempt remount.");
            //Restart to attempt remount 
            esp_restart();
        }
        else
        {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                          "Make sure SD card lines have pull-up resistors in place. Restarting ESP32 to attempt remount.",
                     esp_err_to_name(ret));
            esp_restart();
        }
        return;
    }

    ESP_LOGI(TAG, "Filesystem mounted");
    

    // At this point the card has been initialized, so print its properties
    sdmmc_card_print_info(stdout, card);

    int prev_dir_num = get_previous_written_dir_num();
    ESP_LOGI(TAG, "Prev written dir num: %d \n", prev_dir_num);
    int current_dir_num = prev_dir_num + 1;

    // Create a new directory with the next index
    snprintf(new_dir_name, sizeof(new_dir_name), "%s/DIR%d", MOUNT_POINT, current_dir_num);
    if (mkdir(new_dir_name, 0775) != 0)
    {
        ESP_LOGE(TAG, "Failed to create directory %s. Restarting ESP32 to re-attempt dir creation.", new_dir_name);
        esp_restart();

    }
    else
    {
        ESP_LOGI(TAG, "Directory %s created", new_dir_name);
    }
    // Define the file path to the first file to be written to the new directory
    sprintf(file_path, "%s/FILE_0.txt", new_dir_name);
    ESP_LOGI(TAG, "Writing to this path now: %s \n", file_path);

    // Create a FreeRTOS queue for data chunks to be shared between tasks on different cores
    data_queue = xQueueCreate(QUEUE_LENGTH, QUEUE_ITEM_SIZE);
    if (data_queue == NULL)
    {
        ESP_LOGE(TAG, "Failed to create queue. Restarting ESP32 to re-attempt queue creation.");
        esp_restart();
    }
    //----------------------- FPGA SPI CONFIG --------------------------

    // Configuration for the FPGA SPI slave interface
    spi_slave_interface_config_t slvcfg = {
        .mode = 0,
        .spics_io_num = PIN_FPGA_CS,
        .queue_size = 1,
        .flags = 0,
        .post_setup_cb = NULL,
        .post_trans_cb = NULL};

    // Configuration for the FPGA SPI bus
    spi_bus_config_t fpga_spi_buscfg = {
        .mosi_io_num = PIN_FPGA_MOSI,
        .miso_io_num = PIN_FPGA_MISO,
        .sclk_io_num = PIN_FPGA_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };

    // Initialize SPI slave interface
    ret = spi_slave_initialize(FPGA_SPI_HOST, &fpga_spi_buscfg, &slvcfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG,"Failed to initialize spi slave for FPGA data input. Restarting ESP32 to re-attempt spi slave init.");
        esp_restart();
    }
    //---------------------------------------------------

    xTaskCreatePinnedToCore(sd_card_writer_task, "sd_card_writer_task", 77777, NULL, 7, NULL, 1); // Run on core 1
    // xTaskCreatePinnedToCore(data_generator_task, "data_generator_task", 77777, NULL, 5, NULL, 0); // Run on core 0
    xTaskCreatePinnedToCore(read_spi_task, "read_spi_task", 77777, NULL, 5, NULL, 0); // Run on core 0 

    // Dont need to worry about freeing memory or unmounting cards as this program is
    // expected to run from power on until power off.
}
