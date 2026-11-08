# ALFRED Android app

This Android app wraps the ESP32 dashboard and adds phone-only actions that browsers cannot provide: the system contact picker, permission-gated direct calls, app-targeted music playback requests, and optional hands-free voice listening.

## Build

Open the `android/` folder in Android Studio and build the `app` project, or run:

```sh
./gradlew assembleDebug
```

The installable debug APK is written to `app/build/outputs/apk/debug/app-debug.apk` (version 1.4.0). A debug APK is for testing; a Play Store release needs a release build signed with an upload key that the owner securely keeps.

## Connect ALFRED

Open the app, enter the ESP32 dashboard address shown in Serial Monitor (for example `http://192.168.1.45/` or `http://alfred.local/`), and select **Load**. The phone and ESP32 must be on the same Wi-Fi network.

## Phone actions and voice

- **Music:** choose Any app, Spotify, or YouTube Music. Explicit voice requests are routed locally without a Gemini round-trip. The app sends Android's `ACTION_MEDIA_PLAY_FROM_SEARCH` request to the chosen provider, then falls back to that provider's search page if it does not accept playback directly.
- **Open apps:** explicit “open [app]” requests use Android's launcher list and report when an app is not installed or cannot be opened.
- **Contacts and calls:** name-based lookup requests `READ_CONTACTS` permission when first needed. ALFRED searches the phone's contacts for the requested name; calling requests Android's `CALL_PHONE` permission and requires confirmation. Contact access can be denied in Android Settings.
- **Location:** a request such as “where am I?” asks for coarse or precise location permission at the time it is needed. ALFRED returns the current coordinates and a map link; it does not share location with a trusted contact automatically.
- **Hands-free:** after microphone and notification permissions are granted, the visible microphone foreground service starts automatically on app launch and can remain active when the app is minimized or removed from Recents. It listens for “Alfred,” gives a spoken acknowledgement when the app is open, then listens for the command. If the activity is closed, it can capture and queue the follow-up; reopen ALFRED to process and answer it. Android may stop the service after force-stop, permission revocation, or manufacturer battery restrictions; this is not Alexa-style privileged hotword detection. Tap Stop in the app or notification to end it.
- **Voice input:** the dashboard mic uses Android's native `SpeechRecognizer` while inside the app, avoiding the browser speech pop-up. The separate hands-free service remains opt-in.
- **Voice output:** native Android text-to-speech starts immediately; the selected Hindi/English system voice may differ by phone and installed voice data.

The web dashboard uses Gemini Google Search grounding for current-information questions when enabled and supported by the account. See `../docs/SETUP_AND_DEPLOYMENT.md` for its provider and billing limits.
