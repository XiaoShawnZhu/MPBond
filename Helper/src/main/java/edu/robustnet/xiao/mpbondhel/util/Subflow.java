package edu.robustnet.xiao.mpbondhel.util;

public class Subflow {

    public native String subflowFromJNI(String subflowIP, String rpIP, int feedbackType, int subflowId, int nSubflow);
    static {
        System.loadLibrary("proxy");
    }

}
