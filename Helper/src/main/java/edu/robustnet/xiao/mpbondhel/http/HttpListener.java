package edu.robustnet.xiao.mpbondhel.http;

public interface HttpListener {
//    void onTransferEnd(HttpHelper.NetType netType, int byteRangeStart, int byteRangeEnd);
    void onBytesTransferred(byte b[], int offset, int len, HttpHelper.NetType netType, int byteRangeStart, int byteRangeEnd);
}
