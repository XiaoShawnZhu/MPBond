package edu.robustnet.xiao.mpbondhel;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.os.AsyncTask;
import android.os.Bundle;
import android.os.Handler;
import android.os.Message;
import android.os.PowerManager;
import android.support.wearable.activity.WearableActivity;
import android.text.method.ScrollingMovementMethod;
import android.util.Log;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.RadioGroup;
import android.widget.TextView;

import java.io.File;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.Inet4Address;
import java.net.Inet6Address;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.net.ServerSocket;
import java.net.Socket;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Scanner;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

import edu.robustnet.xiao.mpbondhel.http.*;
import myokhttp.OkHttpClient;
import myokhttp.Protocol;
import myokhttp.Request;
import myokhttp.Response;

public class MainActivity extends Activity {
//public class MainActivity extends WearableActivity {

    private static final String TAG = "MainActivity";
    private Button helperBtn;
    private Button helperHotBtn;
    private TextView textView;
    private EditText ripText;
    private EditText idText;
    private RadioGroup pipeGroup;
    private RadioGroup subflowGroup;
    Intent intent;
    private Button httpBtn;
    private static ButtonClickListener httpListener, helperListener, helperHotListener;

    private int pipeListenPort = 5000;
    private String subflow;
    private String pipe;
//    private String rpIP;
    private String pipeListenIP;
    private static Handler handler;
    private int startB;
    private int endB;
    private String url;

    // config parameters
    private int numSec;
    private int pipeType; // BT0 BLE1 WIFI2 LTE3
    private int subflowType; // BT0 BLE1 WIFI2 LTE3
    private int scheme; // TETHER0 MPBOND1 HTTP2
    private String rpIP;
    private String secondIP = "";
    private int numRun;
    private int feedbackType;
    private int subflowId = 2;

    public static Handler getHandler() {
        return handler;
    }

    private void processConfig(String pathName) {

        File f = new File(pathName);
        if (f.exists() == false) {
            Log.d(TAG, "No config file, follow UI and default config");
            scheme = 1;
            int pipe = pipeGroup.getCheckedRadioButtonId();
            if (pipe == R.id.wifi_pipe) {
                pipeType = 2;
            }
            else if (pipe == R.id.bt_pipe) {
                pipeType = 0;
            }
            int subflow = subflowGroup.getCheckedRadioButtonId();
            if (subflow == R.id.wifi_subflow) {
                subflowType = 2;
            }
            else if (subflow == R.id.lte_subflow) {
                subflowType = 3;
            }
            rpIP = ripText.getText().toString();
            numRun = 1;
//            secondIP += hipText.getText().toString();
            feedbackType = 2;
            subflowId = Integer.parseInt(idText.getText().toString());
            return;
        }
        try {
            /* FILE FORMAT
             * scheme
             * pipe type, subflowId
             * wwan type
             * proxy ip / server url
             * numRun, feedbackType
             * */
            Scanner sc = new Scanner(f);
            String schemeStr = sc.nextLine();
            if (schemeStr.equals("tether")) {
                scheme = 0;
            }
            else if (schemeStr.equals("mpbond")) {
                scheme = 1;
            }
            else if (schemeStr.equals("http")) {
                scheme = 2;
            }
            String pipeAndId = sc.nextLine();
            String[] arrPipeAndId = pipeAndId.split(" ", 2);
            String pipeStr = arrPipeAndId[0];
            subflowId = Integer.parseInt(arrPipeAndId[1]);
            if (pipeStr.equals("wifi")) {
                pipeType = 2;
            }
            else if (pipeStr.equals("bt")) {
                pipeType = 0;
            }
            String subflowStr = sc.nextLine();
            if (subflowStr.equals("wifi")) {
                subflowType = 2;
            }
            else if (subflowStr.equals("cellular")) {
                subflowType = 3;
            }
            rpIP = sc.nextLine();
            Log.d(TAG, rpIP);
            String runAndFeedback = sc.nextLine();
            String[] arrRunAndFeedback = runAndFeedback.split(" ", 2);
            numRun = Integer.parseInt(arrRunAndFeedback[0]);
            feedbackType = Integer.parseInt(arrRunAndFeedback[1]);
            Log.d(TAG, numRun + " " + feedbackType);

            sc.close();
        }
        catch (Exception e) {
            System.out.println(e);
        }
        Log.d(TAG, "scheme: " + scheme + " numSec: " + numSec + " subflow: " + subflowType + " pipe: " + pipeType);

    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_HIDDEN);

        intent = new Intent(this, Helper.class);

        helperBtn = findViewById(R.id.helper_button);
//        helperHotBtn = findViewById(R.id.helper_hotspot_button);
        textView = findViewById(R.id.text);
        ripText = findViewById(R.id.rip_text);
        ripText.setText("67.194.227.226");
        idText = findViewById(R.id.id_text);
        idText.setText("2");
        pipeGroup = findViewById(R.id.pipe_group);
        subflowGroup = findViewById(R.id.subflow_group);
//        httpBtn = findViewById(R.id.http_button);

//        Bundle extras = getIntent().getExtras();
//        if (extras != null) {
//            subflowId = Integer.parseInt(extras.getString("subflowId"));
//        }

        textView.setMovementMethod(new ScrollingMovementMethod());

//        httpListener = new ButtonClickListener("HTTP");
//        httpBtn.setOnClickListener(httpListener);

        helperListener = new ButtonClickListener("HELPER");
        helperBtn.setOnClickListener(helperListener);

//        helperHotListener = new ButtonClickListener("HELPER_HOT");
//        helperHotBtn.setOnClickListener(helperHotListener);

        handler = new Handler() {
            public void handleMessage(Message msg) {
                switch (msg.what) {
                    case 1:
                        textView.append("\nHelper listening at " + pipeListenIP + ":" + Integer.toString(pipeListenPort) + " over pipe.");
                        break;
                    case 2:
                        textView.append("\nPipe connected.");
                        break;
                    case 3:
                        textView.append("\nRequest from primary: " + startB + "-" + endB);
                        break;
                    default:
                        break;
                }
            }
        };

        Handler myHandler = new Handler();
//        myHandler.postDelayed(mMyRunnable, 200);
    }

    private Runnable mMyRunnable = new Runnable()
    {
        @Override
        public void run()
        {
            try {
                if (scheme == 2) {
                    httpBtn.performClick();
                }
                else {
                    helperBtn.performClick();
                }
            }
            catch(Exception e) {

            }

        }
    };

    private class ButtonClickListener implements View.OnClickListener {

        private String buttonName;
        ButtonClickListener(String name) {
            buttonName = name;
        }

        @Override
        public void onClick(View view) {
            switch (buttonName) {
                case "HTTP":
                    processConfig("/sdcard/mpbond/helper.cfg");
                    textView.append("\nRunning HTTP on Helper!");
                    PipeConnTask connTask = new PipeConnTask(MainActivity.this);
                    connTask.execute("");
                    break;
                case "HELPER":
                    processConfig("/sdcard/mpbond/helper.cfg");

                    // BT0 BLE1 WIFI2 LTE3
                    intent.putExtra("rpIP", rpIP);
                    intent.putExtra("pipeType", pipeType);
                    intent.putExtra("subflowType", subflowType);
                    intent.putExtra("scheme", scheme);
                    intent.putExtra("feedback", feedbackType);
                    intent.putExtra("subflowId", subflowId);
                    textView.append("\nRunning MPBond Helper!"
                            + "\nRemote proxy: " + rpIP
                            + "\nSubflowId: " + subflowId);
                    startService(intent);
                    break;
                case "HELPER_HOT":
                    textView.append("\\nConnect Hotspot on primary not implemented yet :(");
                    break;
                default:
                    break;
            }
        }
    }

    private class PipeConnTask extends AsyncTask<String, Void, String> {

        Context context;
        private PowerManager.WakeLock wakeLock;
        private OkHttpClient cellClient;

        public PipeConnTask(Context context) {
            this.context=context;
            PowerManager powerManager = (PowerManager) (this.context.getSystemService(Context.POWER_SERVICE));
            wakeLock = powerManager.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK,"MPBond:MyWakelockTag");
        }

        protected String doInBackground(String... x) {

            wakeLock.acquire();
            List<Protocol> protocolList = new ArrayList<>();
            protocolList.add(Protocol.HTTP_1_1);

            Network netCell = getNetworkFor(HttpHelper.NetType.CELL);

            cellClient = new OkHttpClient.Builder()
                    .socketFactory(netCell.getSocketFactory())
                    .protocols(protocolList)
                    .build();

            try{
                Thread.sleep(1000);

                // tcp socket over pipe
                ServerSocket welcomeSocket = new ServerSocket(pipeListenPort);
                pipeListenIP = getPipeListenIP();
                Message message1 = new Message();
                message1.what = 1;
                handler.sendMessage(message1);
                Socket connectionSocket = welcomeSocket.accept();
                connectionSocket.setSendBufferSize(8*1024*1024);
                Message message2 = new Message();
                message2.what = 2;
                handler.sendMessage(message2);
                Log.d(TAG, "pipe connected");
                Request request = new Request.Builder().url("http://beirut.eecs.umich.edu/index.html").head().build();
                Response response = cellClient.newCall(request).execute();

                InputStream inputStream = connectionSocket.getInputStream();
                OutputStream outputStream = connectionSocket.getOutputStream();
                byte[] readBuffer = new byte[1024];
                int readLen;
                HttpHelper httpClient = new HttpHelper(url, cellClient, startB, endB, outputStream);
                while((readLen = inputStream.read(readBuffer)) > 0) {
                    if (readLen > 8) {
                        url = new String (readBuffer).substring(0, readLen);
                        Log.d(TAG, readLen + " " + url);
                        continue;
                    }
                    byte[] first = new byte[4];
                    byte[] second = new byte[4];
                    System.arraycopy(readBuffer, 0, first, 0, 4);
                    System.arraycopy(readBuffer, 4, second, 0, 4);
                    startB = fromByteArray(first);
                    endB = fromByteArray(second);
//                    Log.d(TAG, "Request from primary: " + chunkId);
                    Message message3 = new Message();
                    message3.what = 3;
                    handler.sendMessage(message3);
//                    int chunkSize = 256 * 1024;
                    httpClient.updateConfig(url, startB, endB);
//                    HttpHelper httpClient = new HttpHelper(url, cellClient, startB, endB, outputStream);
                }
            }
            catch (Exception e) {
                System.out.println(e);
            }

            wakeLock.release();
            return "Done";
        }

        private String getPipeListenIP() {
            String ip = null;
            try {
                List<NetworkInterface> interfaces = Collections.list(NetworkInterface.getNetworkInterfaces());
                for (NetworkInterface intf : interfaces) {
                    if (!intf.getName().equalsIgnoreCase("wlan0")) continue;
                    List<InetAddress> addrs = Collections.list(intf.getInetAddresses());
                    for (InetAddress addr : addrs) {
                        if (addr instanceof Inet4Address) {
                            ip = addr.getHostAddress();
                        }
                    }
                    if (ip == null) {
                        for (InetAddress addr : addrs) {
                            if (addr instanceof Inet6Address) {
                                ip = addr.getHostAddress();
                            }
                        }
                    }
                }

            }
            catch (Exception e) {
                System.out.println(e);
            }

            return ip;
        }

        private Network getNetworkFor(HttpHelper.NetType netType){

            CountDownLatch latch = new CountDownLatch(1);
            ConnectivityNetworkCallback callback = switchNetwork(netType, latch);
            try {
                boolean switchSucceed=latch.await(30, TimeUnit.SECONDS);
                if(!switchSucceed){
                    Log.d("Switch", "Switch to "+(netType== HttpHelper.NetType.CELL?"CELL":"WIFI")+" Failed");
                    return null;
                }
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
            Log.d("Switch",  (netType== HttpHelper.NetType.CELL?"CELL":"WIFI")+" Connected!");
            Network net = callback.getNetwork();
            return net;
        }

        public ConnectivityNetworkCallback switchNetwork(HttpHelper.NetType nettype, CountDownLatch latch){
            ConnectivityManager cm = (ConnectivityManager) context.getSystemService(Context.CONNECTIVITY_SERVICE);
            NetworkRequest.Builder request = new NetworkRequest.Builder();
            if(nettype== HttpHelper.NetType.WIFI){
                request.addTransportType(NetworkCapabilities.TRANSPORT_WIFI);
            }else if(nettype== HttpHelper.NetType.CELL){
                request.addTransportType(NetworkCapabilities.TRANSPORT_CELLULAR);
//            request.addTransportType(ConnectivityManager.TYPE_MOBILE);
            }
            request.addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET);

            ConnectivityNetworkCallback callback = new ConnectivityNetworkCallback(latch, cm);
            cm.requestNetwork(request.build(), callback);
            return callback;

        }

        // packing an array of 4 bytes to an int, small endian, clean code
        int fromByteArray(byte[] bytes) {
            return ((bytes[3] & 0xFF) << 24) |
                    ((bytes[2] & 0xFF) << 16) |
                    ((bytes[1] & 0xFF) << 8 ) |
                    ((bytes[0] & 0xFF) << 0 );
        }

    }

}
