package com.alfred.dashboard;

import android.Manifest;
import android.app.Activity;
import android.app.SearchManager;
import android.content.ActivityNotFoundException;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.Uri;
import android.database.Cursor;
import android.os.Build;
import android.os.Bundle;
import android.os.Looper;
import android.os.Handler;
import android.provider.AlarmClock;
import android.provider.ContactsContract;
import android.provider.MediaStore;
import android.speech.tts.TextToSpeech;
import android.speech.RecognitionListener;
import android.speech.RecognizerIntent;
import android.speech.SpeechRecognizer;
import android.speech.tts.UtteranceProgressListener;
import android.view.Gravity;
import android.view.View;
import android.webkit.JavascriptInterface;
import android.webkit.PermissionRequest;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;
import android.content.pm.PackageManager;
import org.json.JSONObject;

import java.net.URLEncoder;
import java.util.ArrayList;
import java.nio.charset.StandardCharsets;
import java.util.Locale;

public class MainActivity extends Activity implements TextToSpeech.OnInitListener, RecognitionListener {
    private static final int REQUEST_RUNTIME_PERMS = 44;
    private static final int REQUEST_CALL_PERMISSION = 45;
    private static final int REQUEST_CONTACT = 46;
    private static final int REQUEST_NOTIFICATION_PERMISSION = 47;
    private static final int REQUEST_CONTACTS_PERMISSION = 48;
    private static final int REQUEST_LOCATION_PERMISSION = 49;
    private static final int REQUEST_LOCATION = REQUEST_LOCATION_PERMISSION;
    private static final String PREFS = "alfred_prefs";
    private static final String PREF_DASHBOARD_URL = "dashboard_url";
    private static final String WAKE_ACK_UTTERANCE = "alfred-wake-ack";
    private static final String OLD_DEFAULT_URL = "http://192.168.1.100/";
    private static final String DEFAULT_URL = "http://alfred.local/";

    private WebView webView;
    private EditText urlField;
    private SharedPreferences prefs;
    private TextToSpeech textToSpeech;
    private SpeechRecognizer tapRecognizer;
    private String dashboardHost = "";
    private boolean textToSpeechReady = false;
    private boolean receiverRegistered = false;
    private boolean activityResumed = false;
    private boolean handsFreeActive = false;
    private boolean pendingHandsFreeStart = false;
    private String pendingCallNumber = "";
    private String pendingContactAction = "pick";
    private String pendingContactLookup = "";
    private LocationManager locationManager;
    private LocationListener locationListener;
    private final Handler locationHandler = new Handler(Looper.getMainLooper());
    private Runnable locationTimeout;
    private boolean locationRequestPending = false;
    private String pendingTranscript = "";
    private boolean pendingWake = false;
    private boolean pendingLocation = false;
    private boolean tapRecognitionActive = false;

    private final BroadcastReceiver handsFreeReceiver = new BroadcastReceiver() {
        @Override public void onReceive(Context context, Intent intent) {
            if (HandsFreeService.ACTION_STATE.equals(intent.getAction())) {
                handsFreeActive = intent.getBooleanExtra(HandsFreeService.EXTRA_ACTIVE, false);
                deliverHandsFreeState();
            } else if (HandsFreeService.ACTION_TRANSCRIPT.equals(intent.getAction())) {
                String text = intent.getStringExtra(HandsFreeService.EXTRA_TEXT);
                if (text != null && !text.trim().isEmpty()) {
                    pendingTranscript = text.trim();
                    pendingWake = false;
                    prefs.edit().remove("pending_voice_command").putBoolean("pending_voice_wake", false).apply();
                    pauseHandsFreeRecognition();
                    if (activityResumed) deliverPendingTranscript();
                }
            } else if (HandsFreeService.ACTION_WAKE.equals(intent.getAction())) {
                pendingWake = true;
                prefs.edit().putBoolean("pending_voice_wake", false).apply();
                // If ALFRED is visible it will pause only for the spoken acknowledgement,
                // then resume from the TTS completion callback. In background, keep listening.
                if (activityResumed) deliverPendingWake();
            }
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        buildUi();
        configureWebView();
        textToSpeech = new TextToSpeech(this, this);
        registerHandsFreeReceiver();
        pendingTranscript = prefs.getString("pending_voice_command", "").trim();
        pendingWake = prefs.getBoolean("pending_voice_wake", false);
        String savedUrl = prefs.getString(PREF_DASHBOARD_URL, DEFAULT_URL);
        if (OLD_DEFAULT_URL.equals(savedUrl) || "http://192.168.1.13/".equals(savedUrl)) {
            savedUrl = DEFAULT_URL;
        }
        loadDashboard(savedUrl);
        if (prefs.getBoolean("hands_free_enabled", true)) enableHandsFree();
    }

    @Override protected void onResume() {
        super.onResume();
        activityResumed = true;
        if (pendingTranscript.isEmpty() && !pendingWake) resumeHandsFreeRecognition();
        if (webView != null) {
            deliverHandsFreeState();
            deliverPendingWake();
            deliverPendingTranscript();
        }
    }

    @Override protected void onPause() {
        activityResumed = false;
        super.onPause();
    }

    @Override protected void onDestroy() {
        if (receiverRegistered) {
            try { unregisterReceiver(handsFreeReceiver); } catch (IllegalArgumentException ignored) { }
            receiverRegistered = false;
        }
        if (textToSpeech != null) {
            textToSpeech.stop();
            textToSpeech.shutdown();
        }
        stopTapRecognition(false);
        super.onDestroy();
    }

    @Override
    public void onInit(int status) {
        if (status == TextToSpeech.SUCCESS) {
            textToSpeech.setOnUtteranceProgressListener(new UtteranceProgressListener() {
                @Override public void onStart(String utteranceId) { }
                @Override public void onDone(String utteranceId) {
                    if (WAKE_ACK_UTTERANCE.equals(utteranceId)) resumeHandsFreeRecognition();
                }
                @Override public void onError(String utteranceId) {
                    if (WAKE_ACK_UTTERANCE.equals(utteranceId)) resumeHandsFreeRecognition();
                }
                @Override public void onStop(String utteranceId, boolean interrupted) {
                    if (WAKE_ACK_UTTERANCE.equals(utteranceId)) resumeHandsFreeRecognition();
                }
            });
            int result = textToSpeech.setLanguage(Locale.forLanguageTag("en-IN"));
            if (result == TextToSpeech.LANG_MISSING_DATA || result == TextToSpeech.LANG_NOT_SUPPORTED) {
                result = textToSpeech.setLanguage(Locale.UK);
            }
            textToSpeechReady = result != TextToSpeech.LANG_MISSING_DATA
                    && result != TextToSpeech.LANG_NOT_SUPPORTED;
            textToSpeech.setSpeechRate(1.0f);
            textToSpeech.setPitch(1.0f);
        }
    }

    private void buildUi() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(0xff07101d);

        LinearLayout toolbar = new LinearLayout(this);
        toolbar.setOrientation(LinearLayout.HORIZONTAL);
        toolbar.setPadding(12, 12, 12, 10);
        toolbar.setGravity(Gravity.CENTER_VERTICAL);
        toolbar.setBackgroundColor(0xff07101d);

        urlField = new EditText(this);
        urlField.setSingleLine(true);
        urlField.setTextColor(0xffffffff);
        urlField.setHintTextColor(0xff80dff2);
        urlField.setTextSize(13);
        urlField.setHint("ESP32 URL, e.g. http://192.168.1.13/");
        urlField.setBackgroundColor(0xff111827);
        toolbar.addView(urlField, new LinearLayout.LayoutParams(0, 96, 1));

        Button loadButton = new Button(this);
        loadButton.setText("LOAD");
        loadButton.setTextSize(12);
        toolbar.addView(loadButton, new LinearLayout.LayoutParams(150, 96));
        loadButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                loadDashboard(urlField.getText().toString());
            }
        });

        webView = new WebView(this);
        root.addView(toolbar, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));
        root.addView(webView, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                0,
                1));

        TextView offlineHint = new TextView(this);
        offlineHint.setText("Set this to the ESP32 IP printed in Arduino Serial Monitor.");
        offlineHint.setTextColor(0xffa5f3fc);
        offlineHint.setTextSize(11);
        offlineHint.setPadding(12, 6, 12, 12);
        root.addView(offlineHint);

        setContentView(root);
    }

    private void configureWebView() {
        WebSettings settings = webView.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        settings.setMediaPlaybackRequiresUserGesture(false);
        settings.setMixedContentMode(WebSettings.MIXED_CONTENT_NEVER_ALLOW);
        settings.setDatabaseEnabled(true);
        settings.setCacheMode(WebSettings.LOAD_DEFAULT);
        webView.addJavascriptInterface(new AlfredBridge(), "AlfredAndroid");
        webView.setWebChromeClient(new WebChromeClient() {
            @Override
            public void onPermissionRequest(PermissionRequest request) {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP
                        && request.getOrigin() != null
                        && isDashboardOrigin(request.getOrigin())) {
                    String[] resources = request.getResources();
                    for (String resource : resources) {
                        if (PermissionRequest.RESOURCE_AUDIO_CAPTURE.equals(resource)) {
                            request.grant(new String[] { PermissionRequest.RESOURCE_AUDIO_CAPTURE });
                            return;
                        }
                    }
                }
                request.deny();
            }
        });
        webView.setWebViewClient(new WebViewClient() {
            @Override
            public void onPageFinished(WebView view, String url) {
                deliverHandsFreeState();
                deliverPendingWake();
                deliverPendingTranscript();
            }

            @Override
            public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
                return handleExternalUri(request.getUrl());
            }

            @Override
            public boolean shouldOverrideUrlLoading(WebView view, String url) {
                return handleExternalUri(Uri.parse(url));
            }
        });
    }

    private void loadDashboard(String rawUrl) {
        String normalized = normalizeUrl(rawUrl);
        if (normalized.isEmpty()) {
            showToast("Enter ALFRED's local IP address or alfred.local.");
            return;
        }
        Uri uri = Uri.parse(normalized);
        dashboardHost = uri.getHost();
        urlField.setText(normalized);
        prefs.edit().putString(PREF_DASHBOARD_URL, normalized).apply();
        webView.loadUrl(normalized);
    }

    private String normalizeUrl(String rawUrl) {
        String value = rawUrl == null ? "" : rawUrl.trim();
        if (value.isEmpty()) value = DEFAULT_URL;
        if (!value.startsWith("http://") && !value.startsWith("https://")) value = "http://" + value;
        try {
            Uri uri = Uri.parse(value);
            String host = uri.getHost();
            if (host == null || !("http".equalsIgnoreCase(uri.getScheme())
                    || "https".equalsIgnoreCase(uri.getScheme())) || !isAllowedDeviceHost(host)) return "";
            return uri.toString();
        } catch (Exception ex) {
            return "";
        }
    }

    private boolean isDashboardOrigin(Uri uri) {
        if (uri == null || webView == null || webView.getUrl() == null) return false;
        Uri current = Uri.parse(webView.getUrl());
        int currentPort = current.getPort() >= 0 ? current.getPort() : ("https".equalsIgnoreCase(current.getScheme()) ? 443 : 80);
        int targetPort = uri.getPort() >= 0 ? uri.getPort() : ("https".equalsIgnoreCase(uri.getScheme()) ? 443 : 80);
        return current.getScheme() != null && current.getScheme().equalsIgnoreCase(uri.getScheme())
                && current.getHost() != null && current.getHost().equalsIgnoreCase(uri.getHost())
                && currentPort == targetPort;
    }

    private boolean isAllowedDeviceHost(String host) {
        String h = host.toLowerCase(Locale.ROOT);
        if ("localhost".equals(h) || h.endsWith(".local")) return true;
        String[] parts = h.split("\\.");
        if (parts.length != 4) return false;
        try {
            int a = Integer.parseInt(parts[0]), b = Integer.parseInt(parts[1]);
            int c = Integer.parseInt(parts[2]), d = Integer.parseInt(parts[3]);
            if (a < 0 || a > 255 || b < 0 || b > 255 || c < 0 || c > 255 || d < 0 || d > 255) return false;
            return a == 10 || a == 127 || (a == 192 && b == 168)
                    || (a == 172 && b >= 16 && b <= 31) || (a == 169 && b == 254);
        } catch (NumberFormatException ex) {
            return false;
        }
    }

    private boolean handleExternalUri(Uri uri) {
        String scheme = uri.getScheme();
        if (scheme == null) return true;
        if ("http".equalsIgnoreCase(scheme) || "https".equalsIgnoreCase(scheme)) {
            if (isDashboardOrigin(uri)) return false;
            if ("music.youtube.com".equalsIgnoreCase(uri.getHost()) && uri.getPath() != null && uri.getPath().startsWith("/search")) {
                launchMusic("ytmusic", uri.getQueryParameter("q"));
                return true;
            }
            startExternal(new Intent(Intent.ACTION_VIEW, uri), "No browser can open this link.");
            return true;
        }

        if ("spotify".equals(scheme)) {
            String spotifyUri = uri.toString();
            if (spotifyUri.startsWith("spotify:search:")) {
                launchSpotify(spotifyUri.substring("spotify:search:".length()));
            } else {
                startExternal(new Intent(Intent.ACTION_VIEW, uri).setPackage("com.spotify.music"), "Spotify is not installed.");
            }
            return true;
        }

        if ("google.navigation".equals(scheme) || "geo".equals(scheme)) {
            startExternal(new Intent(Intent.ACTION_VIEW, uri).setPackage("com.google.android.apps.maps"), "Google Maps is not installed.");
            return true;
        }

        if ("tel".equals(scheme) || "sms".equals(scheme) || "smsto".equals(scheme)) {
            startExternal(new Intent(Intent.ACTION_VIEW, uri), "No app can handle this request.");
            return true;
        }

        if ("intent".equals(scheme)) {
            try {
                Intent intent = Intent.parseUri(uri.toString(), Intent.URI_INTENT_SCHEME);
                intent.addCategory(Intent.CATEGORY_BROWSABLE);
                String packageName = intent.getPackage();
                String query = intent.getStringExtra(SearchManager.QUERY);
                if (query == null) {
                    query = intent.getStringExtra("query");
                }
                if ("com.spotify.music".equals(packageName)) {
                    launchMusic("spotify", query == null ? "" : query);
                } else if ("com.google.android.apps.youtube.music".equals(packageName)) {
                    launchMusic("ytmusic", query == null ? "" : query);
                } else if ("com.google.android.apps.maps".equals(packageName)) {
                    startExternal(intent, "Google Maps is not installed.");
                } else {
                    startExternal(intent, "No app can handle this request.");
                }
            } catch (Exception ex) {
                showToast("Intent failed: " + ex.getMessage());
            }
            return true;
        }

        return false;
    }

    private void registerHandsFreeReceiver() {
        IntentFilter filter = new IntentFilter();
        filter.addAction(HandsFreeService.ACTION_STATE);
        filter.addAction(HandsFreeService.ACTION_TRANSCRIPT);
        filter.addAction(HandsFreeService.ACTION_WAKE);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(handsFreeReceiver, filter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            registerReceiver(handsFreeReceiver, filter);
        }
        receiverRegistered = true;
        handsFreeActive = HandsFreeService.isRunning();
    }

    private void deliverHandsFreeState() {
        if (webView == null) return;
        runOnUiThread(new Runnable() {
            @Override public void run() {
                webView.evaluateJavascript("if(window.alfredHandsFreeState)window.alfredHandsFreeState(" + handsFreeActive + ");", null);
            }
        });
    }

    private void deliverPendingTranscript() {
        if (webView == null || pendingTranscript.isEmpty()) return;
        final String transcript = pendingTranscript;
        pendingTranscript = "";
        prefs.edit().remove("pending_voice_command").apply();
        runOnUiThread(new Runnable() {
            @Override public void run() {
                webView.evaluateJavascript("if(window.alfredNativeTranscript)window.alfredNativeTranscript(" + JSONObject.quote(transcript) + ");", null);
            }
        });
    }

    private void deliverPendingWake() {
        if (webView == null || !pendingWake) return;
        pendingWake = false;
        prefs.edit().putBoolean("pending_voice_wake", false).apply();
        runOnUiThread(new Runnable() {
            @Override public void run() {
                webView.evaluateJavascript("if(window.alfredWakeDetected)window.alfredWakeDetected();", null);
            }
        });
    }

    private void pauseHandsFreeRecognition() {
        if (!HandsFreeService.isRunning()) return;
        runOnUiThread(new Runnable() {
            @Override public void run() {
                Intent intent = new Intent(MainActivity.this, HandsFreeService.class).setAction(HandsFreeService.ACTION_PAUSE);
                try { startService(intent); } catch (IllegalStateException ignored) { }
            }
        });
    }

    private void resumeHandsFreeRecognition() {
        if (!HandsFreeService.isRunning()) return;
        runOnUiThread(new Runnable() {
            @Override public void run() {
                Intent intent = new Intent(MainActivity.this, HandsFreeService.class).setAction(HandsFreeService.ACTION_RESUME);
                try { startService(intent); } catch (IllegalStateException ignored) { }
            }
        });
    }

    private void startTapRecognition() {
        runOnUiThread(new Runnable() {
            @Override public void run() {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M
                        && checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
                    showToast("Allow microphone access for ALFRED, then tap the mic again.");
                    notifyTapRecognition(false, "Microphone permission is needed.");
                    return;
                }
                if (!SpeechRecognizer.isRecognitionAvailable(MainActivity.this)) {
                    notifyTapRecognition(false, "Speech recognition is unavailable. Check that a speech service is installed.");
                    return;
                }
                try {
                    stopTapRecognition(false);
                    tapRecognizer = SpeechRecognizer.createSpeechRecognizer(MainActivity.this);
                    tapRecognizer.setRecognitionListener(MainActivity.this);
                    Intent request = new Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH);
                    request.putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM);
                    request.putExtra(RecognizerIntent.EXTRA_PARTIAL_RESULTS, false);
                    request.putExtra(RecognizerIntent.EXTRA_MAX_RESULTS, 1);
                    String selected = prefs.getString("voice_language", "auto");
                    String language = "hi-IN".equals(selected) ? "hi-IN"
                            : "en-IN".equals(selected) ? "en-IN" : Locale.getDefault().toLanguageTag();
                    request.putExtra(RecognizerIntent.EXTRA_LANGUAGE, language);
                    tapRecognitionActive = true;
                    pauseHandsFreeRecognition();
                    tapRecognizer.startListening(request);
                    notifyTapRecognition(true, "Listening. Speak your request.");
                } catch (SecurityException | IllegalStateException ex) {
                    stopTapRecognition(false);
                    notifyTapRecognition(false, "Could not start the microphone. Try tapping again.");
                }
            }
        });
    }

    private void stopTapRecognition(boolean notify) {
        if (Looper.myLooper() != Looper.getMainLooper()) {
            runOnUiThread(new Runnable() { @Override public void run() { stopTapRecognition(notify); } });
            return;
        }
        tapRecognitionActive = false;
        SpeechRecognizer recognizer = tapRecognizer;
        tapRecognizer = null;
        if (recognizer != null) {
            try { recognizer.cancel(); } catch (Exception ignored) { }
            try { recognizer.destroy(); } catch (Exception ignored) { }
        }
        if (notify) notifyTapRecognition(false, "Listening stopped.");
    }

    private void notifyTapRecognition(final boolean active, final String message) {
        if (webView == null) return;
        runOnUiThread(new Runnable() {
            @Override public void run() {
                webView.evaluateJavascript("if(window.alfredNativeListeningState)window.alfredNativeListeningState(" + active + "," + JSONObject.quote(message) + ");", null);
            }
        });
    }

    private void deliverTapTranscript(String text) {
        if (webView == null) return;
        webView.evaluateJavascript("if(window.alfredNativeTranscript)window.alfredNativeTranscript(" + JSONObject.quote(text) + ");", null);
    }

    @Override public void onReadyForSpeech(Bundle params) { }
    @Override public void onBeginningOfSpeech() { }
    @Override public void onRmsChanged(float rmsdB) { }
    @Override public void onBufferReceived(byte[] buffer) { }
    @Override public void onEndOfSpeech() { }
    @Override public void onPartialResults(Bundle partialResults) { }
    @Override public void onEvent(int eventType, Bundle params) { }
    @Override public void onResults(Bundle results) {
        ArrayList<String> matches = results == null ? null : results.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION);
        String transcript = matches == null || matches.isEmpty() ? "" : matches.get(0).trim();
        stopTapRecognition(false);
        if (transcript.isEmpty()) notifyTapRecognition(false, "I didn’t catch that. Tap the mic and try again.");
        else {
            notifyTapRecognition(false, "Heard you. Working on it.");
            deliverTapTranscript(transcript);
        }
        resumeHandsFreeRecognition();
    }
    @Override public void onError(int error) {
        stopTapRecognition(false);
        String message = error == SpeechRecognizer.ERROR_NO_MATCH || error == SpeechRecognizer.ERROR_SPEECH_TIMEOUT
                ? "I didn’t catch that. Tap the mic and try again."
                : error == SpeechRecognizer.ERROR_INSUFFICIENT_PERMISSIONS
                    ? "Microphone permission is needed for voice input."
                    : "Voice input stopped. Check your connection and speech service, then try again.";
        notifyTapRecognition(false, message);
        resumeHandsFreeRecognition();
    }

    private void enableHandsFree() {
        runOnUiThread(new Runnable() {
            @Override public void run() { enableHandsFreeOnUiThread(); }
        });
    }

    private void enableHandsFreeOnUiThread() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU
                && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            pendingHandsFreeStart = true;
            requestPermissions(new String[] { Manifest.permission.POST_NOTIFICATIONS }, REQUEST_NOTIFICATION_PERMISSION);
            return;
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M
                && checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            pendingHandsFreeStart = true;
            requestPermissions(new String[] { Manifest.permission.RECORD_AUDIO }, REQUEST_RUNTIME_PERMS);
            return;
        }
        Intent intent = new Intent(this, HandsFreeService.class);
        try {
            prefs.edit().putBoolean("hands_free_enabled", true).apply();
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) startForegroundService(intent);
            else startService(intent);
        } catch (SecurityException | IllegalStateException ex) {
            showToast("Android could not start hands-free listening. Keep ALFRED open and allow microphone and notification access.");
        }
    }

    private void disableHandsFree() {
        prefs.edit().putBoolean("hands_free_enabled", false).apply();
        runOnUiThread(new Runnable() {
            @Override public void run() {
                try { startService(new Intent(MainActivity.this, HandsFreeService.class).setAction(HandsFreeService.ACTION_STOP)); }
                catch (IllegalStateException ignored) { }
            }
        });
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQUEST_NOTIFICATION_PERMISSION) {
            if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED && pendingHandsFreeStart) {
                pendingHandsFreeStart = false;
                enableHandsFree();
            } else {
                pendingHandsFreeStart = false;
                prefs.edit().putBoolean("hands_free_enabled", false).apply();
                showToast("Allow notifications to keep the hands-free microphone visible with a Stop control.");
            }
        } else if (requestCode == REQUEST_RUNTIME_PERMS) {
            if (pendingHandsFreeStart && grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
                pendingHandsFreeStart = false;
                enableHandsFree();
            } else if (pendingHandsFreeStart) {
                pendingHandsFreeStart = false;
                prefs.edit().putBoolean("hands_free_enabled", false).apply();
                showToast("Microphone permission is needed for hands-free listening.");
            }
        } else if (requestCode == REQUEST_CONTACTS_PERMISSION) {
            String name = pendingContactLookup;
            pendingContactLookup = "";
            if (!name.isEmpty()) {
                if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) lookupContactNumber(name);
                else {
                    showToast("Choose a contact manually or allow Contacts access to search by name.");
                    startContactPicker("call");
                }
            }
        } else if (requestCode == REQUEST_LOCATION_PERMISSION) {
            pendingLocation = false;
            boolean granted = checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED
                    || checkSelfPermission(Manifest.permission.ACCESS_COARSE_LOCATION) == PackageManager.PERMISSION_GRANTED;
            if (granted) fetchPhoneLocation(); else reportLocationError("Allow location access to answer location requests.");
        } else if (requestCode == REQUEST_CALL_PERMISSION) {
            String number = pendingCallNumber;
            pendingCallNumber = "";
            if (number.isEmpty()) return;
            if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
                placeDirectCall(number);
            } else {
                Uri tel = Uri.fromParts("tel", number.replaceAll("[^+0-9]", ""), null);
                startExternal(new Intent(Intent.ACTION_DIAL, tel), "No dialer app is available.");
            }
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_CONTACT || resultCode != RESULT_OK || data == null || data.getData() == null) return;
        Cursor cursor = null;
        try {
            cursor = getContentResolver().query(data.getData(), new String[] {
                    ContactsContract.CommonDataKinds.Phone.DISPLAY_NAME,
                    ContactsContract.CommonDataKinds.Phone.NUMBER
            }, null, null, null);
            if (cursor != null && cursor.moveToFirst()) {
                String name = cursor.getString(0);
                String number = cursor.getString(1);
                final String action = pendingContactAction;
                pendingContactAction = "pick";
                final String js = "if(window.alfredContactSelected)window.alfredContactSelected("
                        + JSONObject.quote(name == null ? "" : name) + ","
                        + JSONObject.quote(number == null ? "" : number) + ","
                        + JSONObject.quote(action) + ");";
                if (webView != null) webView.post(new Runnable() {
                    @Override public void run() { webView.evaluateJavascript(js, null); }
                });
            } else {
                showToast("This contact has no phone number.");
            }
        } catch (Exception ex) {
            showToast("Could not read the selected contact.");
        } finally {
            if (cursor != null) cursor.close();
        }
    }

    private void startExternal(final Intent intent, final String missingMessage) {
        startExternal(intent, missingMessage, null);
    }

    private void startExternal(final Intent intent, final String missingMessage, final Runnable fallback) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                try {
                    startActivity(intent);
                } catch (ActivityNotFoundException ex) {
                    if (fallback != null) {
                        fallback.run();
                    } else {
                        Toast.makeText(MainActivity.this, missingMessage, Toast.LENGTH_LONG).show();
                    }
                } catch (SecurityException ex) {
                    Toast.makeText(MainActivity.this, "Permission needed for that action.", Toast.LENGTH_LONG).show();
                }
            }
        });
    }

    private void showToast(final String text) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                Toast.makeText(MainActivity.this, text, Toast.LENGTH_LONG).show();
            }
        });
    }

    private String encode(String value) {
        return URLEncoder.encode(value == null ? "" : value.trim(), StandardCharsets.UTF_8).replace("+", "%20");
    }

    private void launchSpotify(String query) {
        launchMusic("spotify", query);
    }

    private void launchMusic(String provider, String rawQuery) {
        final String query = rawQuery == null ? "" : rawQuery.trim().substring(0, Math.min(120, rawQuery.trim().length()));
        if (query.isEmpty()) { showToast("Say or enter the track name first."); return; }
        Intent play = new Intent(MediaStore.INTENT_ACTION_MEDIA_PLAY_FROM_SEARCH)
                .putExtra(SearchManager.QUERY, query)
                .putExtra(MediaStore.EXTRA_MEDIA_FOCUS, "vnd.android.cursor.item/audio")
                .putExtra(MediaStore.EXTRA_MEDIA_TITLE, query);
        if ("spotify".equals(provider)) {
            play.setPackage("com.spotify.music");
            Intent fallback = new Intent(Intent.ACTION_VIEW, Uri.parse("spotify:search:" + encode(query))).setPackage("com.spotify.music");
            startExternal(play, "Spotify is not installed.", new Runnable() {
                @Override public void run() {
                    showToast("Spotify could not start playback directly; opening its search.");
                    startExternal(fallback, "Spotify is not installed.");
                }
            });
            return;
        }
        if ("ytmusic".equals(provider)) {
            play.setPackage("com.google.android.apps.youtube.music");
            final String searchUrl = "https://music.youtube.com/search?q=" + encode(query);
            Intent appLink = new Intent(Intent.ACTION_VIEW, Uri.parse(searchUrl)).setPackage("com.google.android.apps.youtube.music");
            startExternal(play, "YouTube Music is not installed.", new Runnable() {
                @Override public void run() {
                    startExternal(appLink, "YouTube Music is not installed.", new Runnable() {
                        @Override public void run() {
                            showToast("YouTube Music could not open. Opening the search in your browser instead.");
                            startExternal(new Intent(Intent.ACTION_VIEW, Uri.parse(searchUrl)), "No browser can open YouTube Music.");
                        }
                    });
                }
            });
            return;
        }
        Intent chooser = Intent.createChooser(play, "Choose a music app to play “" + query + "”");
        startExternal(chooser, "No music app can play this request.", new Runnable() {
            @Override public void run() {
                startExternal(new Intent(Intent.ACTION_VIEW, Uri.parse("https://music.youtube.com/search?q=" + encode(query))),
                        "No music app or browser can open this track.");
            }
        });
    }

    private void startContactPicker(String requestedAction) {
        pendingContactAction = "call".equals(requestedAction) ? "call" : "pick";
        pauseHandsFreeRecognition();
        runOnUiThread(new Runnable() {
            @Override public void run() {
                Intent pick = new Intent(Intent.ACTION_PICK);
                pick.setType(ContactsContract.CommonDataKinds.Phone.CONTENT_TYPE);
                try { startActivityForResult(pick, REQUEST_CONTACT); }
                catch (ActivityNotFoundException ex) { showToast("No contacts app is available on this device."); }
            }
        });
    }

    private void requestPhoneLocation() {
        runOnUiThread(new Runnable() {
            @Override public void run() {
                boolean fine = Build.VERSION.SDK_INT < Build.VERSION_CODES.M
                        || checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED;
                boolean coarse = Build.VERSION.SDK_INT < Build.VERSION_CODES.M
                        || checkSelfPermission(Manifest.permission.ACCESS_COARSE_LOCATION) == PackageManager.PERMISSION_GRANTED;
                if (!fine && !coarse) {
                    pendingLocation = true;
                    requestPermissions(new String[] {
                            Manifest.permission.ACCESS_FINE_LOCATION,
                            Manifest.permission.ACCESS_COARSE_LOCATION
                    }, REQUEST_LOCATION_PERMISSION);
                    return;
                }
                fetchPhoneLocation();
            }
        });
    }

    private void fetchPhoneLocation() {
        try {
            locationManager = (LocationManager) getSystemService(Context.LOCATION_SERVICE);
            if (locationManager == null) { reportLocationError("Android location services are unavailable."); return; }
            boolean fine = Build.VERSION.SDK_INT < Build.VERSION_CODES.M
                    || checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED;
            String provider = fine && locationManager.isProviderEnabled(LocationManager.GPS_PROVIDER)
                    ? LocationManager.GPS_PROVIDER : LocationManager.NETWORK_PROVIDER;
            if (!locationManager.isProviderEnabled(provider)) {
                String alternate = provider.equals(LocationManager.GPS_PROVIDER)
                        ? LocationManager.NETWORK_PROVIDER : LocationManager.GPS_PROVIDER;
                if (!locationManager.isProviderEnabled(alternate)) {
                    reportLocationError("Turn on Location in Android settings, then ask again.");
                    return;
                }
                provider = alternate;
            }
            locationRequestPending = true;
            locationTimeout = new Runnable() {
                @Override public void run() {
                    if (locationRequestPending) {
                        locationRequestPending = false;
                        clearLocationUpdates();
                        reportLocationError("I couldn't get a location fix. Check that Location is turned on and try again.");
                    }
                }
            };
            locationHandler.postDelayed(locationTimeout, 15000);
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                locationManager.getCurrentLocation(provider, null, getMainExecutor(), this::onPhoneLocation);
            } else {
                final String requestedProvider = provider;
                locationListener = new LocationListener() {
                    @Override public void onLocationChanged(Location location) { onPhoneLocation(location); }
                    @Override public void onProviderDisabled(String name) { }
                    @Override public void onProviderEnabled(String name) { }
                    @Override public void onStatusChanged(String name, int status, Bundle extras) { }
                };
                // Android 6–10 only provide a single update through this API.
                //noinspection deprecation
                locationManager.requestSingleUpdate(requestedProvider, locationListener, Looper.getMainLooper());
            }
        } catch (SecurityException ex) {
            locationRequestPending = false;
            clearLocationUpdates();
            reportLocationError("Allow location access to use this feature.");
        } catch (IllegalArgumentException ex) {
            locationRequestPending = false;
            clearLocationUpdates();
            reportLocationError("No location provider is available on this phone.");
        }
    }

    private void onPhoneLocation(Location location) {
        if (!locationRequestPending || location == null) return;
        locationRequestPending = false;
        clearLocationUpdates();
        final String js = "if(window.alfredNativeLocation)window.alfredNativeLocation("
                + location.getLatitude() + "," + location.getLongitude() + "," + location.getAccuracy() + ");";
        if (webView != null) webView.post(new Runnable() {
            @Override public void run() { webView.evaluateJavascript(js, null); }
        });
    }

    private void clearLocationUpdates() {
        if (locationTimeout != null) locationHandler.removeCallbacks(locationTimeout);
        locationTimeout = null;
        if (locationManager != null && locationListener != null) {
            try { locationManager.removeUpdates(locationListener); } catch (SecurityException ignored) { }
        }
        locationListener = null;
    }

    private void reportLocationError(String message) {
        if (webView == null) { showToast(message); return; }
        final String js = "if(window.alfredNativeLocationError)window.alfredNativeLocationError(" + JSONObject.quote(message) + ");";
        webView.post(new Runnable() {
            @Override public void run() { webView.evaluateJavascript(js, null); }
        });
    }

    private void resolveContactForCall(final String requestedName) {
        if (requestedName == null || requestedName.trim().isEmpty()) {
            showToast("Say the contact's name first.");
            return;
        }
        runOnUiThread(new Runnable() {
            @Override public void run() {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M
                        && checkSelfPermission(Manifest.permission.READ_CONTACTS) != PackageManager.PERMISSION_GRANTED) {
                    pendingContactLookup = requestedName.trim();
                    requestPermissions(new String[] { Manifest.permission.READ_CONTACTS }, REQUEST_CONTACTS_PERMISSION);
                    return;
                }
                lookupContactNumber(requestedName.trim());
            }
        });
    }

    private void lookupContactNumber(String requestedName) {
        Cursor cursor = null;
        String bestName = null, bestNumber = null;
        try {
            Uri uri = ContactsContract.CommonDataKinds.Phone.CONTENT_URI;
            String selection = ContactsContract.CommonDataKinds.Phone.DISPLAY_NAME + " LIKE ?";
            String[] args = new String[] { "%" + requestedName.replace("%", "").replace("_", "") + "%" };
            cursor = getContentResolver().query(uri, new String[] {
                    ContactsContract.CommonDataKinds.Phone.DISPLAY_NAME,
                    ContactsContract.CommonDataKinds.Phone.NUMBER
            }, selection, args, ContactsContract.CommonDataKinds.Phone.DISPLAY_NAME + " COLLATE NOCASE ASC");
            String wanted = requestedName.trim().toLowerCase(Locale.ROOT);
            if (cursor != null) while (cursor.moveToNext()) {
                String name = cursor.getString(0), number = cursor.getString(1);
                if (name == null || number == null || number.trim().isEmpty()) continue;
                String actual = name.trim().toLowerCase(Locale.ROOT);
                if (bestName == null || actual.equals(wanted)) { bestName = name; bestNumber = number; }
                if (actual.equals(wanted)) break;
            }
        } catch (SecurityException ex) {
            showToast("Allow Contacts access or choose a contact manually.");
        } catch (Exception ex) {
            showToast("Could not search phone contacts. Choose a contact manually.");
        } finally { if (cursor != null) cursor.close(); }

        if (bestNumber == null) {
            showToast("I couldn't find “" + requestedName + "”. Choose the person from your contacts.");
            startContactPicker("call");
            return;
        }
        final String name = bestName, number = bestNumber;
        final String js = "if(window.alfredContactSelected)window.alfredContactSelected("
                + JSONObject.quote(name) + "," + JSONObject.quote(number) + ",\"call\");";
        if (webView != null) webView.post(new Runnable() {
            @Override public void run() { webView.evaluateJavascript(js, null); }
        });
    }

    private void placeDirectCall(String phone) {
        String digits = phone == null ? "" : phone.replaceAll("[^+0-9]", "");
        if (digits.length() < 3 || digits.length() > 20) { showToast("Choose a valid phone number first."); return; }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M
                && checkSelfPermission(Manifest.permission.CALL_PHONE) != PackageManager.PERMISSION_GRANTED) {
            pendingCallNumber = phone;
            runOnUiThread(new Runnable() {
                @Override public void run() { requestPermissions(new String[] { Manifest.permission.CALL_PHONE }, REQUEST_CALL_PERMISSION); }
            });
            return;
        }
        Uri tel = Uri.fromParts("tel", digits, null);
        startExternal(new Intent(Intent.ACTION_CALL, tel), "Could not place the call.", new Runnable() {
            @Override public void run() { startExternal(new Intent(Intent.ACTION_DIAL, tel), "No dialer app is available."); }
        });
    }

    private void launchNavigation(String destination) {
        if (destination == null || destination.trim().isEmpty()) {
            showToast("Enter a destination first.");
            return;
        }
        Uri uri = Uri.parse("google.navigation:q=" + encode(destination) + "&mode=d");
        Intent navIntent = new Intent(Intent.ACTION_VIEW, uri);
        navIntent.setPackage("com.google.android.apps.maps");
        startExternal(navIntent, "Google Maps is not installed.");
    }

    private void launchInstalledApp(final String requestedName) {
        runOnUiThread(new Runnable() {
            @Override public void run() {
                String wanted = requestedName == null ? "" : requestedName.trim().toLowerCase(Locale.ROOT);
                if (wanted.isEmpty()) { showToast("Say which app to open."); return; }
                Intent launcher = new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER);
                java.util.List<android.content.pm.ResolveInfo> apps = getPackageManager().queryIntentActivities(launcher, PackageManager.MATCH_DEFAULT_ONLY);
                android.content.pm.ResolveInfo match = null;
                for (android.content.pm.ResolveInfo app : apps) {
                    CharSequence label = app.loadLabel(getPackageManager());
                    String actual = label == null ? "" : label.toString().trim().toLowerCase(Locale.ROOT);
                    if (actual.equals(wanted)) { match = app; break; }
                    if (match == null && (actual.contains(wanted) || wanted.contains(actual))) match = app;
                }
                if (match == null || match.activityInfo == null) {
                    showToast(requestedName + " is not installed or has no launchable app.");
                    return;
                }
                Intent launch = getPackageManager().getLaunchIntentForPackage(match.activityInfo.packageName);
                if (launch == null) {
                    showToast("Android could not open " + requestedName + ".");
                    return;
                }
                launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                try { startActivity(launch); }
                catch (ActivityNotFoundException | SecurityException ex) { showToast("Android could not open " + requestedName + "."); }
            }
        });
    }

    public class AlfredBridge {
        @JavascriptInterface
        public void openApp(String appName) { launchInstalledApp(appName); }

        @JavascriptInterface
        public void listenOnce() { startTapRecognition(); }

        @JavascriptInterface
        public void stopListenOnce() { stopTapRecognition(true); }

        @JavascriptInterface
        public void playSpotify(String query) {
            launchSpotify(query);
        }

        @JavascriptInterface
        public void playMusic(String provider, String query) {
            String app = "spotify".equals(provider) || "ytmusic".equals(provider) ? provider : "any";
            launchMusic(app, query);
        }

        @JavascriptInterface
        public void selectContact() {
            startContactPicker("pick");
        }

        @JavascriptInterface
        public void callContact(String name) {
            resolveContactForCall(name);
        }

        @JavascriptInterface
        public void getCurrentLocation() { requestPhoneLocation(); }

        @JavascriptInterface
        public void setHandsFree(boolean enabled) {
            if (enabled) enableHandsFree(); else disableHandsFree();
        }

        @JavascriptInterface
        public void setVoiceLanguage(String languageTag) {
            String tag = "hi-IN".equals(languageTag) ? "hi-IN" : "en-IN".equals(languageTag) ? "en-IN" : "auto";
            prefs.edit().putString("voice_language", tag).apply();
        }

        @JavascriptInterface
        public boolean isHandsFreeActive() {
            return HandsFreeService.isRunning() || handsFreeActive;
        }

        @JavascriptInterface
        public void pauseHandsFree() {
            pauseHandsFreeRecognition();
        }

        @JavascriptInterface
        public void resumeHandsFree() {
            resumeHandsFreeRecognition();
        }

        @JavascriptInterface
        public void acknowledgeWake() {
            runOnUiThread(new Runnable() {
                @Override public void run() {
                    pauseHandsFreeRecognition();
                    if (textToSpeechReady && textToSpeech != null) {
                        int result = textToSpeech.speak("Yes?", TextToSpeech.QUEUE_FLUSH, null, WAKE_ACK_UTTERANCE);
                        if (result == TextToSpeech.ERROR) resumeHandsFreeRecognition();
                    } else {
                        resumeHandsFreeRecognition();
                    }
                }
            });
        }

        @JavascriptInterface
        public void navigate(String destination) {
            launchNavigation(destination);
        }

        @JavascriptInterface
        public void callNumber(String phone) {
            if (phone == null || phone.trim().isEmpty()) {
                showToast("Enter a phone number first.");
                return;
            }
            placeDirectCall(phone);
        }

        @JavascriptInterface
        public void sendSms(String phone, String body) {
            if (phone == null || phone.trim().isEmpty()) {
                showToast("Enter a phone number first.");
                return;
            }
            Intent smsIntent = new Intent(Intent.ACTION_SENDTO, Uri.parse("smsto:" + Uri.encode(phone.trim())));
            smsIntent.putExtra("sms_body", body == null ? "" : body);
            startExternal(smsIntent, "No SMS app is available.");
        }

        @JavascriptInterface
        public void setAlarm(String label, int hour, int minute) {
            Intent alarmIntent = new Intent(AlarmClock.ACTION_SET_ALARM)
                    .putExtra(AlarmClock.EXTRA_MESSAGE, label == null ? "Alfred alarm" : label)
                    .putExtra(AlarmClock.EXTRA_HOUR, Math.max(0, Math.min(23, hour)))
                    .putExtra(AlarmClock.EXTRA_MINUTES, Math.max(0, Math.min(59, minute)));
            startExternal(alarmIntent, "No alarm app is available.");
        }

        @JavascriptInterface
        public void speak(final String text) {
            if (text == null || text.trim().isEmpty()) {
                return;
            }
            runOnUiThread(new Runnable() {
                @Override
                public void run() {
                    if (textToSpeechReady && textToSpeech != null) {
                        textToSpeech.speak(text, TextToSpeech.QUEUE_FLUSH, null, "alfred-speech");
                    } else {
                        Toast.makeText(MainActivity.this, text, Toast.LENGTH_LONG).show();
                    }
                }
            });
        }

        @JavascriptInterface
        public void speakLocalized(final String text, final String languageTag) {
            if (text == null || text.trim().isEmpty()) {
                return;
            }
            runOnUiThread(new Runnable() {
                @Override
                public void run() {
                    if (textToSpeech == null) {
                        Toast.makeText(MainActivity.this, text, Toast.LENGTH_LONG).show();
                        return;
                    }
                    String tag = "hi-IN".equals(languageTag) ? "hi-IN" : "en-IN";
                    Locale locale = Locale.forLanguageTag(tag);
                    int result = textToSpeech.setLanguage(locale);
                    if ((result == TextToSpeech.LANG_MISSING_DATA
                            || result == TextToSpeech.LANG_NOT_SUPPORTED) && "en-IN".equals(tag)) {
                        result = textToSpeech.setLanguage(Locale.UK);
                    }
                    if (result == TextToSpeech.LANG_MISSING_DATA
                            || result == TextToSpeech.LANG_NOT_SUPPORTED) {
                        textToSpeechReady = false;
                        Toast.makeText(MainActivity.this,
                                "Hindi speech data is unavailable. Install it in Android text-to-speech settings or choose English.",
                                Toast.LENGTH_LONG).show();
                        return;
                    }
                    textToSpeechReady = true;
                    textToSpeech.setSpeechRate(1.0f);
                    textToSpeech.setPitch(1.0f);
                    textToSpeech.speak(text, TextToSpeech.QUEUE_FLUSH, null, "alfred-speech-localized");
                }
            });
        }

        @JavascriptInterface
        public boolean isNativeReady() {
            return true;
        }

        @JavascriptInterface
        public String networkStatus() {
            ConnectivityManager cm = (ConnectivityManager) getSystemService(Context.CONNECTIVITY_SERVICE);
            if (cm == null) {
                return "unknown";
            }
            Network network = cm.getActiveNetwork();
            NetworkCapabilities caps = cm.getNetworkCapabilities(network);
            if (caps == null) {
                return "offline";
            }
            if (caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) {
                return "wifi";
            }
            if (caps.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR)) {
                return "cellular";
            }
            return "online";
        }
    }
}
