#ifndef PE_VGM_INTERNAL
#define PE_VGM_INTERNAL

#ifdef __cplusplus
extern "C"
{
#endif

#include "../stdtype.h"
#include "engine_base.h"
#include "engine_vgm.h"
#include "../emu/EmuStructs.h"		// for DEVFUNC_*

#include "../player/dblk_compr.h"	// for PCM_COMPR_TBL
#include "../utils/DataLoader.h"	// for DATA_LOADER
#include "../utils/StrUtils.h"		// for CPCONV
#include "../player/helper.h"		// for VGM_BASEDEV
#include "../emu/logging.h"			// for DEV_LOGGER


DEFINE_PE_ARRAY(UINT8, ARR_UINT8)
DEFINE_PE_VECTOR(UINT8, VEC_UINT8)
DEFINE_PE_VECTOR(UINT32, VEC_UINT32)
typedef struct vgm_device_config
{
	size_t deviceID;	// index for devices array
	UINT8 vgmChipType;
	DEV_ID type;
	UINT8 instance;
	ARR_UINT8 cfgData;
} VGM_DEVCFG;	// SONG_DEV_CFG
typedef struct device_log_callback_data
{
	PE_VGM* player;
	size_t chipDevID;
} DEVLOG_CB_DATA;
typedef struct vgm_chip_device
{
	VGM_BASEDEV base;
	UINT8 vgmChipType;
	DEV_ID chipType;
	UINT8 chipID;
	UINT32 flags;
	size_t optID;
	size_t cfgID;
	DEVFUNC_READ_A8D8 read8;		// read 8-bit data from 8-bit register/offset (required by K007232)
	DEVFUNC_WRITE_A8D8 write8;		// write 8-bit data to 8-bit register/offset
	DEVFUNC_WRITE_A16D8 writeM8;	// write 8-bit data to 16-bit memory offset
	DEVFUNC_WRITE_A8D16 writeD16;	// write 16-bit data to 8-bit register/offset
	DEVFUNC_WRITE_A16D16 writeM16;	// write 16-bit data to 16-bit register/offset
	DEVFUNC_WRITE_MEMSIZE romSize;
	DEVFUNC_WRITE_BLOCK romWrite;
	DEVFUNC_WRITE_MEMSIZE romSizeB;
	DEVFUNC_WRITE_BLOCK romWriteB;
	DEVLOG_CB_DATA logCbData;
} VGM_CHIPDEV;	// CHIP_DEVICE

typedef struct extra_header_data32
{
	UINT8 type;
	UINT32 data;
} XHDR_DATA32;
typedef struct extra_header_data16
{
	UINT8 type;
	UINT8 flags;
	UINT16 data;
} XHDR_DATA16;

typedef struct pcm_data_bank
{
	VEC_UINT8 data;
	VEC_UINT32 bankOfs;
	VEC_UINT32 bankSize;
} PCM_BANK;

typedef struct vgm_command_info COMMAND_INFO;
typedef void (*COMMAND_FPTR)(PE_VGM* self, const COMMAND_INFO* ci);	// VGM command member function callback
struct vgm_command_info
{
	UINT8 chipType;
	UINT32 cmdLen;
	COMMAND_FPTR func;
};
#define COMMAND_FUNC(x)	void x(PE_VGM* self, const COMMAND_INFO* ci)

typedef struct qsound_work_memory
{
	void (*write)(VGM_CHIPDEV*, UINT8, UINT16);	// pointer to WriteQSound_A/B
	UINT16 startAddrCache[16];	// QSound register 0x01
	UINT16 pitchCache[16];		// QSound register 0x02
} QSOUND_WORK;

#define HDR_BUF_SIZE	0x100
#define OPT_DEV_COUNT	0x30
#define CHIP_COUNT		0x30
#define PCM_BANK_COUNT	0x40

DEFINE_PE_ARRAY(VGM_DEVCFG, ARR_DEVCFG)
DEFINE_PE_ARRAY(VGM_CHIPDEV, ARR_CHIPDEV)
DEFINE_PE_ARRAY(XHDR_DATA16, ARR_XHDR_D16)
DEFINE_PE_ARRAY(XHDR_DATA32, ARR_XHDR_D32)
DEFINE_PE_ARRAY(const char*, ARR_CSTR)
DEFINE_PE_ARRAY(char*, ARR_STR)	// vector of strings
DEFINE_PE_VECTOR(const char*, VEC_CSTR)
DEFINE_PE_VECTOR(char, VSTR)	// string represented using the VECTOR struct
DEFINE_PE_VECTOR(VGM_PCMSTRM_DEV, VEC_PSTRM_DEV)

struct player_engine_vgm
{
	PEBASE pe;

	CPCONV* cpcUTF16;	// UTF-16 LE -> UTF-8 codepage conversion
	DEV_LOGGER logger;
	DATA_LOADER *dLoad;
	const UINT8* fileData;	// data pointer for quick access, equals self->dLoad->GetFileData().data()
	ARR_UINT8 yrwRom;	// cache for OPL4 sample ROM (yrw801.rom)
	UINT8 shownCmdWarnings[0x100];

	VGM_HEADER fileHdr;
	ARR_XHDR_D32 xHdrChipClk;
	ARR_XHDR_D16 xHdrChipVol;
	UINT8 hdrBuffer[HDR_BUF_SIZE];	// buffer containing the file header
	UINT32 hdrLenFile;
	UINT32 tagVer;

	UINT32 totalTicks;
	UINT32 loopTick;
	ARR_STR tagData;	// strings for tags (stores allocated pointers)
	VEC_CSTR tagList;	// tag list that only stores references

	// tick/sample conversion rates
	UINT64 tsMult;
	UINT64 tsDiv;
	UINT64 ttMult;
	UINT64 lastTsMult;
	UINT64 lastTsDiv;

	UINT32 filePos;	// file offset of next command to parse
	UINT32 fileTick;	// tick time of next command to parse
	UINT32 playTick;	// tick time when last parsing was issued (up to 1 Render() call behind current position)
	UINT32 playSmpl;	// sample time
	UINT32 curLoop;	// current repetition, 0 = first playthrough, 1 = repeating 1st time
	UINT32 lastLoopTick;	// tick time of last loop, used for "0-sample-loop" detection

	UINT8 playState;
	UINT8 psTrigger;	// used to temporarily trigger special commands

	VGM_PLAY_OPTIONS playOpts;
	PLR_DEV_OPTS devOpts[OPT_DEV_COUNT * 2];	// space for 2 instances per chip
	size_t devOptMap[0x100][2];	// maps libvgm device ID to self->devOpts vector

	ARR_DEVCFG devCfgs;
	size_t vdDevMap[CHIP_COUNT][2];	// maps VGM device ID to devices vector
	ARR_CHIPDEV devices;
	ARR_CSTR devNames;
	size_t optDevMap[OPT_DEV_COUNT * 2];	// maps devOpts vector index to devices vector
	VSTR devNameBuffer;

	size_t dacStrmMap[0x100];	// maps VGM DAC stream ID -> dacStreams vector
	VEC_PSTRM_DEV dacStreams;	// TODO: rename to "pcmStreams"

	PCM_BANK pcmBank[PCM_BANK_COUNT];
	PCM_COMPR_TBL pcmComprTbl;

	UINT8 p2612Fix;	// enable hack/fix for Project2612 VGMs
	UINT32 ym2612pcm_bnkPos;
	UINT8 rf5cBank[2][2];	// [0 RF5C68 / 1 RF5C164][chipID]
	QSOUND_WORK qsWork[2];

	UINT8 v101Fix;	// enable hack/fix for v1.00/v1.01 VGMs with FM clock
	UINT32 v101ym2413clock;
	UINT32 v101ym2612clock;
	UINT32 v101ym2151clock;
};

// engine_vgm.c functions
VGM_CHIPDEV* VGMEngine_GetDevicePtr(PE_VGM* self, UINT8 chipType, UINT8 chipID);

// engine_vgm_commands.c functions
void VGMEngine_WriteQSound_A(VGM_CHIPDEV* cDev, UINT8 ofs, UINT16 data);
void VGMEngine_WriteQSound_B(VGM_CHIPDEV* cDev, UINT8 ofs, UINT16 data);
extern const COMMAND_INFO VGMEngine_CMD_INFO[0x100];

#ifdef __cplusplus
}
#endif

#endif	// PE_VGM_INTERNAL
