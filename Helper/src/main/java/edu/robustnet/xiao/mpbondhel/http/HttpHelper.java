package edu.robustnet.xiao.mpbondhel.http;

import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.os.PowerManager;
import android.util.Log;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

//import mphttp.umich.edu.CustomInputStream;
import myokhttp.OkHttpClient;
import myokhttp.Request;
import myokhttp.Response;

public class HttpHelper implements HttpListener {
    private static final String TAG = "Shawn-HttpHelper";
    public enum NetType {
        WIFI, CELL
    }
    private static final int CHUNK_SIZE_BYTE = 256 * 1024;

    private PowerManager.WakeLock wakeLock;
    private final Handler handler;
    private static OkHttpClient cellClient;

    private String url;
    private CustomInputStream inputStream;
    private volatile int chunkIndex;
    private OutputStream outputStream;
    private int start;
    private int end;

    private long startLog;

    public HttpHelper(String url, OkHttpClient cellClient, int start, int end, OutputStream outputStream) {

        this.url=url;
        this.inputStream = new CustomInputStream();
        handler = new Handler(Looper.getMainLooper());
        this.chunkIndex = 0;
        this.outputStream = outputStream;
        this.start = start;
        this.end = end;

        this.cellClient = cellClient;

        this.startLog = System.currentTimeMillis();

//        HTTPChunkDownloader chunkDownloader = new HTTPChunkDownloader(this.handler, this, this.url,
//                this.cellClient, NetType.CELL, this.start, this.end, outputStream);
//        chunkDownloader.start();

    }

    public void updateConfig(String url, int start, int end) {
        this.start = start;
        this.end = end;
        this.url = url;
        HTTPChunkDownloader chunkDownloader = new HTTPChunkDownloader(this.handler, this, this.url,
                this.cellClient, NetType.CELL, this.start, this.end, outputStream);
        chunkDownloader.start();
    }

    public InputStream getInputStream(){
        return this.inputStream;
    }

    @Override
    public void onBytesTransferred(byte[] b, int offset, int len, NetType netType, int byteRangeStart, int byteRangeEnd) {
        if (this.inputStream!=null){
            try {
                inputStream.fill(b, offset, len, byteRangeStart, byteRangeEnd);
            } catch (IOException e) {
                e.printStackTrace();
            }
        }
    }


    public synchronized int getNextIndex(){
        return this.chunkIndex;
    }
    public synchronized void incrementIndex(){
        this.chunkIndex += 1;
    }

    public  void close() throws IOException {
        //TODO
        if (inputStream!=null){
            inputStream.close();
        }

    }

    class HTTPChunkDownloader extends Thread {
        private final Handler eventHandler;
        private final HttpListener listener;
        private final String url;
        private OkHttpClient client;
        public int byteRangeStart;
        public int byteRangeEnd;
        private final HttpHelper.NetType netType;
        private OutputStream outputStream;

        public HTTPChunkDownloader(Handler eventHandler, HttpListener listener, String url, OkHttpClient client,
                                   HttpHelper.NetType netType, int byteRangeStart, int byteRangeEnd, OutputStream outputStream) {
            this.eventHandler = eventHandler;
            this.listener = listener;
            this.url = url;
            this.client = client;
            this.byteRangeStart = byteRangeStart;
            this.byteRangeEnd = byteRangeEnd;
            this.netType = netType;
            this.outputStream = outputStream;
        }

        @Override
        public void run() {
//            logTimestamp(HttpHelper.this.startLog + " " + System.currentTimeMillis() + " " + this.netType);
            Request request = new Request.Builder()
                    .addHeader("Range", "bytes=" + this.byteRangeStart + "-" + this.byteRangeEnd)
                    .addHeader("Keep-Alive", "timeout=60, max=100")
                    .url(url)
                    .build();
            try {
                Response response = client.newCall(request).execute();
//                Log.d(TAG, ">> "+ this.netType + " start: (" + this.byteRangeStart + "," +
//                        this.byteRangeEnd+") " + System.currentTimeMillis() + " " + response.protocol());

                if(response.isSuccessful()) {

                    byte[] readBuffer = new byte[1024];
                    InputStream inputStream = response.body().byteStream();
                    int readLen;
                    int offset = byteRangeStart;
                    int first = 1;
                    while ((readLen = inputStream.read(readBuffer)) > 0) {
                        if (first == 1) {
                            long start = System.currentTimeMillis();
                            Log.d(TAG, "byte-range: " + byteRangeStart + "-" + byteRangeEnd + " start: " + start);
                            first = 0;
                        }
                        outputStream.write(readBuffer, 0, readLen);
//                        sendChunks(readBuffer.clone(), offset, readLen);
//                        Log.d(TAG, "Write data over pipe: " + readLen);
                        offset+=readLen;
                    }
                    long now = System.currentTimeMillis();
                    Log.d(TAG, "byte-range: " + byteRangeStart + "-" + byteRangeEnd + " end: " + now);
                    inputStream.close();
                    response.close();

//                    Log.d("HTTP", ">> " + this.netType + " end: (" + this.byteRangeStart + ","
//                            + this.byteRangeEnd + ") " + System.currentTimeMillis());

                }

            } catch (IOException e) {
                e.printStackTrace();
            }

        }

        private void sendChunks(final byte b[], final int offset, final int len){
            if (eventHandler != null && listener != null) {
                eventHandler.post(new Runnable() {
                    @Override
                    public void run() {
                        listener.onBytesTransferred(b, offset, len, netType,  byteRangeStart, byteRangeEnd);
                    }
                });
            }
        }

    }

    private void logTimestamp(String str) {

        try {
            File root = new File(Environment.getExternalStorageDirectory(),
                    "MPTCP");

            if (!root.exists()) {
                root.mkdirs();
            }

            File file = new File(root, "results");

            BufferedWriter bW = new BufferedWriter(new FileWriter(file, true));
            bW.write(str + "\n");
            bW.flush();
            bW.close();

        } catch (IOException e) {
            e.printStackTrace();
        }
    }

}
