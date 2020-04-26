package edu.robustnet.xiao.mpbondpri.util;

public class Subflow {

    private static final String TAG = "Subflow";

    public native String subflowFromJNI(String rpIP, int feedbackType);
    static {
        System.loadLibrary("proxy");
    }

}
