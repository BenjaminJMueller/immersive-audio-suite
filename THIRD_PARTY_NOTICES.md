# Bestandteile von Dritten / Third-party components

| Bestandteil | Lizenz | Verwendung |
|---|---|---|
| [JUCE](https://github.com/juce-framework/JUCE) 9.0.5 | AGPLv3 (oder kommerzielle JUCE-Lizenz) | App, Plugins, Oberfläche, Audiogeräte |
| VST3 SDK (in JUCE enthalten) | MIT | VST3-Plugin-Format |
| [Inter](https://github.com/rsms/inter) 4.1 (Regular, SemiBold) | SIL Open Font License 1.1, siehe `shared/fonts/OFL.txt` | Schrift der Oberfläche |
| [libmysofa](https://github.com/hoene/libmysofa) 1.3.5, mit dem Speex-Resampler und kdtree daraus | BSD-3-Clause (Speex-Resampler und kdtree: BSD) | SOFA-Dateien lesen (Kopfhörer mit gemessenen HRTFs) |
| MIT KEMAR HRTF (`MIT_KEMAR_normal_pinna.sofa` aus libmysofa) | frei mit Quellenangabe (B. Gardner, K. Martin, MIT Media Lab) | nur Tests, nicht im Download enthalten |
| [doctest](https://github.com/doctest/doctest) 2.4.11 | MIT | nur Tests |

Die Engine in `core/` hat keine Abhängigkeiten.

## libmysofa

```
Copyright (c) 2016-2017, Symonics GmbH, Christian Hoene
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

    (1) Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer. 

    (2) Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in
    the documentation and/or other materials provided with the
    distribution.  
    
    (3)The name of the author may not be used to
    endorse or promote products derived from this software without
    specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.


```
