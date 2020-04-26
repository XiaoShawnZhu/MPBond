package edu.robustnet.xiao.mpbondhel;

import android.app.*;
import android.content.*;
import android.net.*;
import android.os.Bundle;
import android.util.*;

import java.net.*;
import java.util.*;
import edu.robustnet.xiao.mpbondhel.util.*;

import static edu.robustnet.xiao.mpbondhel.util.Constants.*;

public class Helper extends IntentService {

    private static ConnectivityManager mConnectivityManager = null;
    private int pipeType = 0; // BT, WiFi, or LTE
    private int subflowType = 0;
    private String subflowIP = null; // (local) IP address of subflow
    private String rpIP = null; // ip of remote proxy
    private BluetoothListener bluetoothListener = new BluetoothListener();
    private WiFiListener wifiListener;
    private int feedback = 0;
    private int subflowId = 0;
    private Subflow subflow = new Subflow();
    private Pipe pipe = new Pipe();
    private LocalConn localConn = new LocalConn(); // here local conns means conns in general (inc. pipes, subflows)

    @Override
    protected void onHandleIntent(Intent arg0) {
        Bundle extras = arg0.getExtras();
        if (extras == null)
            Log.d("Service","null");
        else
        {
            Log.d("Service","not null");
            pipeType = (int) extras.get("pipeType");
            subflowType = (int) extras.get("subflowType");
            rpIP = (String) extras.get("rpIP");
            feedback = (int) extras.get("feedback");
            subflowId = (int) extras.get("subflowId");
        }
        mpbond();
    }

    public Helper() {
        super("MPBond Helper service");
    }

    public void mpbond() {

        mConnectivityManager = (ConnectivityManager) getSystemService(Context.CONNECTIVITY_SERVICE);
        // retrieve configuration from sdcard
        netConfig();
        Log.i(TAG, "pipeType is " + pipeType + ", Subflow type/IP is "
                + subflowType + "/" + subflowIP + ", rpIP is " + rpIP + ", subflowId is " + subflowId);

        if (pipeType == Constants.WIFI_CHANNEL)
            useNetwork(ConnectivityManager.TYPE_WIFI);

        wifiListener = new WiFiListener();

        /* listener
        helper <-> l pipe l <--l pipe--> l pipe c <-> r pipe l

        helper: C/C++ programs for the proxy main logic on primary (e.g., buffer and forward data from server to primary)
        l pipe l: local pipe listener with c++ socket
        l pipe: local pipe between C++ proxy and Java program
        l pipe c: local pipe connector in Java

        * */
        // Java: local pipe listener; C++: pipe setup
        String r = pipe.pipeSetupFromJNI(subflowIP, rpIP, secNo, isPipeInJava, 1);
        if (r.equals("Pipe listener setup successfully.")) {
            Log.d(TAG, "Pipe listener setup successfully.");
        }
        else {
            Log.d(TAG, r);
            System.exit(0);
        }

        // local pipe connector
        localPipeSetup(pipeType);
        // remote pipe listener
        remotePipeSetup(pipeType);

        // subflow connector
        if (subflowType == LTE_CHANNEL)
            useNetwork(ConnectivityManager.TYPE_MOBILE);
        if (subflowType == WIFI_CHANNEL)
            useNetwork(ConnectivityManager.TYPE_WIFI);
        subflowSetup();
    }

    private void remotePipeSetup(int pipeType) {
        // set up a network pipe with remote Java peer on primary device according to the type specified
        switch (pipeType) {
            case BT_CHANNEL:
                bluetoothListener.connectPeer(); // start BT pipe listen
                Log.i(TAG,"BT connection peer complete");
                if (hasPipeMeasurement) {
                    bluetoothListener.connectPeerSide();
                    Log.i(TAG, "BT connection peer side complete");
                }
                break;
            case BLE_CHANNEL:
                break;
            case WIFI_CHANNEL:
                wifiListener.listenPeer(); // start WiFi pipe listen
                Log.i(TAG,"WiFi connection peer complete");
                if (hasPipeMeasurement) {
                    wifiListener.listenCtrlPeer(); // start WiFi pipe listen
                    Log.i(TAG, "WiFi connection peer side complete");
                }
                break;
        }
    }

    private void localPipeSetup(int pipeType) {
        // set up a local pipe with C++ proxy on the same device according to the type specified
        switch (pipeType) {
            case BT_CHANNEL:
                bluetoothListener.connectProxy(); // start C++/Java connect
                Log.i(TAG,"BT connection proxy complete");
                if (hasPipeMeasurement) {
                    bluetoothListener.connectProxySide();
                    Log.i(TAG, "BT connection proxy side complete");
                }
                break;
            case BLE_CHANNEL:
                break;
            case WIFI_CHANNEL:
                    wifiListener.connectProxy(); // start Java-C++ connect
                    Log.i(TAG, "WiFi connection proxy complete");
                    if (hasPipeMeasurement) {
                        wifiListener.connectCtrlProxy(); // start Java-C++ connect
                        Log.i(TAG, "WiFi connection proxy side complete");
                    }
                break;
        }
    }

    private void subflowSetup() {
        // set up helper subflow
        while (true) {
            if (pipeOk(pipeType) || !isPipeInJava) {
                Log.d(TAG, "pipe established, about to set up subflow");
                String r = subflow.subflowFromJNI(subflowIP, rpIP, feedback, subflowId, 1);
                if (r.equals("SubflowSetupSucc")) {
                    Log.d(TAG, "Subflow setup success.");
                }
                else {
                    Log.d(TAG, r);
                    System.exit(0);
                }
                break;
            }
            else {
//                Log.d(TAG, ""+pipeOk(pipeType));
            }
        }
        Log.d(TAG, localConn.connSetupFromJNI());
        // start proxy main
        proxyFromJNI();
    }

    private boolean pipeOk(int pipeType) {
        switch (pipeType) {
            case BT_CHANNEL:
                return bluetoothListener.getPipeStatus();
            case BLE_CHANNEL:
//                TODO
            case WIFI_CHANNEL:
                return wifiListener.getPipeStatus();
        }
        return false;
    }

    private void netConfig() {

        switch (subflowType) {
            case 2:
                Intent wifiIntent = new Intent(this, NetEnabler.class);
                wifiIntent.putExtra("type", "WiFi");
                startService(wifiIntent);
                break;
            case 3:
                Intent cellIntent = new Intent(this, NetEnabler.class);
                cellIntent.putExtra("type", "Cell");
                startService(cellIntent);
                break;
            default:
                Log.d(TAG, "Subflow name doesn't match to any.");
                System.exit(0);
        }

        switch (pipeType) {
            case 2:
                Intent wifiIntent = new Intent(this, NetEnabler.class);
                wifiIntent.putExtra("type", "WiFi");
                startService(wifiIntent);
                break;
            case 0:
                break;
            default:
                Log.d(TAG, "Pipe name doesn't match to any.");
                System.exit(0);
        }

        try {
            // wait for intent to be fully started
            Thread.sleep(2000);
            List<NetworkInterface> interfaces = Collections.list(NetworkInterface.getNetworkInterfaces());
            for (NetworkInterface intf : interfaces) {
                // 5G
//                if (!intf.getName().equalsIgnoreCase("rmnet_data1") && !intf.getName().equalsIgnoreCase("rmnet_data2") && !intf.getName().equalsIgnoreCase("rmnet0")) continue;
                // 4G
                if (!intf.getName().equalsIgnoreCase("rmnet_data0") && !intf.getName().equalsIgnoreCase("rmnet0")) continue;
                List<InetAddress> addrs = Collections.list(intf.getInetAddresses());
                for (InetAddress addr : addrs) {
                    if (addr instanceof Inet4Address) {
                        subflowIP = addr.getHostAddress();
                    }
                }
                if (subflowIP == null) {
                    for (InetAddress addr : addrs) {
                        if (addr instanceof Inet6Address) {
                            subflowIP = addr.getHostAddress();
                        }
                    }
                }
            }
        }
        catch (Exception e) {
            System.out.println(e);
        }
    }

    private void useNetwork(int type) {
        if (type == -1) {
            mConnectivityManager.bindProcessToNetwork(null);
        }
        else {
            for (Network network:mConnectivityManager.getAllNetworks()) {
                NetworkInfo networkInfo = mConnectivityManager.getNetworkInfo(network);
                if (networkInfo.getType() == type) {
                    mConnectivityManager.bindProcessToNetwork(network);
                }
            }
        }
    }

    public native String proxyFromJNI();
    static {
        System.loadLibrary("proxy");
    }
}