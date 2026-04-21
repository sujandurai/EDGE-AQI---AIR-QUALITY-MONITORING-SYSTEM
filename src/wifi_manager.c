#include "wifi_manager.h"
#include <string.h>
#include <stdio.h>

extern void delay_ms(uint32_t ms); // Defined in main.c

static char espBuffer[ESP_BUFFER_SIZE];
static uint16_t bufferIndex = 0;

void WIFI_Init(void)
{
    // Assuming SERCOM2 is already initialized by SYS_Initialize()
    bufferIndex = 0;
    memset(espBuffer, 0, ESP_BUFFER_SIZE);
}

bool WIFI_SendCommand(const char* command, const char* expected_response, uint32_t timeout_ms)
{
    // 1. Clear buffer
    memset(espBuffer, 0, ESP_BUFFER_SIZE);
    bufferIndex = 0;

    // 2. Send command to ESP via SERCOM2
    // Note: Use the function name that Harmony 3 generates for SERCOM2
    SERCOM2_USART_Write((uint8_t*)command, strlen(command));
    while(SERCOM2_USART_WriteIsBusy());

    // 3. Wait for response (Polling)
    uint32_t elapsed = 0;
    while (elapsed < timeout_ms)
    {
        if (SERCOM2_USART_ReceiverIsReady())
        {
            uint8_t data;
            if (SERCOM2_USART_Read(&data, 1))
            {
                if (bufferIndex < ESP_BUFFER_SIZE - 1)
                {
                    espBuffer[bufferIndex++] = (char)data;
                    espBuffer[bufferIndex] = '\0';
                }
                
                // Check if expected response is in the buffer
                if (strstr(espBuffer, expected_response) != NULL)
                {
                    return true;
                }
            }
        }
        else
        {
            delay_ms(1);
            elapsed++;
        }
    }

    return false;
}

bool WIFI_IsAlive(void)
{
    return WIFI_SendCommand("AT\r\n", "OK", 1000);
}

bool WIFI_SetMode(uint8_t mode)
{
    char cmd[20];
    sprintf(cmd, "AT+CWMODE=%u\r\n", mode);
    return WIFI_SendCommand(cmd, "OK", 2000);
}
