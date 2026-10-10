#pragma once

namespace breark::platform
{
// Lets a desktop window (the toast) appear on every Space and above full-screen
// apps, without ever hiding when the host app is in the background.
// `nativeHandle` is juce::Component::getWindowHandle(). Message thread only.
void floatAboveEverything (void* nativeHandle);
} // namespace breark::platform
