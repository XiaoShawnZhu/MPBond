package edu.robustnet.xiao.mpbondpri;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.net.wifi.WifiManager;
import android.os.AsyncTask;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Message;
import android.os.PowerManager;
import android.text.method.ScrollingMovementMethod;
import android.util.Log;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.RadioGroup;
import android.widget.TextView;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.lang.reflect.Method;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.util.ArrayList;
import java.util.List;
import java.util.Scanner;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

import androidx.annotation.RequiresApi;

import edu.robustnet.xiao.mpbondpri.R;
import edu.robustnet.xiao.mpbondpri.http.ConnectivityNetworkCallback;
import edu.robustnet.xiao.mpbondpri.http.HttpPrimary;
import myokhttp.OkHttpClient;
import myokhttp.Protocol;
import myokhttp.Request;
import myokhttp.Response;

public class MainActivity extends Activity {

    private static final String TAG = "Shawn-MainActivity";
    // config parameters
    private int numSec;
    private int pipeType; // BT0 BLE1 WIFI2 LTE3
    private int subflowType; // BT0 BLE1 WIFI2 LTE3
    private int scheme; // TETHER0 MPBOND1 HTTP2
    private String rpIP;
    private String secondIP = "";
    private int numRun;
    private int feedbackType;
    // UI elements
    private Button primaryBtn;
    private Button primaryHotBtn;
    private Button httpBtn;
    private Button dlBtn;
    private TextView textView;
    private RadioGroup pipeGroup;
    private RadioGroup subflowGroup;
    private EditText ripText;
    private EditText hipText;
    private EditText urlText;
    private EditText numText;
    private Intent intent;

    private Context context = this;
//    private WifiManager.LocalOnlyHotspotReservation mReservation;

    private InputStream [] pipeInputStream = new InputStream[3]; // max 3 helpers
    private OutputStream [] pipeOutputStream = new OutputStream[3];
    private int chunkSizeByte = 256 * 1024;
    private String url = null;
    private static Handler handler;
    private int dlCnt = 0;
    private long dlTime = -1;
    private OkHttpClient cellClient;

    HttpPrimary client2;

    private static class MyTaskParams {
        int port;
        String ip;
        String url;
        InputStream [] pipeInputStream;
        OutputStream [] pipeOutputStream;

        MyTaskParams(int port, String ip) {
            this.port = port;
            this.ip = ip;
        }

        MyTaskParams(String url, String ip, InputStream [] pipeInputStream, OutputStream [] pipeOutputStream) {
            this.url = url;
            this.ip = ip;
            this.pipeInputStream = pipeInputStream;
            this.pipeOutputStream = pipeOutputStream;
        }
    }

    private void processConfig(String pathName) {

        File f = new File(pathName);
        if (f.exists() == false) {
            Log.d(TAG, "No config file, follow UI and default config");
            scheme = 1;
            numSec = Integer.parseInt(numText.getText().toString()) - 1;
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
            secondIP += hipText.getText().toString();
            feedbackType = 2;
            return;
        }
        try{
            /* FILE FORMAT
             * scheme
             * num of devices (n)
             * pipe type
             * wwan type
             * proxy ip / server url
             * numRun feedbackType
             * helper 1 ip
             * ...
             * helper n-1 ip
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
            numSec = Integer.parseInt(sc.nextLine()) - 1;
            String pipeStr = sc.nextLine();
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
            String numAndFeed = sc.nextLine();
            String[] arrOfStr = numAndFeed.split(" ");
            numRun = Integer.parseInt(arrOfStr[0]);
            if (arrOfStr.length > 1) {
                feedbackType = Integer.parseInt(arrOfStr[1]);
                Log.d(TAG, numRun + " " + feedbackType);
            }
            for (int i = 0; i < numSec; i++) {
                secondIP += sc.nextLine() + " ";
            }

            Log.d(TAG, secondIP);
            sc.close();
        }
        catch (Exception e) {
            System.out.println(e);
        }
        Log.d(TAG, "scheme: " + scheme + " numSec: " + numSec + " subflow: " + subflowType + " pipe: " + pipeType + " secondIP: " + secondIP);

    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_HIDDEN);

        pipeGroup = findViewById(R.id.pipe_group);
        subflowGroup = findViewById(R.id.subflow_group);
        primaryBtn = findViewById(R.id.primary_button);
//        primaryHotBtn = findViewById(R.id.primary_hotspot_button);
//        httpBtn = findViewById(R.id.http_button);
//        dlBtn = findViewById(R.id.dl_button);
        textView = findViewById(R.id.text);
        ripText = findViewById(R.id.rip_text);
        hipText = findViewById(R.id.hip_text);
//        urlText = findViewById(R.id.url_text);
        numText = findViewById(R.id.num_text);
        ripText.setText("67.194.227.226");
        hipText.setText("192.168.43.250");
//        hipText.setText("80:4E:81:5C:D0:36");
//        hipText.setText("192.168.43.175 192.168.43.36");
//        urlText.setText("http://beirut.eecs.umich.edu/random10.dat");
        numText.setText("2");

        textView.setMovementMethod(new ScrollingMovementMethod());

        intent = new Intent(this, Primary.class);

        primaryBtn.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {

                processConfig("/sdcard/mpbond/primary.cfg");

                // BT0 BLE1 WIFI2 LTE3
                intent.putExtra("rpIP", rpIP);
                intent.putExtra("pipeType", pipeType);
                intent.putExtra("secondIP", secondIP);
                intent.putExtra("subflowType", subflowType);
                intent.putExtra("numSec", numSec);
                intent.putExtra("scheme", scheme);
                intent.putExtra("feedback", feedbackType);
//                intent.putExtra("nSubflow", nSubflow);

                textView.append("\nRunning MPBond Primary!"
                        + "\nRemote proxy: " + rpIP
                        + "\n# of helpers: " + numSec);
                startService(intent);
            }
        });

//        primaryHotBtn.setOnClickListener(new View.OnClickListener() {
//            public void onClick(View v) {
//                textView.append("Opening Hotspot on Primary!");
//
//                boolean a = isApOn(context); // check Ap state :boolean
//                if (a) {
//                    Log.d(TAG, "ON");
//                    textView.append("Hotspot on Primary already opened!");
//                }
//                else {
//                    Log.d(TAG, "OFF");
//                    if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.O){
//                        // Do something for Ore and above versions
//                        turnOnHotspot();
//                    } else {
//                        // do something for phones running an SDK before Ore
//                        textView.append("Hotspot enabling not supported for Android 7 or earlier");
//                    }
//
//                }
//            }
//        });
//
//        httpBtn.setOnClickListener(new View.OnClickListener() {
//            public void onClick(View v) {
//                processConfig("/sdcard/mpbond/primary.cfg");
//                url = rpIP;
//                textView.append("Running HTTP solution on Primary!"
//                        + "\nurl: " + url
//                        + "\n# of helper device(s): " + numSec);
//                // establish pipe
//                PipeConnTask task = new PipeConnTask(MainActivity.this);
//                MyTaskParams params = new MyTaskParams(5000, secondIP);
//                task.execute(params);
//                textView.append("\nDONE");
//            }
//        });
//
//        dlBtn.setOnClickListener(new View.OnClickListener() {
//            public void onClick(View v) {
//                String secondIP = hipText.getText().toString();
//                textView.append("\nHTTP DL started!");
//                DownloadFilesTask task = new DownloadFilesTask(MainActivity.this);
//                Log.d(TAG, "" + pipeInputStream.length);
//                MyTaskParams params1 = new MyTaskParams(url, secondIP, pipeInputStream, pipeOutputStream);
//                task.execute(params1);
//            }
//        });

        handler = new Handler() {
            public void handleMessage(Message msg) {
                switch (msg.what) {
                    case 1:
                        textView.append("\nDownload " + dlCnt + " took " + dlTime + " ms");
                        break;
                    case 2:
                        textView.append("\nPipe connected.");
                        break;
                    case 3:
                        textView.append("\nRequest from primary: ");
                        break;
                    case 4:
                        httpBtn.performClick();
                        textView.append("\nHTTP button clicked");
                        break;
                    case 5:
                        dlBtn.performClick();
                        textView.append("\nDL button clicked");
                        break;
                    default:
                        break;
                }
            }
        };

        Handler myHandler = new Handler();
//        myHandler.postDelayed(mMyRunnable, 5000);
    }

    private Runnable mMyRunnable = new Runnable()
    {
        @Override
        public void run()
        {
            try {
                if (scheme == 2) {
                    httpBtn.performClick();
                    Thread.sleep(15000);
                    dlBtn.performClick();
                }
                else {
//                    Thread.sleep(3000);
                    primaryBtn.performClick();
                }
            }
            catch(Exception e) {

            }
        }
    };

    //check whether wifi hotspot on or off
    public static boolean isApOn(Context context) {
        WifiManager wifimanager = (WifiManager) context.getSystemService(context.WIFI_SERVICE);
        try {
            Method method = wifimanager.getClass().getDeclaredMethod("isWifiApEnabled");
            method.setAccessible(true);
            return (Boolean) method.invoke(wifimanager);
        }
        catch (Throwable ignored) {}
        return false;
    }

//    @RequiresApi(api = Build.VERSION_CODES.O)
//    private void turnOnHotspot() {
//        WifiManager manager = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
//
//        manager.startLocalOnlyHotspot(new WifiManager.LocalOnlyHotspotCallback() {
//
//            @Override
//            public void onStarted(WifiManager.LocalOnlyHotspotReservation reservation) {
//                super.onStarted(reservation);
//                Log.d(TAG, "Wifi Hotspot is on now");
//                textView.append("Primary hotspot is on now");
//                mReservation = reservation;
//            }
//
//            @Override
//            public void onStopped() {
//                super.onStopped();
//                Log.d(TAG, "onStopped: ");
//            }
//
//            @Override
//            public void onFailed(int reason) {
//                super.onFailed(reason);
//                Log.d(TAG, "onFailed: ");
//            }
//        }, new Handler());
//    }
//
//    private void turnOffHotspot() {
//        if (mReservation != null) {
//            mReservation.close();
//        }
//    }

    private class PipeConnTask extends AsyncTask<MyTaskParams, Void, String> {

        Context context;
        private PowerManager.WakeLock wakeLock;

        public PipeConnTask(Context context) {
            this.context=context;
            PowerManager powerManager = (PowerManager) (this.context.getSystemService(Context.POWER_SERVICE));
            wakeLock = powerManager.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK,"MPBond:MyWakelockTag");
        }

        protected String doInBackground(MyTaskParams... addrList) {

            Log.d(TAG, "pipeconntask");

            wakeLock.acquire();
            List<Protocol> protocolList = new ArrayList<>();
            protocolList.add(Protocol.HTTP_1_1);
            Network netCell = getNetworkFor(HttpPrimary.NetType.CELL);
            cellClient = new OkHttpClient.Builder()
                    .socketFactory(netCell.getSocketFactory())
                    .protocols(protocolList)
                    .build();
            String [] secIP = addrList[0].ip.split(" ");
            Log.d(TAG, ""+secIP[0]);
            int secPort = addrList[0].port;
            try {
                client2 = new HttpPrimary(url, chunkSizeByte, cellClient, secondIP.split(" "), pipeInputStream,
                        pipeOutputStream, numSec);
                Request request = new Request.Builder().url("http://beirut.eecs.umich.edu/index.html").head().build();
                Response response = cellClient.newCall(request).execute();
                for (int i = 0; i < numSec; i ++) {
                    // tcp socket over pipe
                    Socket clientSocket = new Socket();
                    Log.d(TAG, "pipe connecting");
                    clientSocket.connect(new InetSocketAddress(secIP[i], secPort));
                    Log.d(TAG, "pipe connected");
                    pipeInputStream[i] = clientSocket.getInputStream();
                    pipeOutputStream[i] = clientSocket.getOutputStream();
                    byte[] buf = url.getBytes();
                    // send the url to each helper
                    pipeOutputStream[i].write(buf,0, buf.length);
                }
            }
            catch (Exception e) {
                e.printStackTrace();
            }
            wakeLock.release();
            return "Done";
        }

        private Network getNetworkFor(HttpPrimary.NetType netType){

            CountDownLatch latch = new CountDownLatch(1);
            ConnectivityNetworkCallback callback = switchNetwork(netType, latch);
            try {
                boolean switchSucceed=latch.await(30, TimeUnit.SECONDS);
                if(!switchSucceed){
                    Log.d(TAG, "Switch to "+(netType== HttpPrimary.NetType.CELL?"CELL":"WIFI")+" Failed");
                    return null;
                }
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
            Log.d(TAG,  (netType== HttpPrimary.NetType.CELL?"CELL":"WIFI")+" Connected!");
            Network net = callback.getNetwork();
            return net;
        }

        public ConnectivityNetworkCallback switchNetwork(HttpPrimary.NetType nettype, CountDownLatch latch){
            ConnectivityManager cm = (ConnectivityManager) context.getSystemService(Context.CONNECTIVITY_SERVICE);
            NetworkRequest.Builder request = new NetworkRequest.Builder();
            if(nettype== HttpPrimary.NetType.WIFI){
                request.addTransportType(NetworkCapabilities.TRANSPORT_WIFI);
            } else if (nettype== HttpPrimary.NetType.CELL) {
                request.addTransportType(NetworkCapabilities.TRANSPORT_CELLULAR);
            }
            request.addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET);

            ConnectivityNetworkCallback callback = new ConnectivityNetworkCallback(latch, cm);
            cm.requestNetwork(request.build(), callback);
            return callback;
        }
    }

    private class DownloadFilesTask extends AsyncTask<MyTaskParams, Void, String> {
        Context context;
        private PowerManager.WakeLock wakeLock;
//        private OkHttpClient cellClient;

        public DownloadFilesTask(Context context){
            this.context=context;
            PowerManager powerManager = (PowerManager) (this.context.getSystemService(Context.POWER_SERVICE));
            wakeLock = powerManager.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK,"MPBond:MyWakelockTag");
        }

        protected String doInBackground(MyTaskParams... params) {

            wakeLock.acquire();
//            List<Protocol> protocolList = new ArrayList<>();
//            protocolList.add(Protocol.HTTP_1_1);
//            Network netCell = getNetworkFor(HttpPrimary.NetType.CELL);
//            cellClient = new OkHttpClient.Builder()
//                    .socketFactory(netCell.getSocketFactory())
//                    .protocols(protocolList)
//                    .build();
            try {
                Thread.sleep(1000);
            }
            catch (Exception e) {
                e.printStackTrace();
            }

            long start = System.currentTimeMillis();
            writeOnSD("start");
            int leng = 5;
//            Log.d(TAG, url);
            int chunkSizes[] = {64 * 1024, 128 * 1024, 256 * 1024, 512 * 1024, 1024 * 1024};
            if (url.equals("http://beirut.eecs.umich.edu/random1.dat")) {
                leng = 4;
            }
            if (url.equals("http://beirut.eecs.umich.edu/random512.dat")) {
                leng = 3;
            }
            if (numSec > 1) {
                leng -= 1;
            }
//            HttpPrimary client2 = new HttpPrimary(params[0].url, chunkSizeByte, cellClient, params[0].ip.split(" "), params[0].pipeInputStream,
//                    params[0].pipeOutputStream, numSec);

//            for (int k = 0; k < leng; k++) {
//                chunkSizeByte = chunkSizes[k];
            chunkSizeByte = chunkSizes[2];

                for (int i = 0; i < numRun; i++) {
                    Log.d(TAG, "DL-START " +System.currentTimeMillis() + " " +i);
                    client2.updateConfig(chunkSizeByte);
//                    HttpPrimary client2 = new HttpPrimary(params[0].url, chunkSizeByte, cellClient, params[0].ip.split(" "), params[0].pipeInputStream,
//                            params[0].pipeOutputStream, numSec);
//                ArrayList<String> samples= new ArrayList<>();
                    int totalLen=0;
                    int readLen;
                    byte[] readBuffer = new byte[1024];
                    try {
                        InputStream is = client2.getInputStream();

                        File dir = new File(Environment.getExternalStorageDirectory(),"mpbond");

                        if (!dir.exists()) {
                            dir.mkdirs();
                        }

                        if (is != null) {
                            readLen = is.read(readBuffer);
                            totalLen+=readLen;
                            while (client2.fileSizeBytes > totalLen) {
                                readLen = is.read(readBuffer);
                                totalLen+=readLen;
                            }

                            Log.i(TAG, "DL-END "+System.currentTimeMillis()+" " + i);
                            Log.i(TAG, "--------------------------------");

                        }
                        else{
                            Log.d(TAG, "MainActivity: is is null.");
                        }
                        long now=System.currentTimeMillis();
                        Log.d(TAG, "URL: " + url + " Chunk size: " + chunkSizeByte + " Run: " + i +" Download time (ms): " + (now -start));
                        dlCnt += 1;
                        dlTime = now - start;
                        Message message1 = new Message();
                        message1.what = 1;
                        handler.sendMessage(message1);
                        now = System.currentTimeMillis();
                        Thread.sleep(2500);
                        if (10000-(now-start) > 0) {
                            Thread.sleep(10000 - (now - start));
                        }
                        start=System.currentTimeMillis();
                    } catch (IOException e){
                        e.printStackTrace();
                    }
                    catch (InterruptedException e) {
                        e.printStackTrace();
                    }

                }
//            }

            writeOnSD("finish");

            wakeLock.release();
            return "Done";
        }

        @Override
        protected void onPostExecute(String str) {
            super.onPostExecute(str);
        }

        private  void writeOnSD(String text) {

            try {
                File root = new File(Environment.getExternalStorageDirectory(),
                        "mpbond");

                if (!root.exists()) {
                    root.mkdirs();
                }
                File file = new File(root, "status");

                BufferedWriter bW = new BufferedWriter(new FileWriter(file, false));
//                for (String line : data){
                    bW.write(text);
//                }
                bW.flush();
                bW.close();
            } catch (IOException e) {
                e.printStackTrace();
            }
            Log.d(TAG, text);
        }

    }

}

