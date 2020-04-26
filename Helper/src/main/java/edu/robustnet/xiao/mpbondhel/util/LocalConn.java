package edu.robustnet.xiao.mpbondhel.util;

public class LocalConn {

    public native String connSetupFromJNI();
    static {
        System.loadLibrary("proxy");
    }

}
