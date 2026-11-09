# Third-party licenses

| Dependency | Used for | License | Commercial use |
|---|---|---|---|
| [JUCE 8](https://github.com/juce-framework/JUCE) | Plugin framework, UI, FFT | AGPLv3 **or** commercial JUCE licence | Yes, with a JUCE licence (check the current JUCE licence tiers before selling) or by publishing the source under AGPLv3 |
| Steinberg VST3 SDK (bundled with JUCE 8.0.10) | VST3 format | Steinberg VST3 License or GPLv3 (see JUCE `VST3_SDK/LICENSE.txt`) | Yes, under the Steinberg VST3 License (free; read its terms, e.g. VST logo/trademark rules) |
| [pluginval](https://github.com/Tracktion/pluginval) | Testing only (not shipped) | GPLv3 | Not shipped |

All DSP code (pitch detection, PSOLA pitch shifting, EQ, compressor, de-esser) is written from scratch for this project.

Algorithms used (published methods, no third-party code):
- YIN: de Cheveigné & Kawahara (2002)
- pYIN probabilistic thresholds: Mauch & Dixon (2014)
- TD-PSOLA: Moulines & Charpentier (1990)
- Biquad filters: Robert Bristow-Johnson, "Audio EQ Cookbook"

**Before selling the plugin:** the JUCE licence must be sorted out (either a JUCE commercial licence or releasing the source under AGPLv3).
