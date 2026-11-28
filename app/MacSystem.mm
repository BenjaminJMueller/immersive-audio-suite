#include "MacSystem.h"

#import <AppKit/AppKit.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace rkapp {

void keepAudioResponsive() {
    static id token = nil;
    if (token != nil) return;
    token = [[NSProcessInfo processInfo]
        beginActivityWithOptions:(NSActivityUserInitiatedAllowingIdleSystemSleep | NSActivityLatencyCritical)
                          reason:@"Raumklang renders audio in real time"];
   #if !__has_feature(objc_arc)
    [token retain];
   #endif
}

bool windowOnScreen(const juce::Component& c) {
    auto* peer = c.getPeer();
    if (peer == nullptr) return false;
    if (peer->isMinimised()) return false;
    auto* view = (NSView*)peer->getNativeHandle();
    NSWindow* w = view != nil ? [view window] : nil;
    if (w == nil) return true;
    return ([w occlusionState] & NSWindowOcclusionStateVisible) != 0;
}

} // namespace rkapp
