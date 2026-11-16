/**
 * @file AudioHandle.cpp
 * @brief Implementation of AudioHandle, AudioSources, and Biquad IIR Equalizer.
 *
 * DSA & OOP Concepts:
 *  - RAII (Resource Acquisition Is Initialization): AudioHandle manages audio resources
 *    safely, guaranteeing that file handles and codecs are released in the destructor.
 *  - Polymorphism & Abstract Base Classes: AudioSource base class with derived MP3Source
 *    and MP4Source concrete implementations.
 *  - Associative Container (std::map): Fast O(log n) lookup for audio equalizer presets.
 *  - Digital Signal Processing (DSP): Implementation of Robert Bristow-Johnson's Audio EQ
 *    Cookbook Biquad filter (Direct Form II Transposed) for real-time 6-band equalization.
 *
 * Author: Akash Kumar Nath
 */

#include "AudioHandle.h"
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace mediax {

// ─────────────────────────────────────────────────────────────────────────────
// §1  AudioSource Implementations (Polymorphism)
// ─────────────────────────────────────────────────────────────────────────────

bool MP3Source::open(const std::string& path) {
    info_.filePath = path;
    info_.codec = "MP3";
    info_.sampleRate = 44100;
    info_.channels = 2;
    info_.bitDepth = 16;
    open_ = true;
    return true;
}

void MP3Source::close() {
    open_ = false;
}

AudioInfo MP3Source::getInfo() const {
    return info_;
}

bool MP4Source::open(const std::string& path) {
    info_.filePath = path;
    info_.codec = "AAC";
    info_.sampleRate = 48000;
    info_.channels = 2;
    info_.bitDepth = 16;
    open_ = true;
    return true;
}

void MP4Source::close() {
    open_ = false;
}

AudioInfo MP4Source::getInfo() const {
    return info_;
}

std::unique_ptr<AudioSource> createAudioSource(const std::string& path) {
    size_t dot = path.find_last_of('.');
    std::string ext = (dot != std::string::npos) ? path.substr(dot + 1) : "";
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == "mp4" || ext == "m4a" || ext == "m4v" || ext == "aac") {
        return std::make_unique<MP4Source>();
    }
    return std::make_unique<MP3Source>();
}

// ─────────────────────────────────────────────────────────────────────────────
// §2  AudioHandle & Equalizer Implementation
// ─────────────────────────────────────────────────────────────────────────────

AudioHandle::AudioHandle(const std::string& path)
    : eqEnabled_(true) {
    // RAII: Acquire resource in constructor
    source_ = createAudioSource(path);
    if (source_) {
        source_->open(path);
    }

    std::fill(std::begin(gains_), std::end(gains_), 0.0);
    initPresets();

    for (int i = 0; i < NUM_EQ_BANDS; ++i) {
        recomputeCoeffs(i);
        state_[i][0].reset();
        state_[i][1].reset();
    }
}

AudioHandle::~AudioHandle() {
    // RAII: Resource automatically cleaned up in destructor
    if (source_ && source_->isOpen()) {
        source_->close();
    }
}

bool AudioHandle::isValid() const {
    return source_ && source_->isOpen();
}

AudioInfo AudioHandle::getInfo() const {
    return source_ ? source_->getInfo() : AudioInfo{};
}

std::string AudioHandle::getPath() const {
    return source_ ? source_->getPath() : "";
}

void AudioHandle::initPresets() {
    // Presets stored in std::map (Red-Black Tree: O(log n) lookup)
    presets_["flat"]   = { 0.0,  0.0,  0.0,  0.0,  0.0,  0.0};
    presets_["bass"]   = { 6.0,  5.0,  2.0,  0.0, -1.0, -2.0};
    presets_["treble"] = {-2.0, -1.0,  0.0,  2.0,  5.0,  6.0};
    presets_["vocal"]  = {-2.0,  0.0,  4.0,  4.0,  1.0, -1.0};
    presets_["rock"]   = { 5.0,  3.0, -1.0,  0.0,  2.0,  4.0};
    presets_["pop"]    = {-1.0,  2.0,  4.0,  2.0,  0.0, -1.0};
}

std::vector<std::string> AudioHandle::availablePresets() {
    return {"flat", "bass", "treble", "vocal", "rock", "pop"};
}

BiquadCoeffs AudioHandle::makePeakingEQ(double freq, double gainDb, double Q, double sampleRate) {
    // Audio EQ Cookbook Peaking EQ Filter Design (by Robert Bristow-Johnson)
    // A = 10^(dBgain / 40)
    double A = std::pow(10.0, gainDb / 40.0);
    double w0 = 2.0 * M_PI * freq / sampleRate;
    double alpha = std::sin(w0) / (2.0 * Q);

    double b0 = 1.0 + alpha * A;
    double b1 = -2.0 * std::cos(w0);
    double b2 = 1.0 - alpha * A;
    double a0 = 1.0 + alpha / A;
    double a1 = -2.0 * std::cos(w0);
    double a2 = 1.0 - alpha / A;

    // Normalize coefficients by dividing by a0
    BiquadCoeffs c;
    c.b0 = b0 / a0;
    c.b1 = b1 / a0;
    c.b2 = b2 / a0;
    c.a1 = a1 / a0;
    c.a2 = a2 / a0;
    return c;
}

void AudioHandle::recomputeCoeffs(int band) {
    if (band < 0 || band >= NUM_EQ_BANDS) return;

    double srate = (source_ && source_->isOpen()) ? static_cast<double>(source_->getInfo().sampleRate) : 44100.0;
    if (srate <= 0.0) srate = 44100.0;

    // Q factor of 1.0 provides smooth proportional bandwidth across octave bands
    coeffs_[band] = makePeakingEQ(EQ_BAND_FREQS[band], gains_[band], 1.0, srate);
}

void AudioHandle::setGain(int band, double gainDb) {
    if (band < 0 || band >= NUM_EQ_BANDS) return;
    gains_[band] = gainDb;
    recomputeCoeffs(band);
}

double AudioHandle::getGain(int band) const {
    if (band < 0 || band >= NUM_EQ_BANDS) return 0.0;
    return gains_[band];
}

std::vector<double> AudioHandle::getGains() const {
    return std::vector<double>(gains_, gains_ + NUM_EQ_BANDS);
}

void AudioHandle::setEnabled(bool on) {
    eqEnabled_ = on;
    if (!eqEnabled_) {
        // Clear filter state when disabled to prevent clicks when re-enabled
        for (int i = 0; i < NUM_EQ_BANDS; ++i) {
            state_[i][0].reset();
            state_[i][1].reset();
        }
    }
}

void AudioHandle::applyPreset(const std::string& presetName) {
    auto it = presets_.find(presetName); // O(log n) map lookup
    if (it != presets_.end()) {
        for (int i = 0; i < NUM_EQ_BANDS; ++i) {
            gains_[i] = it->second[i];
            recomputeCoeffs(i);
        }
    }
}

void AudioHandle::processBuffer(float* samples, int frameCount, int channels) {
    if (!eqEnabled_ || !samples || frameCount <= 0 || channels <= 0) return;

    int chLimit = std::min(channels, 2); // support up to stereo

    for (int i = 0; i < frameCount; ++i) {
        for (int ch = 0; ch < chLimit; ++ch) {
            int idx = i * channels + ch;
            double sample = static_cast<double>(samples[idx]);

            // Cascade through all 6 biquad peaking filters
            for (int b = 0; b < NUM_EQ_BANDS; ++b) {
                const auto& c = coeffs_[b];
                auto& s = state_[b][ch];

                // Direct Form II Transposed Difference Equation:
                // y[n]  = b0 * x[n] + s1[n-1]
                // s1[n] = b1 * x[n] - a1 * y[n] + s2[n-1]
                // s2[n] = b2 * x[n] - a2 * y[n]
                double y = c.b0 * sample + s.x1;
                s.x1 = c.b1 * sample - c.a1 * y + s.x2;
                s.x2 = c.b2 * sample - c.a2 * y;

                sample = y;
            }

            samples[idx] = static_cast<float>(sample);
        }
    }
}

} // namespace mediax

