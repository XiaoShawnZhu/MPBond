#include "stdafx.h"

#include "Storage.h"
#include "tools.h"

STORAGE::STORAGE() {
	maxSize = 32768;
	pData = new BYTE[maxSize];
	size = 0;
}

STORAGE::~STORAGE() {
	delete [] pData;
	pData = NULL;
}

void STORAGE::Clear() {
	size = 0;
}

BYTE * STORAGE::GetDataAt(int offset) {
	MyAssert(offset < size, 6);
	return pData + offset;
}

int STORAGE::PushData(const BYTE * pData, int size) {
	MyAssert(size != 0, 8);

	if (this->size + size >= maxSize) {
		int newMaxSize = (this->size + size) * 2;
		BYTE * pNewData = new BYTE[newMaxSize];
		memcpy(pNewData, this->pData, this->size);
		maxSize = newMaxSize;
		delete [] this->pData;
		this->pData = pNewData;
	}

	int r = this->size;

	memcpy(this->pData + this->size, pData, size);
	this->size += size;

	return r;
}


int STORAGE::PushData(BYTE data) {
	BYTE b[1];
	b[0] = data;
	return PushData(b, 1);
}

int STORAGE::PushData(STORAGE & s) {
	return PushData(s.GetDataAt(0), s.GetSize());
}

char * STORAGE::GetString() {
	*(pData + size) = 0;
	return (char *)pData;
}

void STORAGE::SetString(const char * str) {
	Clear();
	PushData((const BYTE *)str, strlen(str));
	PushData(0);
}

int STORAGE::SetData(STORAGE & s) {
	Clear();
	return PushData(s);
}

int STORAGE::SetData(const BYTE * pData, int size) {
	Clear();
	return PushData(pData, size);
}

void STORAGE::ShrinkBy(int size) {
	this->size -= size;
	MyAssert(this->size >=0, 1317);
	if (this->size < 0) this->size = 0;
}
