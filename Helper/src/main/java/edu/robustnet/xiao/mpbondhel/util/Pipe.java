package edu.robustnet.xiao.mpbondhel.util;

public class Pipe {

    public native String pipeSetupFromJNI(String subflowIP, String rpIP, int secNo, boolean isJava, int nSubflow);
    static {
        System.loadLibrary("proxy");
    }

    private byte[] data;
    private long seq;
    private int len;
    private long time;

    public byte[] getData(){
        return data;
    }

    public void setData(byte [] data)
    {
        this.data = data;
        this.len = data.length;
    }

    public void setData(byte [] data, long time, long seq)
    {
        this.data = data;
        this.seq = seq;
        this.time = time;
        this.len = data.length;
    }

    public long getTime(){return time;}

}

