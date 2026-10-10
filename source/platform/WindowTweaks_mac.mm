#include "WindowTweaks.h"

#import <AppKit/AppKit.h>

namespace breark::platform
{
void floatAboveEverything (void* nativeHandle)
{
    auto* view = (NSView*) nativeHandle;
    NSWindow* window = [view window];

    if (window == nil)
        return;

    [window setHidesOnDeactivate: NO];
    [window setCollectionBehavior: NSWindowCollectionBehaviorCanJoinAllSpaces
                                 | NSWindowCollectionBehaviorFullScreenAuxiliary
                                 | NSWindowCollectionBehaviorStationary
                                 | NSWindowCollectionBehaviorIgnoresCycle];
}
} // namespace breark::platform
