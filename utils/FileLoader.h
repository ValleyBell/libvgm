#ifndef __FILELOADER_H__
#define __FILELOADER_H__

#include <stdtype.h>
#include <stdio.h>
#include <zlib.h>

#ifdef __cplusplus
extern "C" {
#endif

union LoaderHandles
{
	const void *dataPtr;
    FILE *hFileRaw;
    gzFile hFileGZ;
};

typedef union LoaderHandles LoaderHandles;

enum
{
	FLSTAT_EMPTY = 0,
	FLSTAT_LOADING = 1,
	FLSTAT_LOADED = 2
};

enum
{
	// mode: sources
	FLMODE_SRC_MEMORY = 0x01,
	FLMODE_SRC_FILE = 0x02,

	// mode: compression
	FLMODE_CMP_RAW = 0x00,
	FLMODE_CMP_GZ = 0x10
};

struct FileLoader {
	UINT8 _modeSrc;
	UINT8 _modeCompr;
	UINT8 _status;
	UINT32 _bytesTotal;
	UINT32 _bytesLoaded;
    UINT32 _readStopOfs;
    LoaderHandles _hLoad[1];
    UINT8 *_data;
};

typedef struct FileLoader FileLoader;

FileLoader *FileLoader_New(void);
void FileLoader_Delete(FileLoader *loader);
UINT8 FileLoader_Init(FileLoader *loader);
UINT8 *FileLoader_GetFileData(FileLoader *loader);
UINT32 FileLoader_GetTotalFileSize(FileLoader *loader);
UINT32 FileLoader_GetFileSize(FileLoader *loader);
UINT8 FileLoader_GetMode(FileLoader *loader);
UINT8 FileLoader_GetStatus(FileLoader *loader);
UINT8 FileLoader_ReadData(FileLoader *loader, UINT32 numBytes);
UINT8 FileLoader_CancelLoading(FileLoader *loader);
UINT8 FileLoader_LoadData(FileLoader *loader, UINT32 dataSize, const UINT8 *data);
UINT8 FileLoader_LoadFile(FileLoader *loader, const char *fileName);
UINT8 FileLoader_FreeData(FileLoader *loader);
void FileLoader_SetPreloadBytes(FileLoader *loader, UINT32 byteCount);
void FileLoader_ReadUntil(FileLoader *loader, UINT32 fileOffset);
void FileLoader_ReadFullFile(FileLoader *loader);

#ifdef __cplusplus
}
#endif

#endif /* __FILELOADER_H__ */
