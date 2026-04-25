#include "tinyml_wrapper.h"

// Include the Edge Impulse Classifier SDK
// Note: EI_CLASSIFIER_SENSOR_* macros are defined in model-parameters/model_metadata.h
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"

#include <stdio.h>
#include <string.h>

void tinyml_init(void) {
    // No explicit initialization is required for tabular models, 
    // but this function preserves standard library interface norms.
}

void tinyml_run_inference(float *raw_data, int data_length, char *out_buffer, int out_buffer_size) {
    if (data_length != EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE) {
        snprintf(out_buffer, out_buffer_size, "[TinyML] ERR: Params length %d != Required %zu\r\n", 
                 data_length, (size_t)EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE);
        return;
    }

    // Turn the raw buffer into a signal which the classifier SDK expects
    signal_t features_signal;
    int err = numpy::signal_from_buffer(raw_data, EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE, &features_signal);
    if (err != 0) {
        snprintf(out_buffer, out_buffer_size, "[TinyML] ERR: signal_from_buffer fail: %d\r\n", err);
        return;
    }

    ei_impulse_result_t result = { 0 };

    // Invoke the classifier
    EI_IMPULSE_ERROR res = run_classifier(&features_signal, &result, false);
    
    if (res != EI_IMPULSE_OK) {
        snprintf(out_buffer, out_buffer_size, "[TinyML] ERR: run_classifier fail: %d\r\n", res);
        return;
    }

    // Format the results into the provided character buffer so it can be printed via C
    int offset = 0;
    offset += snprintf(out_buffer + offset, out_buffer_size - offset, "\r\n--- TinyML Prediction ---\r\n");
    
#if EI_CLASSIFIER_OBJECT_DETECTION == 1
    offset += snprintf(out_buffer + offset, out_buffer_size - offset, "Object detection is not formatted yet\r\n");
#else
    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        if (offset < out_buffer_size - 1) {
            offset += snprintf(out_buffer + offset, out_buffer_size - offset, "    %s: %.2f%%\r\n", 
                               result.classification[ix].label, 
                               result.classification[ix].value * 100.0f);
        }
    }
    snprintf(out_buffer + offset, out_buffer_size - offset, "-------------------------\r\n");
#endif
}
