package edu.robustnet.xiao.mpbondhel.util;

import android.util.Log;
import java.io.*;
import java.net.*;
import java.nio.ByteBuffer;
import java.util.concurrent.*;

import static edu.robustnet.xiao.mpbondhel.util.Constants.*;

public class WiFiListener {
    private boolean isPipeEstablished = false;
    private int seqDataFromLocal = 0;
    private long sumQueue = 0;
    private int bytesInPipe = 0; // amount of bytes in userspace + networking stack + pipe link
    private int seqDataACKedBT = 0;
    private int RTT = 0;
    private int throughput = 10000; // init
    private Socket connectionSocket;
    private Socket ctrlConnectionSocket;
    private InputStream dataInputStream;
    private OutputStream dataOutputStream;
    private InputStream localInputStream;
    private OutputStream localOutputStream;
    private InputStream localCtrlInputStream;
    private OutputStream  localCtrlOutputStream;
    private InputStream ctrlInputStream;
    private OutputStream ctrlOutputStream;
    private BlockingQueue<Pipe> toLocalQueue = new ArrayBlockingQueue<Pipe>(1000000);
    private BlockingQueue<Pipe> toBTQueue = new ArrayBlockingQueue<Pipe>(1000000);
    private long[] time = new long[256];

    public WiFiListener() {

    }

    public void listenPeer() {
        try{
            ServerSocket welcomeSocket = new ServerSocket(serverPort);
            Log.i(WiFiListenerTAG, "WiFi remote data pipe listening.");
            connectionSocket = welcomeSocket.accept();
            Log.i(WiFiListenerTAG, "WiFi remote data pipe connected.");
            dataInputStream = connectionSocket.getInputStream();
            dataOutputStream = connectionSocket.getOutputStream();
            dataRead myDataRead = new dataRead();
            dataWrite myDataWrite = new dataWrite();
            myDataRead.start();
            myDataWrite.start();
            isPipeEstablished = true;
        }
        catch(Exception e) {
            System.out.println(e);
        }
    }

    public void connectProxy() {
        new connectProxy().start();
    }

    public class connectProxy extends Thread {
        @Override
        public void run() {
            try {
                Socket socket = new Socket();
                socket.connect(new InetSocketAddress(proxyIP, proxyPort));
                Log.i(WiFiListenerTAG, "WiFi local data pipe connected.");
                localInputStream = socket.getInputStream();
                localOutputStream = socket.getOutputStream();
                LocalRead localRead = new LocalRead();
                localRead.start();
                LocalWrite localWrite = new LocalWrite();
                localWrite.start();
                Thread.sleep(1000);
            } catch (Exception e) {
                System.out.println(e);
            }
        }
    }

    public void listenCtrlPeer() {
        try {
            ServerSocket welcomeSocket = new ServerSocket(serverPortSide);
            Log.i(WiFiListenerTAG, "WiFi remote ctrl pipe listening.");
            ctrlConnectionSocket = welcomeSocket.accept();
            Log.i(WiFiListenerTAG, "WiFi remote ctrl pipe connected.");
            new ctrlRead().start();
            new ctrlWrite().start();
        }
        catch (Exception e) {

        }
    }

    public void connectCtrlProxy() {
        new connectCtrlProxy().start();
    }

    public class connectCtrlProxy extends Thread {
        @Override
        public void run() {
            try {
                Socket socketSide = new Socket();
                socketSide.connect(new InetSocketAddress(proxyIP, proxyPortSide));
                Log.i(WiFiListenerTAG, "WiFi local ctrl pipe connected.");
                localCtrlInputStream = socketSide.getInputStream();
                localCtrlOutputStream = socketSide.getOutputStream();
                new LocalCtrlWrite().start();
                Thread.sleep(1000);
            }
            catch (Exception e) {
                e.printStackTrace();
            }
        }
    }

    public boolean getPipeStatus() {
        return isPipeEstablished;
    }

    // thread to handle data Rx over BT
    public class dataRead extends Thread {
//        private int i;
        public dataRead(){
//            this.i = i;
        }
        @Override
        public void run(){
            recvData();
        }
    }

    // thread to handle data Tx over BT
    public class dataWrite extends Thread {
//        private int i;
        public dataWrite(){
//            this.i = i;
        }
        @Override
        public void run(){
            sendData();
        }
    }

    // thread to handle ctrl Rx from MPBond pipe
    public class ctrlRead extends Thread {
//        private int i;
        public ctrlRead(){
//            this.i = i;
        }
        @Override
        public void run(){
            recvCtrl();
        }
    }

    // thread to handle ctrl Tx over MPBond pipe
    public class ctrlWrite extends Thread {

//        private int i;
        public ctrlWrite(){
//            this.i = i;
        }
        @Override
        public void run(){
            sendCtrl();
        }
    }

    // send data over MPBond pipe
    public void sendData() {
        try {
            while (true) {
                Pipe pipeMsg = toBTQueue.take();
                byte[] buf = pipeMsg.getData();
//                long time = pipeMsg.getTime();
                dataOutputStream.write(buf, 0, buf.length);
            }
        }
        catch (Exception e) {
            System.out.println(e);
        }
    }

    // recv data from MPBond pipe
    public void recvData() {
        byte[] buf = new byte[8192];
        int bytesRead;
        try {
            while (true) {
                bytesRead = dataInputStream.read(buf);
                String msg = new String(buf).substring(0, bytesRead);
                // add received msgs to msgQueue
                Pipe pipeMsg = new Pipe();
                byte[] realbuf = new byte[bytesRead];
                System.arraycopy(buf,0,realbuf,0,bytesRead);
                pipeMsg.setData(realbuf);
                toLocalQueue.add(pipeMsg);
            }
        }
        catch (Exception e) {
            System.out.println(e);
        }
    }

    // send ctrl over MPBond pipe
    public void sendCtrl() {
        // n + RTT + bytesInPipe
        // n = a number incremented from 0 to 255 and over and over for RTT measurement
        try {
            int n = 0;
            ctrlOutputStream = ctrlConnectionSocket.getOutputStream();
            while (true) {
                if (n <= 255) {
                    time[n] = System.currentTimeMillis();
                    byte[] buf = {(byte) n}; // determine the RTT data
                    byte[] realbuf = new byte[9]; // 1 byte n + 4 byte RTT + 4 byte bytesInPipe
                    System.arraycopy(buf, 0, realbuf, 0, 1);
                    System.arraycopy(IntToBytes(RTT),0, realbuf, 1, 4);
                    System.arraycopy(IntToBytes(bytesInPipe), 0, realbuf, 5, 4);
                    ctrlOutputStream.write(realbuf, 0, realbuf.length);
//                    Log.d(TAG, "n=" + n + " RTT=" + RTT + " bytesInPipe=" + bytesInPipe);
                    Thread.sleep(100);
                    n++;
                } else {
                    n = 0;
                }
            }
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    // recv ctrl from MPBond pipe
    public void recvCtrl() {
        try {
            byte[] buf = new byte[10];
            int bytesRead;
            ctrlInputStream = ctrlConnectionSocket.getInputStream();
            while (true) {
                bytesRead = ctrlInputStream.read(buf);
//                Log.d(TAG, "readbytes=" + bytesRead);
                if (bytesRead == 10) {
                    int id = buf[0] & 0xff;
                    int seq = buf[1] & 0xff;
                    RTT = (int) (System.currentTimeMillis() - time[seq]);
                    byte[] buf_second = new byte[4];
                    System.arraycopy(buf, 2, buf_second, 0, 4);
                    byte[] buf_third = new byte[4];
                    System.arraycopy(buf, 6, buf_third, 0, 4);
                    int recv = ByteBuffer.wrap(buf_second).order(java.nio.ByteOrder.LITTLE_ENDIAN).getInt();
                    long curr_time = System.currentTimeMillis();
                    seqDataACKedBT = recv;
                    throughput = ByteBuffer.wrap(buf_third).order(java.nio.ByteOrder.LITTLE_ENDIAN).getInt();
//                    Log.d(TAG, "Recv ctrl from MPBond pipe, id=" + id + " seq=" + seq + " rtt=" + RTT +" thrpt=" + throughput);
                }
            }
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    // Forwarding data received from MPBond pipe to local proxy
    private class LocalWrite extends Thread {

//        private int i;
        public LocalWrite(){
//            this.i = i;
        }
        @Override
        public void run() {
            sendDataToLocal();
        }
    }

    // Reading data from local and add it to msgQueue
    private class LocalRead extends Thread {
//        private int i;
        public LocalRead(){
//            this.i = i;
        }
        @Override
        public void run() {
            recvDataFromLocal();
        }
    }

    // send data over local socket
    public void sendDataToLocal() {
        try {
            while (true) {
                // take msg from msgQueue and write it to C++ proxy
                Pipe toLocalMsg;
                toLocalMsg = toLocalQueue.take();
                byte [] data = toLocalMsg.getData();
                localOutputStream.write(data, 0, data.length);
            }
        }
        catch (Exception e) {
            System.out.println(e);
        }
    }

    // recv data from local socket
    public void recvDataFromLocal() {
        byte[] buf = new byte[8192];
        int bytesRead;
        try {
            while (true) {
                // read data from local socket and add it to the msgQueue
                bytesRead = localInputStream.read(buf);
                Pipe fromLocalMsg = new Pipe();
                byte[] realbuf = new byte[bytesRead];
                System.arraycopy(buf,0,realbuf,0,bytesRead);
                fromLocalMsg.setData(realbuf, System.currentTimeMillis(), seqDataFromLocal);
                seqDataFromLocal += bytesRead;
                toBTQueue.add(fromLocalMsg);
                sumQueue += bytesRead;
//                bytesOnDevice += bytesRead;
            }
        }
        catch (Exception e) {
            System.out.println(e);
        }
    }

    private class LocalCtrlWrite extends Thread {
//        private int i;
        public LocalCtrlWrite() {
//            this.i = i;
        }
        @Override
        public void run() {
            sendCtrlToLocal();
        }
    }

    public void sendCtrlToLocal() {
        // send pipe information to C++
        try {
            while (true) {
                byte[] buf = new byte[12];
                bytesInPipe = (int) sumQueue - seqDataACKedBT;
                Log.d(TAG, "bytesInPipe=" + bytesInPipe + " sumQueue=" + (int) sumQueue + " seqDataACKedBT=" + seqDataACKedBT);
//                bytesOnDevice = seqDataFromLocal - seqDataSent;
                if (bytesInPipe < 0) bytesInPipe = 0;
                // 4 bytes thrpt + 4 bytes RTT + 4 bytes bytesInPipe //+ 4 bytes bytesOnDevice
                System.arraycopy(IntToBytes(throughput), 0, buf, 0, 4);
//                System.arraycopy(IntToBytes(seqDataACKedBT), 0, buf, 0, 4);
                System.arraycopy(IntToBytes(RTT), 0, buf, 4, 4);
//                System.arraycopy(IntToBytes(timedif*throughput/8), 0, buf, 8, 4);
                System.arraycopy(IntToBytes(bytesInPipe), 0, buf, 8, 4);
//                System.arraycopy(IntToBytes(bytesOnDevice), 0, buf, 12, 4);
                localCtrlOutputStream.write(buf,0, buf.length);
                Thread.sleep(50); // report every 50ms
            }
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    public byte[] IntToBytes(int x) {
        byte[] src = new byte[4];
        src[3] =  (byte) ((x>>24) & 0xFF);
        src[2] =  (byte) ((x>>16) & 0xFF);
        src[1] =  (byte) ((x>>8) & 0xFF);
        src[0] =  (byte) (x & 0xFF);
        return src;
    }

}
