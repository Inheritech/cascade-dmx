#include <FastLED.h>

#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
extern "C"  {
#include "wifi_util.h"
}

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#define PORT CONFIG_ARTNET_PORT

#define LEFT_STRIP_PIN   19
#define MIDDLE_STRIP_PIN 21
#define RIGHT_STRIP_PIN  18

#define LEFT_STRIP_LEDS_COUNT   65
#define MIDDLE_STRIP_LEDS_COUNT 85
#define RIGHT_STRIP_LEDS_COUNT  65

static const char *TAG = "Cascade DMX";

extern "C"  {
    void app_main();
}

CRGB left_strip[LEFT_STRIP_LEDS_COUNT];
CRGB middle_strip[MIDDLE_STRIP_LEDS_COUNT];
CRGB right_strip[RIGHT_STRIP_LEDS_COUNT];

void update_leds(CRGB led_strip[], uint8_t led_strip_size, uint8_t* led_data) {
    // Update each color on led_strip with a new CRGB composed of 3 bytes from led_data
    for (int i = 0; i < led_strip_size; i++) {
        led_strip[i] = CRGB(led_data[i * 3 + 1], led_data[i * 3], led_data[i * 3 + 2]);
    }
    // Log the color of the last CRGB in the led_strip
    ESP_LOGI(TAG, "Color: %d, %d, %d", led_strip[led_strip_size - 1].r, led_strip[led_strip_size - 1].g, led_strip[led_strip_size - 1].b);
    FastLED.show();
}

void clear_leds(CRGB led_strip[], uint8_t led_strip_size, CRGB color = CRGB::Black) {
    // Clear led_strip
    for (uint8_t i = 0; i < led_strip_size; i++) {
        led_strip[i] = color;
    }
    ESP_LOGI(TAG, "Color: %d, %d, %d", led_strip[led_strip_size - 1].r, led_strip[led_strip_size - 1].g, led_strip[led_strip_size - 1].b);
    FastLED.show();
}

static void udp_server_task(void *pvParameters)
{
    uint8_t rx_buffer[512];
    char addr_str[128];
    int addr_family = (int)pvParameters;
    int ip_protocol = 0;
    struct sockaddr_in6 dest_addr;

    while (1) {

        if (addr_family == AF_INET) {
            struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
            dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
            dest_addr_ip4->sin_family = AF_INET;
            dest_addr_ip4->sin_port = htons(PORT);
            ip_protocol = IPPROTO_IP;
        } else if (addr_family == AF_INET6) {
            bzero(&dest_addr.sin6_addr.un, sizeof(dest_addr.sin6_addr.un));
            dest_addr.sin6_family = AF_INET6;
            dest_addr.sin6_port = htons(PORT);
            ip_protocol = IPPROTO_IPV6;
        }

        int sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket created");

#if defined(CONFIG_EXAMPLE_IPV4) && defined(CONFIG_EXAMPLE_IPV6)
        if (addr_family == AF_INET6) {
            // Note that by default IPV6 binds to both protocols, it is must be disabled
            // if both protocols used at the same time (used in CI)
            int opt = 1;
            setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
            setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));
        }
#endif

        int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err < 0) {
            ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        }
        ESP_LOGI(TAG, "Socket bound, port %d", PORT);

        // Enable broadcast option to socket
        int broadcastEnable = 1;
        err = setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));
        if (err < 0) {
            ESP_LOGE(TAG, "Socket unable to set broadcast option: errno %d", errno);
            closesocket(sock);
            break;
        }

        while (1) {
            struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
            socklen_t socklen = sizeof(source_addr);
            int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);

            // Error occurred during receiving
            if (len < 0) {
                ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
                break;
            }
            // Data received
            else {
                // Get the sender's ip address as string
                if (source_addr.ss_family == PF_INET) {
                    inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
                } else if (source_addr.ss_family == PF_INET6) {
                    inet6_ntoa_r(((struct sockaddr_in6 *)&source_addr)->sin6_addr, addr_str, sizeof(addr_str) - 1);
                }

                // ESP_LOGI(TAG, "Received %d bytes from %s:", len, addr_str);

                // Verify that the data on rx_buffer is an Art-Net packet by
                // verifying that it starts with 'Art-Net' when rx_buffer and has the
                // ArtDMX OpCode
                if (strncmp((const char*)rx_buffer, "Art-Net", 7) == 0 && rx_buffer[8] == 0x00 && rx_buffer[9] == 0x50) {
                    // ESP_LOGI(TAG, "Art-Net DMX packet received");
                } else {
                    ESP_LOGI(TAG, "Not an Art-Net DMX packet");
                    continue;
                }

                // Parse the OpCode from rx_buffer starting at index 8
                uint16_t opCode = (rx_buffer[8] << 8) | rx_buffer[9];
                uint8_t protocol_version_hi = rx_buffer[10];
                uint8_t protocol_version_lo = rx_buffer[11];
                uint8_t sequence = rx_buffer[12];
                uint8_t physical = rx_buffer[13];
                // Parse the universe
                uint8_t universe = rx_buffer[14];
                // Parse the subnet
                uint8_t subnet = rx_buffer[15];
                // Parse the length of the packet in little endian
                uint16_t data_length = (rx_buffer[16] << 8) | rx_buffer[17];

                if (universe == 1) {
                    update_leds(left_strip, LEFT_STRIP_LEDS_COUNT, rx_buffer + 18);
                } else if (universe == 3) {
                    update_leds(right_strip, RIGHT_STRIP_LEDS_COUNT, rx_buffer + 18);
                } else if (universe == 2) {
                    update_leds(middle_strip, MIDDLE_STRIP_LEDS_COUNT, rx_buffer + 18);
                }
            }
        }

        if (sock != -1) {
            ESP_LOGE(TAG, "Shutting down socket and restarting...");
            shutdown(sock, 0);
            close(sock);
        }
    }
    vTaskDelete(NULL);
}

void app_main()
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    wifi_init_sta();
    FastLED.addLeds<WS2812B, LEFT_STRIP_PIN>(left_strip, LEFT_STRIP_LEDS_COUNT);
    FastLED.addLeds<WS2812B, MIDDLE_STRIP_PIN>(middle_strip, MIDDLE_STRIP_LEDS_COUNT);
    FastLED.addLeds<WS2812B, RIGHT_STRIP_PIN>(right_strip, RIGHT_STRIP_LEDS_COUNT);

	FastLED.setMaxPowerInVoltsAndMilliamps(5, 20000);

	FastLED.setCorrection(TypicalPixelString);

    clear_leds(left_strip, LEFT_STRIP_LEDS_COUNT, CRGB::Green);
    clear_leds(middle_strip, MIDDLE_STRIP_LEDS_COUNT, CRGB::Green);
    clear_leds(right_strip, RIGHT_STRIP_LEDS_COUNT, CRGB::Green);

#ifdef CONFIG_ENABLE_IPV4
    xTaskCreate(udp_server_task, "udp_server", 4096, (void*)AF_INET, 12, NULL);
#endif
#ifdef CONFIG_ENABLE_IPV6
    xTaskCreate(udp_server_task, "udp_server", 4096, (void*)AF_INET6, 12, NULL);
#endif

}