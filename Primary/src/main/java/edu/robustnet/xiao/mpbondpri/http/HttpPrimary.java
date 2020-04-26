package edu.robustnet.xiao.mpbondpri.http;

import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.os.PowerManager;
import android.util.Log;
import android.util.Pair;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;

//import mphttp.umich.edu.CustomInputStream;
import myokhttp.OkHttpClient;
import myokhttp.Request;
import myokhttp.Response;

public class HttpPrimary implements HttpListener {

    private static final String TAG = "Shawn-HttpPrimary";
    public enum NetType {
        WIFI, CELL
    }

    private PowerManager.WakeLock wakeLock;
    private Handler handler;
    private static OkHttpClient cellClient;

    public int fileSizeBytes;
    private String url;
    private CustomInputStream inputStream;
    private ArrayList<Pair<Integer, Integer>> chunks;
    private volatile int chunkIndex;
    private int numSec;

    private int chunkSizeByte;
    private InputStream[] pipeInputStream;
    private OutputStream[] pipeOutputStream;

    public HttpPrimary(String url, int chunkSizeByte, OkHttpClient cellClient, String [] hip, InputStream [] pipeInputStream, OutputStream [] pipeOutputStream, int numSec) {

        this.url=url;
        this.fileSizeBytes = -1;
        this.inputStream = new CustomInputStream();
        handler = new Handler(Looper.getMainLooper());
        this.chunkIndex = 0;
        this.chunks = new ArrayList<>();
        this.chunkSizeByte = chunkSizeByte;
        this.numSec = numSec;
        this.pipeInputStream = pipeInputStream;
        this.pipeOutputStream = pipeOutputStream;

        this.cellClient = cellClient;

        this.getFileSize(cellClient);

//        // fetch the first chunk over PS-Path
//        PSChunkDownloader psChunkDownloader = new PSChunkDownloader(this.handler,this, this.url, this.chunkSizeByte,
//                this.cellClient, NetType.CELL, 0, this.chunkSizeByte - 1);
//
//        psChunkDownloader.start();
//        this.chunkIndex += 1;
//
//        for(int i = 0; i < this.numSec; i++) {
//            this.byPipe(pipeInputStream[i], pipeOutputStream[i]);
//        }

    }

    public void updateConfig(int chunkSizeByte) {
        this.chunkSizeByte = chunkSizeByte;
        this.chunks.removeAll(this.chunks);
        this.chunkIndex = 0;
        this.inputStream = new CustomInputStream();
        this.handler = new Handler(Looper.getMainLooper());
        for(int i = 0; i < (int) Math.ceil((fileSizeBytes*1.0f) / this.chunkSizeByte) ; i++) {
            int start = i * this.chunkSizeByte;
            int end =  ((i+1) * this.chunkSizeByte) - 1;
            if (end >=  this.fileSizeBytes) {
                end = this.fileSizeBytes - 1;
            }
//                    Log.d(TAG, "\t"+start+","+end);
            this.chunks.add(new Pair<Integer, Integer>(start, end));
        }
        PSChunkDownloader psChunkDownloader = new PSChunkDownloader(this.handler,this, this.url, this.chunkSizeByte,
                this.cellClient, NetType.CELL, 0, this.chunkSizeByte - 1);

        psChunkDownloader.start();
        this.chunkIndex += 1;
        for(int i = 0; i < this.numSec; i++) {
            this.byPipe(pipeInputStream[i], pipeOutputStream[i]);
        }
        Log.d(TAG, ""+ this.chunkIndex + " " + this.numSec + " " + chunks.size());
    }

    private synchronized void getFileSize(OkHttpClient client) {
        // construct a HTTP HEAD request to query the file size
        Request request = new Request.Builder().url(this.url).head().build();
        Response response = null;
        try {
            response = client.newCall(request).execute();
            if(response.isSuccessful()) {
                this.fileSizeBytes = Integer.parseInt(response.header("content-length"));
                this.inputStream.setSize(fileSizeBytes);
//                Log.d(TAG, "file size: "+fileSizeBytes+" "+(int) Math.ceil(fileSizeBytes / this.chunkSizeByte));
                for(int i = 0; i < (int) Math.ceil((fileSizeBytes*1.0f) / this.chunkSizeByte) ; i++) {
                    int start = i * this.chunkSizeByte;
                    int end =  ((i+1) * this.chunkSizeByte) - 1;
                    if (end >=  this.fileSizeBytes) {
                        end = this.fileSizeBytes - 1;
                    }
//                    Log.d(TAG, "\t"+start+","+end);
                    this.chunks.add(new Pair<Integer, Integer>(start, end));
                }

            }
            response.close();
        } catch (IOException e) {
            if (response!=null) {
                response.close();
            }
            e.printStackTrace();
        }
    }

    private synchronized void byPipe(InputStream pipeInputStream, OutputStream pipeOutputStream) {

//        PipeChunkDownloader pipeChunkDownloader = new PipeChunkDownloader(this.handler, this,
//                NetType.WIFI, 0, this.chunkSizeByte - 1, pipeInputStream, pipeOutputStream, this.chunkSizeByte);
//        pipeChunkDownloader.start();
        int nextIndex = getNextIndex();
        if(nextIndex<this.chunks.size()) {
            Pair<Integer, Integer> nextChunk = this.chunks.get(nextIndex);

            // primary downloads chunks through pipes (helpers)
            PipeChunkDownloader pipeChunkDownloader = new PipeChunkDownloader(this.handler, this,
                    NetType.WIFI, nextChunk.first, nextChunk.second, pipeInputStream, pipeOutputStream, this.chunkSizeByte);
            pipeChunkDownloader.start();
            incrementIndex();
        }

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

    @Override
    public synchronized void onTransferEnd(HttpPrimary.NetType netType) {
//        logTimestamp(this.startLog+" "+System.currentTimeMillis()+" "+netType+" "+byteRangeEnd);
        int nextIndex = getNextIndex();
        if(nextIndex<this.chunks.size()) {
//            Log.d(TAG, "nextIndex=" + nextIndex);
            Pair<Integer, Integer> nextChunk = this.chunks.get(nextIndex);
            PSChunkDownloader chunkDownloader;

            chunkDownloader = new PSChunkDownloader(this.handler, this, this.url, this.chunkSizeByte, this.cellClient, NetType.CELL, nextChunk.first, nextChunk.second);
            chunkDownloader.start();
            incrementIndex();
        }
    }

    @Override
    public synchronized void onPipeTransferEnd(HttpPrimary.NetType netType, int byteRangeStart, int byteRangeEnd,
                                               InputStream pipeInputStream, OutputStream pipeOutputStream) {
//        Log.d(TAG, "onPipeTransferEnd" );
//        logTimestamp(this.startLog+" "+System.currentTimeMillis()+" "+netType+" "+byteRangeEnd);
        int nextIndex = getNextIndex();
        if(nextIndex<this.chunks.size()) {
//            Log.d(TAG, "nextIndex=" + nextIndex);
            Pair<Integer, Integer> nextChunk = this.chunks.get(nextIndex);
//            Log.d(TAG, "next chunk on pipe is " + nextChunk);
            PipeChunkDownloader pipeChunkDownloader;

            pipeChunkDownloader = new PipeChunkDownloader(this.handler, this,
                    NetType.WIFI, nextChunk.first, nextChunk.second, pipeInputStream, pipeOutputStream, chunkSizeByte);
            pipeChunkDownloader.start();
            incrementIndex();
        }
    }

    public synchronized int getNextIndex() {
        return this.chunkIndex;
    }

    public synchronized void incrementIndex() {
        this.chunkIndex += 1;
    }

    public  void close() throws IOException {
        //TODO
        if (inputStream!=null) {
            inputStream.close();
        }
    }

    class PSChunkDownloader extends Thread {
        private final Handler eventHandler;
        private final HttpListener listener;
        private final String url;
        private OkHttpClient client;
        public int byteRangeStart;
        public int byteRangeEnd;
        private final HttpPrimary.NetType netType;
        private int chunkSizeBytes;

        public PSChunkDownloader(Handler eventHandler, HttpListener listener, String url, int chunkSizeBytes, OkHttpClient client,
                                   HttpPrimary.NetType netType, int byteRangeStart, int byteRangeEnd) {
            this.eventHandler = eventHandler;
            this.listener = listener;
            this.url = url;
            this.client = client;
            this.byteRangeStart = byteRangeStart;
            this.byteRangeEnd = byteRangeEnd;
            this.netType = netType;
            this.chunkSizeBytes = chunkSizeBytes;
        }

        @Override
        public void run() {
//            logTimestamp(HttpPrimary.this.startLog + " " + System.currentTimeMillis() + " " + this.netType);
            Request request = new Request.Builder()
                    .addHeader("Range", "bytes=" + this.byteRangeStart + "-" + this.byteRangeEnd)
                    .addHeader("Keep-Alive", "timeout=60, max=100")
                    .url(url)
                    .build();
            try {
                Response response = client.newCall(request).execute();
//                int chunkId = this.byteRangeEnd / chunkSizeBytes;
//                Log.d(TAG, ">> "+ this.netType + " start: " + chunkId + " (" + this.byteRangeStart + "," +
//                        this.byteRangeEnd+") " + System.currentTimeMillis() + " " + response.protocol());

                if(response.isSuccessful()) {

                    byte[] readBuffer = new byte[1024];
                    InputStream inputStream = response.body().byteStream();
                    int readLen;
                    int offset = byteRangeStart;
                    int first = 1;
                    while((readLen = inputStream.read(readBuffer)) > 0) {
                        if (first == 1) {
                            long start = System.currentTimeMillis();
                            Log.d(TAG, "byte-range: " + byteRangeStart + "-" + byteRangeEnd + " start: " + start);
                            first = 0;
                        }
//                        Log.d(TAG, "read from WWAN " + readLen);
                        sendChunks(readBuffer.clone(), offset, readLen);
                        offset+=readLen;
                    }
                    long now = System.currentTimeMillis();
                    Log.d(TAG, "byte-range: " + byteRangeStart + "-" + byteRangeEnd + " end: " + now);
                    inputStream.close();
                    response.close();

//                    Log.d(TAG, ">> " + this.netType + " end: (" + this.byteRangeStart + ","
//                            + this.byteRangeEnd + ") " + System.currentTimeMillis());
                    if (eventHandler != null && listener != null) {
                        eventHandler.post(new Runnable() {
                            @Override
                            public void run() {
                                listener.onTransferEnd(netType);
                            }
                        });
                    }
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

    class PipeChunkDownloader extends Thread {
        private final Handler eventHandler;
        private final HttpListener listener;
        public int byteRangeStart;
        public int byteRangeEnd;
        private final HttpPrimary.NetType netType;
        private InputStream pipeInputStream;
        private OutputStream pipeOutputStream;
        private int chunkSizeBytes;

        public PipeChunkDownloader(Handler eventHandler, HttpListener listener,
                                   HttpPrimary.NetType netType, int byteRangeStart, int byteRangeEnd,
                                   InputStream pipeInputStream, OutputStream pipeOutputStream, int chunkSizeBytes) {
            this.eventHandler = eventHandler;
            this.listener = listener;
            this.byteRangeStart = byteRangeStart;
            this.byteRangeEnd = byteRangeEnd;
            this.netType = netType;
            this.pipeInputStream = pipeInputStream;
            this.pipeOutputStream = pipeOutputStream;
            this.chunkSizeBytes = chunkSizeBytes;
        }

        @Override
        public void run() {
//            logTimestamp(HttpPrimary.this.startLog + " " + System.currentTimeMillis() + " " + this.netType);

            try {
//                Log.d("PIPE", ">> "+ this.netType + " start: (" + this.byteRangeStart + "," +
//                        this.byteRangeEnd+") " + System.currentTimeMillis());

                byte[] buf = new byte[8];
//                int id = byteRangeStart / chunkSizeBytes;
                System.arraycopy(toByteArray(byteRangeStart), 0, buf, 0, 4);
                System.arraycopy(toByteArray(byteRangeEnd), 0, buf, 4, 4);

                pipeOutputStream.write(buf,0, buf.length);
//                Log.d(TAG, "Request to helper sent out: " + id);

                byte[] readBuffer = new byte[1024];
                int readLen;
                int offset = byteRangeStart;
                int first = 1;
                while (offset < byteRangeEnd + 1) {
                    readLen = pipeInputStream.read(readBuffer);
                    if (first == 1) {
                        long start = System.currentTimeMillis();
                        Log.d(TAG, "byte-range: " + byteRangeStart + "-" + byteRangeEnd + " start: " + start);
                        first = 0;
                    }
//                    Log.d(TAG, "Read over pipe=" + readLen + " offset=" + offset);
                    sendChunks(readBuffer.clone(), offset, readLen);
                    offset+=readLen;
                }
                long now = System.currentTimeMillis();
                Log.d(TAG, "byte-range: " + byteRangeStart + "-" + byteRangeEnd + " end: " + now);

                if (eventHandler != null && listener != null) {
                    eventHandler.post(new Runnable() {
                        @Override
                        public void run() {
                            listener.onPipeTransferEnd(netType, byteRangeStart, byteRangeEnd, pipeInputStream, pipeOutputStream);
                        }
                    });
                }
            }
            catch (Exception e) {
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

    public byte[] toByteArray(int x) {
        byte[] src = new byte[4];
        src[3] =  (byte) ((x>>24) & 0xFF);
        src[2] =  (byte) ((x>>16) & 0xFF);
        src[1] =  (byte) ((x>>8) & 0xFF);
        src[0] =  (byte) (x & 0xFF);
        return src;
    }

}
