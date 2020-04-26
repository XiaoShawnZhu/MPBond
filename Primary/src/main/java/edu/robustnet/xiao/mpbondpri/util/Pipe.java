package edu.robustnet.xiao.mpbondpri.util;

public class Pipe {

    public native String pipeSetupFromJNI(boolean isTether, int n, boolean isJava, String remoteIP, String rpIP);

    static {
        System.loadLibrary("proxy");
    }
}
