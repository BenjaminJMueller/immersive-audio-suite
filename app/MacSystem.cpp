#include "MacSystem.h"

#include <juce_gui_basics/juce_gui_basics.h>

#if !JUCE_MAC
namespace rkapp {
void keepAudioResponsive() {}
bool windowOnScreen(const juce::Component& c) {
    auto* peer = c.getPeer();
    return peer != nullptr && !peer->isMinimised();
}
} // namespace rkapp
#endif
