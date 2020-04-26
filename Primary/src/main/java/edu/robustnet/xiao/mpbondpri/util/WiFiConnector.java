package edu.robustnet.xiao.mpbondpri.util;

import android.util.Log;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.util.Arrays;
import java.util.Iterator;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;

import static edu.robustnet.xiao.mpbondpri.util.Constants.*;

public class WiFiConnector {

    // local pipe connector class for MPBond WiFi pipe
    private static final String TAG = "Shawn-WiFiConnector";
    private Socket ctrlSocket;
    private DatagramSocket ctrlLocalSocket;
    private InputStream dataInputStream; // input data message stream
    private OutputStream dataOutputStream; // output data message stream
    private InputStream localInputStream; // input WiFiConnector-to-proxy data stream
    private OutputStream localOutputStream; // output WiFiConnector-to-proxy data stream
    // side only contains pipe ack now, to add more control messages
    private InputStream ctrlInputStream; // input ctrl message stream
    private OutputStream ctrlOutputStream; // output ctrl message stream
    private volatile boolean flagRecvRTT = false; //
    private volatile int numEcho; //
    private int seqDataFromBT = 0;
    private int bytesInPipe = 0; // amount of bytes in userspace + networking stack + pipe link
    private int RTT = 0;
    private int n;
    private InputStream localCtrlInputStream;
    private OutputStream localCtrlOutputStream;

    private BlockingQueue<PipeMsg> toLocalQueue = new ArrayBlockingQueue<>(1000000);
    private BlockingQueue<PipeMsg> toBTQueue = new ArrayBlockingQueue<>(1000000);

    private int throughput = 10000; // init
    private int moveLen = 25;
    private BlockingQueue<Long> BWsamples = new ArrayBlockingQueue<Long>(moveLen);

    public WiFiConnector(int n) {
        // the nth WiFi connnector connects to the nth helper (zero-indexed)
        this.n = n;
    }

    // connect to the corresponding listening fd in proxy (local pipe listener)
    public void connectProxy() {
        try {
            Socket socket = new Socket();
            socket.connect(new InetSocketAddress(proxyIP, proxyPort));
            Log.d(TAG, "Connected to pipe proxy (local pipe listener) as the " + n + "th connector");
            localInputStream = socket.getInputStream();
            localOutputStream = socket.getOutputStream();
            LocalRead localRead = new LocalRead();
            localRead.start();
            LocalWrite localWrite = new LocalWrite();
            localWrite.start();
            startCtrlLocal();
        }
        catch (Exception e) {
            System.out.println(e);
        }
    }

    public void startCtrlLocal() {
        try {
            ctrlLocalSocket = new DatagramSocket();
            new LocalCtrlWrite().start();
        }
        catch (Exception e) {
            System.out.println(e);
        }
    }

    public void connectDataPeer(String secondIP) {
        try {
            Socket clientSocket = new Socket();
            clientSocket.connect(new InetSocketAddress(secondIP, serverPort));
            Log.d(TAG, "Connected to remote device " + n);
            dataInputStream = clientSocket.getInputStream();
            dataOutputStream = clientSocket.getOutputStream();
            dataRead myDataRead = new dataRead();
            dataWrite myDataWrite = new dataWrite();
            myDataRead.start();
            myDataWrite.start();
        }
        catch (Exception e) {
            System.out.println(e);
        }
    }

    public void connectCtrlPeer(String secondIP) {
        try {
            ctrlSocket = new Socket();
            ctrlSocket.connect(new InetSocketAddress(secondIP, serverPortSide));
            Log.d(TAG, "Connected to remote device ctrl " + n);
            ctrlInputStream = ctrlSocket.getInputStream();
            ctrlOutputStream = ctrlSocket.getOutputStream();
            new ctrlRead().start();
            new ctrlWrite().start();
        }
        catch (Exception e) {
            System.out.println(e);
        }
    }

    // thread to handle control recv from MPBond pipe
    public class ctrlRead extends Thread {
        public void run() {
            recvCtrl();
        }
    }

    // thread to handle ctrl Tx over MPBond pipe
    public class ctrlWrite extends Thread {
        public void run() {
            sendCtrl();
        }
    }

    // thread to handle data Rx over MPBond pipe
    public class dataRead extends Thread {
        public void run() {
            recvData();
        }
    }

    // thread to handle data Tx over MPBond pipe
    public class dataWrite extends Thread {
        public void run() {
            sendData();
        }
    }

    // send data messages over MPBond pipe
    public void sendData() {
        try {
            while (true) {
                PipeMsg pipeMsg = toBTQueue.take();
                byte[] buf = pipeMsg.getData();
                dataOutputStream.write(buf, 0, buf.length);
            }
        }
        catch (Exception e) {
            System.out.println(e);
        }
    }

    // recv data messages from MPBond pipe
    public void recvData() {
        byte [] buf = new byte[8192];
        int bytesRead;
        try {
            int start_recv = 0;
            long start_time = System.currentTimeMillis();
            long last_recv_time = System.currentTimeMillis();
            long BW_kbps;
            BWsamples = new ArrayBlockingQueue<Long>(moveLen);
            for (int i = 0; i < 1; i++) BWsamples.put(Long.valueOf(throughput));
            while (true) {
                bytesRead = dataInputStream.read(buf);
                if (bytesRead > 0) {
//                    Log.d(TAG, System.currentTimeMillis() + " read " + bytesRead + " bytes from WLAN " + n);
                    byte [] connID_b = Arrays.copyOfRange(buf, 0, 2);
                    ByteBuffer wrapped = ByteBuffer.wrap(connID_b); // big-endian by default
                    short connID = wrapped.order(java.nio.ByteOrder.LITTLE_ENDIAN).getShort();
                    byte [] seq_b = Arrays.copyOfRange(buf, 2, 6);
                    wrapped = ByteBuffer.wrap(seq_b); // big-endian by default
                    int seq = wrapped.order(java.nio.ByteOrder.LITTLE_ENDIAN).getInt();
                    byte [] len_b = Arrays.copyOfRange(buf, 6, 8);
                    wrapped = ByteBuffer.wrap(len_b); // big-endian by default
                    short len = wrapped.order(java.nio.ByteOrder.LITTLE_ENDIAN).getShort();
                    // add received msgs to msgQueue
                    PipeMsg pipeMsg = new PipeMsg();
                    byte[] realbuf = new byte[bytesRead];
                    System.arraycopy(buf,0, realbuf,0, bytesRead);
                    pipeMsg.setData(realbuf);
                    toLocalQueue.add(pipeMsg);
                    seqDataFromBT += bytesRead;
                    long curr_time = System.currentTimeMillis();
                    if (curr_time - last_recv_time > 50) {
                        last_recv_time = curr_time;
                        start_time = curr_time;
                        start_recv = seqDataFromBT;
                    }
                    // already enough (20KB, 50ms) packet samples
                    else if ((seqDataFromBT - start_recv > 20000) && (curr_time - start_time > 50)) {
                        BW_kbps = (seqDataFromBT - start_recv) * 8 / (curr_time - start_time);
                        throughput = getBW(BW_kbps);
                        start_time = curr_time;
                        start_recv = seqDataFromBT;
                        last_recv_time = curr_time;
                    }
                    else {
                        last_recv_time = curr_time;
                    }
                }
            }
        }
        catch (Exception e) {
            System.out.println(e);
        }
    }

    // send control messages to MPBond pipe
    public void sendCtrl() {
        // now the ctrl message sent = pipeId + number to echo (1 byte) + ACK (i.e. the relative seq of the data received over pipe, 4 bytes) + thrpt (4 bytes)
        // TODO: also add the seq delivered to the client app to the ctrl msg
        try {
            while (true) {
                if (flagRecvRTT) {
                    flagRecvRTT = false;
                    byte[] id = {(byte) n};
                    byte[] buf = {(byte) numEcho};
                    byte[] ACK = IntToBytes(seqDataFromBT);
                    byte[] thrpt = IntToBytes(throughput);
                    byte[] realbuf = new byte[10];
                    System.arraycopy(id, 0, realbuf, 0, 1);
                    System.arraycopy(buf,0,realbuf,1, 1);
                    System.arraycopy(ACK,0,realbuf,2, 4);
                    System.arraycopy(thrpt,0,realbuf,6, 4);
                    ctrlOutputStream.write(realbuf, 0, realbuf.length);
                }
            }
        } catch (IOException e) {
            e.printStackTrace();
        }
    }

    // recv control messages from MPBond pipe
    public void recvCtrl() {
        // the ctrl msg received is the 1-byte number from 0~255 + RTT and bytesInPipe as well
        try {
            byte[] buf = new byte[9];
            int bytesRead = 0;
            while (true) {
                if (bytesRead == 9) {
                    bytesRead = 0;
                    numEcho = buf[0] & 0xff;
                    flagRecvRTT = true;
                    byte[] buf_second = new byte[4];
                    System.arraycopy(buf, 1, buf_second, 0, 4);
                    byte[] buf_third = new byte[4];
                    System.arraycopy(buf, 5, buf_third, 0, 4);
                    RTT = ByteBuffer.wrap(buf_second).order(java.nio.ByteOrder.LITTLE_ENDIAN).getInt();
                    bytesInPipe = ByteBuffer.wrap(buf_third).order(java.nio.ByteOrder.LITTLE_ENDIAN).getInt();
//                    Log.d(TAG, "numEcho=" + numEcho + " RTT=" + RTT + " bytesInPipe=" + bytesInPipe);
                } else {
                    int nLeft = 9 - bytesRead;
                    int r = ctrlInputStream.read(buf, bytesRead, nLeft);
                    bytesRead += r;
                }
            }

        }  catch (IOException e) {
            e.printStackTrace();
        }
    }

    // Forwarding data received from MPBond pipe to local proxy
    private class LocalWrite extends Thread {

        @Override
        public void run() {
            sendDataToLocal();
        }
    }

    // Reading data from local and add it to msgQueue
    private class LocalRead extends Thread {

        @Override
        public void run() {
            recvDataFromLocal();
        }
    }

    private class LocalCtrlWrite extends Thread {
        @Override
        public void run() {
            sendCtrlToLocal();
        }
    }

    private class LocalCtrlRead extends Thread {
        @Override
        public void run() {
            readCtrlFromLocal();
        }
    }

    public void sendCtrlToLocal() {
        // send pipeInfo to C++
        try {
            while (true) {
                // test
                byte[] buf = new byte[16];
                System.arraycopy(IntToBytes(n), 0, buf, 0, 4);
                System.arraycopy(IntToBytes(throughput),0, buf,4, 4);
                System.arraycopy(IntToBytes(RTT),0, buf,8, 4);
                System.arraycopy(IntToBytes(bytesInPipe),0, buf,12, 4);
                DatagramPacket DpSend = new DatagramPacket(buf, buf.length, InetAddress.getLocalHost(), proxyPortSide);
                ctrlLocalSocket.send(DpSend);
                Thread.sleep(50); // report every 50ms
            }
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    public void readCtrlFromLocal() {
        try {
            while (true) {

                byte[] buf = new byte[12];
//                localCtrlOutputStream.write(buf,0, buf.length);
                Thread.sleep(50); // report every 50ms
            }
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    // send data over local socket
    public void sendDataToLocal() {
        try {
            while (true) {
                // take msg from msgQueue and write it to C++ proxy
                PipeMsg toLocalMsg;
                toLocalMsg = toLocalQueue.take();
                byte [] data = toLocalMsg.getData();
//                Log.d(TAG, "Data length to be sent to local" + n + " is " + Integer.toString(data.length));
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
            while (true)
            {
                // read data from local socket and add it to the msgQueue
                bytesRead = localInputStream.read(buf);
                PipeMsg fromLocalMsg = new PipeMsg();
                byte[] realbuf = new byte[bytesRead];
                System.arraycopy(buf,0,realbuf,0,bytesRead);
                fromLocalMsg.setData(realbuf);
                toBTQueue.add(fromLocalMsg);
            }
        }
        catch (Exception e) {
            System.out.println(e);
        }
    }

    public int getBW(Long BW_kbps) {
        int BW = 0;
        double ewma = 0.0;
        try {
            if (BWsamples.size() == moveLen) {
                BWsamples.take();
            }
            BWsamples.put(BW_kbps);
            Iterator iter = BWsamples.iterator();
            int cnt = 0;
            while (iter.hasNext()) {
                if (cnt == 0) {
                    ewma = Long.parseLong(iter.next().toString());
                }
                else {
                    ewma = 0.25 * ewma + 0.75 * Long.parseLong(iter.next().toString());
                }
                cnt++;
            }
//            BW = (int)(sum / samples.size());
            BW = (int)(ewma*100.0/95.0);
        }
        catch(Exception e) {
            System.out.println(e);
        }
        return BW;
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
