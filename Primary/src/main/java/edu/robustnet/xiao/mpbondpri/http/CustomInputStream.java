package edu.robustnet.xiao.mpbondpri.http;

import android.util.Pair;

import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.Iterator;

public class CustomInputStream extends InputStream {
    private static final int DEFAULT_MAX_BUFFER_SIZE_BYTE = 10 * 1024 * 1025;
    protected volatile byte buf[];
    protected volatile int count;//TODO check
    protected int pos;
    protected volatile int avail;
    private  ArrayList<Pair<Integer, Integer>> availChunks;

    public CustomInputStream(){
        buf = new byte[DEFAULT_MAX_BUFFER_SIZE_BYTE];
        this.count = DEFAULT_MAX_BUFFER_SIZE_BYTE;
        availChunks = new ArrayList<>();
    }

    public CustomInputStream(int size){
        if (size <= 0) {
            throw new IllegalArgumentException("Buffer size <= 0");
        }
        buf = new byte[size];
        this.count = size;
        availChunks = new ArrayList<>();
    }

    public void setSize(int size){
        if (size <= 0) {
            throw new IllegalArgumentException("Buffer size <= 0");
        }
        buf = new byte[size];
        this.count = size;
        availChunks = new ArrayList<>();
    }


    private void printChunks(int i, int start, int len){

        String out="";
        for (Pair<Integer, Integer> p : availChunks){
            out+="("+p.first+","+p.second+") ";
        }
//        Log.d("HTTP", i+" "+ pos+" "+avail+" "+out +" "+start+" "+len);
    }

    private void updateAvailableChunks(int start, int len, int byteRangeStart, int byteRangeEnd){

        //merge with existing
//        printChunks(1, start, len);
        boolean merged=false;
        for (int i = 0; i<availChunks.size(); i++) {
            Pair p = availChunks.get(i);
//            if((start >= (int) p.first && start <= (int) p.second)){ //|| (start + len >= (int) p.first && start + len <= (int) p.second)
            if(start>(int) p.first && start <=(int) p.second){

                if(start+len <= (int) p.second){
                    return;
                }

                int newStart = Math.min(start, (int) p.first);
                int newEnd = Math.max(start + len, (int) p.second);
                Pair newPair = new Pair(newStart, newEnd);
                availChunks.set(i, newPair);
                merged=true;
                break;
            }

        }

        if (start+len<byteRangeEnd && start!=byteRangeStart && merged){
            if(start==avail){
                avail+=len;
            }
//            printChunks(2, start, len);
//            return;
        }
        if(start+len==count){
            if(start==avail){
                avail+=len;
            }
//            printChunks(3, start, len);
            return;
        }


        //if not added, add it, and sort it

        if (!merged){
            availChunks.add(new Pair<Integer, Integer>(start, start+len));
            if(start==avail){
                avail+=len;
            }
            Collections.sort(availChunks, new Comparator<Pair<Integer, Integer>>() {
                @Override
                public int compare(Pair<Integer, Integer> o1, Pair<Integer, Integer> o2) {
                    if (o1.first==o2.first){
                        return o1.second.compareTo(o2.second);
                    }else{
                        return o1.first.compareTo(o2.first);
                    }
                }
            });
        }

        //merge with the next one
        int i=0;
        Pair prev=null;
        for (Iterator<Pair<Integer, Integer>> iterator = availChunks.iterator(); iterator.hasNext();) {
            Pair p = iterator.next();
            if(prev!=null){
                if((int) prev.second >= (int)p.first && (int) prev.second <=(int)p.second ){
                    int newStart = Math.min((int) prev.first, (int) p.first);
                    int newEnd = Math.max((int) prev.second, (int) p.second);
                    Pair newPair = new Pair(newStart, newEnd);
                    availChunks.set(i-1, newPair);
                    if (avail>= newStart && avail<=newEnd){
                        avail=newEnd;
                    }

                    iterator.remove();
                }else{
                    if (avail>= (int)p.first && avail<=(int)p.second){
                        avail=(int)p.second;
                    }

                }
            }else{
                if (avail>= (int)p.first && avail<=(int)p.second){
                    avail=(int)p.second;
                }
            }
            prev=p;
            i+=1;
        }
//        printChunks(4, start, len);
        return;

    }

    //TODO implement close

    /**
     * Check to make sure that buffer has not been nulled out due to
     * close; if not return it;
     */
    private byte[] getBufIfOpen() throws IOException {
        byte[] buffer = buf;
        if (buffer == null)
            throw new IOException("Stream closed");
        return buffer;
    }

    public synchronized void fill(byte b[], int off, int len, int byteRangeStart, int byteRangeEnd) throws IOException {

        if(off+len<avail){
            return;
        }

        byte[] buffer = getBufIfOpen();
//        assert off>=avail;
//        assert len<=count-avail;
        System.arraycopy(b, 0, buffer, off, len);
        updateAvailableChunks(off, len, byteRangeStart, byteRangeEnd);

    }

    @Override
    public  int read() throws IOException {
        if (pos >= count)
            return -1;
        while(pos>=avail){}//TODO -----------
        return getBufIfOpen()[pos++] & 0xff;
    }


    /*
    The number of bytes actually read is returned as an integer.
    <p> This method blocks until input data is available, end of file is
     * detected, or an exception is thrown.
     */
    @Override
    public  int read(byte b[], int off, int len) throws IOException {
//        if (count == 0){
//            return 0;
//        }
//        getBufIfOpen(); // Check for closed stream

        if (off < 0 || len < 0 || len > b.length - off) {
            throw new IndexOutOfBoundsException();
        } else if (len == 0) {
            return 0;
        }

        //new implementation
        if (pos >= count)
            return -1;
//        if (pos>=avail){
//            return 0;
//        }
        while(pos>=avail){}//TODO -----------
        int c=avail-pos;
        if (c>len){
            c=len;
        }
        byte[] buffer=getBufIfOpen();
        System.arraycopy(buffer, pos, b, off, c);
        pos+=c;
        return c;


    }

    public void close(){
//        buf=null;
        availChunks.clear();
    }


}