package edu.robustnet.xiao.mpbondhel.util;

import android.app.IntentService;
import android.content.*;
import android.net.*;
import android.util.Log;

public class NetEnabler extends IntentService {

    private static final String TAG = "OpenNet";
    private ConnectivityManager mConnectivityManager;
    private ConnectivityManager.NetworkCallback networkCallback = null;
    private int networkCapabilities = NetworkCapabilities.TRANSPORT_CELLULAR;

    @Override
    protected void onHandleIntent(Intent intent) {

        String network = intent.getStringExtra("type");
        mConnectivityManager = (ConnectivityManager) getSystemService(Context.CONNECTIVITY_SERVICE);
        enableNet(network);
    }

    private void enableNet(String value) {
        if (networkCallback != null) {
            return;
        }
        if (value == "WiFi") {
            networkCapabilities = NetworkCapabilities.TRANSPORT_WIFI;
        }
        else if (value == "Cell") {
            networkCapabilities = NetworkCapabilities.TRANSPORT_CELLULAR;
        }

        networkCallback =
                new ConnectivityManager.NetworkCallback() {
                    @Override
                    public void onAvailable(Network network) {
                        Log.d(TAG, "LTE Network available.");
                    }
                };
        NetworkRequest request = new NetworkRequest.Builder()
                .addTransportType(networkCapabilities)
                .addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
                .build();
        mConnectivityManager.requestNetwork(request, networkCallback);
    }

    public NetEnabler() {
        super("MyIntentService");
    }
}
