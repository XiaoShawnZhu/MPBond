package edu.robustnet.xiao.mpbondpri.http;

import android.net.ConnectivityManager;
import android.net.Network;

import java.util.concurrent.CountDownLatch;

public class ConnectivityNetworkCallback extends ConnectivityManager.NetworkCallback{
    private CountDownLatch latch;
    private ConnectivityManager cm;
    private Network network;
    public ConnectivityNetworkCallback(CountDownLatch l, ConnectivityManager cm){
        this.latch=l;
        this.cm=cm;
    }

    public ConnectivityManager getConnectivityManager(){
        return this.cm;
    }
    public Network getNetwork(){
        return this.network;
    }
    @Override
    public void onAvailable(Network network) {
        super.onAvailable(network);
//                cm.bindProcessToNetwork(network);
        this.latch.countDown();
        this.network= network;
//			this.cm.unregisterNetworkCallback(this);
    }
}
