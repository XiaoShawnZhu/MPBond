package edu.robustnet.xiao.mpbondhel.util;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothServerSocket;
import android.bluetooth.BluetoothSocket;
import android.util.Log;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.util.UUID;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;
import java.util.Calendar;

public class BluetoothListener {

    private static final String TAG = "BluetoothListener";
    private static final UUID MY_UUID_SECURE = UUID.fromString("00001101-0000-1000-8000-00805F9B34BF");
    private static final UUID MY_UUID_SECURE2 = UUID.fromString("566c2d8f-152f-4821-bd7e-b65469ae16ba");

    BluetoothAdapter myAdapter = BluetoothAdapter.getDefaultAdapter();
    private boolean isPipeEstablished = false;
    String proxyIP = "127.0.0.1";
    int proxyPort = 1303;
    int proxyPortSide = 1304;
    private static BluetoothSocket btSocket;
    private static BluetoothSocket btSocketSide;
    private static InputStream btInputStream;
    private static OutputStream btOutputStream;
    private static InputStream localInputStream;
    private static OutputStream localOutputStream;
    private static InputStream btInputStreamSide;
    private static OutputStream btOutputStreamSide;
    private static InputStream localInputStreamSide;
    private static OutputStream localOutputStreamSide;
    private static int segmentSize = 1000;
    private static int recvBufSize = 65536;
    private static int fileSize = 10000000;
    private static byte[] sendData = new byte[fileSize];
    static BlockingQueue<Pipe> toLocalQueue = new ArrayBlockingQueue<Pipe>(1000000);
    static BlockingQueue<Pipe> toBTQueue = new ArrayBlockingQueue<Pipe>(1000000);

    private static volatile int sumDataSent = 0;
    private static volatile int RTT = 0;
    private static volatile int timedif = 0;
    private static volatile int throughput = 0;
    private static volatile long[] time = new long[256];

    public void connectPeer() {
        new connectPeer().start();
    }

    public void connectPeerSide() {
        new connectPeerSide().start();
    }

    public void connectProxy() {
        new connectProxy().start();
    }

    public void connectProxySide() {
        new connectProxySide().start();
    }

    public class connectPeer extends Thread {
        @Override
        public void run() {
            try {
                BluetoothServerSocket myServerSocket = myAdapter.listenUsingRfcommWithServiceRecord(TAG, MY_UUID_SECURE);
                Log.d(TAG, "BTpipe listening.");
                btSocket = myServerSocket.accept();
                Log.d(TAG, String.valueOf(btSocket));
                isPipeEstablished = true;
                new BtRead().start();
                new BtWrite().start();
            } catch (IOException ioe) {
                if (btSocket != null) {
                    try {
                        btSocket.close();
                    } catch (IOException ioe2) {
                        ioe2.printStackTrace();
                    }
                }
                ioe.printStackTrace();
            } catch (Exception e) {
                e.printStackTrace();
            }
        }
    }

    public class connectPeerSide extends Thread {
        @Override
        public void run() {
            try {
                BluetoothServerSocket myServerSocketSide = myAdapter.listenUsingRfcommWithServiceRecord(TAG, MY_UUID_SECURE2);
                Log.d(TAG, "BTpipe listening.");
                btSocketSide = myServerSocketSide.accept();
                Log.d(TAG, String.valueOf(btSocketSide));
                new BtReadSide().start();
                new BtWriteSide().start();
            } catch (IOException ioe) {
                if (btSocketSide != null) {
                    try {
                        btSocketSide.close();
                    } catch (IOException ioe2) {
                        ioe2.printStackTrace();
                    }
                }
                ioe.printStackTrace();
            } catch (Exception e) {
                e.printStackTrace();
            }
        }
    }

    public boolean getPipeStatus() {
        return isPipeEstablished;
    }

    public class connectProxy extends Thread {
        @Override
        public void run() {
            try {
                Socket socket = new Socket();
                socket.connect(new InetSocketAddress(proxyIP, proxyPort));
                localInputStream = socket.getInputStream();
                localOutputStream = socket.getOutputStream();
                new LocalRead().start();
                new LocalWrite().start();

            }
            catch (IOException ioe) {
                ioe.printStackTrace();
            }
            catch (Exception e) {
                e.printStackTrace();
            }
        }
    }

    public class connectProxySide extends Thread {
        @Override
        public void run() {
            try {
                Socket socketSide = new Socket();
                socketSide.connect(new InetSocketAddress(proxyIP, proxyPortSide));
                //localInputStreamSide = socketSide.getInputStream();
                localOutputStreamSide = socketSide.getOutputStream();
                new measureThroughput().start();
                new LocalWriteSide().start();
            } catch (IOException ioe) {
                ioe.printStackTrace();
            } catch (Exception e) {
                e.printStackTrace();
            }
        }
    }

    // thread to handle data Rx over BT
    public class BtRead extends Thread {
        @Override
        public void run(){
            recvDataFromBT();
        }

    }

    // thread to handle data Tx over BT
    public class BtWrite extends Thread {
        @Override
        public void run(){
            sendDataToBT();
        }

    }

    public class BtReadSide extends Thread {
        @Override
        public void run(){
            recvDataFromBTSide();
        }

    }

    // thread to handle data Tx over BT
    public class BtWriteSide extends Thread {
        @Override
        public void run(){
            sendDataToBTSide();
        }
    }

    // send data over BT socket
    public void sendDataToBT() {
        try{
            int dataSent;
            btOutputStream = btSocket.getOutputStream();
            while (true) {
                if (toBTQueue.isEmpty()){
                    timedif = 0;
                }
                Pipe pipeMsg = toBTQueue.take();
                byte[] buf = pipeMsg.getData();
                byte[] time_then = new byte[8];
                System.arraycopy(buf,0,time_then,0,8);
                timedif = (int) (Calendar.getInstance().getTimeInMillis() -bytesToLong(time_then));
                dataSent = buf.length - 8;
                btOutputStream.write(buf, 8, buf.length - 8);
                btOutputStream.flush();
                sumDataSent += dataSent;
                //Log.d(TAG,"DATA SENT IS: " + dataSent + " the sum is: " + sum);
            }

        } catch (IOException ioe){
            ioe.printStackTrace();
        } catch(Exception e){
            System.out.println(e);
        }
    }

    public void sendDataToBTSide(){
        try {
            int n = 0;
            btOutputStreamSide = btSocketSide.getOutputStream();
            while (true) {
                if (n <= 255) {
                    time[n] = Calendar.getInstance().getTimeInMillis();
                    byte[] buf = {(byte) n}; // determine the RTT data
//                    Log.d(TAG, "About to write btOutputStreamSide.");
                    btOutputStreamSide.write(buf, 0, buf.length);
                    Thread.sleep(100);
                    n++;
                }else{
                    n = 0;
                }
            }

        } catch (IOException ioe){
            ioe.printStackTrace();
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    // recv data from BT socket
    public void recvDataFromBT(){
        try {
            btInputStream = btSocket.getInputStream();
            byte[] buf = new byte[recvBufSize];
            int bytesRead;
            while (true) {
                bytesRead = btInputStream.read(buf);
                String msg = new String(buf).substring(0, bytesRead);
                // add received msgs to msgQueue
                Pipe pipeMsg = new Pipe();
                byte[] realbuf = new byte[bytesRead];
                System.arraycopy(buf, 0, realbuf, 0, bytesRead);
                pipeMsg.setData(realbuf);
                toLocalQueue.add(pipeMsg);
            }

        } catch (IOException ioe){
            ioe.printStackTrace();
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    public void recvDataFromBTSide(){
        try {
            byte[] buf = new byte[1];
            int bytesRead;
            btInputStreamSide = btSocketSide.getInputStream();
            while (true) {
                bytesRead = btInputStreamSide.read(buf);
                if (bytesRead == 1) {
                    Calendar rightNow = Calendar.getInstance();
                    RTT = (int) (rightNow.getTimeInMillis() - time[buf[0] & 0xff]);
//                    Log.d("RTT NOW IS: ", String.valueOf(RTT));
                }
            }

        } catch (IOException ioe){
            ioe.printStackTrace();
        } catch (Exception e) {
            e.printStackTrace();
        }
    }


    // Forwarding data received from BT to local
    private class LocalWrite extends Thread {

        @Override
        public void run(){
            sendDataToLocal();
        }
    }

    // Reading data from local and add it to msgQueue
    private class LocalRead extends Thread {

        @Override
        public void run(){
            recvDataFromLocal();
        }
    }

    public void measureThroughput(){
        try {
            while(true) {
                int temp = sumDataSent;
                Thread.sleep(1000);
                throughput = sumDataSent - temp;
            }
        }catch (Exception e){
            e.printStackTrace();
        }
    }

    // send data over local socket
    public void sendDataToLocal() {
        try{
            while(true){
                // take msg from msgQueue and write it to C++ proxy
                Pipe toLocalMsg;
                toLocalMsg = toLocalQueue.take();
                byte [] data = toLocalMsg.getData();
//                Log.d(TAG, "About to write localOutputStream.");
                localOutputStream.write(data, 0, data.length);
            }
        }
        catch (IOException ioe){
            ioe.printStackTrace();
        }
        catch(Exception e){
            e.printStackTrace();
        }
    }

    // recv data from local socket
    public void recvDataFromLocal(){
        byte[] buf = new byte[recvBufSize];
        int bytesRead;
        try{
            while(true)
            {
                // read data from local socket and add it to the msgQueue
                bytesRead = localInputStream.read(buf);
                Pipe fromLocalMsg = new Pipe();
                byte[] realbuf = new byte[bytesRead];
                System.arraycopy(buf,0,realbuf,0,bytesRead);
                byte[] time_now = longToBytes(Calendar.getInstance().getTimeInMillis());
                byte[] timerealbuf = new byte[realbuf.length + time_now.length];
                System.arraycopy(time_now, 0, timerealbuf, 0, time_now.length);
                System.arraycopy(realbuf, 0, timerealbuf, time_now.length, realbuf.length);
                fromLocalMsg.setData(timerealbuf);
                toBTQueue.add(fromLocalMsg);
                //Log.d(TAG, "successfully read " + bytesRead);
            }
        } catch (IOException ioe){
            ioe.printStackTrace();
        } catch(Exception e){
            e.printStackTrace();
        }
    }


    // Reading data from local and add it to msgQueue
    private class measureThroughput extends Thread {
        @Override
        public void run(){
            measureThroughput();
        }
    }



    private class LocalWriteSide extends Thread {
        @Override
        public void run(){
            sendDataToLocalSide();
        }
    }

    public void sendDataToLocalSide() {
        try{
            while (true){
                byte[] throughputByte;
                byte[] RTTByte;
                byte[] timedifByte;
                throughputByte = IntToBytes(throughput);
                RTTByte = IntToBytes(RTT);
                timedifByte = IntToBytes(timedif);
                byte[] buf = new byte[12];
                System.arraycopy(throughputByte, 0, buf, 0, 4);
                System.arraycopy(RTTByte, 0, buf, 4, 4);
                System.arraycopy(timedifByte, 0, buf, 8, 4);
//                Log.d(TAG, "About to write localOutputStreamSide.");
                localOutputStreamSide.write(buf,0,buf.length);
                Thread.sleep(100);
            }
        } catch (IOException ioe){
            ioe.printStackTrace();
        } catch (Exception e){
            e.printStackTrace();
        }
    }

    public byte[] IntToBytes(int x) {
        /*
        ByteBuffer buffer = ByteBuffer.allocate(Integer.BYTES);
        buffer.putInt(x);
        return buffer.array();
        */
        byte[] src = new byte[4];
        src[3] =  (byte) ((x>>24) & 0xFF);
        src[2] =  (byte) ((x>>16) & 0xFF);
        src[1] =  (byte) ((x>>8) & 0xFF);
        src[0] =  (byte) (x & 0xFF);
        return src;
    }

    public byte[] longToBytes(long x) {
        ByteBuffer buffer = ByteBuffer.allocate(Long.BYTES);
        buffer.putLong(x);
        return buffer.array();
    }

    public long bytesToLong(byte[] bytes) {
        ByteBuffer buffer = ByteBuffer.allocate(Long.BYTES);
        buffer.put(bytes);
        buffer.flip();//need flip
        return buffer.getLong();
    }

}


