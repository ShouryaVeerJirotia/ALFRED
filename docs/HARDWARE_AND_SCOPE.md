# ALFRED hardware and product scope

## Purpose

ALFRED is an AI-powered assistive-intelligence prototype for people who are blind or have low vision. It combines camera-based scene description, voice interaction, nearby-object distance sensing and environmental readings, with a phone/browser interface. Its purpose is to make everyday information more accessible, not to direct safe travel.

## Known/current hardware

| Part | Role | Code status |
|---|---|---|
| ESP32-CAM AI Thinker + OV2640 | Camera, Wi-Fi, web dashboard | Used by firmware |
| HC-SR04 ultrasonic sensor | Nearby distance estimate | Read on GPIO 12/13; Echo needs a 5 V-to-3.3 V divider |
| DHT11 | Temperature/humidity | Read on GPIO 15 |
| IR flame module | Hobby flame indication | Read on GPIO 14; not a fire alarm |
| microSD module/card | Potential snapshots/log storage | Mentioned in the project brief; not used by v2.2 firmware |
| Android phone | Browser/app, microphone, TTS, contacts and phone actions | Native contact picker, permission-gated calling, app-targeted play-from-search and optional hands-free service are implemented in source; GPS SOS is not implemented |
| Bluetooth earbuds | Phone audio output | Used through phone/browser audio, not paired by ESP32 firmware |
| USB-to-TTL adapter | Firmware upload/serial logs | CP2102 flash verified for current firmware |
| Portable power | Portable operation | Historical notes mention a Sony power bank; other notes list 18650/TP4056 parts that may not all be installed—confirm the actual unit |

## Firmware pin map

The AI Thinker camera uses its standard ESP32-CAM pin map in the sketch. External sensors use DHT11 data GPIO15, HC-SR04 Trigger GPIO12, Echo GPIO13 and flame module digital output GPIO14. GPIO12 and GPIO15 are strapping pins, so attached modules may affect boot. Verify the actual wiring, input protection and startup levels before a public demo.

## Feature boundary

The current v2.2 source has camera snapshots, Gemini visual requests, an embedded dashboard, browser speech input/output where supported, phone action hooks, Wi-Fi credential recovery, and sensor telemetry. The Android app adds a foreground microphone service with an “Alfred” wake phrase and 20-second follow-up window, a contact picker and direct calling, music-provider selection, and a shared neural speech voice when Gemini TTS is available. These Android functions have compiled source but still need a handset test. The speech recognizer and fallback TTS can vary with the Android provider. Browser recognition still depends on browser support and page state. ALFRED-Showcase is deployed with authenticated per-account chat history, explicit memories, a 120-request daily limit, and server-side Gemini key rotation.

Barcode/QR scanning, persistent reminders and trusted-contact SOS/location sharing are future features. Scene description, read-visible-text and object-finding controls are in the firmware but need camera verification. The Codex companion works on a separate project snapshot and requires a second user action to apply a patch; it does not control other desktop apps. Android's standard play-from-search request can start playback only when the selected music app supports it; the fallback opens that app's search. Google Search grounding requires an eligible Gemini API account. No feature has been validated with a blind/low-vision participant yet. Ultrasonic sensing is an additional proximity cue, not collision avoidance. The prototype must never be presented as a substitute for a white cane, guide dog or another established mobility aid.
