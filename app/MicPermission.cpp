#include "MicPermission.h"

#include <juce_events/juce_events.h>

#if !JUCE_MAC
namespace rkapp {
MicAccess micAccess() { return MicAccess::Granted; }
void requestMicAccess(std::function<void(bool)> done) {
    juce::MessageManager::callAsync([done] { if (done) done(true); });
}
} // namespace rkapp
#endif
