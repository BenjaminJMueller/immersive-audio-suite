#include "MicPermission.h"

#import <AVFoundation/AVFoundation.h>
#include <juce_events/juce_events.h>

namespace rkapp {

MicAccess micAccess() {
    if (@available(macOS 10.14, *)) {
        switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]) {
        case AVAuthorizationStatusAuthorized: return MicAccess::Granted;
        case AVAuthorizationStatusNotDetermined: return MicAccess::NotAsked;
        case AVAuthorizationStatusDenied:
        case AVAuthorizationStatusRestricted:
        default: return MicAccess::Denied;
        }
    }
    return MicAccess::Granted;
}

void requestMicAccess(std::function<void(bool)> done) {
    if (@available(macOS 10.14, *)) {
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                                 completionHandler:^(BOOL granted) {
                                   auto cb = done;
                                   juce::MessageManager::callAsync([cb, granted] { if (cb) cb(granted); });
                                 }];
        return;
    }
    juce::MessageManager::callAsync([done] { if (done) done(true); });
}

} // namespace rkapp
