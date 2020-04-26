package edu.robustnet.xiao.mpbondpri.util;

public class LocalConn {

    private static final String TAG = "LocalConn";

//    @Override
//    public void run() {
//        Log.d(TAG, connFromJNI());
//    }

    public native String connSetupFromJNI();
    static {
        System.loadLibrary("proxy");
    }

}