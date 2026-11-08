package com.alfred.dashboard;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.speech.RecognitionListener;
import android.speech.RecognizerIntent;
import android.speech.SpeechRecognizer;
import android.content.pm.ServiceInfo;

import java.util.ArrayList;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** User-started speech recognition that remains active behind the dashboard, with a visible stop action. */
public class HandsFreeService extends Service implements RecognitionListener {
    public static final String ACTION_STATE = "com.alfred.dashboard.HANDS_FREE_STATE";
    public static final String ACTION_TRANSCRIPT = "com.alfred.dashboard.HANDS_FREE_TRANSCRIPT";
    public static final String ACTION_WAKE = "com.alfred.dashboard.HANDS_FREE_WAKE";
    public static final String ACTION_PAUSE = "com.alfred.dashboard.HANDS_FREE_PAUSE";
    public static final String ACTION_RESUME = "com.alfred.dashboard.HANDS_FREE_RESUME";
    public static final String ACTION_STOP = "com.alfred.dashboard.HANDS_FREE_STOP";
    public static final String EXTRA_ACTIVE = "active";
    public static final String EXTRA_TEXT = "text";
    private static final String CHANNEL_ID = "alfred_hands_free";
    private static final int NOTIFICATION_ID = 73;

    private static volatile boolean running = false;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private SpeechRecognizer recognizer;
    private boolean paused = false;
    private boolean listening = false;
    private int failures = 0;
    private String languageTag;
    private boolean conversationOpen = false;
    private final Runnable closeConversation = new Runnable() {
        @Override public void run() { conversationOpen = false; }
    };

    public static boolean isRunning() { return running; }

    @Override
    public void onCreate() {
        super.onCreate();
        createNotificationChannel();
        languageTag = getSharedPreferences("alfred_prefs", MODE_PRIVATE).getString("voice_language", "auto");
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String action = intent == null ? null : intent.getAction();
        if (ACTION_STOP.equals(action)) {
            getSharedPreferences("alfred_prefs", MODE_PRIVATE).edit().putBoolean("hands_free_enabled", false).apply();
            stopSelf();
            return START_NOT_STICKY;
        }
        if (ACTION_PAUSE.equals(action)) {
            paused = true;
            destroyRecognizer();
            return START_STICKY;
        }
        if (ACTION_RESUME.equals(action)) paused = false;
        try {
            Notification notification = buildNotification();
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                startForeground(NOTIFICATION_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE);
            } else {
                startForeground(NOTIFICATION_ID, notification);
            }
        } catch (SecurityException | IllegalStateException ex) {
            broadcastState(false);
            stopSelf();
            return START_NOT_STICKY;
        }
        running = true;
        broadcastState(true);
        if (!paused) scheduleRecognition(250);
        return START_STICKY;
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel channel = new NotificationChannel(CHANNEL_ID, "ALFRED hands-free voice", NotificationManager.IMPORTANCE_LOW);
            channel.setDescription("Visible status and stop control while ALFRED listens for your voice commands.");
            NotificationManager manager = getSystemService(NotificationManager.class);
            if (manager != null) manager.createNotificationChannel(channel);
        }
    }

    private Notification buildNotification() {
        return buildNotification("Listening for ‘Alfred’ in the background. Tap to open ALFRED or Stop to end.");
    }

    private Notification buildNotification(String status) {
        Intent open = new Intent(this, MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        int openFlags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) openFlags |= PendingIntent.FLAG_IMMUTABLE;
        PendingIntent openPending = PendingIntent.getActivity(this, 75, open, openFlags);
        Intent stop = new Intent(this, HandsFreeService.class).setAction(ACTION_STOP);
        int pendingFlags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) pendingFlags |= PendingIntent.FLAG_IMMUTABLE;
        PendingIntent stopPending = PendingIntent.getService(this, 74, stop, pendingFlags);
        Notification.Builder builder = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                ? new Notification.Builder(this, CHANNEL_ID) : new Notification.Builder(this);
        return builder.setSmallIcon(android.R.drawable.ic_btn_speak_now)
                .setContentTitle("ALFRED hands-free listening")
                .setContentText(status)
                .setCategory(Notification.CATEGORY_SERVICE)
                .setOngoing(true)
                .setContentIntent(openPending)
                .addAction(android.R.drawable.ic_media_pause, "Stop", stopPending)
                .build();
    }

    private void scheduleRecognition(long delayMs) {
        handler.removeCallbacks(recognitionTask);
        if (running && !paused) handler.postDelayed(recognitionTask, delayMs);
    }

    private final Runnable recognitionTask = new Runnable() {
        @Override public void run() { startRecognition(); }
    };

    private void startRecognition() {
        if (!running || paused || listening) return;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M
                && checkSelfPermission(android.Manifest.permission.RECORD_AUDIO) != android.content.pm.PackageManager.PERMISSION_GRANTED) {
            broadcastState(false);
            stopSelf();
            return;
        }
        if (!SpeechRecognizer.isRecognitionAvailable(this)) {
            failures++;
            scheduleRecognition(Math.min(10000, 1000L * failures));
            return;
        }
        try {
            if (recognizer == null) {
                recognizer = SpeechRecognizer.createSpeechRecognizer(this);
                recognizer.setRecognitionListener(this);
            }
            Intent intent = new Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH);
            intent.putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM);
            intent.putExtra(RecognizerIntent.EXTRA_PARTIAL_RESULTS, false);
            intent.putExtra(RecognizerIntent.EXTRA_MAX_RESULTS, 1);
            String tag = "hi-IN".equals(languageTag) ? "hi-IN"
                    : "en-IN".equals(languageTag) ? "en-IN" : Locale.getDefault().toLanguageTag();
            intent.putExtra(RecognizerIntent.EXTRA_LANGUAGE, tag);
            intent.putExtra(RecognizerIntent.EXTRA_LANGUAGE_PREFERENCE, tag);
            listening = true;
            recognizer.startListening(intent);
        } catch (SecurityException | IllegalStateException ex) {
            listening = false;
            failures++;
            scheduleRecognition(Math.min(10000, 1000L * failures));
        }
    }

    private void destroyRecognizer() {
        handler.removeCallbacks(recognitionTask);
        listening = false;
        if (recognizer != null) {
            try { recognizer.cancel(); } catch (Exception ignored) { }
            try { recognizer.destroy(); } catch (Exception ignored) { }
            recognizer = null;
        }
    }

    private void sendTranscript(String text) {
        getSharedPreferences("alfred_prefs", MODE_PRIVATE).edit()
                .putString("pending_voice_command", text).putBoolean("pending_voice_wake", false).apply();
        pauseForQueuedCommand("Command heard. Tap to open ALFRED and continue.");
        Intent event = new Intent(ACTION_TRANSCRIPT).setPackage(getPackageName()).putExtra(EXTRA_TEXT, text);
        sendBroadcast(event);
    }

    private void keepConversationOpen() {
        conversationOpen = true;
        handler.removeCallbacks(closeConversation);
        handler.postDelayed(closeConversation, 20000);
    }

    private void sendWake() {
        getSharedPreferences("alfred_prefs", MODE_PRIVATE).edit()
                .putBoolean("pending_voice_wake", true).apply();
        // A bare wake word is only the start of a request. Keep the recognizer alive
        // so the next utterance can be captured even when MainActivity is backgrounded.
        NotificationManager manager = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        if (manager != null) manager.notify(NOTIFICATION_ID,
                buildNotification("Wake word heard. Listening for your command now."));
        Intent event = new Intent(ACTION_WAKE).setPackage(getPackageName());
        sendBroadcast(event);
    }

    private void pauseForQueuedCommand(String notificationText) {
        paused = true;
        destroyRecognizer();
        NotificationManager manager = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        if (manager != null) manager.notify(NOTIFICATION_ID, buildNotification(notificationText));
    }

    private void broadcastState(boolean active) {
        Intent event = new Intent(ACTION_STATE).setPackage(getPackageName()).putExtra(EXTRA_ACTIVE, active);
        sendBroadcast(event);
    }

    @Override public void onReadyForSpeech(Bundle params) { }
    @Override public void onBeginningOfSpeech() { }
    @Override public void onRmsChanged(float rmsdB) { }
    @Override public void onBufferReceived(byte[] buffer) { }
    @Override public void onEndOfSpeech() { listening = false; }
    @Override public void onError(int error) {
        listening = false;
        if (error == SpeechRecognizer.ERROR_INSUFFICIENT_PERMISSIONS) {
            broadcastState(false);
            stopSelf();
            return;
        }
        failures = error == SpeechRecognizer.ERROR_NO_MATCH || error == SpeechRecognizer.ERROR_SPEECH_TIMEOUT
                ? 0 : Math.min(5, failures + 1);
        scheduleRecognition(Math.min(5000, 650L * (1L << failures)));
    }
    @Override public void onResults(Bundle results) {
        listening = false;
        failures = 0;
        ArrayList<String> matches = results == null ? null : results.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION);
        if (matches != null && !matches.isEmpty() && matches.get(0) != null && !matches.get(0).trim().isEmpty()) {
            String phrase = matches.get(0).trim();
            Matcher wake = Pattern.compile("(?iu)(?:\\balfred\\b|अल्फ्रेड|अलफ्रेड|एल्फ्रेड)").matcher(phrase);
            if (wake.find()) {
                String command = (phrase.substring(0, wake.start()) + " " + phrase.substring(wake.end()))
                        .replaceFirst("(?iu)^\\s*(?:hey|hello|ok|okay)\\s+", "")
                        .replaceFirst("^[\\s,.:;!?—–।-]+", "").trim();
                keepConversationOpen();
                if (command.isEmpty()) sendWake(); else sendTranscript(command);
            } else if (conversationOpen) {
                keepConversationOpen();
                sendTranscript(phrase);
            }
        }
        scheduleRecognition(conversationOpen ? 350 : 800);
    }
    @Override public void onPartialResults(Bundle partialResults) { }
    @Override public void onEvent(int eventType, Bundle params) { }

    @Override public void onDestroy() {
        running = false;
        destroyRecognizer();
        handler.removeCallbacksAndMessages(null);
        conversationOpen = false;
        broadcastState(false);
        stopForeground(true);
        super.onDestroy();
    }

    @Override public IBinder onBind(Intent intent) { return null; }
}
