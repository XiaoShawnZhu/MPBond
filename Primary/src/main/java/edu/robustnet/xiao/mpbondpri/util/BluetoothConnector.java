package edu.robustnet.xiao.mpbondpri.util;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothSocket;
import android.util.Log;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.util.UUID;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;

public class BluetoothConnector {

    private static final String TAG = "BluetoothConnector";
    private static final UUID MY_UUID_SECURE = UUID.fromString("00001101-0000-1000-8000-00805F9B34BF");
    private static final UUID MY_UUID_SECURE2 = UUID.fromString("566c2d8f-152f-4821-bd7e-b65469ae16ba");

    BluetoothAdapter myAdapter = BluetoothAdapter.getDefaultAdapter();
    BluetoothDevice remoteDevice = null; // myAdapter.getRemoteDevice("F8:95:C7:04:E5:B5"); //connect to wearable
    String deviceName = null; //remoteDevice.getName();
    String deviceMAC = null; //remoteDevice.getAddress(); // MAC address
    String proxyIP = "127.0.0.1";
    int proxyPort = 1303;
    private BluetoothSocket btSocket;
    private BluetoothSocket btSocketSide;
    private static InputStream btInputStream;
    private static OutputStream btOutputStream;
    private static InputStream localInputStream;
    private static OutputStream localOutputStream;
    private static InputStream btInputStreamSide;
    private static OutputStream btOutputStreamSide;
    private static int segmentSize = 1000;
    private static int recvBufSize = 65536;
    private static int fileSize = 10000000;
    private static byte[] sendData = new byte[fileSize];
    static BlockingQueue<PipeMsg> toLocalQueue = new ArrayBlockingQueue<>(1000000);
    static BlockingQueue<PipeMsg> toBTQueue = new ArrayBlockingQueue<>(1000000);
    private static volatile boolean flagRecvRTT = false;
    private static volatile int needsend;
    private static String secondIP;

    public void connectPeer(String secondIP){
        this.secondIP = secondIP;
        new connectPeer().start();
    }

    public class connectPeer extends Thread {
        @Override
        public void run() {
            try {
                remoteDevice = myAdapter.getRemoteDevice(secondIP); //connect to wearable
                btSocket = remoteDevice.createRfcommSocketToServiceRecord(MY_UUID_SECURE);
                btSocket.connect();
                deviceName = remoteDevice.getName();
                deviceMAC = remoteDevice.getAddress(); // MAC address
                Log.d(TAG, "Connected to remote device " + deviceName + "@" + deviceMAC);
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
            } catch (Exception e) {
                e.printStackTrace();
            }
        }
    }


    public void connectPeerSide(String secondIP){
        this.secondIP = secondIP;
        new connectPeerSide().start();
    }


    public class connectPeerSide extends Thread{
        @Override
        public void run() {
            try {
                remoteDevice = myAdapter.getRemoteDevice(secondIP); //connect to wearable
                btSocketSide = remoteDevice.createRfcommSocketToServiceRecord(MY_UUID_SECURE2);
                btSocketSide.connect();
                deviceName = remoteDevice.getName();
                deviceMAC = remoteDevice.getAddress(); // MAC address
                Log.d(TAG, "Connected to remote device " + deviceName + "@" + deviceMAC);
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
            } catch (Exception e) {
                e.printStackTrace();
            }
        }
    }

    public void connectProxy() {
        new connectProxy().start();
    }

    // connect to the corresponding listening fd in proxy
    public class connectProxy extends Thread {
        @Override
        public void run() {
            try{
                Socket socket = new Socket();
                socket.connect(new InetSocketAddress(proxyIP, proxyPort));
                localInputStream = socket.getInputStream();
                localOutputStream = socket.getOutputStream();
                LocalRead localRead = new LocalRead();
                localRead.start();
                LocalWrite localWrite = new LocalWrite();
                localWrite.start();
            }
            catch (Exception e){
                System.out.println(e);
            }
        }
    }


    // thread to handle data Rx over BT
    public class BtRead extends Thread {

        public void run(){
            recvDataFromBT();
        }

    }

    // thread to handle data Tx over BT
    public class BtWrite extends Thread {

        public void run(){
            sendDataToBT();
        }

    }

    // thread to handle data Rx over BT
    public class BtReadSide extends Thread {

        public void run(){
            recvDataFromBTSide();
        }

    }

    // thread to handle data Tx over BT
    public class BtWriteSide extends Thread {

        public void run(){
            sendDataToBTSide();
        }

    }

    // send data over BT socket
    public void sendDataToBT() {
        try{
            btOutputStream = btSocket.getOutputStream();
            while(true){
                PipeMsg pipeMsg = toBTQueue.take();
                byte[] buf = pipeMsg.getData();
                btOutputStream.write(buf, 0, buf.length);
            }
        }
        catch (IOException ioe) {
            ioe.printStackTrace();
        }
        catch(Exception e){
            System.out.println(e);
        }
    }

    public void sendDataToBTSide() {
        try {
            btOutputStreamSide = btSocketSide.getOutputStream();
            while (true) {
                if (flagRecvRTT) {
                    flagRecvRTT = false;
                    byte[] buf = {(byte) needsend};
                    btOutputStreamSide.write(buf, 0, buf.length);
                }
            }
        }  catch (IOException e) {
            e.printStackTrace();
        }
    }

    public void recvDataFromBTSide() {
        try {
            byte[] buf = new byte[1];
            int bytesRead;
            btInputStreamSide = btSocketSide.getInputStream();
            while (true) {
                bytesRead = btInputStreamSide.read(buf);
                if (bytesRead == 1){
                    byte[] realbuf = new byte[bytesRead];
                    System.arraycopy(buf, 0, realbuf, 0, bytesRead);
                    needsend = realbuf[0] & 0xff;
                    flagRecvRTT = true;
                }
            }
        }  catch (IOException e) {
            e.printStackTrace();
        }
    }

    // recv data from BT socket
    public void recvDataFromBT() {
        try {
            btInputStream = btSocket.getInputStream();
            byte[] buf = new byte[recvBufSize];
            int bytesRead;
            int sumRead = 0;
            while (true) {
                bytesRead = btInputStream.read(buf);
                sumRead += bytesRead;
                PipeMsg pipeMsg = new PipeMsg();
                byte[] realbuf = new byte[bytesRead];
                System.arraycopy(buf, 0, realbuf, 0, bytesRead);
                pipeMsg.setData(realbuf);
                toLocalQueue.add(pipeMsg);
            }
        }  catch (IOException e) {
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

    // send data over local socket
    public void sendDataToLocal() {
        int sum = 0;
        try{
            while(true){
                // take msg from msgQueue and write it to C++ proxy
                PipeMsg toLocalMsg;
                toLocalMsg = toLocalQueue.take();
                byte [] data = toLocalMsg.getData();
                sum += data.length;
                //log.d(TAG,"DATA SENT TO LOCAL: " + data.length + " the sum is : " + sum);
                localOutputStream.write(data, 0, data.length);
            }
        }
        catch (IOException ioe) {
            ioe.printStackTrace();
        }
        catch (Exception e) {
            System.out.println(e);
        }
    }

    // recv data from local socket
    public void recvDataFromLocal() {
        byte[] buf = new byte[recvBufSize];
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

}
