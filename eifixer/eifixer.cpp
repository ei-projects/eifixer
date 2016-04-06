#include <Windows.h>
#include "resource.h"

#define GOTO_ERROR(error) { result = (error); goto __exit; }

#define FE_UNKNOWN -1
#define FE_SUCCESS 0
#define FE_ALREADY 1

#ifdef NDEBUG
#pragma comment(linker,"/NODEFAULTLIB")
#pragma comment(linker,"/ENTRY:WinMain")
#endif

TCHAR strSuccess[50], strError[50], strAlready[50];
int langDelta = RSRC_LANG_EN_DELTA;
#define LOCALIZED(StringID) ((StringID)+langDelta*100)

void prepareLangStrings()
{
	if (PRIMARYLANGID(GetUserDefaultLangID()) == LANG_RUSSIAN)
		langDelta = RSRC_LANG_RU_DELTA;

	LoadString(NULL, LOCALIZED(IDS_SUCCESS), strSuccess, _countof(strSuccess));
	LoadString(NULL, LOCALIZED(IDS_ERROR), strError, _countof(strError));
	LoadString(NULL, LOCALIZED(IDS_ALREADY), strAlready, _countof(strAlready));
}

unsigned int Crc32(unsigned char *buf, unsigned long len)
{
	unsigned long crc_table[256];
	unsigned long crc;
	for (int i = 0; i < 256; i++)
	{
		crc = i;
		for (int j = 0; j < 8; j++)
			crc = crc & 1 ? (crc >> 1) ^ 0xEDB88320UL : crc >> 1;
		crc_table[i] = crc;
	}

	crc = 0xFFFFFFFFUL;
	while (len--)
		crc = crc_table[(crc ^ *buf++) & 0xFF] ^ (crc >> 8);

	return crc ^ 0xFFFFFFFFUL;
}

LPTSTR appendStr(LPTSTR str1, LPCTSTR str2)
{
	int len1 = lstrlen(str1);
	int len2 = lstrlen(str2);

	LPTSTR newStr = (LPTSTR)LocalAlloc(0, (len1 + len2 + 1) * sizeof(*newStr));
	if (newStr == NULL)
		return NULL;

	lstrcpy(newStr, str1);
	lstrcat(newStr, str2);
	return newStr;
}

void trailPath(LPTSTR *path)
{
	int len = lstrlen(*path);
	if (len <= 0)
		return;

	if ((*path)[len-1] == '\\')
		return;
	if ((*path)[len-1] == '/')
	{
		(*path)[len-1] = '\\';
		return;
	}

	LPTSTR newPath = appendStr(*path, TEXT("\\"));
	if (newPath == NULL)
		return;

	LPTSTR oldPath = *path;
	*path = newPath;
	LocalFree(oldPath);
}

BOOL isFullPath(LPCTSTR path)
{
	return path != NULL && path[0] != '\0' && path[1] == ':';
}

LPTSTR regReadStr(HKEY hRoot, LPCTSTR key, LPCTSTR valueName)
{
	int result = 0;
	HKEY hKey = HKEY_CURRENT_USER;
	LPTSTR resultValue = NULL;

	if (RegOpenKeyEx(hRoot, key, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
		GOTO_ERROR(-1);

	DWORD valueSize, type;
	if (RegQueryValueEx(hKey, valueName, NULL, &type, NULL, &valueSize) != ERROR_SUCCESS)
		GOTO_ERROR(-1);

	if (type != REG_SZ)
		GOTO_ERROR(-1);

	resultValue = (LPTSTR)LocalAlloc(0, valueSize);
	if (resultValue == NULL)
		GOTO_ERROR(-1);

	if (RegQueryValueEx(hKey, valueName, NULL, &type, (LPBYTE)resultValue, &valueSize) != ERROR_SUCCESS)
		GOTO_ERROR(-1);

__exit:
	if (result != 0 && resultValue)
	{
		LocalFree(resultValue);
		resultValue = NULL;
	}
	if (hKey != HKEY_CURRENT_USER)
		RegCloseKey(hKey);

	return resultValue;
}

int regWriteStr(HKEY hRoot, LPCTSTR key, LPCTSTR valueName, LPCTSTR value)
{
	int result = 0;
	HKEY hKey = HKEY_CURRENT_USER;

	if (RegOpenKeyEx(hRoot, key, 0, KEY_WRITE, &hKey) != ERROR_SUCCESS)
		GOTO_ERROR(-1);

	if (RegSetValueEx(hKey, valueName, 0, REG_SZ, (LPCBYTE)value, (lstrlen(value) + 1) * sizeof(*value)) != ERROR_SUCCESS)
		GOTO_ERROR(-1);

__exit:
	if (hKey != HKEY_CURRENT_USER)
		RegCloseKey(hKey);

	return result;
}

LPTSTR getEIPath()
{
	LPTSTR eiPath = regReadStr(HKEY_CURRENT_USER,
		TEXT("Software\\Nival Interactive\\EvilIslands\\Path Settings"),
		TEXT("WORK PATH"));

	if (eiPath == NULL || !isFullPath(eiPath))
		return NULL;

	trailPath(&eiPath);
	return eiPath;
}

int fixGameExe(LPCTSTR gameExePath)
{
	int result = FE_SUCCESS;
	HANDLE hFile = INVALID_HANDLE_VALUE;
	DWORD fileSize = 0;
	BYTE *fileData = NULL;
	const DWORD PATCH_OFFSET = 0x56BB5;
	const DWORD ORIGINAL_CRC = 0x09EBC906;

	hFile = CreateFile(gameExePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
		GOTO_ERROR(FE_UNKNOWN);

	fileSize = GetFileSize(hFile, NULL);
	if (fileSize != 3825725)
		GOTO_ERROR(FE_UNKNOWN);

	fileData = (BYTE *)LocalAlloc(0, fileSize);
	if (fileData == NULL)
		GOTO_ERROR(FE_UNKNOWN);

	DWORD fileOpSize;
	if (!ReadFile(hFile, fileData, fileSize, &fileOpSize, NULL))
		GOTO_ERROR(FE_UNKNOWN);

	//if (fileData[PATCH_OFFSET] != 0xEB)
	//	GOTO_ERROR(FE_UNKNOWN);
	BOOL already = fileData[PATCH_OFFSET] == 0x74;
	fileData[PATCH_OFFSET] = 0x74;
	if (Crc32(fileData, fileSize) != ORIGINAL_CRC)
		GOTO_ERROR(FE_UNKNOWN);
	if (already)
		GOTO_ERROR(FE_ALREADY);

	if (SetFilePointer(hFile, PATCH_OFFSET, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
		GOTO_ERROR(FE_UNKNOWN);
	if (!WriteFile(hFile, fileData + PATCH_OFFSET, 1, &fileOpSize, NULL))
		GOTO_ERROR(FE_UNKNOWN);

__exit:
	if (hFile != INVALID_HANDLE_VALUE)
		CloseHandle(hFile);
	LocalFree(fileData);
	return result;
}

int fixEIPath(LPCTSTR eiPath)
{
	HKEY root = HKEY_CURRENT_USER;
	LPCTSTR key = TEXT("Software\\Nival Interactive\\EvilIslands\\Path Settings");
	int result1 = FE_ALREADY, result2 = FE_ALREADY;

	LPTSTR workPath, cdPath;
	workPath = regReadStr(root, key, TEXT("WORK PATH"));
	cdPath = regReadStr(root, key, TEXT("CD-ROM PATH"));

	if (lstrcmpi(workPath, eiPath) != 0)
	{
		if (regWriteStr(root, key, TEXT("WORK PATH"), eiPath) == 0)
			result1 = FE_SUCCESS;
		else
			result1 = FE_UNKNOWN;
	}

	if (lstrcmpi(cdPath, eiPath) != 0)
	{
		if (regWriteStr(root, key, TEXT("CD-ROM PATH"), eiPath) == 0)
			result2 = FE_SUCCESS;
		else
			result2 = FE_UNKNOWN;
	}

	LocalFree(workPath);
	LocalFree(cdPath);

	if (result1 == result2)
		return result1;
	else if (result1 == FE_UNKNOWN || result2 == FE_UNKNOWN)
		return FE_UNKNOWN;
	return FE_SUCCESS;
}

int fixAutorunpro(LPCTSTR autorunproPath)
{
	int result = FE_SUCCESS;
	HRSRC hRsrc = NULL;
	HGLOBAL hGlobal = NULL;
	LPCBYTE rsrcData = NULL;
	DWORD rsrcSize = 0;
	HANDLE hFile = INVALID_HANDLE_VALUE;
	BYTE *fileData = NULL;
	DWORD fileSize = 0;

	const DWORD AUTORUNPRO_GOG_CRC = 0xA952531A;
	const DWORD AUTORUNPRO_ORIG_CRC = 0x5b9aa0e0;

	if ((hRsrc = FindResource(NULL, MAKEINTRESOURCE(IDR_AUTORUNPRO), RT_RCDATA)) == NULL)
		GOTO_ERROR(FE_UNKNOWN);
	if ((hGlobal = LoadResource(NULL, hRsrc)) == NULL)
		GOTO_ERROR(FE_UNKNOWN);
	if ((rsrcData = (LPCBYTE)LockResource(hGlobal)) == NULL)
		GOTO_ERROR(FE_UNKNOWN);
	if ((rsrcSize = SizeofResource(NULL, hRsrc)) == 0)
		GOTO_ERROR(FE_UNKNOWN);

	hFile = CreateFile(autorunproPath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
		GOTO_ERROR(FE_UNKNOWN);

	fileSize = GetFileSize(hFile, NULL);
	if (fileSize != 1587)
		GOTO_ERROR(FE_UNKNOWN);

	fileData = (BYTE *)LocalAlloc(0, fileSize);
	if (fileData == NULL)
		GOTO_ERROR(FE_UNKNOWN);

	DWORD fileOpSize;
	if (!ReadFile(hFile, fileData, fileSize, &fileOpSize, NULL))
		GOTO_ERROR(FE_UNKNOWN);

	DWORD crc = Crc32(fileData, fileSize);
	if (crc == AUTORUNPRO_ORIG_CRC)
		GOTO_ERROR(FE_ALREADY);
	if (crc != AUTORUNPRO_GOG_CRC)
		GOTO_ERROR(FE_UNKNOWN);

	if (SetFilePointer(hFile, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
		GOTO_ERROR(FE_UNKNOWN);
	if (!WriteFile(hFile, rsrcData, rsrcSize, &fileOpSize, NULL))
		GOTO_ERROR(FE_UNKNOWN);

__exit:
	if (hFile != INVALID_HANDLE_VALUE)
		CloseHandle(hFile);
	LocalFree(fileData);

	return result;
}

int CALLBACK WinMain(
	HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPSTR     lpCmdLine,
	int       nCmdShow
)
{
	TCHAR buffer[1200], bufText[400], bufTitle[400];
	prepareLangStrings();

	LPTSTR eiPath = getEIPath();
	if (eiPath == NULL)
	{
		LoadString(NULL, LOCALIZED(IDS_EIPATH_ERROR), bufText, _countof(bufText));
		MessageBox(0, bufText, strError, MB_ICONERROR);
		return -1;
	}

	LPTSTR gameExePath = appendStr(eiPath, TEXT("game.exe"));
	LPTSTR autorunproPath = appendStr(eiPath, TEXT("autorunpro.reg"));

	int resultPath = 0, resultExe = 0, resultAutorun = 0;
	resultPath    = fixEIPath(eiPath);
	resultExe     = fixGameExe(gameExePath);
	resultAutorun = fixAutorunpro(autorunproPath);

	LoadString(NULL, LOCALIZED(IDS_FIXING_STATUS), bufTitle, _countof(bufTitle));
	LoadString(NULL, LOCALIZED(IDS_FIXING_FORMAT), bufText, _countof(bufText));
	wsprintf(buffer, bufText,
		resultPath    == FE_SUCCESS ? strSuccess : resultPath    == FE_ALREADY ? strAlready : strError,
		resultExe     == FE_SUCCESS ? strSuccess : resultExe     == FE_ALREADY ? strAlready : strError,
		resultAutorun == FE_SUCCESS ? strSuccess : resultAutorun == FE_ALREADY ? strAlready : strError
	);

	MessageBox(0, buffer, bufTitle, MB_ICONINFORMATION);

	LocalFree(eiPath);
	LocalFree(gameExePath);
	LocalFree(autorunproPath);

	return resultPath == 0 && resultExe == 0 && resultAutorun == 0 ? 0 : -1;
}
