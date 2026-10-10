#include "AlertSound.h"

#import <AVFoundation/AVFoundation.h>

AlertSound::AlertSound (const void* mp3Data, size_t numBytes)
{
    @autoreleasepool
    {
        NSData* data = [NSData dataWithBytes: mp3Data length: numBytes];
        NSError* error = nil;
        AVAudioPlayer* newPlayer = [[AVAudioPlayer alloc] initWithData: data error: &error];

        if (newPlayer != nil && error == nil)
        {
            [newPlayer prepareToPlay];
            player = newPlayer; // owned: released in the destructor
        }
        else
        {
            [newPlayer release];
        }
    }
}

AlertSound::~AlertSound()
{
    if (auto* p = (AVAudioPlayer*) player)
    {
        [p stop];
        [p release];
    }
}

void AlertSound::play()
{
    if (auto* p = (AVAudioPlayer*) player)
    {
        p.currentTime = 0;
        [p play];
    }
}
