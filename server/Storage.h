#ifndef _STORAGE_H_
#define _STORAGE_H_

#include "proxy.h"

class STORAGE {
public:
	void Clear();
	int PushData(const BYTE * pData, int size);
	int PushData(BYTE data);
	int PushData(STORAGE & s);
	//int PushDataEx(const BYTE * pData, int size, int pktID);

	void ShrinkBy(int size);

	int SetData(const BYTE * pData, int size);
	int SetData(STORAGE & s);

	BYTE * GetDataAt(int offset);
	int GetSize() {return size;}
	//const char * GetPrintableText();
	//const char * GetPrintableText(int ofsBegin, int ofsEnd);

	//int ReadFromFile(const char * filename);
	//int WriteToFile(FILE * ofs);
	char * GetString();
	void SetString(const char * str);

	STORAGE();
	~STORAGE();

private:
	BYTE * pData;	
	int size;
	int maxSize;
};

#endif
