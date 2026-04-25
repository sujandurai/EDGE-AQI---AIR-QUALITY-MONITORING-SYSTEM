#ifndef TINYML_WRAPPER_H
#define TINYML_WRAPPER_H

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the TinyML models if necessary
void tinyml_init(void);

// Run inference on the sensor data.
// The raw_data array size is 7 elements (O3, CO, NH3, Dust, CO2, Temp, Hum).
// Results (classification labels and values) will be printed directly
// to the output buffer provided.
void tinyml_run_inference(float *raw_data, int data_length, char *out_buffer, int out_buffer_size);

#ifdef __cplusplus
}
#endif

#endif // TINYML_WRAPPER_H
