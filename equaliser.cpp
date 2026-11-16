#include "../include/audio_engine.h"
#include <iostream>
#include <cmath>

Equalizer::Equalizer()
{
    currentBands = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    isEnabled = true;
}

void Equalizer::setBandGain(int bandIndex, float gainDb)
{
    if (gainDb < -12.0f) gainDb = -12.0f;
    if (gainDb > 12.0f) gainDb = 12.0f;

    switch (bandIndex)
        {
        case 0:
            currentBands.bass = gainDb;
            break;
        case 1:
            currentBands.lowMid = gainDb;
            break;
        case 2:
            currentBands.mid = gainDb;
            break;
        case 3:
            currentBands.highMid = gainDb;
            break;
        case 4:
            currentBands.treble = gainDb;
            break;
        default:
            std::cerr << "Invalid band index." << std::endl;
            break;
    }
}

void Equalizer::applyPreset(const std::string& presetName)
{
    if (presetName == "Bass Boost") {
        currentBands = {6.0f, 4.0f, 0.0f, 0.0f, 0.0f};
    } else if (presetName == "Vocal Booster") {
        currentBands = {-2.0f, 1.0f, 5.0f, 3.0f, 0.0f};
    } else if (presetName == "Pop") {
        currentBands = {-1.0f, 2.0f, 4.0f, 2.0f, -1.0f};
    } else if (presetName == "Flat") {
        currentBands = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    }
}

void Equalizer::processAudioBuffer(std::vector<float>& buffer)
{
    if (!isEnabled) {
        return;
    }

    float avgGainDb = (
        currentBands.bass +
        currentBands.lowMid +
        currentBands.mid +
        currentBands.highMid +
        currentBands.treble
    ) / 5.0f;

    float linearScale = std::pow(10.0f, avgGainDb / 20.0f);

    for (size_t i = 0; i < buffer.size(); i++) {
        buffer[i] *= linearScale;

        if (buffer[i] > 1.0f) {
            buffer[i] = 1.0f;
        }

        if (buffer[i] < -1.0f) {
            buffer[i] = -1.0f;
        }
    }
}

void Equalizer::displayEQStatus() const 
{
    std::cout << "\n--- 5-Band Equalizer Settings ---" << std::endl;
    std::cout << "1. Bass (60Hz):     " << currentBands.bass << " dB" << std::endl;
    std::cout << "2. Low Mid (230Hz): " << currentBands.lowMid << " dB" << std::endl;
    std::cout << "3. Mid (910Hz):     " << currentBands.mid << " dB" << std::endl;
    std::cout << "4. High Mid (4kHz): " << currentBands.highMid << " dB" << std::endl;
    std::cout << "5. Treble (14kHz):  " << currentBands.treble << " dB" << std::endl;
}
