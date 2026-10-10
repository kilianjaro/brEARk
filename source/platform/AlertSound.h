#pragma once

#include <cstddef>

//==============================================================================
// Plays the alert mp3 through the Mac's default output (AVAudioPlayer),
// completely outside the host's audio stream: it can never end up in a bounce,
// a resample or a print. Message thread only.
class AlertSound
{
public:
    AlertSound (const void* mp3Data, size_t numBytes);
    ~AlertSound();

    void play();

private:
    void* player = nullptr; // AVAudioPlayer*

    AlertSound (const AlertSound&) = delete;
    AlertSound& operator= (const AlertSound&) = delete;
};
