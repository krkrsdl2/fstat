#include "ncbind/ncbind.hpp"
#include <string>
using namespace std;

// Date �N���X�����o
static iTJSDispatch2 *dateClass = NULL;    // Date �̃N���X�I�u�W�F�N�g
static iTJSDispatch2 *dateSetTime = NULL;  // Date.setTime ���\�b�h

/**
 * �t�@�C�������� Date �N���X�ɂ��ĕۑ�
 * @param store �i�[��
 * @param filetime �t�@�C������
 */
static void storeDate(tTJSVariant &store, FILETIME &filetime, iTJSDispatch2 *objthis)
{
	// �t�@�C��������
	tjs_uint64 ft = filetime.dwHighDateTime * 0x100000000 | filetime.dwLowDateTime;
	if (ft > 0) {
		iTJSDispatch2 *obj;
		if (TJS_SUCCEEDED(dateClass->CreateNew(0, NULL, NULL, &obj, 0, NULL, objthis))) {
			// UNIX TIME �ɕϊ�
			tjs_int64 unixtime = (ft - 0x19DB1DED53E8000 ) / 10000;
			tTJSVariant time(unixtime);
			tTJSVariant *param[] = { &time };
			dateSetTime->FuncCall(0, NULL, NULL, NULL, 1, param, obj);
			store = tTJSVariant(obj, obj);
			obj->Release();
		}
	}
}

/**
 * ���\�b�h�ǉ��p
 */
class StoragesFstat {

public:
	StoragesFstat(){};

	/**
	 * �w�肳�ꂽ�t�@�C���̏����擾����
	 * @param filename �t�@�C����
	 * @return 
	 */
	static tjs_error TJS_INTF_METHOD fstat(tTJSVariant *result,
										   tjs_int numparams,
										   tTJSVariant **param,
										   iTJSDispatch2 *objthis) {
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		ttstr filename = TVPGetPlacedPath(*param[0]);
		if (filename.length()) {
			if (!wcschr(filename.c_str(), '>')) {
				// ���t�@�C�������݂���ꍇ
				TVPGetLocalName(filename);
				HANDLE hFile;
				if ((hFile = CreateFile(filename.c_str(), GENERIC_READ, 0, NULL ,
										OPEN_EXISTING , FILE_ATTRIBUTE_NORMAL , NULL)) != INVALID_HANDLE_VALUE) {
					tTJSVariant size;
					tTJSVariant mtime;
					tTJSVariant ctime;
					tTJSVariant atime;

					LARGE_INTEGER fsize;
					if (GetFileSizeEx(hFile, &fsize)) {
						size = (tjs_int64)fsize.QuadPart;
					}
					FILETIME c, a, m;
					if (GetFileTime(hFile , &c, &a, &m)) {
						storeDate(atime, a, objthis);
						storeDate(ctime, c, objthis);
						storeDate(mtime, m, objthis);
					}

					if (result) {
						iTJSDispatch2 *dict;
						if ((dict = TJSCreateDictionaryObject()) != NULL) {
							dict->PropSet(TJS_MEMBERENSURE, L"size",  NULL, &size, dict);
							dict->PropSet(TJS_MEMBERENSURE, L"mtime", NULL, &mtime, dict);
							dict->PropSet(TJS_MEMBERENSURE, L"ctime", NULL, &ctime, dict);
							dict->PropSet(TJS_MEMBERENSURE, L"atime", NULL, &atime, dict);
							*result = dict;
							dict->Release();
						}
					}
					CloseHandle(hFile);
					return TJS_S_OK;
				}
			} else {
				IStream *in = TVPCreateIStream(filename, TJS_BS_READ);
				if (in) {
					STATSTG stat;
					in->Stat(&stat, STATFLAG_NONAME);
					tTJSVariant size((tjs_int64)stat.cbSize.QuadPart);
					if (result) {
						iTJSDispatch2 *dict;
						if ((dict = TJSCreateDictionaryObject()) != NULL) {
							dict->PropSet(TJS_MEMBERENSURE, L"size",  NULL, &size, dict);
							*result = dict;
							dict->Release();
						}
					}
					in->Release();
					return TJS_S_OK;
				}
			}
		}

		TVPThrowExceptionMessage((ttstr(TJS_W("cannot open : ")) + param[0]->GetString()).c_str());
		return TJS_S_OK;
	}

	/**
	 * �g���g���̃X�g���[�W��Ԓ��̃t�@�C���𒊏o����
	 * @param src �ۑ����t�@�C��
	 * @param dest �ۑ���t�@�C��
	 */
	static void exportFile(const char *src, const char *dest) {
		ttstr filename = src;
		IStream *in = TVPCreateIStream(filename, TJS_BS_READ);
		if (in) {
			ttstr storename = dest;
			IStream *out = TVPCreateIStream(storename, TJS_BS_WRITE);
			if (out) {
				BYTE buffer[1024*16];
				DWORD size;
				while (in->Read(buffer, sizeof buffer, &size) == S_OK && size > 0) {			
					out->Write(buffer, size, &size);
				}
				out->Release();
				in->Release();
			} else {
				in->Release();
				TVPThrowExceptionMessage((ttstr(TJS_W("cannot open storefile: ")) + storename).c_str());
			}
		} else {
			TVPThrowExceptionMessage((ttstr(TJS_W("cannot open readfile: ")) + filename).c_str());
		}
	}

	/**
	 * �g���g���̃X�g���[�W��Ԓ��̎w��t�@�C�����폜����B
	 * @param file �폜�Ώۃt�@�C��
	 * @return ���ۂɍ폜���ꂽ�� true
	 * ���t�@�C��������ꍇ�̂ݍ폜����܂�
	 */
	static bool deleteFile(ttstr filename) {
		filename = TVPGetPlacedPath(filename);
		if (filename.length() && !wcschr(filename.c_str(), '>')) {
			TVPGetLocalName(filename);
			if (DeleteFile(filename.c_str())) {
				return true;
			}
		}
		return false;
	}

	/**
	 * �w��f�B���N�g���̃t�@�C���ꗗ���擾����
	 * @param dir �f�B���N�g����
	 * @return �t�@�C�����ꗗ���i�[���ꂽ�z��
	 */
	static tTJSVariant dirlist(ttstr dir) {

		if (dir.GetLastChar() != TJS_W('/')) {
			TVPThrowExceptionMessage(TJS_W("'/' must be specified at the end of given directory name."));
		}
		
		// OS�l�C�e�B�u�ȕ\���ɕϊ�
		dir = TVPNormalizeStorageName(dir);
		TVPGetLocalName(dir);

		// Array �N���X�̃I�u�W�F�N�g���쐬
		iTJSDispatch2 * array = TJSCreateArrayObject();
		tTJSVariant result;
		
		try {
			ttstr wildcard = dir + "*.*";
			WIN32_FIND_DATA data;
			HANDLE handle = FindFirstFile(wildcard.c_str(), &data);
			if (handle != INVALID_HANDLE_VALUE) {
				tjs_int count = 0;
				do {
					ttstr file = dir;
					file += data.cFileName;
					if (GetFileAttributes(file.c_str()) & FILE_ATTRIBUTE_DIRECTORY) {
						// �f�B���N�g���̏ꍇ�͍Ō�� / ������
						file = data.cFileName;
						file += "/";
					} else {
						// ���ʂ̃t�@�C���̏ꍇ�͂��̂܂�
						file = data.cFileName;
					}
					// �z��ɒǉ�����
					tTJSVariant val(file);
					array->PropSetByNum(0, count++, &val, array);
				} while(FindNextFile(handle, &data));
				FindClose(handle);
			} else {
				TVPThrowExceptionMessage(TJS_W("Directory not found."));
			}
			result = tTJSVariant(array, array);
			array->Release();
		} catch(...) {
			array->Release();
			throw;
		}

		return result;
	}

};

NCB_ATTACH_CLASS(StoragesFstat, Storages) {
	RawCallback("fstat", &StoragesFstat::fstat, TJS_STATICMEMBER);
	NCB_METHOD(exportFile);
	NCB_METHOD(deleteFile);
	NCB_METHOD(dirlist);
};

/**
 * �o�^������
 */
static void PostRegistCallback()
{
	tTJSVariant var;
	TVPExecuteExpression(TJS_W("Date"), &var);
	dateClass = var.AsObject();
	TVPExecuteExpression(TJS_W("Date.setTime"), &var);
	dateSetTime = var.AsObject();
}

#define RELEASE(name) name->Release();name= NULL

/**
 * �J�������O
 */
static void PreUnregistCallback()
{
	RELEASE(dateClass);
	RELEASE(dateSetTime);
}

NCB_POST_REGIST_CALLBACK(PostRegistCallback);
NCB_PRE_UNREGIST_CALLBACK(PreUnregistCallback);
