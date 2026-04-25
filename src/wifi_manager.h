#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "config/default/definitions.h" // Need for SERCOM5_REGS

// --- ESP-01S Configuration ---
#define ESP_BUFFER_SIZE 256

// --- Function Prototypes ---

/**
 * @brief Initializes the Wi-Fi module communication structures.
 */
void WIFI_Init(void);

/**
 * @brief Sends an AT command to the ESP-01S via SERCOM5 and waits for a specific response.
 * 
 * @param command The AT command string (e.g., "AT\r\n")
 * @param expected_response The substring to look for (e.g., "OK")
 * @param timeout_ms How long to wait for the response
 * @return true if response was found, false otherwise
 */
bool WIFI_SendCommand(const char* command, const char* expected_response, uint32_t timeout_ms);

/**
 * @brief Checks if the ESP-01S is responding (Sends "AT").
 * @return true if module is alive.
 */
bool WIFI_IsAlive(void);

/**
 * @brief Sets the Wi-Fi mode (1=Station, 2=SoftAP, 3=Both).
 */
bool WIFI_SetMode(uint8_t mode);

/**
 * @brief Connects to a Wi-Fi network using the provided SSID and password.
 * 
 * @param ssid The SSID of the Wi-Fi network.
 * @param password The password of the Wi-Fi network.
 * @return true if connected successfully, false otherwise.
 */
bool WIFI_Connect(const char* ssid, const char* password);

#endif // WIFI_MANAGER_H
