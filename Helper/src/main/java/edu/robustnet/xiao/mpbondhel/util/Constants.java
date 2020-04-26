package edu.robustnet.xiao.mpbondhel.util;

public final class Constants {
    // Secondary
    public static final int BT_CHANNEL = 0;
    public static final int BLE_CHANNEL = 1;
    public static final int WIFI_CHANNEL = 2;
    public static final int LTE_CHANNEL = 3;
    public static final String TAG = "Shawn-Helper";
    public static final boolean isPipeInJava = true;
    public static final int rateLimit = 0; // in kbps, limit pipe bandwidth
    public static final int secNo = 2; // id of subflow, e.g., the first secondary device maps to subflow 2
    public static final boolean hasPipeMeasurement = true;
    public static final int feedbackType = 1; // 0: no feedback; 1: always-on; 2: on-demand

    public static final int proxyPort = 1303;
    public static final int proxyPortSide = 1304;
    public static final String proxyIP = "127.0.0.1";
    public static final int serverPort = 5000;
    public static final int serverPortSide = 5501;

    // WiFiListener
    public static final String WiFiListenerTAG = "Shawn-WiFiListener";
}
