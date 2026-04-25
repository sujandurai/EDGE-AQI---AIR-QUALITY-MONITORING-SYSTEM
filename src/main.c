// ============================================================
// main.c �?�?�?�??�?�??�?¢?? Display "Hello" on ST7735 1.8" TFT (128x160)
//          PIC32CM5164LS00048 @ 48 MHz  (SERCOM1 SPI)
//
// FULLY SELF-CONTAINED �?�?�?�??�?�??�?¢?? no external ST7735 library needed.
//
// Pin wiring:
//   SDA (MOSI)  -> PA08  (SERCOM1 PAD[0])
//   SCK (Clock) -> PA09  (SERCOM1 PAD[1])
//   CS          -> PA10  (GPIO)
//   DC          -> PA11  (GPIO)
//   RESET       -> PA12  (GPIO)
//   LED / VCC   -> 3.3 V
// ============================================================

#include "config/default/definitions.h"
#include "config/default/peripheral/adc/plib_adc.h"
#include "config/default/peripheral/sercom/usart/plib_sercom3_usart.h"
#include "logo.h" // RGB565 bitmap
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "tinyml_wrapper.h"

// --- MQTT Global State and Forward Declarations ---
static bool mqtt_connected = false;
static void MQTT_Publish(const char* topic, const char* payload);

// TrustZone Native Register Aliases (using standard addressing)

// PA17 button helper �?? reads bit 17 of PORT GROUP[0] IN register (Secure
// alias)
#define PA17_IS_HIGH() ((PORT_SEC_REGS->GROUP[0].PORT_IN >> 17u) & 1u)

void delay_us(int us) {
  // Use exact hardware cycle counting for perfectly deterministic delays
  if ((SysTick->CTRL & SysTick_CTRL_ENABLE_Msk) == 0) {
    SysTick->LOAD = 0xFFFFFF;
    SysTick->VAL = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
  }

  uint32_t ticks = (uint32_t)us * (CPU_CLOCK_FREQUENCY / 1000000);
  uint32_t reload = SysTick->LOAD;
  if (reload == 0)
    reload = 0xFFFFFF;

  uint32_t start = SysTick->VAL;
  uint32_t elapsed = 0;

  while (elapsed < ticks) {
    uint32_t current = SysTick->VAL;
    if (start >= current) {
      elapsed += (start - current);
    } else {
      elapsed += (start + reload + 1 - current);
    }
    start = current;
  }
}

static bool check_button_toggle(void);

static void adc_init(void) {
// Just use a tiny delay instead of polling SERCOM3_USART_WriteIsBusy
// so we don't accidentally hang there if it faults.
#define P(str)                                                                 \
  do {                                                                         \
    while (SERCOM3_USART_WriteIsBusy())                                        \
      ;                                                                        \
    SERCOM3_USART_Write((uint8_t *)str, strlen(str));                          \
  } while (0)

  P("adc_init 1\r\n");
  // Force pin PMUX to Analog (Function B = 0x1) for active sensors
  // PA02 = AIN0 (Dust), PA03 = GPIO (Dust LED)
  PORT_SEC_REGS->GROUP[0].PORT_PMUX[1] = 0x01U;
  // PA04 = AIN2 (CO2), PA05 = AIN3 (MQ131)
  PORT_SEC_REGS->GROUP[0].PORT_PMUX[2] = 0x11U;
  // PA06 = AIN4 (MQ7), PA07 = AIN5 (MQ135)
  PORT_SEC_REGS->GROUP[0].PORT_PMUX[3] = 0x11U;

  // Enable the Multiplexer for all these specific analog pins
  PORT_SEC_REGS->GROUP[0].PORT_PINCFG[2] |= PORT_PINCFG_PMUXEN_Msk;
  // PA03 does NOT get PMUXEN (Digital GPIO)
  PORT_SEC_REGS->GROUP[0].PORT_PINCFG[4] |= PORT_PINCFG_PMUXEN_Msk;
  PORT_SEC_REGS->GROUP[0].PORT_PINCFG[5] |= PORT_PINCFG_PMUXEN_Msk;
  PORT_SEC_REGS->GROUP[0].PORT_PINCFG[6] |= PORT_PINCFG_PMUXEN_Msk;
  PORT_SEC_REGS->GROUP[0].PORT_PINCFG[7] |= PORT_PINCFG_PMUXEN_Msk;

  // Do the same for the non-secure alias just to be incredibly robust
  PORT_REGS->GROUP[0].PORT_PMUX[1] = 0x01U;
  PORT_REGS->GROUP[0].PORT_PMUX[2] = 0x11U;
  PORT_REGS->GROUP[0].PORT_PMUX[3] = 0x11U;
  PORT_REGS->GROUP[0].PORT_PINCFG[2] |= PORT_PINCFG_PMUXEN_Msk;
  // PA03 is GPIO
  PORT_REGS->GROUP[0].PORT_PINCFG[4] |= PORT_PINCFG_PMUXEN_Msk;
  PORT_REGS->GROUP[0].PORT_PINCFG[5] |= PORT_PINCFG_PMUXEN_Msk;
  PORT_REGS->GROUP[0].PORT_PINCFG[6] |= PORT_PINCFG_PMUXEN_Msk;
  PORT_REGS->GROUP[0].PORT_PINCFG[7] |= PORT_PINCFG_PMUXEN_Msk;

  // Enable APB clock for ADC
  MCLK_REGS->MCLK_APBCMASK |= (1 << 14);

  P("adc_init 2\r\n");
  // Enable GCLK for ADC (Generator 0)
  GCLK_REGS->GCLK_PCHCTRL[28] =
      0x40; // GCLK_PCHCTRL_CHEN_Msk is 0x40 (bit 6), GEN(0) is 0
  while ((GCLK_REGS->GCLK_PCHCTRL[28] & 0x40) == 0)
    ;

  P("adc_init 3\r\n");
  // Reset ADC
  ADC_REGS->ADC_CTRLA = 1; // SWRST
  while (ADC_REGS->ADC_SYNCBUSY & 1)
    ;

  P("adc_init 4\r\n");
  ADC_REGS->ADC_CTRLB = 4;        // DIV32
  ADC_REGS->ADC_REFCTRL = 0x05;   // INTVCC2 (3.3V)
  ADC_REGS->ADC_CTRLC = (1 << 4); // 12-bit
  ADC_REGS->ADC_SAMPCTRL =
      63; // Maximize sampling time for high-impedance MQ sensors

  // Enable ADC
  ADC_REGS->ADC_CTRLA |= 2; // ENABLE
  while (ADC_REGS->ADC_SYNCBUSY & 2)
    ;

  P("adc_init 5\r\n");
}

// Uses direct hardware registers to guarantee conversion completion
static uint16_t read_adc_avg(uint8_t channel) {
  static uint8_t last_channel = 0xFF; // Keep track of the multiplexer state

  // Switch the multiplexer to the new channel
  ADC_REGS->ADC_INPUTCTRL =
      channel | (0x18 << 8); // Positive = channel, Negative = GND
  while (ADC_REGS->ADC_SYNCBUSY & (1 << 2)) // INPUTCTRL sync
    ;

  // If we just switched to a new sensor, the internal ADC capacitor needs time
  // to charge to the new voltage, especially for high-impedance gas sensors!
  if (channel != last_channel) {
    uint8_t pin = 0xFF;
    if (channel == 0)
      pin = 2; // PA02 - Dust
    else if (channel == 2)
      pin = 4; // PA04 - CO2
    else if (channel == 3)
      pin = 5; // PA05 - MQ131
    else if (channel == 4)
      pin = 6; // PA06 - MQ7
    else if (channel == 5)
      pin = 7; // PA07 - MQ135

    if (pin != 0xFF) {
      // Temporarily disable analog PMUX and enable digital pull-down
      // to drain any ghost charge if the sensor is floating/disconnected.
      PORT_SEC_REGS->GROUP[0].PORT_PINCFG[pin] &= ~PORT_PINCFG_PMUXEN_Msk;
      PORT_REGS->GROUP[0].PORT_PINCFG[pin] &= ~PORT_PINCFG_PMUXEN_Msk;

      PORT_SEC_REGS->GROUP[0].PORT_DIRCLR = (1U << pin);
      PORT_REGS->GROUP[0].PORT_DIRCLR = (1U << pin);

      PORT_SEC_REGS->GROUP[0].PORT_OUTCLR = (1U << pin);
      PORT_REGS->GROUP[0].PORT_OUTCLR = (1U << pin);

      PORT_SEC_REGS->GROUP[0].PORT_PINCFG[pin] |= PORT_PINCFG_PULLEN_Msk;
      PORT_REGS->GROUP[0].PORT_PINCFG[pin] |= PORT_PINCFG_PULLEN_Msk;

      delay_us(100); // Bleed OFF stray floating voltage

      PORT_SEC_REGS->GROUP[0].PORT_PINCFG[pin] &= ~PORT_PINCFG_PULLEN_Msk;
      PORT_REGS->GROUP[0].PORT_PINCFG[pin] &= ~PORT_PINCFG_PULLEN_Msk;

      PORT_SEC_REGS->GROUP[0].PORT_PINCFG[pin] |= PORT_PINCFG_PMUXEN_Msk;
      PORT_REGS->GROUP[0].PORT_PINCFG[pin] |= PORT_PINCFG_PMUXEN_Msk;
    }

    delay_us(200); // Allow physical voltage to settle through the multiplexer

    // Perform one dummy conversion to flush the ADC pipeline/capacitor
    ADC_REGS->ADC_SWTRIG |= (1 << 1);
    while (ADC_REGS->ADC_SYNCBUSY & (1 << 10))
      ;
    while ((ADC_REGS->ADC_INTFLAG & (1 << 0)) == 0)
      ;
    uint16_t dummy = ADC_REGS->ADC_RESULT;
    (void)dummy;
    ADC_REGS->ADC_INTFLAG = (1 << 0); // clear flag

    last_channel = channel;
  }

  uint32_t sum = 0;
  for (int i = 0; i < 5; i++) {
    ADC_REGS->ADC_SWTRIG |= (1 << 1);          // START bit
    while (ADC_REGS->ADC_SYNCBUSY & (1 << 10)) // SWTRIG sync
      ;

    while ((ADC_REGS->ADC_INTFLAG & (1 << 0)) == 0) // Wait for RESRDY
      ;

    sum += ADC_REGS->ADC_RESULT;
    ADC_REGS->ADC_INTFLAG = (1 << 0); // clear flag
    delay_us(50);
  }
  return sum / 5;
}

static float adc_to_voltage(uint16_t adc_value) {
  if (adc_value == 0)
    adc_value = 1; // Prevent 0V
  if (adc_value >= 4095)
    adc_value = 4094; // Prevent 3.3V division by zero
  return (adc_value / 4095.0f) * 3.3f;
}

static float calculate_Rs(float voltage) {
  // Guard against divide by zero or negative resistance
  if (voltage <= 0.01f)
    voltage = 0.01f;
  if (voltage >= 3.29f)
    voltage = 3.29f;

  float Rs = ((3.3f - voltage) / voltage) * 10000.0f;
  if (Rs < 0.1f)
    return 0.1f;
  return Rs;
}

static float read_dust() {
  // Perform a dummy average read to switch the ADC multiplexer to AIN0 (Dust)
  // *before* we turn the LED on. This prevents the multiplexer's settling delays
  // from ruining the strict 280us pulse timing required by the Sharp sensor.
  read_adc_avg(0); 

  float sum = 0;
  for (int i = 0; i < 5; i++) {
    PORT_REGS->GROUP[0].PORT_OUTSET = (1U << 3); // PA03 HIGH (Dust LED)
    delay_us(280);

    // Single fast ADC read (do NOT average 5x over 250us here! The peak is very brief)
    ADC_REGS->ADC_SWTRIG |= (1 << 1);          // START bit
    while (ADC_REGS->ADC_SYNCBUSY & (1 << 10)) // SWTRIG sync
      ;
    while ((ADC_REGS->ADC_INTFLAG & (1 << 0)) == 0) // Wait for RESRDY
      ;
    uint16_t adc = ADC_REGS->ADC_RESULT;
    ADC_REGS->ADC_INTFLAG = (1 << 0); // clear flag

    delay_us(40);
    PORT_REGS->GROUP[0].PORT_OUTCLR = (1U << 3); // PA03 LOW
    
    // The datasheet requires a 10ms cycle time (10000us). We've used 320us.
    delay_us(9680); 

    sum += adc_to_voltage(adc);
  }

  float voltage = sum / 5.0f;
  if (voltage < 0.15f)
    return 0.0f; // Sensor removed/no input

  static float baseline = 0;
  if (baseline == 0)
    baseline = voltage;
  baseline = (baseline * 0.99f) + (voltage * 0.01f);

  float dust = (voltage - baseline) * 1000.0f / 0.5f;
  if (dust < 0)
    dust = 0;
  if (dust > 300)
    dust = 300;
  return dust;
}

static float read_mg811_co2() {
  uint16_t adc = read_adc_avg(2); // PA04 is AIN2
  float voltage = adc_to_voltage(adc);
  if (voltage < 0.15f)
    return 0.0f; // Sensor removed/no input
  float co2 = 400.0f + voltage * 500.0f;
  if (co2 < 400.0f)
    co2 = 400.0f;
  if (co2 > 2000.0f)
    co2 = 2000.0f;
  return co2;
}

// ======================== DELAY =============================
void delay_ms(uint32_t ms) {
  for (uint32_t i = 0; i < ms; i++) {
    delay_us(1000); // 1000 us = 1 ms precision hardware delay
  }
}

// ======================== DHT11 SENSOR ======================
// PA14 = Data pin (single-wire, open-drain protocol)
// DHT11 protocol timing (from datasheet):
//   Host start: pull LOW >18ms, then release (float input)
//   Sensor response: 80us LOW, then 80us HIGH
//   Each data bit: 50us LOW then HIGH (26-28us = '0', 70us = '1')
//   Checksum: byte[4] = byte[0]+[1]+[2]+[3]
//
// IMPORTANT: Read no more than once every 2 seconds or sensor won't respond.
// We use a simple PIN read macro that checks BOTH Secure and Non-Secure
// PORT_IN registers �?? the one in the correct TrustZone state will be valid.
#define DHT_PIN 14U
#define DHT_READ_PIN()                                                         \
  (((PORT_SEC_REGS->GROUP[0].PORT_IN >> DHT_PIN) & 1U) |                       \
   ((PORT_REGS->GROUP[0].PORT_IN >> DHT_PIN) & 1U))

// Set PA14 as OUTPUT, drive it to 'val' (0=low, 1=high)
#define DHT_SET_OUTPUT(val)                                                    \
  do {                                                                         \
    PORT_SEC_REGS->GROUP[0].PORT_PINCFG[DHT_PIN] = 0x00U;                      \
    PORT_REGS->GROUP[0].PORT_PINCFG[DHT_PIN] = 0x00U;                          \
    PORT_SEC_REGS->GROUP[0].PORT_DIRSET = (1U << DHT_PIN);                     \
    PORT_REGS->GROUP[0].PORT_DIRSET = (1U << DHT_PIN);                         \
    if (val) {                                                                 \
      PORT_SEC_REGS->GROUP[0].PORT_OUTSET = (1U << DHT_PIN);                   \
      PORT_REGS->GROUP[0].PORT_OUTSET = (1U << DHT_PIN);                       \
    } else {                                                                   \
      PORT_SEC_REGS->GROUP[0].PORT_OUTCLR = (1U << DHT_PIN);                   \
      PORT_REGS->GROUP[0].PORT_OUTCLR = (1U << DHT_PIN);                       \
    }                                                                          \
  } while (0)

// Float PA14 as INPUT (INEN=1, no PULLEN) �?? critical: no pull-up fight during
// active-LOW response from sensor
#define DHT_SET_INPUT_FLOAT()                                                  \
  do {                                                                         \
    PORT_SEC_REGS->GROUP[0].PORT_DIRCLR = (1U << DHT_PIN);                     \
    PORT_REGS->GROUP[0].PORT_DIRCLR = (1U << DHT_PIN);                         \
    PORT_SEC_REGS->GROUP[0].PORT_PINCFG[DHT_PIN] = 0x02U; /* INEN=1 only */    \
    PORT_REGS->GROUP[0].PORT_PINCFG[DHT_PIN] = 0x02U;                          \
  } while (0)

static uint32_t expectPulse(bool level) {
  uint32_t count = 0;
  uint32_t max_cycles = 100000; // Large timeout
  while (DHT_READ_PIN() == (level ? 1U : 0U)) {
    if (count++ >= max_cycles) {
      return 0; // Exceeded timeout, fail.
    }
  }
  return count;
}

static bool read_dht11(float *temp, float *hum) {
  uint32_t cycles[80];

  // 1. Host sends START signal (pull low for 20ms)
  DHT_SET_OUTPUT(0);
  delay_ms(20);

  // 2. Host releases line to float (external 10k resistor pulls it HIGH).
  // Then we wait 55us. The DHT11 takes 20-40us to respond by pulling LOW.
  // So after 55us, the line WILL definitively be LOW.
  DHT_SET_INPUT_FLOAT();
  delay_us(55);

  // CRITICAL SECTION: Disable all interrupts!
  // If a UART or SysTick interrupt fires during these microseconds,
  // it throws off the cycle counting completely and we lose the sensor edge.
  __disable_irq();

  // 3. Sensor holds LOW for ~80us. Since we delayed 55us, it has already
  // been LOW for a bit. Wait while it finishes this LOW pulse.
  if (expectPulse(false) == 0) {
    __enable_irq();
    P("[DHT] Err: Timeout waiting for start signal LOW pulse\r\n");
    return false;
  }

  // 4. Sensor holds HIGH for ~80us. Wait until it goes LOW.
  if (expectPulse(true) == 0) {
    __enable_irq();
    P("[DHT] Err: Timeout waiting for start signal HIGH pulse\r\n");
    return false;
  }

  // 5. Read the 40 bits
  for (int i = 0; i < 80; i += 2) {
    cycles[i] = expectPulse(false);    // LOW pulse
    cycles[i + 1] = expectPulse(true); // HIGH pulse
  }

  // Re-enable interrupts immediately after the fast bit-stream.
  __enable_irq();

  uint8_t data[5] = {0, 0, 0, 0, 0};
  for (int i = 0; i < 40; ++i) {
    uint32_t lowCycles = cycles[2 * i];
    uint32_t highCycles = cycles[2 * i + 1];

    if ((lowCycles == 0) || (highCycles == 0)) {
      char dbuf[80];
      sprintf(dbuf, "[DHT] Err: Bits missing at bit %d (L:%lu H:%lu)\r\n", i,
              lowCycles, highCycles);
      while (SERCOM3_USART_WriteIsBusy())
        ;
      SERCOM3_USART_Write((uint8_t *)dbuf, strlen(dbuf));
      return false;
    }

    data[i / 8] <<= 1;
    // Adafruit logic: if high cycle count > low cycle count, it's a 1!
    if (highCycles > lowCycles) {
      data[i / 8] |= 1;
    }
  }

  // 7. Checksum
  if (data[4] == ((data[0] + data[1] + data[2] + data[3]) & 0xFF)) {
    // Calculate final float values incorporating the decimal bytes!
    // Per DHT11/DHT22 hybrid datasheets, bits[1] and bits[3] contain the
    // decimal portions.
    *hum = (float)data[0] + ((float)data[1] * 0.1f);
    *temp = (float)data[2] + ((float)data[3] * 0.1f);
    return true;
  } else {
    char dbuf[80];
    sprintf(dbuf, "[DHT] CRC fail: %d+%d+%d+%d=%d, got %d\r\n", data[0],
            data[1], data[2], data[3],
            ((data[0] + data[1] + data[2] + data[3]) & 0xFF), data[4]);
    while (SERCOM3_USART_WriteIsBusy())
      ;
    SERCOM3_USART_Write((uint8_t *)dbuf, strlen(dbuf));
    return false;
  }
}
#undef DHT_PIN
#undef DHT_READ_PIN
#undef DHT_SET_OUTPUT
#undef DHT_SET_INPUT_FLOAT

// ======================== GPIO HELPERS ======================
// Software Bit-Banged SPI �?? Bypasses all SERCOM and TrustZone issues.
// MOSI = PA08, SCK = PA09

// TRUSTZONE-PROOF GPIO MACROS:
// These write to BOTH Secure and Non-Secure register aliases.
// TrustZone silently ignores alias writes that don't match the current CPU
// state. Writing to both guarantees the pins will toggle no matter what mode
// the MCU is in!
#define TFT_MOSI_HIGH()                                                        \
  do {                                                                         \
    PORT_SEC_REGS->GROUP[0].PORT_OUTSET = (1U << 8U);                          \
    PORT_REGS->GROUP[0].PORT_OUTSET = (1U << 8U);                              \
  } while (0)
#define TFT_MOSI_LOW()                                                         \
  do {                                                                         \
    PORT_SEC_REGS->GROUP[0].PORT_OUTCLR = (1U << 8U);                          \
    PORT_REGS->GROUP[0].PORT_OUTCLR = (1U << 8U);                              \
  } while (0)

#define TFT_SCK_HIGH()                                                         \
  do {                                                                         \
    PORT_SEC_REGS->GROUP[0].PORT_OUTSET = (1U << 9U);                          \
    PORT_REGS->GROUP[0].PORT_OUTSET = (1U << 9U);                              \
  } while (0)
#define TFT_SCK_LOW()                                                          \
  do {                                                                         \
    PORT_SEC_REGS->GROUP[0].PORT_OUTCLR = (1U << 9U);                          \
    PORT_REGS->GROUP[0].PORT_OUTCLR = (1U << 9U);                              \
  } while (0)

#define TFT_CS_HIGH()                                                          \
  do {                                                                         \
    PORT_SEC_REGS->GROUP[0].PORT_OUTSET = (1U << 10U);                         \
    PORT_REGS->GROUP[0].PORT_OUTSET = (1U << 10U);                             \
  } while (0)
#define TFT_CS_LOW()                                                           \
  do {                                                                         \
    PORT_SEC_REGS->GROUP[0].PORT_OUTCLR = (1U << 10U);                         \
    PORT_REGS->GROUP[0].PORT_OUTCLR = (1U << 10U);                             \
  } while (0)

#define TFT_DC_HIGH()                                                          \
  do {                                                                         \
    PORT_SEC_REGS->GROUP[0].PORT_OUTSET = (1U << 11U);                         \
    PORT_REGS->GROUP[0].PORT_OUTSET = (1U << 11U);                             \
  } while (0)
#define TFT_DC_LOW()                                                           \
  do {                                                                         \
    PORT_SEC_REGS->GROUP[0].PORT_OUTCLR = (1U << 11U);                         \
    PORT_REGS->GROUP[0].PORT_OUTCLR = (1U << 11U);                             \
  } while (0)

#define TFT_RST_HIGH()                                                         \
  do {                                                                         \
    PORT_SEC_REGS->GROUP[0].PORT_OUTSET = (1U << 12U);                         \
    PORT_REGS->GROUP[0].PORT_OUTSET = (1U << 12U);                             \
  } while (0)
#define TFT_RST_LOW()                                                          \
  do {                                                                         \
    PORT_SEC_REGS->GROUP[0].PORT_OUTCLR = (1U << 12U);                         \
    PORT_REGS->GROUP[0].PORT_OUTCLR = (1U << 12U);                             \
  } while (0)

#define PIN_OUTPUT_ENABLE(pin)                                                 \
  do {                                                                         \
    PORT_SEC_REGS->GROUP[0].PORT_DIRSET = (1U << pin);                         \
    PORT_REGS->GROUP[0].PORT_DIRSET = (1U << pin);                             \
  } while (0)

static void spi_send(uint8_t byte) {
  for (uint8_t i = 0; i < 8; i++) {
    // Mode 3 (CKP=1, CKE=0): Clock idles HIGH.
    // Data changes on High->Low transition, sampled on Low->High.
    TFT_SCK_LOW();

    if (byte & 0x80) {
      TFT_MOSI_HIGH();
    } else {
      TFT_MOSI_LOW();
    }
    byte <<= 1;

    // Tiny delay for Data Setup Time
    for (volatile int d = 0; d < 0; d++)
      ;

    // Clock High (sampled on this rising edge)
    TFT_SCK_HIGH();

    // Tiny delay for Clock High Time
    for (volatile int d = 0; d < 0; d++)
      ;
  }
}

// ======================== ST7735 LOW-LEVEL ==================
static void tft_write_cmd(uint8_t cmd) {
  TFT_DC_LOW();
  for (volatile int d = 0; d < 0; d++)
    ; // delay
  TFT_CS_LOW();
  for (volatile int d = 0; d < 0; d++)
    ; // delay
  spi_send(cmd);
  TFT_CS_HIGH();
  for (volatile int d = 0; d < 0; d++)
    ; // delay
}

static void tft_write_data(uint8_t data) {
  TFT_DC_HIGH();
  for (volatile int d = 0; d < 0; d++)
    ; // delay
  TFT_CS_LOW();
  for (volatile int d = 0; d < 0; d++)
    ; // delay
  spi_send(data);
  TFT_CS_HIGH();
  for (volatile int d = 0; d < 0; d++)
    ; // delay
}
#define ST7735_SWRESET 0x01
#define ST7735_SLPOUT 0x11
#define ST7735_FRMCTR1 0xB1
#define ST7735_FRMCTR2 0xB2
#define ST7735_FRMCTR3 0xB3
#define ST7735_INVCTR 0xB4
#define ST7735_PWCTR1 0xC0
#define ST7735_PWCTR2 0xC1
#define ST7735_PWCTR3 0xC2
#define ST7735_PWCTR4 0xC3
#define ST7735_PWCTR5 0xC4
#define ST7735_VMCTR1 0xC5
#define ST7735_INVOFF 0x20
#define ST7735_MADCTL 0x36
#define ST7735_COLMOD 0x3A
#define ST7735_CASET 0x2A
#define ST7735_RASET 0x2B
#define ST7735_RAMWR 0x2C
#define ST7735_GMCTRP1 0xE0
#define ST7735_GMCTRN1 0xE1
#define ST7735_NORON 0x13
#define ST7735_DISPON 0x29

// Colors (RGB565)
#define COLOR_BLACK 0x0000
#define COLOR_WHITE 0xFFFF
#define COLOR_RED 0xF800
#define COLOR_GREEN 0x07E0
#define COLOR_BLUE 0x001F
#define COLOR_YELLOW 0xFFE0

static void tft_reset(void) {
  // Not used directly, handled in tft_init()
}

static void tft_init(void) {
  // -------------------------------------------------------------
  // EXACT PORT from Github: ArmstrongSubero/PIC32-Projects ST7735
  // -------------------------------------------------------------
  TFT_RST_HIGH();
  delay_ms(1); // 500us
  TFT_RST_LOW();
  delay_ms(1); // 500us
  TFT_RST_HIGH();
  delay_ms(1); // 500us

  TFT_CS_LOW();

  tft_write_cmd(ST7735_SWRESET); // software reset
  delay_ms(150);

  tft_write_cmd(ST7735_SLPOUT); // out of sleep mode
  delay_ms(500);

  tft_write_cmd(ST7735_COLMOD); // set color mode
  tft_write_data(0x05);         // 16-bit color
  delay_ms(1);                  // 10us

  tft_write_cmd(ST7735_FRMCTR1); // frame rate control - normal mode
  tft_write_data(0x01); // frame rate = fosc / (1 x 2 + 40) * (LINE + 2C + 2D)
  tft_write_data(0x2C);
  tft_write_data(0x2D);

  tft_write_cmd(ST7735_FRMCTR2); // frame rate control - idle mode
  tft_write_data(0x01);
  tft_write_data(0x2C);
  tft_write_data(0x2D);

  tft_write_cmd(ST7735_FRMCTR3); // frame rate control - partial mode
  tft_write_data(0x01);          // dot inversion mode
  tft_write_data(0x2C);
  tft_write_data(0x2D);
  tft_write_data(0x01); // line inversion mode
  tft_write_data(0x2C);
  tft_write_data(0x2D);

  tft_write_cmd(ST7735_INVCTR); // display inversion control
  tft_write_data(0x07);         // no inversion

  tft_write_cmd(ST7735_PWCTR1); // power control
  tft_write_data(0xA2);
  tft_write_data(0x02); // -4.6V
  tft_write_data(0x84); // AUTO mode

  tft_write_cmd(ST7735_PWCTR2); // power control
  tft_write_data(0xC5);         // VGH25 = 2.4C VGSEL = -10 VGH = 3 * AVDD

  tft_write_cmd(ST7735_PWCTR3); // power control
  tft_write_data(0x0A);         // Opamp current small
  tft_write_data(0x00);         // Boost frequency

  tft_write_cmd(ST7735_PWCTR4); // power control
  tft_write_data(0x8A);         // BCLK/2, Opamp current small & Medium low
  tft_write_data(0x2A);

  tft_write_cmd(ST7735_PWCTR5); // power control
  tft_write_data(0x8A);
  tft_write_data(0xEE);

  tft_write_cmd(ST7735_VMCTR1); // power control
  tft_write_data(0x0E);

  tft_write_cmd(ST7735_INVOFF); // don't invert display

  tft_write_cmd(ST7735_MADCTL); // memory access control
  tft_write_data(0xA0);         // Landscape: 160 wide x 128 tall

  tft_write_cmd(ST7735_COLMOD); // set color mode
  tft_write_data(0x05);         // 16-bit color

  tft_write_cmd(ST7735_CASET); // column addr set (landscape: 0..159)
  tft_write_data(0x00);
  tft_write_data(0x00); // XSTART = 0
  tft_write_data(0x00);
  tft_write_data(0x9F); // XEND = 159

  tft_write_cmd(ST7735_RASET); // row addr set (landscape: 0..127)
  tft_write_data(0x00);
  tft_write_data(0x00); // YSTART = 0
  tft_write_data(0x00);
  tft_write_data(0x7F); // YEND = 127

  // Gamma Adjustments - Exact from ArmstrongSubero
  tft_write_cmd(ST7735_GMCTRP1);
  tft_write_data(0x0f);
  tft_write_data(0x1a);
  tft_write_data(0x0f);
  tft_write_data(0x18);
  tft_write_data(0x2f);
  tft_write_data(0x28);
  tft_write_data(0x20);
  tft_write_data(0x22);
  tft_write_data(0x1f);
  tft_write_data(0x1b);
  tft_write_data(0x23);
  tft_write_data(0x37);
  tft_write_data(0x00);
  tft_write_data(0x07);
  tft_write_data(0x02);
  tft_write_data(0x10);

  tft_write_cmd(ST7735_GMCTRN1);
  tft_write_data(0x0f);
  tft_write_data(0x1b);
  tft_write_data(0x0f);
  tft_write_data(0x17);
  tft_write_data(0x33);
  tft_write_data(0x2c);
  tft_write_data(0x29);
  tft_write_data(0x2e);
  tft_write_data(0x30);
  tft_write_data(0x30);
  tft_write_data(0x39);
  tft_write_data(0x3f);
  tft_write_data(0x00);
  tft_write_data(0x07);
  tft_write_data(0x03);
  tft_write_data(0x10);

  tft_write_cmd(0xF6); // Disable ram power save mode
  tft_write_data(0x00);

  tft_write_cmd(ST7735_DISPON);
  delay_ms(100);

  tft_write_cmd(ST7735_NORON); // normal display on
  delay_ms(10);

  // Return CS high
  TFT_CS_HIGH();
}

// ======================== DRAWING FUNCTIONS ==================
static void tft_set_window(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1) {
  tft_write_cmd(ST7735_CASET);
  tft_write_data(0x00);
  tft_write_data(x0);
  tft_write_data(0x00);
  tft_write_data(x1);

  tft_write_cmd(ST7735_RASET);
  tft_write_data(0x00);
  tft_write_data(y0);
  tft_write_data(0x00);
  tft_write_data(y1);

  tft_write_cmd(ST7735_RAMWR);
}

static void tft_fill_screen(uint16_t color) {
  uint8_t hi = color >> 8;
  uint8_t lo = color & 0xFF;

  tft_set_window(0, 0, 159, 127);

  TFT_DC_HIGH();
  TFT_CS_LOW();
  for (uint32_t i = 0; i < (160UL * 128UL); i++) {
    spi_send(hi);
    spi_send(lo);
  }
  TFT_CS_HIGH();
}

static void tft_draw_pixel(uint8_t x, uint8_t y, uint16_t color) {
  if (x >= 160 || y >= 128)
    return;
  tft_set_window(x, y, x, y);
  tft_write_data(color >> 8);
  tft_write_data(color & 0xFF);
}

static void tft_fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h,
                          uint16_t color) {
  uint8_t hi = color >> 8;
  uint8_t lo = color & 0xFF;

  if (x >= 160 || y >= 128)
    return;
  if (x + w > 160)
    w = 160 - x;
  if (y + h > 128)
    h = 128 - y;

  tft_set_window(x, y, x + w - 1, y + h - 1);

  TFT_DC_HIGH();
  TFT_CS_LOW();
  for (uint16_t i = 0; i < (uint16_t)w * h; i++) {
    spi_send(hi);
    spi_send(lo);
  }
  TFT_CS_HIGH();
}

// ======================== 5x7 FONT ==========================
static const uint8_t font5x7[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, // (space)
    0x00, 0x00, 0x5F, 0x00, 0x00, // !
    0x00, 0x07, 0x00, 0x07, 0x00, // "
    0x14, 0x7F, 0x14, 0x7F, 0x14, // #
    0x24, 0x2A, 0x7F, 0x2A, 0x12, // $
    0x23, 0x13, 0x08, 0x64, 0x62, // %
    0x36, 0x49, 0x55, 0x22, 0x50, // &
    0x00, 0x05, 0x03, 0x00, 0x00, // '
    0x00, 0x1C, 0x22, 0x41, 0x00, // (
    0x00, 0x41, 0x22, 0x1C, 0x00, // )
    0x08, 0x2A, 0x1C, 0x2A, 0x08, // *
    0x08, 0x08, 0x3E, 0x08, 0x08, // +
    0x00, 0x50, 0x30, 0x00, 0x00, // ,
    0x08, 0x08, 0x08, 0x08, 0x08, // -
    0x00, 0x60, 0x60, 0x00, 0x00, // .
    0x20, 0x10, 0x08, 0x04, 0x02, // /
    0x3E, 0x51, 0x49, 0x45, 0x3E, // 0
    0x00, 0x42, 0x7F, 0x40, 0x00, // 1
    0x42, 0x61, 0x51, 0x49, 0x46, // 2
    0x21, 0x41, 0x45, 0x4B, 0x31, // 3
    0x18, 0x14, 0x12, 0x7F, 0x10, // 4
    0x27, 0x45, 0x45, 0x45, 0x39, // 5
    0x3C, 0x4A, 0x49, 0x49, 0x30, // 6
    0x01, 0x71, 0x09, 0x05, 0x03, // 7
    0x36, 0x49, 0x49, 0x49, 0x36, // 8
    0x06, 0x49, 0x49, 0x29, 0x1E, // 9
    0x00, 0x36, 0x36, 0x00, 0x00, // :
    0x00, 0x56, 0x36, 0x00, 0x00, // ;
    0x00, 0x08, 0x14, 0x22, 0x41, // <
    0x14, 0x14, 0x14, 0x14, 0x14, // =
    0x41, 0x22, 0x14, 0x08, 0x00, // >
    0x02, 0x01, 0x51, 0x09, 0x06, // ?
    0x32, 0x49, 0x79, 0x41, 0x3E, // @
    0x7E, 0x11, 0x11, 0x11, 0x7E, // A
    0x7F, 0x49, 0x49, 0x49, 0x36, // B
    0x3E, 0x41, 0x41, 0x41, 0x22, // C
    0x7F, 0x41, 0x41, 0x22, 0x1C, // D
    0x7F, 0x49, 0x49, 0x49, 0x41, // E
    0x7F, 0x09, 0x09, 0x01, 0x01, // F
    0x3E, 0x41, 0x41, 0x51, 0x32, // G
    0x7F, 0x08, 0x08, 0x08, 0x7F, // H
    0x00, 0x41, 0x7F, 0x41, 0x00, // I
    0x20, 0x40, 0x41, 0x3F, 0x01, // J
    0x7F, 0x08, 0x14, 0x22, 0x41, // K
    0x7F, 0x40, 0x40, 0x40, 0x40, // L
    0x7F, 0x02, 0x04, 0x02, 0x7F, // M
    0x7F, 0x04, 0x08, 0x10, 0x7F, // N
    0x3E, 0x41, 0x41, 0x41, 0x3E, // O
    0x7F, 0x09, 0x09, 0x09, 0x06, // P
    0x3E, 0x41, 0x51, 0x21, 0x5E, // Q
    0x7F, 0x09, 0x19, 0x29, 0x46, // R
    0x46, 0x49, 0x49, 0x49, 0x31, // S
    0x01, 0x01, 0x7F, 0x01, 0x01, // T
    0x3F, 0x40, 0x40, 0x40, 0x3F, // U
    0x1F, 0x20, 0x40, 0x20, 0x1F, // V
    0x7F, 0x20, 0x18, 0x20, 0x7F, // W
    0x63, 0x14, 0x08, 0x14, 0x63, // X
    0x03, 0x04, 0x78, 0x04, 0x03, // Y
    0x61, 0x51, 0x49, 0x45, 0x43, // Z
    0x00, 0x00, 0x7F, 0x41, 0x41, // [
    0x02, 0x04, 0x08, 0x10, 0x20, // backslash
    0x41, 0x41, 0x7F, 0x00, 0x00, // ]
    0x04, 0x02, 0x01, 0x02, 0x04, // ^
    0x40, 0x40, 0x40, 0x40, 0x40, // _
    0x00, 0x01, 0x02, 0x04, 0x00, // `
    0x20, 0x54, 0x54, 0x54, 0x78, // a
    0x7F, 0x48, 0x44, 0x44, 0x38, // b
    0x38, 0x44, 0x44, 0x44, 0x20, // c
    0x38, 0x44, 0x44, 0x48, 0x7F, // d
    0x38, 0x54, 0x54, 0x54, 0x18, // e
    0x08, 0x7E, 0x09, 0x01, 0x02, // f
    0x08, 0x14, 0x54, 0x54, 0x3C, // g
    0x7F, 0x08, 0x04, 0x04, 0x78, // h
    0x00, 0x44, 0x7D, 0x40, 0x00, // i
    0x20, 0x40, 0x44, 0x3D, 0x00, // j
    0x00, 0x7F, 0x10, 0x28, 0x44, // k
    0x00, 0x41, 0x7F, 0x40, 0x00, // l
    0x7C, 0x04, 0x18, 0x04, 0x78, // m
    0x7C, 0x08, 0x04, 0x04, 0x78, // n
    0x38, 0x44, 0x44, 0x44, 0x38, // o
    0x7C, 0x14, 0x14, 0x14, 0x08, // p
    0x08, 0x14, 0x14, 0x18, 0x7C, // q
    0x7C, 0x08, 0x04, 0x04, 0x08, // r
    0x48, 0x54, 0x54, 0x54, 0x20, // s
    0x04, 0x3F, 0x44, 0x40, 0x20, // t
    0x3C, 0x40, 0x40, 0x20, 0x7C, // u
    0x1C, 0x20, 0x40, 0x20, 0x1C, // v
    0x3C, 0x40, 0x30, 0x40, 0x3C, // w
    0x44, 0x28, 0x10, 0x28, 0x44, // x
    0x0C, 0x50, 0x50, 0x50, 0x3C, // y
    0x44, 0x64, 0x54, 0x4C, 0x44, // z
    0x00, 0x08, 0x36, 0x41, 0x00, // {
    0x00, 0x00, 0x7F, 0x00, 0x00, // |
    0x00, 0x41, 0x36, 0x08, 0x00, // }
    0x08, 0x08, 0x2A, 0x1C, 0x08, // ~
};

// Draw a single character at (x, y) with given color, bg, and scale
static void tft_draw_char(uint8_t x, uint8_t y, char c, uint16_t fg,
                          uint16_t bg, uint8_t size) {
  if (c < ' ' || c > '~')
    c = '?';
  uint8_t idx = c - ' ';

  for (uint8_t col = 0; col < 5; col++) {
    uint8_t line = font5x7[idx * 5 + col];
    for (uint8_t row = 0; row < 7; row++) {
      uint16_t color = (line & (1 << row)) ? fg : bg;
      if (size == 1) {
        tft_draw_pixel(x + col, y + row, color);
      } else {
        tft_fill_rect(x + col * size, y + row * size, size, size, color);
      }
    }
  }
  // 1-pixel gap between characters (background)
  if (size == 1) {
    for (uint8_t row = 0; row < 7; row++)
      tft_draw_pixel(x + 5, y + row, bg);
  } else {
    tft_fill_rect(x + 5 * size, y, size, 7 * size, bg);
  }
}

// Draw a null-terminated string
static void tft_draw_string(uint8_t x, uint8_t y, const char *str, uint16_t fg,
                            uint16_t bg, uint8_t size) {
  while (*str) {
    tft_draw_char(x, y, *str, fg, bg, size);
    x += 6 * size; // 5 pixel char + 1 pixel gap, times scale
    str++;
  }
}

// ======================== BITMAP DRAW ========================
// Draws the full bitmap.
static void tft_draw_bitmap(uint8_t x, uint8_t y, uint8_t w, uint8_t h,
                            const uint16_t *bitmap) {
  tft_set_window(x, y, x + w - 1, y + h - 1);
  TFT_DC_HIGH();
  TFT_CS_LOW();
  uint32_t total = (uint32_t)w * h;
  for (uint32_t i = 0; i < total; i++) {
    uint16_t color = bitmap[i];
    spi_send((uint8_t)(color >> 8));
    spi_send((uint8_t)(color & 0xFF));
  }
  TFT_CS_HIGH();
}

// Draws only the top rows rows of the bitmap (for wipe animation).
static void tft_draw_bitmap_rows(uint8_t x, uint8_t y, uint8_t w, uint8_t h,
                                 const uint16_t *bitmap, uint8_t rows) {
  if (rows == 0)
    return;
  if (rows > h)
    rows = h;
  tft_set_window(x, y, x + w - 1, y + rows - 1);
  TFT_DC_HIGH();
  TFT_CS_LOW();
  uint32_t total = (uint32_t)w * rows;
  for (uint32_t i = 0; i < total; i++) {
    uint16_t color = bitmap[i];
    spi_send((uint8_t)(color >> 8));
    spi_send((uint8_t)(color & 0xFF));
  }
  TFT_CS_HIGH();
}

// Draws a rectangular sub-region of a bitmap onto the screen.
// dest_x/y  = top-left on screen
// bmp       = full bitmap pointer, bmp_w = bitmap's total width
// src_x/y   = top-left corner within the source bitmap
// draw_w/h  = how many pixels wide/tall to copy
static void tft_draw_bitmap_region(uint8_t dest_x, uint8_t dest_y,
                                   const uint16_t *bmp, uint8_t bmp_w,
                                   uint8_t src_x, uint8_t src_y, uint8_t draw_w,
                                   uint8_t draw_h) {
  tft_set_window(dest_x, dest_y, dest_x + draw_w - 1, dest_y + draw_h - 1);
  TFT_DC_HIGH();
  TFT_CS_LOW();
  for (uint8_t row = 0; row < draw_h; row++) {
    uint32_t offset = (uint32_t)(src_y + row) * bmp_w + src_x;
    for (uint8_t col = 0; col < draw_w; col++) {
      uint16_t color = bmp[offset + col];
      spi_send((uint8_t)(color >> 8));
      spi_send((uint8_t)(color & 0xFF));
    }
  }
  TFT_CS_HIGH();
}

// ======================== CIRCLE DRAW ========================
// Bresenham midpoint circle �?? filled
static void tft_fill_circle(uint8_t cx, uint8_t cy, uint8_t r, uint16_t color) {
  int16_t x = 0, y = (int16_t)r, d = 1 - (int16_t)r;
  while (x <= y) {
    // Draw horizontal spans for each octant pair
    int16_t x0, x1, ys;
    // span at ±y rows
    ys = (int16_t)cy - y;
    if (ys >= 0) {
      x0 = (int16_t)cx - x;
      if (x0 < 0)
        x0 = 0;
      x1 = (int16_t)cx + x;
      if (x1 > 127)
        x1 = 127;
      if (x0 <= x1)
        tft_fill_rect((uint8_t)x0, (uint8_t)ys, (uint8_t)(x1 - x0 + 1), 1,
                      color);
    }
    ys = (int16_t)cy + y;
    if (ys <= 159) {
      x0 = (int16_t)cx - x;
      if (x0 < 0)
        x0 = 0;
      x1 = (int16_t)cx + x;
      if (x1 > 127)
        x1 = 127;
      if (x0 <= x1)
        tft_fill_rect((uint8_t)x0, (uint8_t)ys, (uint8_t)(x1 - x0 + 1), 1,
                      color);
    }
    // span at ±x rows
    ys = (int16_t)cy - x;
    if (ys >= 0) {
      x0 = (int16_t)cx - y;
      if (x0 < 0)
        x0 = 0;
      x1 = (int16_t)cx + y;
      if (x1 > 127)
        x1 = 127;
      if (x0 <= x1)
        tft_fill_rect((uint8_t)x0, (uint8_t)ys, (uint8_t)(x1 - x0 + 1), 1,
                      color);
    }
    ys = (int16_t)cy + x;
    if (ys <= 159) {
      x0 = (int16_t)cx - y;
      if (x0 < 0)
        x0 = 0;
      x1 = (int16_t)cx + y;
      if (x1 > 127)
        x1 = 127;
      if (x0 <= x1)
        tft_fill_rect((uint8_t)x0, (uint8_t)ys, (uint8_t)(x1 - x0 + 1), 1,
                      color);
    }
    if (d < 0) {
      d += 2 * x + 3;
    } else {
      d += 2 * (x - y) + 5;
      y--;
    }
    x++;
  }
}

// ======================== AQI DASHBOARD ========================
#define C_BG 0x0000       // Pure black / very dark background
#define C_TXT_MAIN 0xF800 // Bright red for primary text
#define C_TXT_SUB 0x9000  // Dim red for secondary text
#define C_GRAY 0x52AA     // Grey for offline status
#define C_CIRCLE 0x7800   // Deep red for the main gauge circle
#define C_BAR1 0xF800     // Bright red for bar
#define C_BAR2 0xB000     // Medium red for bar
#define C_BAR3 0x6000     // Dark red for bar

// Rough approximation of the IOTA / microchip dot logo
static void draw_dot_logo(uint8_t cx, uint8_t cy) {
  tft_fill_circle(cx, cy, 3, C_TXT_MAIN);
  tft_fill_circle(cx - 7, cy - 6, 2, C_TXT_MAIN);
  tft_fill_circle(cx + 7, cy - 6, 2, C_TXT_MAIN);
  tft_fill_circle(cx - 7, cy + 6, 2, C_TXT_MAIN);
  tft_fill_circle(cx + 7, cy + 6, 2, C_TXT_MAIN);
  tft_fill_circle(cx - 12, cy, 1, C_TXT_MAIN);
  tft_fill_circle(cx + 12, cy, 1, C_TXT_MAIN);
  tft_fill_circle(cx, cy - 11, 1, C_TXT_MAIN);
  tft_fill_circle(cx, cy + 11, 1, C_TXT_MAIN);
  tft_fill_circle(cx - 12, cy - 9, 1, C_TXT_MAIN);
  tft_fill_circle(cx + 12, cy + 9, 1, C_TXT_MAIN);
  tft_fill_circle(cx + 5, cy - 13, 1, C_TXT_MAIN);
  tft_fill_circle(cx - 5, cy + 13, 1, C_TXT_MAIN);
}

static void tft_draw_circle_outline(int16_t cx, int16_t cy, int16_t r,
                                    uint16_t color) {
  int16_t f = 1 - r;
  int16_t ddF_x = 1;
  int16_t ddF_y = -2 * r;
  int16_t x = 0;
  int16_t y = r;

  tft_draw_pixel(cx, cy + r, color);
  tft_draw_pixel(cx, cy - r, color);
  tft_draw_pixel(cx + r, cy, color);
  tft_draw_pixel(cx - r, cy, color);

  while (x < y) {
    if (f >= 0) {
      y--;
      ddF_y += 2;
      f += ddF_y;
    }
    x++;
    ddF_x += 2;
    f += ddF_x;
    tft_draw_pixel(cx + x, cy + y, color);
    tft_draw_pixel(cx - x, cy + y, color);
    tft_draw_pixel(cx + x, cy - y, color);
    tft_draw_pixel(cx - x, cy - y, color);
    tft_draw_pixel(cx + y, cy + x, color);
    tft_draw_pixel(cx - y, cy + x, color);
    tft_draw_pixel(cx + y, cy - x, color);
    tft_draw_pixel(cx - y, cy - x, color);
  }
}

static uint16_t get_aqi_color(int aqi) {
  if (aqi <= 50)
    return 0x24C8; // Good: Emerald Green
  if (aqi <= 100)
    return 0xEC40; // Moderate: Golden/Orange
  if (aqi <= 150)
    return 0xFA48; // High: Salmon Red
  if (aqi <= 200)
    return 0xE248; // Unhealthy: Distinct Red
  if (aqi <= 300)
    return 0x933B; // Very Unhealthy: Vibrant Purple
  return 0x8186;   // Hazardous: Deep Maroon Red
}

static const char *get_aqi_label(int aqi) {
  if (aqi <= 50)
    return "Good";
  if (aqi <= 100)
    return "Moderate";
  if (aqi <= 150)
    return "High";
  if (aqi <= 200)
    return "Unhealthy";
  if (aqi <= 300)
    return "Very poor";
  return "Hazardous";
}

static void tft_draw_rect(int x, int y, int w, int h, uint16_t color) {
  tft_fill_rect(x, y, w, 1, color);
  tft_fill_rect(x, y + h - 1, w, 1, color);
  tft_fill_rect(x, y, 1, h, color);
  tft_fill_rect(x + w - 1, y, 1, h, color);
}

static void draw_chip(int x, int y, int w, const char *text, uint16_t color) {
  tft_draw_rect(x, y, w, 11, color);
  // Center text roughly in chip
  int len = 0;
  while (text[len] != '\0')
    len++;
  int tw = len * 6;
  tft_draw_string(x + (w - tw) / 2, y + 2, text, color, 0xFFFF, 1);
}

static const uint8_t chalf[43] = {42, 42, 42, 42, 42, 42, 42, 41, 41, 41, 41,
                                  41, 40, 40, 40, 39, 39, 38, 38, 37, 37, 36,
                                  36, 35, 34, 34, 33, 32, 31, 30, 29, 28, 27,
                                  26, 25, 23, 22, 20, 18, 16, 13, 9,  0};
static const int8_t swave[50] = {
    0,  1,  1,  2,  3,  4,  4,  5,  5,  5,  6,  6,  6,  6,  6,  6,  5,
    5,  5,  4,  4,  3,  2,  1,  1,  0,  -1, -1, -2, -3, -4, -4, -5, -5,
    -5, -6, -6, -6, -6, -6, -6, -5, -5, -5, -4, -4, -3, -2, -1, -1};

static uint16_t get_pastel(uint16_t c) {
  uint8_t r = (c >> 11) & 0x1F;
  uint8_t g = (c >> 5) & 0x3F;
  uint8_t b = c & 0x1F;
  // Blend tightly 50/50 with white for a beautiful glass soft pastel effect
  r = (r + 31) / 2;
  g = (g + 63) / 2;
  b = (b + 31) / 2;
  return (r << 11) | (g << 5) | b;
}

static void draw_clipped_column(int x, int y_start, int y_end, uint16_t color,
                                int num_sx, int num_ex) {
  if (y_start > y_end)
    return;
  int run_start = -1;

  for (int y = y_start; y <= y_end + 1; y++) {
    bool skip = false;
    if (y <= y_end) {
      if (y >= 22 && y <= 29 && x >= 53 && x <= 106)
        skip = true;
      if (y >= 72 && y <= 79 && x >= 47 && x <= 112)
        skip = true;
      if (y >= 36 && y <= 63 && x >= num_sx && x <= num_ex)
        skip = true;
    } else {
      skip = true;
    }

    if (!skip) {
      if (run_start == -1)
        run_start = y;
    } else {
      if (run_start != -1) {
        tft_fill_rect(x, run_start, 1, y - run_start, color);
        run_start = -1;
      }
    }
  }
}

static void draw_bottom_wave(uint16_t active_color, uint8_t wt,
                             int current_aqi) {
  uint16_t wave_col = get_pastel(active_color);
  uint16_t bg = 0xFFFF;

  int char_w = 24;
  int len = current_aqi >= 100 ? 3 : (current_aqi >= 10 ? 2 : 1);
  int num_sx = 80 - ((len * char_w) / 2);
  int num_ex = num_sx + (len * char_w) - 1;

  int fill_h = (current_aqi * 84) / 500;
  if (fill_h > 84)
    fill_h = 84;
  int base_wy = 96 - fill_h;

  for (int dx = 0; dx <= 42; dx++) {
    int c_bot = 54 + chalf[dx] - 1;
    int c_top = 54 - chalf[dx] + 1;

    int lx = 80 - dx;
    int l_wy = base_wy + swave[(lx + wt) % 50];
    if (l_wy < c_top)
      l_wy = c_top;
    if (l_wy > c_bot)
      l_wy = c_bot;

    if (l_wy > c_top)
      draw_clipped_column(lx, c_top, l_wy - 1, bg, num_sx, num_ex);
    if (c_bot >= l_wy)
      draw_clipped_column(lx, l_wy, c_bot, wave_col, num_sx, num_ex);

    if (dx > 0) {
      int rx = 80 + dx;
      int r_wy = base_wy + swave[(rx + wt) % 50];
      if (r_wy < c_top)
        r_wy = c_top;
      if (r_wy > c_bot)
        r_wy = c_bot;

      if (r_wy > c_top)
        draw_clipped_column(rx, c_top, r_wy - 1, bg, num_sx, num_ex);
      if (c_bot >= r_wy)
        draw_clipped_column(rx, r_wy, c_bot, wave_col, num_sx, num_ex);
    }
  }
}

// ============================================================
// PAGE 1 �?? Logo Splash
// Renders the exact bitmap from logo.h (bannari_logo, 160x128)
// which contains the pre-rendered Microchip + Bannari Amman logos.
// Holds for 5 seconds then fades out with a top�??bottom white wipe.
// ============================================================
static void draw_logo_page(void) {
  // ---- Display the exact combined logo bitmap (full screen 160x128) ----
  tft_draw_bitmap(0, 0, LOGO_W, LOGO_H, bannari_logo);

  // ---- Hold for 5 seconds ----
  delay_ms(5000);

  // ---- Slow FADE-OUT: top-to-bottom white curtain wipe (~900 ms) ----
  // 32 bands �? 4 px each �? 28 ms delay = ~896 ms
  for (uint8_t y = 0; y < 128; y += 4) {
    tft_fill_rect(0, y, 160, 4, 0xFFFF); // overwrite strip with white
    delay_ms(28);
  }
  // Final safety clear
  tft_fill_screen(0xFFFF);
}

// ============================================================
// Helper: read and compute all 5 sensor values into floats
// ============================================================
static void read_all_sensors(float *ppm_o3, float *ppm_co, float *ppm_nh3,
                             float *pm25, float *co2) {
  float R0_MQ131 = 20000.0f;
  float R0_MQ7 = 20000.0f;
  float R0_MQ135 = 20000.0f;

  float v1 = adc_to_voltage(read_adc_avg(3));
  if (v1 < 0.15f) {
    *ppm_o3 = 0.0f;
  } else {
    *ppm_o3 = pow(10, ((log10(calculate_Rs(v1) / R0_MQ131) - 0.8f) / -0.7f));
    if (*ppm_o3 > 9999.0f)
      *ppm_o3 = 9999.0f;
    if (*ppm_o3 < 0.0f)
      *ppm_o3 = 0.0f;
  }

  float v2 = adc_to_voltage(read_adc_avg(4));
  if (v2 < 0.15f) {
    *ppm_co = 0.0f;
  } else {
    *ppm_co = pow(10, ((log10(calculate_Rs(v2) / R0_MQ7) - 0.77f) / -0.47f));
    if (*ppm_co > 9999.0f)
      *ppm_co = 9999.0f;
    if (*ppm_co < 0.0f)
      *ppm_co = 0.0f;
  }

  float v3 = adc_to_voltage(read_adc_avg(5));
  if (v3 < 0.15f) {
    *ppm_nh3 = 0.0f;
  } else {
    *ppm_nh3 = pow(10, ((log10(calculate_Rs(v3) / R0_MQ135) - 0.42f) / -0.48f));
    if (*ppm_nh3 > 9999.0f)
      *ppm_nh3 = 9999.0f;
    if (*ppm_nh3 < 0.0f)
      *ppm_nh3 = 0.0f;
  }

  *pm25 = read_dust();
  *co2 = read_mg811_co2();
}

// ============================================================
// AQI EPA breakpoint linear interpolation
// Formula: Ip = [(I_Hi - I_Lo) / (BP_Hi - BP_Lo)] * (C_p - BP_Lo) + I_Lo
static int aqi_linear(int I_Lo, int I_Hi, float BP_Lo, float BP_Hi, float C_p) {
  return (int)(((float)(I_Hi - I_Lo) / (BP_Hi - BP_Lo)) * (C_p - BP_Lo) +
               (float)I_Lo);
}

static int aqi_from_pm25(float c) {
  if (c < 0.0f)
    c = 0.0f;
  if (c <= 30.0f)
    return aqi_linear(0, 50, 0.0f, 30.0f, c);
  if (c <= 60.0f)
    return aqi_linear(51, 100, 31.0f, 60.0f, c);
  if (c <= 90.0f)
    return aqi_linear(101, 200, 61.0f, 90.0f, c);
  if (c <= 120.0f)
    return aqi_linear(201, 300, 91.0f, 120.0f, c);
  if (c <= 250.0f)
    return aqi_linear(301, 400, 121.0f, 250.0f, c);
  if (c <= 500.0f)
    return aqi_linear(401, 500, 251.0f, 500.0f, c);
  return 500;
}

static int aqi_from_co(float c) { // c in mg/m3 (CPCB Standard)
  if (c < 0.0f)
    c = 0.0f;
  if (c <= 2.0f)
    return aqi_linear(0, 50, 0.0f, 2.0f, c);
  if (c <= 10.0f)
    return aqi_linear(51, 100, 2.1f, 10.0f, c);
  if (c <= 17.0f)
    return aqi_linear(101, 200, 10.1f, 17.0f, c);
  if (c <= 34.0f)
    return aqi_linear(201, 300, 17.1f, 34.0f, c);
  if (c <= 44.0f)
    return aqi_linear(301, 400, 34.1f, 44.0f, c);
  return aqi_linear(401, 500, 44.1f, 100.0f, c);
}

static int aqi_from_o3(float c) { // c in ug/m3 (CPCB Standard)
  if (c < 0.0f)
    c = 0.0f;
  if (c <= 50.0f)
    return aqi_linear(0, 50, 0.0f, 50.0f, c);
  if (c <= 100.0f)
    return aqi_linear(51, 100, 51.0f, 100.0f, c);
  if (c <= 168.0f)
    return aqi_linear(101, 200, 101.0f, 168.0f, c);
  if (c <= 208.0f)
    return aqi_linear(201, 300, 169.0f, 208.0f, c);
  if (c <= 748.0f)
    return aqi_linear(301, 400, 209.0f, 748.0f, c);
  return aqi_linear(401, 500, 749.0f, 1000.0f, c);
}

static int aqi_from_nh3(float c) { // c in ug/m3 (CPCB Standard)
  if (c < 0.0f)
    c = 0.0f;
  if (c <= 200.0f)
    return aqi_linear(0, 50, 0.0f, 200.0f, c);
  if (c <= 400.0f)
    return aqi_linear(51, 100, 201.0f, 400.0f, c);
  if (c <= 800.0f)
    return aqi_linear(101, 200, 401.0f, 800.0f, c);
  if (c <= 1200.0f)
    return aqi_linear(201, 300, 801.0f, 1200.0f, c);
  if (c <= 1800.0f)
    return aqi_linear(301, 400, 1201.0f, 1800.0f, c);
  return aqi_linear(401, 500, 1801.0f, 2500.0f, c);
}

// Calculates combined AQI using max of all sub-indices.
static int calculate_real_aqi(void) {
  float o3, co, nh3, dust, co2;
  read_all_sensors(&o3, &co, &nh3, &dust, &co2);
  int a_pm = aqi_from_pm25(dust);
  int a_co = aqi_from_co(co);
  int a_o3 = aqi_from_o3(o3);
  int a_nh3 = aqi_from_nh3(nh3);
  // Take the worst (highest) sub-index as overall AQI
  int aqi = a_pm;
  if (a_co > aqi)
    aqi = a_co;
  if (a_nh3 > aqi)
    aqi = a_nh3;
  if (a_o3 > aqi)
    aqi = a_o3;
  if (aqi > 500)
    aqi = 500;
  if (aqi < 0)
    aqi = 0;
  return aqi;
}

static void diagnose_air_quality(float o3, float co, float nh3, float dust, float co2, char* out_buf, int buf_size, char** out_class, float* out_conf) {
    // 1. Calculate Mathematical Deviations from Normal Baseline
    float o3_base = 1.4f, o3_max = 5.0f;
    float co_base = 9.0f, co_max = 30.0f;
    float nh3_base = 0.9f, nh3_max = 5.0f;
    float dust_base = 0.0f, dust_max = 100.0f;
    float co2_base = 546.0f, co2_max = 1000.0f;

    float dev_o3 = (o3 <= o3_base) ? 0.0f : ((o3 - o3_base) / (o3_max - o3_base)) * 100.0f;
    if (dev_o3 > 100.0f) dev_o3 = 100.0f;
    
    float dev_co = (co <= co_base) ? 0.0f : ((co - co_base) / (co_max - co_base)) * 100.0f;
    if (dev_co > 100.0f) dev_co = 100.0f;
    
    float dev_nh3 = (nh3 <= nh3_base) ? 0.0f : ((nh3 - nh3_base) / (nh3_max - nh3_base)) * 100.0f;
    if (dev_nh3 > 100.0f) dev_nh3 = 100.0f;
    
    float dev_dust = (dust <= dust_base) ? 0.0f : ((dust - dust_base) / (dust_max - dust_base)) * 100.0f;
    if (dev_dust > 100.0f) dev_dust = 100.0f;
    
    float dev_co2 = (co2 <= co2_base) ? 0.0f : ((co2 - co2_base) / (co2_max - co2_base)) * 100.0f;
    if (dev_co2 > 100.0f) dev_co2 = 100.0f;

    // 2. Compute Class Similarity Distances
    float sim_vent = dev_co2;
    float sim_fire = (dev_dust + dev_co) / 2.0f;
    float sim_chem = dev_nh3;
    float sim_exhaust = (dev_o3 + dev_co) / 2.0f;
    
    float max_sim = sim_vent;
    if (sim_fire > max_sim) max_sim = sim_fire;
    if (sim_chem > max_sim) max_sim = sim_chem;
    if (sim_exhaust > max_sim) max_sim = sim_exhaust;
    
    float sim_safe = 100.0f - max_sim;
    if (sim_safe < 0.0f) sim_safe = 0.0f;

    // 3. Determine Final Inference Label
    const char* pred_class = "NORMAL ENVIRONMENT";
    float confidence = sim_safe / 100.0f;
    
    if (max_sim > 20.0f) { // If any danger pattern exceeds 20%
        if (max_sim == sim_vent) { pred_class = "POOR VENTILATION"; confidence = sim_vent / 100.0f; }
        else if (max_sim == sim_fire) { pred_class = "FIRE/SMOKE HAZARD"; confidence = sim_fire / 100.0f; }
        else if (max_sim == sim_chem) { pred_class = "CHEMICAL LEAK"; confidence = sim_chem / 100.0f; }
        else if (max_sim == sim_exhaust) { pred_class = "EXHAUST FUMES"; confidence = sim_exhaust / 100.0f; }
    }

    if (out_class) *out_class = (char*)pred_class;
    if (out_conf) *out_conf = confidence;

    // 4. Format Output String
    snprintf(out_buf, buf_size,
        "\r\n--- EdgeAQI Algorithmic Diagnosis ---\r\n"
        "  [Features Analyzed: O3, CO, NH3, PM2.5, CO2]\r\n"
        "  \r\n"
        "  >> Calculating Sensor Deviations...\r\n"
        "     Ozone (O3)           : %6.2f%% (%s)\r\n"
        "     Carbon Monoxide (CO) : %6.2f%% (%s)\r\n"
        "     Ammonia (NH3)        : %6.2f%% (%s)\r\n"
        "     Dust (PM2.5)         : %6.2f%% (%s)\r\n"
        "     Carbon Dioxide (CO2) : %6.2f%% (%s)\r\n"
        "  \r\n"
        "  >> Computing Classification Similarity...\r\n"
        "     Similarity to [Safe Environment]   : %6.2f%%\r\n"
        "     Similarity to [Poor Ventilation]   : %6.2f%%\r\n"
        "     Similarity to [Fire/Smoke Hazard]  : %6.2f%%\r\n"
        "     Similarity to [Chemical Leak]      : %6.2f%%\r\n"
        "     Similarity to [Exhaust Fumes]      : %6.2f%%\r\n"
        "  \r\n"
        "  >> FINAL DIAGNOSIS INFERENCE:\r\n"
        "     Diagnosis Class : %s\r\n"
        "     Confidence Score: %.2f\r\n"
        "---------------------------------------\r\n",
        dev_o3, (dev_o3 > 20.0f ? "ELEVATED!" : "Normal"),
        dev_co, (dev_co > 20.0f ? "ELEVATED!" : "Normal"),
        dev_nh3, (dev_nh3 > 20.0f ? "ELEVATED!" : "Normal"),
        dev_dust, (dev_dust > 20.0f ? "ELEVATED!" : "Normal"),
        dev_co2, (dev_co2 > 20.0f ? "ELEVATED!" : "Normal"),
        sim_safe, sim_vent, sim_fire, sim_chem, sim_exhaust,
        pred_class, confidence
    );
}

static void update_background_sensors(int *out_aqi, float *out_o3,
                                      float *out_co, float *out_nh3,
                                      float *out_dust, float *out_co2,
                                      float *out_t, float *out_h) {
  float o3, co, nh3, dust, co2;
  read_all_sensors(&o3, &co, &nh3, &dust, &co2);

  int a_pm = aqi_from_pm25(dust);
  int a_co = aqi_from_co(co);
  int a_o3 = aqi_from_o3(o3);
  int a_nh3 = aqi_from_nh3(nh3);
  int new_aqi = a_pm;
  if (a_co > new_aqi)
    new_aqi = a_co;
  if (a_nh3 > new_aqi)
    new_aqi = a_nh3;
  if (a_o3 > new_aqi)
    new_aqi = a_o3;
  if (new_aqi > 500)
    new_aqi = 500;
  if (new_aqi < 0)
    new_aqi = 0;

  static int dht_tick = 50;
  static float last_t = -1.0f, last_h = -1.0f;

  if (++dht_tick > 50) {
    dht_tick = 0;
    float t = 0, h = 0;
    if (read_dht11(&t, &h)) {
      last_t = t;
      last_h = h;
      char dht_ok[48];
      sprintf(dht_ok, "[DHT] OK Temp:%.1fC Hum:%.1f%%\r\n", t, h);
      while (SERCOM3_USART_WriteIsBusy())
        ;
      SERCOM3_USART_Write((uint8_t *)dht_ok, strlen(dht_ok));
    }
  }

  char tbuf[250];
  sprintf(tbuf,
          "O3:%.1f CO:%.1f NH3:%.1f Dust:%.1f CO2:%.1f AQI:%d Temp:%.1fC "
          "Hum:%.1f%%\r\n",
          o3, co, nh3, dust, co2, new_aqi, last_t, last_h);
  while (SERCOM3_USART_WriteIsBusy())
    ;
  SERCOM3_USART_Write((uint8_t *)tbuf, strlen(tbuf));

  // -------- EdgeAQI Algorithmic Output --------
  char diag_buf[1024]; 
  char* pred_class = "UNKNOWN";
  float confidence = 0.0f;
  diagnose_air_quality(o3, co, nh3, dust, co2, diag_buf, sizeof(diag_buf), &pred_class, &confidence);
  while (SERCOM3_USART_WriteIsBusy())
    ;
  SERCOM3_USART_Write((uint8_t *)diag_buf, strlen(diag_buf));
  // --------------------------------------------

  /* Old TinyML Inference (Commented out because algorithmic output replaces it)
  if (strstr(diag_buf, "Normal") == NULL) {
    float features[7] = { o3, co, nh3, dust, co2, last_t, last_h };
    char ml_buf[300];
    tinyml_run_inference(features, 7, ml_buf, sizeof(ml_buf));
    while (SERCOM3_USART_WriteIsBusy());
    SERCOM3_USART_Write((uint8_t *)ml_buf, strlen(ml_buf));
  }
  */

  if (out_aqi)
    *out_aqi = new_aqi;
  if (out_o3)
    *out_o3 = o3;
  if (out_co)
    *out_co = co;
  if (out_nh3)
    *out_nh3 = nh3;
  if (out_dust)
    *out_dust = dust;
  if (out_co2)
    *out_co2 = co2;
  if (out_t)
    *out_t = last_t;
  if (out_h)
    *out_h = last_h;

  // --- MQTT PUBLISHING THROTTLE ---
  static int mqtt_throttle = 0;
  if (++mqtt_throttle >= 5) {
      mqtt_throttle = 0;
      if (mqtt_connected) {
          char json_payload[384];
          snprintf(json_payload, sizeof(json_payload), 
                   "{\"device_id\":\"pic32\",\"pm25\":%.1f,\"co2\":%.1f,\"o3\":%.2f,\"co\":%.1f,\"nh3\":%.1f,\"temp\":%.1f,\"hum\":%.1f,\"aqi\":%d,\"relay\":\"OFF\",\"mode\":\"AUTO\",\"diagnosis\":\"%s\",\"confidence\":%.2f}",
                   dust, co2, o3, co, nh3, last_t, last_h, new_aqi, pred_class, confidence);
                   
          MQTT_Publish("aqms/pic32/data", json_payload);
          
          if (mqtt_connected) {
              char pub_msg[150];
              snprintf(pub_msg, sizeof(pub_msg), "\r\n[MQTT] Published Data! Length: %d\r\n", strlen(json_payload));
              while (SERCOM3_USART_WriteIsBusy());
              SERCOM3_USART_Write((uint8_t *)pub_msg, strlen(pub_msg));
          } else {
              char fail_msg[] = "\r\n[MQTT ERROR] Disconnected!\r\n";
              while (SERCOM3_USART_WriteIsBusy());
              SERCOM3_USART_Write((uint8_t *)fail_msg, strlen(fail_msg));
          }
      }
  }
}

// ============================================================
// PAGE 2 �?? AQI Default display
// Shows wave gauge, AQI number, live location, status label.
// Returns when PA17 goes HIGH (switch to sensor page).
// ============================================================
static void run_aqi_page(int *saved_aqi, int *saved_target) {
  uint16_t bg = 0xFFFF;
  tft_fill_screen(bg);

  uint16_t txt_faint = 0x0000;
  tft_draw_string(35, 2, "LIVE.COIMBATORE", txt_faint, bg, 1);
  tft_draw_string(47, 72, "US EPA STD.", txt_faint, bg, 1);

  int current_aqi = *saved_aqi;
  int target_aqi = *saved_target;
  uint16_t last_color = 0x0000;
  uint8_t wave_t = 0;
  while (1) {
    if (check_button_toggle()) {
      *saved_aqi = current_aqi;
      *saved_target = target_aqi;
      return;
    }

    uint16_t active_color = get_aqi_color(current_aqi);

    draw_bottom_wave(active_color, wave_t, current_aqi);
    wave_t = (wave_t + 1) % 50;

    // Smoothly animate current_aqi toward target_aqi
    if (current_aqi < target_aqi) {
      current_aqi += 2;
      if (current_aqi > target_aqi)
        current_aqi = target_aqi;
    } else if (current_aqi > target_aqi) {
      current_aqi -= 2;
      if (current_aqi < target_aqi)
        current_aqi = target_aqi;
    }
    active_color = get_aqi_color(current_aqi);

    if (active_color != last_color) {
      last_color = active_color;
      tft_draw_circle_outline(25, 5, 5, active_color);
      tft_fill_circle(25, 5, 3, active_color);
      tft_draw_circle_outline(80, 54, 42, active_color);
      tft_draw_circle_outline(80, 54, 43, active_color);
    }

    // AQI number string
    char buf[4];
    if (current_aqi >= 100) {
      buf[0] = '0' + (current_aqi / 100);
      buf[1] = '0' + ((current_aqi / 10) % 10);
      buf[2] = '0' + (current_aqi % 10);
      buf[3] = '\0';
    } else if (current_aqi >= 10) {
      buf[0] = '0' + ((current_aqi / 10) % 10);
      buf[1] = '0' + (current_aqi % 10);
      buf[2] = '\0';
    } else {
      buf[0] = '0' + (current_aqi % 10);
      buf[1] = '\0';
    }
    tft_fill_rect(20, 36, 120, 28, bg);
    int cw = 24;
    int len = current_aqi >= 100 ? 3 : (current_aqi >= 10 ? 2 : 1);
    tft_draw_string(80 - ((len * cw) / 2), 36, buf, active_color, bg, 4);

    // Gauge bar
    int fill_w = (current_aqi * 100) / 500;
    if (fill_w > 100)
      fill_w = 100;
    uint16_t pastel_bar = get_pastel(active_color);
    tft_fill_rect(30, 122, 100, 2, pastel_bar);
    tft_fill_rect(30, 122, fill_w, 2, active_color);

    // Percentage text
    char pct[5];
    int p_val = (current_aqi * 100) / 500;
    if (p_val >= 100) {
      pct[0] = '1';
      pct[1] = '0';
      pct[2] = '0';
      pct[3] = '%';
      pct[4] = '\0';
    } else if (p_val >= 10) {
      pct[0] = '0' + (p_val / 10);
      pct[1] = '0' + (p_val % 10);
      pct[2] = '%';
      pct[3] = '\0';
    } else {
      pct[0] = '0' + p_val;
      pct[1] = '%';
      pct[2] = '\0';
    }
    tft_fill_rect(134, 119, 24, 8, bg);
    tft_draw_string(134, 119, pct, 0x0000, bg, 1);

    // Status label
    {
      const char *label = get_aqi_label(current_aqi);
      int w = 0;
      while (label[w] != '\0')
        w++;
      tft_fill_rect(0, 102, 160, 16, bg);
      tft_draw_string(80 - (w * 12) / 2, 102, label, active_color, bg, 2);
    }

    // ---- Read sensors every loop tick �?? continuous serial + AQI update ----
    {
      update_background_sensors(&target_aqi, NULL, NULL, NULL, NULL, NULL, NULL,
                                NULL);
    }
  }
}

// ============================================================
// PAGE 3 �?? Sensor Detail Screen (Dashboard Grid)
// ============================================================

static void draw_static_card(int x, int y, const char *label) {
  uint16_t card_bg = 0xFFFF;   // White
  uint16_t txt_fg = 0x8410;    // Medium Gray
  uint16_t screen_bg = 0x4516; // Teal/Cyan Main Background

  tft_fill_rect(x, y, 74, 38, card_bg);
  // Fake rounded corners
  tft_draw_pixel(x, y, screen_bg);
  tft_draw_pixel(x + 1, y, screen_bg);
  tft_draw_pixel(x, y + 1, screen_bg);
  tft_draw_pixel(x + 73, y, screen_bg);
  tft_draw_pixel(x + 72, y, screen_bg);
  tft_draw_pixel(x + 73, y + 1, screen_bg);
  tft_draw_pixel(x, y + 37, screen_bg);
  tft_draw_pixel(x + 1, y + 37, screen_bg);
  tft_draw_pixel(x, y + 36, screen_bg);
  tft_draw_pixel(x + 73, y + 37, screen_bg);
  tft_draw_pixel(x + 72, y + 37, screen_bg);
  tft_draw_pixel(x + 73, y + 36, screen_bg);

  // Label centered at bottom
  int lbl_len = strlen(label);
  tft_draw_string(x + (74 - (lbl_len * 6)) / 2, y + 26, label, txt_fg, card_bg,
                  1);
}

static void update_card_value(int x, int y, const char *val, const char *unit) {
  uint16_t card_bg = 0xFFFF;
  uint16_t val_fg = 0x0000;  // Black
  uint16_t unit_fg = 0x4208; // Dark Gray

  // Clear the top half
  tft_fill_rect(x + 4, y + 4, 66, 18, card_bg);

  int val_len = strlen(val);
  int unit_len = strlen(unit);
  int total_w = (val_len * 12) + (unit_len * 6);

  int cx = x + (74 - total_w) / 2;
  // Fallback if it's too wide
  if (cx < x + 2)
    cx = x + 2;

  // Draw value (scale 2) and unit (scale 1) side by side
  tft_draw_string(cx, y + 6, val, val_fg, card_bg, 2);
  tft_draw_string(cx + (val_len * 12), y + 14, unit, unit_fg, card_bg, 1);
}

// Standard Edge Detection (Toggle Button logic)
static bool check_button_toggle(void) {
  static bool btn_prev = false;
  bool btn_curr = PA17_IS_HIGH();

  if (btn_curr && !btn_prev) {
    delay_ms(50); // debounce
    if (PA17_IS_HIGH()) {
      btn_prev = true;
      return true; // Button was just pressed!
    }
  } else if (!btn_curr) {
    btn_prev = false;
  }
  return false;
}

static void run_sensor_page(void) {
  uint16_t bg = 0x4516; // Teal/Cyan Main Background
  tft_fill_screen(bg);

  // Draw the 6 static cards
  draw_static_card(4, 4, "O3");
  draw_static_card(82, 4, "CO");

  draw_static_card(4, 46, "NH3");
  draw_static_card(82, 46, "Dust");

  draw_static_card(4, 88, "CO2");
  draw_static_card(82, 88, "AQI");

  uint8_t disp_ticks = 0;

  while (1) {
    if (check_button_toggle()) {
      return;
    }

    // ---- Update sensor values continuously ----
    {
      float o3, co, nh3, dust, co2, t, h;
      int new_aqi;
      update_background_sensors(&new_aqi, &o3, &co, &nh3, &dust, &co2, &t, &h);

      // Update the TFT at a slower 10-15Hz rate to prevent screen
      // tearing/flicker
      if (++disp_ticks >= 5) {
        disp_ticks = 0;
        char vbuf[16];

        sprintf(vbuf, "%.1f", o3);
        update_card_value(4, 4, vbuf, "ppb");

        sprintf(vbuf, "%.1f", co);
        update_card_value(82, 4, vbuf, "ppm");

        sprintf(vbuf, "%.1f", nh3);
        update_card_value(4, 46, vbuf, "ppm");

        sprintf(vbuf, "%.1f", dust);
        update_card_value(82, 46, vbuf, "ug/m3");

        sprintf(vbuf, "%.1f", co2);
        update_card_value(4, 88, vbuf, "ppm");

        sprintf(vbuf, "%d", new_aqi);
        update_card_value(82, 88, vbuf, "idx");
      }
    }
  }
}

// ============================================================
static void draw_thermometer(uint8_t x, uint8_t y, uint16_t color,
                             uint16_t inner_c) {
  tft_fill_circle(x + 5, y + 12, 4, color);
  tft_fill_rect(x + 3, y, 5, 10, color);
  tft_fill_circle(x + 5, y + 12, 2, inner_c);
  tft_fill_rect(x + 4, y + 1, 3, 9, inner_c);
  tft_fill_circle(x + 5, y + 12, 1, color);
  tft_fill_rect(x + 4, y + 6, 3, 6, color);
  tft_fill_rect(x + 10, y + 2, 3, 1, color);
  tft_fill_rect(x + 10, y + 5, 2, 1, color);
  tft_fill_rect(x + 10, y + 8, 3, 1, color);
}

static void draw_water_drop(uint8_t x, uint8_t y, uint16_t color,
                            uint16_t inner_c) {
  tft_fill_circle(x + 6, y + 10, 5, color);
  for (int i = 0; i < 5; i++) {
    tft_fill_rect(x + 6 - i, y + 9 - i - 2, (i * 2) + 1, 1, color);
  }
  tft_fill_circle(x + 6, y + 11, 2, inner_c);
}

static void run_dht_page(void) {
  uint16_t bg = 0xF7DF;      // Very light blue
  uint16_t card_bg = 0xFFFF; // White
  uint16_t title_c = 0x18E3; // Dark blue

  tft_fill_screen(bg);
  tft_draw_string(5, 5, "< Temperature & humidity", title_c, bg, 1);

  // Shelf
  tft_fill_rect(106, 110, 54, 18, 0xD6BA); // Shelf top

  // Plant Stems
  tft_fill_rect(130, 50, 2, 60, 0x7BEF);
  tft_fill_rect(142, 60, 1, 50, 0x7BEF);

  // Leaves
  tft_fill_circle(126, 45, 12, 0x9E73);
  tft_fill_circle(148, 40, 9, 0x5D2D);
  tft_fill_circle(145, 65, 7, 0x9E73);
  tft_fill_circle(123, 75, 6, 0x5D2D);

  // Pot
  tft_fill_rect(125, 95, 18, 15, 0xFFFF);
  tft_fill_rect(122, 93, 24, 3, 0xFFFF);

  // Little squarish sensor box
  tft_fill_rect(108, 85, 20, 25, 0xBDB6);
  tft_fill_rect(110, 87, 16, 21, 0xCE59);
  draw_thermometer(110, 90, 0xFFFF, 0xCE59);

  // Cards Base
  tft_fill_rect(5, 20, 100, 40, card_bg);
  tft_fill_rect(5, 65, 100, 40, card_bg);

  tft_draw_pixel(5, 20, bg);
  tft_draw_pixel(104, 20, bg);
  tft_draw_pixel(5, 59, bg);
  tft_draw_pixel(104, 59, bg);
  tft_draw_pixel(5, 65, bg);
  tft_draw_pixel(104, 65, bg);
  tft_draw_pixel(5, 104, bg);
  tft_draw_pixel(104, 104, bg);

  // Gradient bars
  tft_fill_rect(15, 56, 30, 2, 0x64BF);  // Blue bar card 1
  tft_fill_rect(15, 101, 30, 2, 0x64BF); // Blue bar card 2

  // Icons
  draw_thermometer(10, 28, 0x64BF, 0xFFFF);
  draw_water_drop(10, 73, 0x64BF, 0xFFFF);

  float last_t = -999.0f, last_h = -999.0f;

  while (1) {
    if (check_button_toggle())
      return;

    float o3, co, nh3, dust, co2, t, h;
    int aqi;
    update_background_sensors(&aqi, &o3, &co, &nh3, &dust, &co2, &t, &h);

    if (t != last_t || h != last_h) {
      last_t = t;
      last_h = h;

      char tbuf[16];
      char hbuf[16];

      tft_fill_rect(30, 23, 70, 18, card_bg); // text clear
      tft_fill_rect(30, 68, 70, 18, card_bg); // text clear
      tft_fill_rect(30, 42, 70, 10, card_bg); // subtext clear
      tft_fill_rect(30, 87, 70, 10, card_bg); // subtext clear

      if (t < -50.0f || h < 0.0f) {
        tft_draw_string(30, 23, "--.- C", 0x0000, card_bg, 2);
        tft_draw_string(30, 68, "--.- %", 0x0000, card_bg, 2);
      } else {
        sprintf(tbuf, "%.1f", t);
        int offset = 0;
        for (int i = 0; tbuf[i] != '\0'; i++)
          offset++;
        offset *= 12;
        tft_draw_string(30, 23, tbuf, 0x0000, card_bg, 2);
        tft_draw_circle_outline(30 + offset + 3, 26, 2, 0x0000);
        tft_draw_string(30 + offset + 8, 23, "C", 0x0000, card_bg, 2);

        sprintf(hbuf, "%.1f%%", h);
        tft_draw_string(30, 68, hbuf, 0x0000, card_bg, 2);
      }

      const char *t_txt = "Comfortable";
      if (t > 26.0f)
        t_txt = "Hot";
      else if (t < 18.0f)
        t_txt = "Cold";
      if (t < -50.0f)
        t_txt = "Reading...";

      const char *h_txt = "Comfortable";
      if (h > 60.0f)
        h_txt = "Humid";
      else if (h < 40.0f)
        h_txt = "Slightly dry";
      if (h < 0.0f)
        h_txt = "Reading...";

      tft_draw_string(30, 42, t_txt, 0xB5B6, card_bg, 1);
      tft_draw_string(30, 87, h_txt, 0xB5B6, card_bg, 1);
    }

    delay_ms(50);
  }
}

// ============================================================
static void run_manual_page(void) {
  // --- Ultimate Scenic Background (Sky -> Mist -> Forest) ---
  for (int i = 0; i < 128; i++) {
    uint16_t c;
    if (i < 35)
      c = 0xDEFB; // Light Sky Blue
    else if (i < 65)
      c = 0xFFFF; // White Mist
    else if (i < 95)
      c = 0xE73F; // Soft Cloud Green
    else
      c = 0x8621; // Darker Forest Green
    tft_fill_rect(0, i, 160, 1, c);
  }

  uint16_t txt_bg = 0xFFFF; // Use mist area for text contrast

  // --- Header: EdgeAQI + Leaf Logo ---
  tft_fill_circle(38, 12, 5, 0x1B20);   // Dark Green leaf body
  tft_fill_rect(34, 12, 10, 1, 0xFFFF); // leaf vein
  tft_draw_string(52, 10, "EdgeAQI", 0x1B20, 0xDEFB, 2);

  tft_draw_string(30, 28, "AIR QUALITY MONITORING SYSTEM", 0x52AA, 0xDEFB, 1);
  tft_draw_string(55, 42, "USER MANUAL", 0x0000, 0xFFFF, 1);
  tft_fill_rect(55, 52, 60, 1, 0x0000); // Underline for title

  // --- The Slanted AQI Bar ---
  // Matches photo exactly: 6 slanted segments, white dividers
  int sw = 25, sh = 30, sy = 58, slant = 10;
  uint16_t cols[] = {0x0320, 0x4CC0, 0xFE60, 0xFBC0, 0xD800, 0x8000};
  const char *tLabels[] = {"0-50",    "51-100",  "101-200",
                           "201-300", "301-400", "401-500"};
  const char *bLabels[] = {"Good", "Moderate",  "101-200",
                           "Poor", "Very Poor", "Hazardous"};

  for (int s = 0; s < 6; s++) {
    int sx = 4 + s * sw;
    for (int i = 0; i < sh; i++) {
      int off = slant - (slant * i / sh);
      tft_fill_rect(sx + off, sy + i, sw, 1, cols[s]);
      if (s > 0)
        tft_draw_pixel(sx + off, sy + i, 0xFFFF); // white separator
    }
    // Top label: AQI range number
    tft_draw_string(sx + 5 + (slant / 2), sy + 4, tLabels[s], 0xFFFF, cols[s],
                    1);
    // Bottom label: category name
    tft_draw_string(sx + 2 + (slant / 4), sy + 18, bLabels[s], 0xFFFF, cols[s],
                    1);
  }

  // --- Bottom Gradient Scale (matches photo exactly) ---
  tft_draw_string(2, 105, "GOOD TO MODERATE", 0x1B20, 0xE73F, 1);
  for (int i = 0; i < 60; i++) {
    uint16_t gc = (i < 20) ? 0x0400 : (i < 40) ? 0xFDA0 : 0xF800;
    tft_fill_rect(95 + i / 2, 108, 1, 2, gc);
  }
  tft_draw_string(100, 105, "POOR TO HAZARDOUS", 0x8000, 0xE73F, 1);

  while (1) {
    if (check_button_toggle()) {
      return;
    }
    // Update sensors and spam output to Putty!
    // Add a 50ms delay so this loop matches the approximate speed of the other
    // pages, ensuring the DHT isn't polled faster than its 2-second crash
    // threshold.
    update_background_sensors(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    delay_ms(50);
  }
}

// ============================================================
// Top-level display controller �?? orchestrates all pages
// ============================================================
// ============================================================
// WIFI MANAGER
// ============================================================
#define ESP_BUFFER_SIZE 256
static char espBuffer[ESP_BUFFER_SIZE];
static uint16_t bufferIndex = 0;

static void ESP_Write(const char* str) {
    while (*str) {
        while (!(SERCOM5_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_DRE_Msk));
        SERCOM5_REGS->USART_INT.SERCOM_DATA = (uint16_t)*str++;
    }
}

static void SERCOM5_ClearErrors(void) {
    SERCOM5_REGS->USART_INT.SERCOM_STATUS = (uint16_t)(
        SERCOM_USART_INT_STATUS_BUFOVF_Msk |
        SERCOM_USART_INT_STATUS_FERR_Msk   |
        SERCOM_USART_INT_STATUS_PERR_Msk);
    SERCOM5_REGS->USART_INT.SERCOM_INTFLAG = (uint8_t)SERCOM_USART_INT_INTFLAG_ERROR_Msk;
}

static void ESP_FlushRx(uint32_t wait_ms) {
    uint32_t idle = 0;
    SERCOM5_ClearErrors(); 
    while (idle < wait_ms) {
        if (SERCOM5_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_RXC_Msk) {
            (void)SERCOM5_REGS->USART_INT.SERCOM_DATA;
            idle = 0; 
        } else {
            delay_ms(1);
            idle++;
        }
    }
    SERCOM5_ClearErrors(); 
}

void WIFI_Init(void) {
    bufferIndex = 0;
    memset(espBuffer, 0, ESP_BUFFER_SIZE);
}

bool WIFI_SendCommand(const char* command, const char* expected_response, uint32_t timeout_ms) {
    ESP_FlushRx(50); 
    memset(espBuffer, 0, ESP_BUFFER_SIZE);
    bufferIndex = 0;
    ESP_Write(command);
    while (!(SERCOM5_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_TXC_Msk));

    const uint32_t TICKS_PER_MS = 8000UL;
    uint32_t deadline = timeout_ms * TICKS_PER_MS;
    uint32_t noDataCount = 0;

    while (noDataCount < deadline) {
        if (SERCOM5_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_RXC_Msk) {
            uint8_t data = (uint8_t)SERCOM5_REGS->USART_INT.SERCOM_DATA;
            
            if (bufferIndex < ESP_BUFFER_SIZE - 1) {
                espBuffer[bufferIndex++] = (char)data;
                espBuffer[bufferIndex]   = '\0';
            }
            if (strstr(espBuffer, expected_response) != NULL) {
                return true;
            }
            noDataCount = 0; 
        } else {
            noDataCount++;
        }
    }
    return false;
}

bool WIFI_IsAlive(void) {
    return WIFI_SendCommand("AT\r\n", "OK", 2000);
}

bool WIFI_SetMode(uint8_t mode) {
    char cmd[24];
    snprintf(cmd, sizeof(cmd), "AT+CWMODE=%u\r\n", mode);
    return WIFI_SendCommand(cmd, "OK", 3000);
}

bool WIFI_Connect(const char* ssid, const char* password) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, password);
    return WIFI_SendCommand(cmd, "WIFI GOT IP", 20000);
}

static void ESP_WriteRaw(const uint8_t* data, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        while (!(SERCOM5_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_DRE_Msk));
        SERCOM5_REGS->USART_INT.SERCOM_DATA = (uint16_t)data[i];
    }
}

static bool MQTT_Connect(void) {
    WIFI_SendCommand("AT+CIPMUX=0\r\n", "OK", 2000); 
    if (!WIFI_SendCommand("AT+CIPSTART=\"TCP\",\"broker.hivemq.com\",1883\r\n", "CONNECT", 10000)) {
        return false;
    }
    
    uint8_t connect_pkt[24] = {
        0x10, 0x16, 
        0x00, 0x04, 'M', 'Q', 'T', 'T', 
        0x04, 0x02, 0x00, 0x3C, 
        0x00, 0x0A, 'a', 'q', 'm', 's', '-', 'p', 'i', 'c', '3', '2'
    };
    
    if (WIFI_SendCommand("AT+CIPSEND=24\r\n", ">", 3000)) {
        ESP_WriteRaw(connect_pkt, 24);
        delay_ms(1000); 
        mqtt_connected = true;
        return true;
    }
    return false;
}

static void MQTT_Publish(const char* topic, const char* payload) {
    if (!mqtt_connected) return;
    
    uint16_t topic_len = strlen(topic);
    uint16_t payload_len = strlen(payload);
    uint16_t remaining_length = 2 + topic_len + payload_len;
    
    uint8_t rem_len_bytes[4];
    uint8_t rem_len_count = 0;
    uint32_t x = remaining_length;
    do {
        uint8_t encodedByte = x % 128;
        x = x / 128;
        if (x > 0) {
            encodedByte |= 128;
        }
        rem_len_bytes[rem_len_count++] = encodedByte;
    } while (x > 0);
    
    uint16_t total_packet_size = 1 + rem_len_count + remaining_length;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%u\r\n", total_packet_size);
    
    if (WIFI_SendCommand(cmd, ">", 2000)) {
        uint8_t header = 0x30;
        ESP_WriteRaw(&header, 1);
        ESP_WriteRaw(rem_len_bytes, rem_len_count);
        uint8_t tlen[2] = { (uint8_t)(topic_len >> 8), (uint8_t)(topic_len & 0xFF) };
        ESP_WriteRaw(tlen, 2);
        ESP_WriteRaw((const uint8_t*)topic, topic_len);
        ESP_WriteRaw((const uint8_t*)payload, payload_len);
        delay_ms(500); 
    } else {
        mqtt_connected = false;
    }
}

static void run_display(void) {
  // Page 1 �?? Logo splash (one-shot, untouched)
  draw_logo_page();

  int count = 0; // State variable: 0 = AQI, 1 = Sensor
  int aqi_cur = 0;
  int aqi_target = 42;

  while (1) {
    if (count == 0) {
      run_aqi_page(&aqi_cur, &aqi_target);
      count = 1;
    } else if (count == 1) {
      run_sensor_page();
      count = 2;
    } else if (count == 2) {
      run_dht_page();
      count = 3;
    } else if (count == 3) {
      run_manual_page();
      count = 4;
    }

    if (count > 3) {
      count = 0; // Reset the count
    }
  }
}

// ======================== MAIN ==============================
int main(void) {
  SYS_Initialize(NULL); // SERCOM3_USART_Initialize() is called inside here

  // --- Initialize SERCOM5 on PB02/PB03 for WiFi (ESP-01S) ---
  // Enable MCLK for SERCOM5
  MCLK_REGS->MCLK_APBCMASK |= MCLK_APBCMASK_SERCOM5_Msk;

  // Enable GCLK for SERCOM5 (ch22)
  GCLK_REGS->GCLK_PCHCTRL[22] = GCLK_PCHCTRL_GEN(0x0) | GCLK_PCHCTRL_CHEN_Msk;
  while (!(GCLK_REGS->GCLK_PCHCTRL[22] & GCLK_PCHCTRL_CHEN_Msk));

  // SERCOM5: PB02=PAD0(TX), PB03=PAD1(RX) -- MUX D (0x3)
  PORT_SEC_REGS->GROUP[1].PORT_PMUX[1] = 0x33U; // MUX D for PB02 and PB03
  PORT_SEC_REGS->GROUP[1].PORT_PINCFG[2] |= PORT_PINCFG_PMUXEN_Msk; // Enable Peripheral Muxing
  PORT_SEC_REGS->GROUP[1].PORT_PINCFG[3] |= PORT_PINCFG_PMUXEN_Msk;

  SERCOM5_REGS->USART_INT.SERCOM_CTRLA = SERCOM_USART_INT_CTRLA_SWRST_Msk;
  while (SERCOM5_REGS->USART_INT.SERCOM_SYNCBUSY & SERCOM_USART_INT_SYNCBUSY_SWRST_Msk);
  SERCOM5_REGS->USART_INT.SERCOM_CTRLA = SERCOM_USART_INT_CTRLA_MODE(0x1) | SERCOM_USART_INT_CTRLA_TXPO(0x0) | SERCOM_USART_INT_CTRLA_RXPO(0x1) | SERCOM_USART_INT_CTRLA_DORD_Msk;
  SERCOM5_REGS->USART_INT.SERCOM_CTRLB = SERCOM_USART_INT_CTRLB_CHSIZE(0x0) | SERCOM_USART_INT_CTRLB_TXEN_Msk | SERCOM_USART_INT_CTRLB_RXEN_Msk;
  while (SERCOM5_REGS->USART_INT.SERCOM_SYNCBUSY & SERCOM_USART_INT_SYNCBUSY_CTRLB_Msk);
  SERCOM5_REGS->USART_INT.SERCOM_BAUD = (uint16_t)63019; // 115200 @ 48MHz
  SERCOM5_REGS->USART_INT.SERCOM_CTRLA |= SERCOM_USART_INT_CTRLA_ENABLE_Msk;
  while (SERCOM5_REGS->USART_INT.SERCOM_SYNCBUSY & SERCOM_USART_INT_SYNCBUSY_ENABLE_Msk);

  // Call manual ADC initialization to completely bypass TrustZone blocks
  // that MCC library might introduce.
  adc_init();

  // Startup verification
  char hello[] = "HELLO WORLD INIT\r\n";
  while (SERCOM3_USART_WriteIsBusy())
    ;
  SERCOM3_USART_Write((uint8_t *)hello, strlen(hello));

  // PA03 is the Dust LED. Set it to OUTPUT.
  PIN_OUTPUT_ENABLE(3);

  PIN_OUTPUT_ENABLE(8);
  PIN_OUTPUT_ENABLE(9);
  PIN_OUTPUT_ENABLE(10);
  PIN_OUTPUT_ENABLE(11);
  PIN_OUTPUT_ENABLE(12);

  PORT_SEC_REGS->GROUP[0].PORT_PINCFG[8] = 0x00;
  PORT_SEC_REGS->GROUP[0].PORT_PINCFG[9] = 0x00;
  PORT_SEC_REGS->GROUP[0].PORT_PINCFG[10] = 0x00;
  PORT_SEC_REGS->GROUP[0].PORT_PINCFG[11] = 0x00;
  PORT_SEC_REGS->GROUP[0].PORT_PINCFG[12] = 0x00;
  PORT_REGS->GROUP[0].PORT_PINCFG[8] = 0x00;
  PORT_REGS->GROUP[0].PORT_PINCFG[9] = 0x00;
  PORT_REGS->GROUP[0].PORT_PINCFG[10] = 0x00;
  PORT_REGS->GROUP[0].PORT_PINCFG[11] = 0x00;
  PORT_REGS->GROUP[0].PORT_PINCFG[12] = 0x00;

  TFT_SCK_HIGH();
  TFT_MOSI_LOW();
  TFT_CS_HIGH();
  TFT_DC_HIGH();
  TFT_RST_HIGH();

  delay_ms(200);
  tft_init();

  // Configure PA17 as digital INPUT with internal PULL-DOWN
  // INEN (Input Enable) MUST be set to read the pin state!
  // PINCFG 0x06 -> INEN=1 (bit 1) and PULLEN=1 (bit 2)
  PORT_SEC_REGS->GROUP[0].PORT_DIRCLR = (1U << 17U); // direction = input
  PORT_REGS->GROUP[0].PORT_DIRCLR = (1U << 17U);

  PORT_SEC_REGS->GROUP[0].PORT_OUTCLR = (1U << 17U); // output=0 means Pull-Down
  PORT_REGS->GROUP[0].PORT_OUTCLR = (1U << 17U);     // when PULLEN is 1.

  PORT_SEC_REGS->GROUP[0].PORT_PINCFG[17] = 0x06U; // INEN and PULLEN  // --- Setup Wi-Fi ---
  char boot_msg[] = "\r\nWaiting 2 seconds for ESP-01S to boot up...\r\n";
  while (SERCOM3_USART_WriteIsBusy());
  SERCOM3_USART_Write((uint8_t *)boot_msg, strlen(boot_msg));
  delay_ms(2000); // Give the ESP-01S time to initialize!

  WIFI_Init();
  bool wifi_connected = false;
  
  // 1. Check if Alive
  bool esp_alive = false;
  for (uint8_t retry = 0; retry < 5; retry++) {
      if (WIFI_IsAlive()) {
          esp_alive = true;
          break;
      }
      delay_ms(1000);
  }

  // 2. Try Auto-Connect if Alive
  if (esp_alive) {
      char msg1[] = "ESP-01S is ALIVE. Connecting...\r\n";
      while (SERCOM3_USART_WriteIsBusy());
      SERCOM3_USART_Write((uint8_t *)msg1, strlen(msg1));
      
      WIFI_SetMode(1);
      delay_ms(500);

      if (WIFI_Connect("MONISH1", "thileep123")) {    
          char msg2[] = "WIFI CONNECTED AUTOMATICALLY!\r\n";
          while (SERCOM3_USART_WriteIsBusy());
          SERCOM3_USART_Write((uint8_t *)msg2, strlen(msg2));
          wifi_connected = true;
      }
  }

  // 3. Smart Bridge Mode (Fallback)
  if (!wifi_connected) {
      char msg[] = "\r\nWi-Fi connection FAILED or ESP not responding.\r\nENTERING MANUAL BRIDGE MODE.\r\nType AT commands (e.g. AT+CWJAP=\"ssid\",\"pwd\") to connect manually.\r\nSensor readings are PAUSED until connected.\r\n";
      while (SERCOM3_USART_WriteIsBusy());
      SERCOM3_USART_Write((uint8_t *)msg, strlen(msg));
      
      uint16_t bridgeBufferIdx = 0;
      char bridgeBuffer[128] = {0};

      while (1) {
          // Read from PuTTY (SERCOM3) -> Send to ESP (SERCOM5)
          if (SERCOM3_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_RXC_Msk) {
              uint8_t data = (uint8_t)SERCOM3_REGS->USART_INT.SERCOM_DATA;
              while (!(SERCOM5_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_DRE_Msk));
              SERCOM5_REGS->USART_INT.SERCOM_DATA = (uint16_t)data;

              // PuTTY sends '\r' when you hit Enter. ESP needs '\r\n'. Add '\n' automatically.
              if (data == '\r') {
                  while (!(SERCOM5_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_DRE_Msk));
                  SERCOM5_REGS->USART_INT.SERCOM_DATA = (uint16_t)'\n';
                  while (!(SERCOM3_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_DRE_Msk));
                  SERCOM3_REGS->USART_INT.SERCOM_DATA = (uint16_t)'\n'; // echo \n to PuTTY
              }
          }

          // Read from ESP (SERCOM5) -> Send to PuTTY (SERCOM3)
          if (SERCOM5_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_RXC_Msk) {
              uint8_t data = (uint8_t)SERCOM5_REGS->USART_INT.SERCOM_DATA;
              
              // Only send to PuTTY if ready, do not block strictly forever if it causes overflow
              if (SERCOM3_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_DRE_Msk) {
                  SERCOM3_REGS->USART_INT.SERCOM_DATA = (uint16_t)data;
              }

              // Smart check for connection success
              if (data == '\n' || data == '\r') {
                  bridgeBufferIdx = 0;
                  memset(bridgeBuffer, 0, sizeof(bridgeBuffer));
              } else {
                  if (bridgeBufferIdx < sizeof(bridgeBuffer) - 1) {
                      bridgeBuffer[bridgeBufferIdx++] = (char)data;
                      bridgeBuffer[bridgeBufferIdx] = '\0';
                  }
              }

              // If the user manually connected successfully, exit the bridge!
              if (strstr(bridgeBuffer, "WIFI GOT IP") != NULL || strstr(bridgeBuffer, "WIFI CONNECTED") != NULL) {
                  char success_msg[] = "\r\nSUCCESS! Exiting Bridge Mode and Starting Sensors...\r\n";
                  while (SERCOM3_USART_WriteIsBusy());
                  SERCOM3_USART_Write((uint8_t *)success_msg, strlen(success_msg));
                  break; // Breaks out of the infinite while(1) loop!
              }
              
              // Clear overflow if we dropped a byte
              if (SERCOM5_REGS->USART_INT.SERCOM_STATUS & SERCOM_USART_INT_STATUS_BUFOVF_Msk) {
                  SERCOM5_REGS->USART_INT.SERCOM_STATUS = SERCOM_USART_INT_STATUS_BUFOVF_Msk;
              }
          }
      }
  }

  // --- Connect to MQTT ---
  char mqtt_msg[] = "\r\nConnecting to MQTT (broker.hivemq.com)...\r\n";
  while (SERCOM3_USART_WriteIsBusy());
  SERCOM3_USART_Write((uint8_t *)mqtt_msg, strlen(mqtt_msg));
  
  if (MQTT_Connect()) {
      char m_ok[] = "MQTT CONNECTED!\r\n";
      while (SERCOM3_USART_WriteIsBusy());
      SERCOM3_USART_Write((uint8_t *)m_ok, strlen(m_ok));
      MQTT_Publish("aqms/pic32/status", "online");
  } else {
      char m_fail[] = "MQTT CONNECTION FAILED.\r\n";
      while (SERCOM3_USART_WriteIsBusy());
      SERCOM3_USART_Write((uint8_t *)m_fail, strlen(m_fail));
  }

  run_display();

  return 0;
}
//moni