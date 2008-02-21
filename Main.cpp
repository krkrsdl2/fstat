#include "ncbind/ncbind.hpp"
#include <string>
using namespace std;
#include <tchar.h>
#include <shlobj.h>

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

static const tjs_nchar * StoragesFstatPreScript	= TJS_N("\
global.FILE_ATTRIBUTE_READONLY = 0x00000001,\
global.FILE_ATTRIBUTE_HIDDEN = 0x00000002,\
global.FILE_ATTRIBUTE_SYSTEM = 0x00000004,\
global.FILE_ATTRIBUTE_DIRECTORY = 0x00000010,\
global.FILE_ATTRIBUTE_ARCHIVE = 0x00000020,\
global.FILE_ATTRIBUTE_NORMAL = 0x00000080,\
global.FILE_ATTRIBUTE_TEMPORARY = 0x00000100;");

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

		// OS�l�C�e�B�u�ȕ\���ɕϊ�
		dir = TVPNormalizeStorageName(dir);
		if (dir.GetLastChar() != TJS_W('/')) {
			TVPThrowExceptionMessage(TJS_W("'/' must be specified at the end of given directory name."));
		}
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

	/**
	 * �w��f�B���N�g�����폜����
	 * @param dir �f�B���N�g����
	 * @return ���ۂɍ폜���ꂽ�� true
	 * ���Ƀt�@�C���������ꍇ�̂ݍ폜����܂�
	 */
	static bool removeDirectory(ttstr dir) {

		if (dir.GetLastChar() != TJS_W('/')) {
			TVPThrowExceptionMessage(TJS_W("'/' must be specified at the end of given directory name."));
		}

		// OS�l�C�e�B�u�ȕ\���ɕϊ�
		dir = TVPNormalizeStorageName(dir);
		TVPGetLocalName(dir);

		return RemoveDirectory(dir.c_str()) == TRUE;
	}

	/**
	 * �f�B���N�g���̍쐬
	 * @param dir �f�B���N�g����
	 * @return ���ۂɍ쐬�ł����� true
	 */
	static bool createDirectory(ttstr dir)
	{
		if(dir.GetLastChar() != TJS_W('/'))
		{
			TVPThrowExceptionMessage(TJS_W("'/' must be specified at the end of given directory name."));
		}
		dir	= TVPNormalizeStorageName(dir);
		TVPGetLocalName(dir);
		return CreateDirectory(dir.c_str(), NULL) == TRUE;
	}

	/**
	 * �t�@�C���̑�����ݒ肷��
	 * @param filename �t�@�C��/�f�B���N�g����
	 * @param attr �ݒ肷�鑮��
	 * @return ���ۂɕύX�ł����� true
	 */
	static bool setFileAttributes(ttstr filename, DWORD attr)
	{
		filename	= TVPNormalizeStorageName(filename);
		TVPGetLocalName(filename);

		attr	= attr & (FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN |
			FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_TEMPORARY);

		DWORD	orgattr = GetFileAttributes(filename.c_str());

		return SetFileAttributes(filename.c_str(), orgattr | attr) == TRUE;
	}

	/**
	 * �t�@�C���̑�������������
	 * @param filename �t�@�C��/�f�B���N�g����
	 * @param attr �������鑮��
	 * @return ���ۂɕύX�ł����� true
	 */
	static bool resetFileAttributes(ttstr filename, DWORD attr)
	{
		filename	= TVPNormalizeStorageName(filename);
		TVPGetLocalName(filename);

		attr	= attr & (FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN |
			FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_TEMPORARY);

		DWORD	orgattr = GetFileAttributes(filename.c_str());

		return SetFileAttributes(filename.c_str(), orgattr & ~attr) == TRUE;
	}

	/**
	 * �t�@�C���̑������擾����
	 * @param filename �t�@�C��/�f�B���N�g����
	 * @return �擾��������
	 */
	static DWORD getFileAttributes(ttstr filename)
	{
		filename	= TVPNormalizeStorageName(filename);
		TVPGetLocalName(filename);

		return GetFileAttributes(filename.c_str());
	}

	/**
	 * �t�H���_�I���_�C�A���O���J��
	 * @param window �E�B���h�E
	 * @param caption �L���v�V����
	 * @param initialDir �����f�B���N�g��
	 * @param rootDir ���[�g�f�B���N�g��
	 */
	static tjs_error TJS_INTF_METHOD selectDirectory(
		tTJSVariant	*result,
		tjs_int numparams,
		tTJSVariant **param,
		iTJSDispatch2 *objthis)
	{
		if(numparams < 1)
			return TJS_E_BADPARAMCOUNT;

		iTJSDispatch2*	tmp;
		tTJSVariant		val;
		BROWSEINFO		bi;
		ITEMIDLIST*		rootidl	= NULL;
		tjs_char		folder[2048+1];
		memset(&bi, 0, sizeof(bi));

		//	HWND
		iTJSDispatch2*	elm	= param[0]->AsObjectNoAddRef();
		if(elm->IsValid(0, L"window", NULL, elm) == TJS_S_TRUE &&
			TJS_SUCCEEDED(elm->PropGet(0, L"window", NULL, &val, elm)) &&
			val.IsInstanceOf(L"Window"))
		{
			tmp	= val.AsObjectNoAddRef();
			if(TJS_SUCCEEDED(tmp->PropGet(0, L"HWND", NULL, &val, tmp)))
				bi.hwndOwner	= (HWND)val.AsInteger();
			else
				bi.hwndOwner	= TVPGetApplicationWindowHandle();
		}
		else
			bi.hwndOwner	= NULL;

		//	title
		if(elm->IsValid(0, L"title", NULL, elm) == TJS_S_TRUE &&
			TJS_SUCCEEDED(elm->PropGet(0, L"title", NULL, &val, elm)))
		{
			ttstr	title	= val.AsStringNoAddRef();
			bi.lpszTitle	= title.c_str();
		}
		else
			bi.lpszTitle	= NULL;

		//	name
		bi.pszDisplayName	= NULL;
		bi.ulFlags	= BIF_RETURNONLYFSDIRS;/* BIF_NEWDIALOGSTYLE */
		if(elm->IsValid(0, L"name", NULL, elm) == TJS_S_TRUE &&
			TJS_SUCCEEDED(elm->PropGet(0, L"name", NULL, &val, elm)) &&
			!val.NormalCompare(ttstr(L"")))
		{
			ttstr	name	= TVPNormalizeStorageName(val.AsStringNoAddRef());
			TVPGetLocalName(name);
			_tcscpy_s(folder, 2048, name.c_str());
		}
		else
			folder[0]	= 0;

		//	root dir
		if(elm->IsValid(0, L"rootDir", NULL, elm) == TJS_S_TRUE &&
			TJS_SUCCEEDED(elm->PropGet(0, L"rootDir", NULL, &val, elm)))
		{
			ttstr	rootDir	= TVPNormalizeStorageName(val.AsStringNoAddRef());
			TVPGetLocalName(rootDir);
			rootidl	= Path2ITEMIDLIST(rootDir.c_str());
		}
		bi.pidlRoot	= rootidl;

		bi.lpfn		= SelectDirectoryCallBack;
		bi.lParam	= (LPARAM)folder;

		ITEMIDLIST*	pidl;
		if((pidl = ::SHBrowseForFolder(&bi)) != NULL)
		{
			if(::SHGetPathFromIDList(pidl, folder) != TRUE)
				*result	= 0;
			else
			{
				*result	= TJS_S_TRUE;
				val	= folder;
				elm->PropSet(0, L"name", NULL, &val, elm);
			}
			FreeITEMIDLIST(pidl);
		}
		else
			*result	= 0;
		FreeITEMIDLIST(rootidl);

		return TJS_S_OK;
	}

	/**
	 * �f�B���N�g���̑��݃`�F�b�N
	 * @param directory �f�B���N�g����
	 * @return �f�B���N�g�������݂���� true/���݂��Ȃ���� -1/�f�B���N�g���łȂ���� false
	 */
	static tjs_error TJS_INTF_METHOD isExistentDirectory(
		tTJSVariant	*result,
		tjs_int numparams,
		tTJSVariant **param,
		iTJSDispatch2 *objthis)
	{
		if(numparams < 1) return TJS_E_BADPARAMCOUNT;

		if(result == NULL)
			return TJS_S_OK;

		if(param[0]->NormalCompare(ttstr(L"")))
		{
			*result	= (tTVInteger)-1;
			return TJS_S_OK;
		}

		ttstr	dir = TVPNormalizeStorageName(param[0]->AsStringNoAddRef());
		TVPGetLocalName(dir);
		DWORD	attr;
		if(dir[dir.length()-1] != TJS_W('\\'))
		{
			attr	= GetFileAttributes(dir.c_str());
		}
		else
		{	// �Ō����\���t���Ă���Ɛ���������ł��Ȃ��̂Ŏ�菜��
			tjs_int len = dir.length(), i;
			tjs_char* path = new tjs_char[len];
			const tjs_char* dp = dir.c_str();
			len--;
			for(i=0; i<len; i++)
				path[i]	= dp[i];
			path[i]	= 0;
			attr	= GetFileAttributes(path);
		}
		if(attr == 0xFFFFFFFF)
			*result	= (tTVInteger)-1;	//	���݂��Ȃ�
		else if((attr & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY)
			*result	= (tTVInteger)TJS_S_TRUE;	//	���݂���
		else
			*result	= (tTVInteger)0;	//	�f�B���N�g���ł͂Ȃ�

		return TJS_S_OK;
	}

private:
	//	�w��̃p�X����ITEMIDLIST���擾
	static ITEMIDLIST*	Path2ITEMIDLIST(const tjs_char* path)
	{
		IShellFolder* isf;
		ITEMIDLIST* pidl;
		if(SUCCEEDED(::SHGetDesktopFolder(&isf)))
		{
			ULONG	tmp;
			if(SUCCEEDED(isf->ParseDisplayName(NULL, NULL, (LPOLESTR)path, &tmp, &pidl, &tmp)))
				return pidl;
		}
		return NULL;
	}

	//	ITEMIDLIST�����
	static void	FreeITEMIDLIST(ITEMIDLIST* pidl)
	{
		IMalloc*	im;
		if(::SHGetMalloc(&im) != NOERROR)
			im	= NULL;
		if(im != NULL)
			im->Free((void*)pidl);
	}

	//	SHBrowserForFolder�̃R�[���o�b�N�֐�
	static int	CALLBACK SelectDirectoryCallBack(HWND hwnd, UINT msg, LPARAM lparam, LPARAM lpdata)
	{
		//	��������
		if(msg == BFFM_INITIALIZED)
		{
			//	�����t�H���_���w��
			ITEMIDLIST*	pidl;
			pidl	= Path2ITEMIDLIST((tjs_char*)lpdata);
			if(pidl != NULL)
			{
				::SendMessage(hwnd, BFFM_SETSELECTION, FALSE, (LPARAM)pidl);
				FreeITEMIDLIST(pidl);
			}

			//	�őO�ʂֈړ�
			SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE);
		}
		return 0;
	}
};

NCB_ATTACH_CLASS(StoragesFstat, Storages) {
	RawCallback("fstat",               &Class::fstat,               TJS_STATICMEMBER);
	NCB_METHOD(exportFile);
	NCB_METHOD(deleteFile);
	NCB_METHOD(dirlist);
	NCB_METHOD(removeDirectory);
	NCB_METHOD(createDirectory);
	NCB_METHOD(setFileAttributes);
	NCB_METHOD(resetFileAttributes);
	NCB_METHOD(getFileAttributes);
	RawCallback("selectDirectory",     &Class::selectDirectory,     TJS_STATICMEMBER);
	RawCallback("isExistentDirectory", &Class::isExistentDirectory, TJS_STATICMEMBER);
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
	TVPExecuteExpression(StoragesFstatPreScript);
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
