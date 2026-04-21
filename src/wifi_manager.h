#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "definitions.h"

// --- ESP-01S Configuration ---
#define ESP_BUFFER_SIZE 256

// --- Function Prototypes ---

/**
 * @brief Initializes the Wi-Fi module communication.
 */
void WIFI_Init(void);

/**
 * @brief Sends an AT command to the ESP-01S and waits for a specific response.
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

#endif // WIFI_MANAGER_H
