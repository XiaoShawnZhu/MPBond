package edu.robustnet.xiao.mpbondpri;

import android.app.*;
import android.content.*;
import android.net.*;
import android.os.Bundle;
import android.util.*;
import java.net.*;
import java.util.*;
import edu.robustnet.xiao.mpbondpri.util.BluetoothConnector;
import edu.robustnet.xiao.mpbondpri.util.LocalConn;
import edu.robustnet.xiao.mpbondpri.util.NetEnabler;
import edu.robustnet.xiao.mpbondpri.util.Pipe;
import edu.robustnet.xiao.mpbondpri.util.Subflow;
import edu.robustnet.xiao.mpbondpri.util.WiFiConnector;
import edu.robustnet.xiao.mpbondpri.util.Constants;

public class Primary extends IntentService {
    // MPBond Primary
    private static ConnectivityManager mConnectivityManager = null;
    private int pipeType = 0;
    private int subflowType = 0;
    private String subflowIP = null;
    private String rpIP = null;
    private String[] secondIP = new String[10]; // allow max 10 nSec
    private int numSec = 0;
    private int scheme = 0;
    private int feedback = 0;
    private BluetoothConnector bluetoothConnector = new BluetoothConnector();
    private WiFiConnector[] wifiConnector = new WiFiConnector[10];
    private Subflow subflow = new Subflow();
    private Pipe pipe = new Pipe();
    private LocalConn localConn = new LocalConn();
    private boolean isTether = false;

    @Override
    protected void onHandleIntent(Intent arg0) {
        // parse parameters passed from the main activity
        Bundle extras = arg0.getExtras();
        if(extras == null)
            Log.d("Service","null");
        else
        {
            Log.d("Service","not null");
            pipeType = (int) extras.get("pipeType");
            subflowType = (int) extras.get("subflowType");
            rpIP = (String) extras.get("rpIP");
            secondIP = ((String) extras.get("secondIP")).split(" ");
            numSec = (int) extras.get("numSec");
            scheme = (int) extras.get("scheme");
            feedback = (int) extras.get("feedback");
        }
        mpbond();
    }

    public Primary() {
        super("MyIntentService");
    }

    public void mpbond() {

        mConnectivityManager = (ConnectivityManager) getSystemService(Context.CONNECTIVITY_SERVICE);
        netConfig();
        Log.d(Constants.TAG, ": subflowType = " + subflowType + ", pipeType = " + pipeType + ", numSec = " + numSec);
        for (int i = 0; i < numSec; i++) {
            Log.d(Constants.TAG, secondIP[i]);
        }

        if (scheme == 0) {
            // tether
            isTether = true;
            if (subflowType == Constants.LTE_CHANNEL)
                useNetwork(ConnectivityManager.TYPE_MOBILE);
            String r = subflow.subflowFromJNI(rpIP, feedback);
            if (r.equals("SubflowSetupSucc")) {
                Log.d(Constants.TAG, "Subflow setup success.");
            }
            else {
                Log.e(Constants.TAG, r);
                System.exit(0);
            }
            if (pipeType == Constants.WIFI_CHANNEL)
                useNetwork(ConnectivityManager.TYPE_WIFI);
            Log.d(Constants.TAG, pipe.pipeSetupFromJNI(true, numSec, Constants.isPipeInJava, secondIP[0], rpIP));
        }
        else {
            /*
            primary <-> l pipe l <--l pipe1--> l pipe c1 <-> r pipe c1 <--MPBond pipe1--> h1 r pipe l <-> h1 l pipe c <--h1 l pipe--> h1 l pipe l <-> helper1
                                 <--l pipe2--> l pipe c2 <-> r pipe c2 <--MPBond pipe2--> h2 r pipe l <-> h2 l pipe c <--h2 l pipe--> h2 l pipe l <-> helper2
                                 ......
                                 <--l pipe numSec--> l pipe c numSec <-> r pipe c numSec<---MPBond pipe numSec---> r pipe listener numSec in Java/C++ <-> helper numSec C++ programs
            *
            *
            A special case: one primary + one helper with n subflows
            primary <-> l pipe l <--l pipe1--> l pipe c1 <-> r pipe c1 <--MPBond pipe1--> h r pipe l <-> h l pipe c1 <--h l pipe1--> h l pipe l <-> helper
                                 <--l pipe2--> l pipe c2 <-> r pipe c2 <--MPBond pipe2-->            <-> h l pipe c2 <--h l pipe2-->
                                 ......
            *
            *
            One local pipe listener (a.k.a. pipe proxy), numSec local pipe connectors.
            "<->" connectes two components written in the same language & process
            *
            primary: C/C++ programs for the proxy main logic on primary (e.g., merge data from helper with server, redirect application traffic)
            l pipe l: local pipe listener with C++ socket
            l pipe: local on-device pipe (local socket)
            l pipe c: local pipe connector in Java (e.g., WiFiConnector.java)
            r pipe c: remote pipe connector in Java
            MPBond pipe
            h r pipe l: helper remote ripe listener in Java (e.g., WiFiListener.java)
            h l pipe c: helper local pipe connector with Java socket, connectProxy()
            h l pipe: helper local on-device pipe
            h l pipe l: helper local pipe listener with C++ socket
            helper: C/C++ programs for the proxy main logic on helper (e.g., forward data from helper to server)
            *
            local pipe connector:
            0. BluetoothConnector (need testing)
            1. BLE (not implemented yet)
            2. WiFiConnctor (ready to deploy)
            3. LTE (not implemented yet)
             */
            if (pipeType == Constants.WIFI_CHANNEL)
                useNetwork(-1);

            for (int i = 0; i < numSec; i++) {
                // for each pipe/helper, there is a wifiConnector on the primary
                wifiConnector[i] = new WiFiConnector(i);
            }

            // local pipe listener (param secondIP[0] is only used for C++ pipe)
            Log.d(Constants.TAG, pipe.pipeSetupFromJNI(isTether, numSec, Constants.isPipeInJava, secondIP[0], rpIP));

            // local pipe connectors connect to the local pipe listener (e.g,, via the WiFiConnector class)
            localPipeSetup(pipeType);
            // subflow connector
            if (subflowType == Constants.LTE_CHANNEL)
                useNetwork(ConnectivityManager.TYPE_MOBILE);
            String r = subflow.subflowFromJNI(rpIP, feedback);
            if (r.equals("SubflowSetupSucc")) {
                Log.d(Constants.TAG, "Subflow setup success.");
            }
            else {
                Log.d(Constants.TAG, r);
                System.exit(0);
            }
            if (pipeType == Constants.WIFI_CHANNEL)
                useNetwork(-1);
            // remote pipe connector
            remotePipeSetup(pipeType);
            try {
                Thread.sleep(2000);
            }
            catch (Exception e) {
                System.out.println(e);
            }
        }
        // local app connection setup
        Log.d(Constants.TAG, localConn.connSetupFromJNI());
        if (subflowType == Constants.LTE_CHANNEL)
            useNetwork(ConnectivityManager.TYPE_MOBILE);
        // start proxy main
        proxyFromJNI();
    }

    private void remotePipeSetup(int pipeType) {
        // set up a pipe with peer according to the type specified
        switch (pipeType) {
            case Constants.BT_CHANNEL:
                bluetoothConnector.connectPeer(secondIP[0]); // start BT pipe connect
                Log.d(Constants.TAG,"connection peer complete");
                if (Constants.hasPipeMeasurement) {
                    bluetoothConnector.connectPeerSide(secondIP[0]);
                    Log.d(Constants.TAG,"connection peerSide complete");
                }
                break;
            case Constants.BLE_CHANNEL:
                break;
            case Constants.WIFI_CHANNEL:
                for (int i = 0; i < numSec; i++) {
                    try {
                        Thread.sleep(200);
                    }
                    catch (Exception e) {
                        System.out.println(e);
                    }
                    Log.d(Constants.TAG,"connecting to peer " + i + " that listens at " + secondIP[i]);
                    wifiConnector[i].connectDataPeer(secondIP[i]); // start WiFi pipe connect
                    Log.d(Constants.TAG,"connection peer " + i + " complete");
                    if (Constants.hasPipeMeasurement) {
                        try {
                            Thread.sleep(200);
                        }
                        catch (Exception e) {
                            System.out.println(e);
                        }
                        Log.d(Constants.TAG,"connecting to peerSide " + i + " that listens at " + secondIP[i]);
                        wifiConnector[i].connectCtrlPeer(secondIP[i]);
                        Log.d(Constants.TAG,"connection peerSide " + i + " complete");
                    }
                }
                break;
        }
    }

    private void localPipeSetup(int pipeType) {
        // set up a pipe to bridge Java/C++ (pipe connector) with C++ (pipe listener) according to the pipe type specified
        switch (pipeType) {
            case Constants.BT_CHANNEL:
                bluetoothConnector.connectProxy(); // start C++/Java connect
                Log.d(Constants.TAG,"connection peer complete");
                break;
            case Constants.BLE_CHANNEL:
                break;
            case Constants.WIFI_CHANNEL:
                for (int i = 0; i < numSec; i++) {
                    wifiConnector[i].connectProxy();
//                    wifiConnector[i].startCtrlLocal();
                    try {
                        Thread.sleep(200); // wait for listener re-ready
                    }
                    catch (Exception e) {
                        System.out.println(e);
                    }
                }
                break;
        }
    }

    private void netConfig() {

        switch (subflowType) {
            case 2:
                Intent wifiIntent = new Intent(this, NetEnabler.class);
                wifiIntent.putExtra("type", "WiFi");
                startService(wifiIntent);
                break;
            // assume LTE intf named as rmnet0 or rmnet_dataX where X = 0 | 1
            case 3:
                Log.d(Constants.TAG, "subflow is LTE");
                Intent cellIntent = new Intent(this, NetEnabler.class);
                cellIntent.putExtra("type", "Cell");
                Log.d(Constants.TAG, "About to start service");
                startService(cellIntent);
                Log.d(Constants.TAG, "Service started");
//                System.exit(0);
                break;
            default:
                Log.d(Constants.TAG, "Subflow name doesn't match to any.");
                System.exit(0);
        }

        switch (pipeType) {
            case 2:
                Intent wifiIntent = new Intent(this, NetEnabler.class);
                wifiIntent.putExtra("type", "WiFi");
                startService(wifiIntent);
                break;
            case 0:
                pipeType = Constants.BT_CHANNEL;
                break;
            default:
                Log.d(Constants.TAG, "Pipe name doesn't match to any.");
                System.exit(0);
        }

        try {
            Thread.sleep(3000); // wait for intent started
            List<NetworkInterface> interfaces = Collections.list(NetworkInterface.getNetworkInterfaces());
            for (NetworkInterface intf : interfaces) {
                if (!intf.getName().equalsIgnoreCase("rmnet_data0")) continue;
                List<InetAddress> addrs = Collections.list(intf.getInetAddresses());
                for (InetAddress addr : addrs) {
                    if (addr instanceof Inet4Address) {
                        subflowIP = addr.getHostAddress();
                        Log.d(Constants.TAG, subflowIP);
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
            Thread.sleep(1000); // wait for intent started
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
                if (networkInfo.getType()==type) {
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
