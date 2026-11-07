# ALFRED

ALFRED is an AI-powered assistive-intelligence prototype for people who are blind or have low vision. It combines ESP32-CAM vision, nearby sensing, browser-based voice interaction, and an optional authenticated cloud assistant with cross-device memory.

## Project package

`ALFRED_Source_2026-11-07.zip` contains the firmware, Android companion source, Supabase Edge Function and migration, setup instructions, hardware notes, and verification checklist. The firmware compiles for AI Thinker ESP32-CAM with ESP32 Arduino core 3.3.10. The board upload could not be verified because the CP2102 serial port returned no ESP32 response.

The untouched original firmware backup stays in the local project folder and is not included here because it contains a shared recovery-network password. A redacted copy is included in the package. API keys and personal Wi-Fi credentials are not included.

## Quick start

Open `firmware/AlfredsFirmware_v2.2/AlfredsFirmware_v2.2.ino` in Arduino IDE. Join `ALFRED-Setup` if the device cannot join saved Wi-Fi, then visit `http://192.168.4.1/`. After setup, open `http://alfred.local/` when the network supports mDNS, or use the IP shown in Serial Monitor.

For cross-device memory, deploy the optional Supabase migration and Edge Function and set Gemini API keys in Supabase secrets. Never put provider keys in firmware or commit them to GitHub.

## Safety

ALFRED provides supplementary information only. It does not guarantee safe navigation and does not replace a white cane, guide dog, or other established mobility aid. The hobby flame sensor is not a fire alarm.
