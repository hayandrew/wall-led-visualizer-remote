#ifndef AUDIO_PROCESSOR_H
#define AUDIO_PROCESSOR_H

#include <Arduino.h>

namespace AudioProcessor {
    // Initialize the I2S interface and start the background sampling task
    void init();

    // Get the smoothed volume envelope (useful for VU meters)
    float getVolumeEnvelope();

    // Get the raw peak-to-peak amplitude from the most recent buffer
    float getPeakAmplitude();

    // Get a pointer to the buffer of DC-removed float samples (size = I2S_BUFFER_SIZE)
    float* getAudioBuffer();

    // Check if a new buffer of samples has been read and is ready for FFT processing
    bool isNewBufferReady();

    // Reset the new buffer flag after reading
    void clearNewBufferFlag();

    // Get a pointer to the 7 frequency band amplitudes (normalized 0.0 to 1.0)
    float* getFrequencyBands();

    // Get the number of frequency bands (7)
    uint8_t getNumBands();

    // Run the FFT calculation on the active audio buffer
    void runFFT();

    // Set the gain multiplier for incoming audio
    void setGain(float gain);

    // Get the current gain multiplier
    float getGain();
}

#endif // AUDIO_PROCESSOR_H
