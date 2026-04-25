#include "wifi_manager.h"
#include <string.h>
#include <stdio.h>

extern void delay_ms(uint32_t ms); // Defined in main.c

static char espBuffer[ESP_BUFFER_SIZE];
static uint16_t bufferIndex = 0;

// Send a string to the ESP-01S via SERCOM5 using direct register access
static void ESP_Write(const char* str) {
    while (*str) {
        while (!(SERCOM5_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_DRE_Msk));
        SERCOM5_REGS->USART_INT.SERCOM_DATA = (uint16_t)*str++;
    }
}

// Clear SERCOM5 receive errors
static void SERCOM5_ClearErrors(void) {
    SERCOM5_REGS->USART_INT.SERCOM_STATUS = (uint16_t)(
        SERCOM_USART_INT_STATUS_BUFOVF_Msk |
        SERCOM_USART_INT_STATUS_FERR_Msk   |
        SERCOM_USART_INT_STATUS_PERR_Msk);
    SERCOM5_REGS->USART_INT.SERCOM_INTFLAG = (uint8_t)SERCOM_USART_INT_INTFLAG_ERROR_Msk;
}

// Flush any pending bytes from ESP before sending a command
static void ESP_FlushRx(uint32_t wait_ms) {
    uint32_t idle = 0;
    SERCOM5_ClearErrors(); // clear overflow errors first
    while (idle < wait_ms) {
        if (SERCOM5_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_RXC_Msk) {
            (void)SERCOM5_REGS->USART_INT.SERCOM_DATA; // read and discard
            idle = 0; // reset idle count when data comes in
        } else {
            delay_ms(1);
            idle++;
        }
    }
    SERCOM5_ClearErrors(); // clear any errors that arose during flush
}

void WIFI_Init(void)
{
    bufferIndex = 0;
    memset(espBuffer, 0, ESP_BUFFER_SIZE);
}

bool WIFI_SendCommand(const char* command, const char* expected_response, uint32_t timeout_ms)
{
    // Flush stale RX bytes + clear errors before sending
    ESP_FlushRx(50); // Using 50ms flush to keep it responsive but clear

    memset(espBuffer, 0, ESP_BUFFER_SIZE);
    bufferIndex = 0;

    ESP_Write(command);

    // Wait for last byte to fully shift out (TXC = shift register empty)
    while (!(SERCOM5_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_TXC_Msk));

    // Tight polling ? noDataCount ticks only when NO byte is available.
    // When a byte arrives, noDataCount resets so we don't time out mid-response.
    // ~8000 iterations with no data ? 1ms (calibrated for 48MHz -O1)
    const uint32_t TICKS_PER_MS = 8000UL;
    uint32_t deadline = timeout_ms * TICKS_PER_MS;
    uint32_t noDataCount = 0;

    while (noDataCount < deadline) {
        if (SERCOM5_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_RXC_Msk) {
            uint8_t data = (uint8_t)SERCOM5_REGS->USART_INT.SERCOM_DATA;
            
            // --- ECHO TO PUTTY (SERCOM3) ---
            while (!(SERCOM3_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_DRE_Msk));
            SERCOM3_REGS->USART_INT.SERCOM_DATA = (uint16_t)data;
            // -------------------------------
            
            if (bufferIndex < ESP_BUFFER_SIZE - 1) {
                espBuffer[bufferIndex++] = (char)data;
                espBuffer[bufferIndex]   = '\0';
            }
            if (strstr(espBuffer, expected_response) != NULL) {
                return true;
            }
            noDataCount = 0; // reset timeout while data is flowing
        } else {
            noDataCount++;
        }
    }
    return false;
}

bool WIFI_IsAlive(void)
{
    return WIFI_SendCommand("AT\r\n", "OK", 2000);
}

bool WIFI_SetMode(uint8_t mode)
{
    char cmd[24];
    snprintf(cmd, sizeof(cmd), "AT+CWMODE=%u\r\n", mode);
    return WIFI_SendCommand(cmd, "OK", 3000);
}

bool WIFI_Connect(const char* ssid, const char* password)
{
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, password);
    return WIFI_SendCommand(cmd, "WIFI GOT IP", 20000);
}
