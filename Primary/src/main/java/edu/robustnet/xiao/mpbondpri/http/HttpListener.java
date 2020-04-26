package edu.robustnet.xiao.mpbondpri.http;

import java.io.InputStream;
import java.io.OutputStream;

public interface HttpListener {
    void onTransferEnd(HttpPrimary.NetType netType);
    void onPipeTransferEnd(HttpPrimary.NetType netType, int byteRangeStart, int byteRangeEnd, InputStream pipeInputStream, OutputStream pipeOutputStream);
    void onBytesTransferred(byte b[], int offset, int len, HttpPrimary.NetType netType, int byteRangeStart, int byteRangeEnd);
}
