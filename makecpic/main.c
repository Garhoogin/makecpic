#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gdip.h"
#include "bggen.h"
#include "palette.h"
#include "color.h"

#ifdef WIN32
#include <Windows.h>
#endif

static int ReadImageScaled(const wchar_t *path, COLOR32 *px, unsigned int width, unsigned int height) {
	unsigned int pxWidth, pxHeight;
	COLOR32 *pxUnscaled = ImgRead(path, &pxWidth, &pxHeight);
	if (pxUnscaled == NULL) return 0;

	COLOR32 *pxScaled = ImgScaleEx(pxUnscaled, pxWidth, pxHeight, width, height, IMG_SCALE_COVER);
	free(pxUnscaled);
	if (pxScaled == NULL) return 0;

	memcpy(px, pxScaled, width * height * sizeof(COLOR32));
	free(pxScaled);
	return 1;
}

static void NnsWriteFileHeader(FILE *fp, uint32_t magic, unsigned int fileSize, unsigned int nSections) {
	struct {
		uint32_t magic;
		uint16_t endian;
		uint16_t version;
		uint32_t fileSize;
		uint16_t headerSize;
		uint16_t nSections;
	} header = { magic, 0xFEFF, 0x1, fileSize, 0x10, nSections };
	fwrite(&header, sizeof(header), 1, fp);
}

static void NnsWriteBlock(FILE *fp, uint32_t magic, const void *header, unsigned int headerSize, const void *data, unsigned int dataSize) {
	struct {
		uint32_t magic;
		uint32_t size;
	} blockHeader = { magic, headerSize + dataSize + sizeof(blockHeader) };
	fwrite(&blockHeader, sizeof(blockHeader), 1, fp);
	fwrite(header, headerSize, 1, fp);
	fwrite(data, dataSize, 1, fp);
}

static void GetOutputPath(wchar_t *buffer, const wchar_t *inFile, const wchar_t *extension) {
	//write until we reach null terminator or dot
	const wchar_t *lastDot = wcsrchr(inFile, L'.');
	while (inFile != lastDot && *inFile != L'\0') {
		*(buffer++) = *(inFile++);
	}

	//copy extension
	if (*extension == L'.') extension++;
	*(buffer++) = L'.';
	memcpy(buffer, extension, (wcslen(extension) + 1) * sizeof(wchar_t));
}

#define CPIC_WIDTH        70  // width of used image
#define CPIC_HEIGHT       88  // height of used image
#define CPIC_WIDTH_FULL   80  // width of padded image
#define CPIC_HEIGHT_FULL  88  // height of padded image
#define CPIC_XOFS        180  // X coordinate of used image left edge
#define CPIC_YOFS         64  // Y coordinate of used image top edge
#define CPIC_PALETTE_COUNT 7  // number of palettes for course picture

int wmain(int argc, wchar_t **argv) {
	FILE *fp;
	wchar_t pathBuffer[MAX_PATH + 1];

#ifdef WIN32
	CoInitialize(NULL); // for WIC
#endif

	//read args
	if (argc < 2) {
		printf("Usage: makecpic <source image>\n");
		return 1;
	}

	const wchar_t *path = argv[1];

	//read & scale image
	COLOR32 *in = (COLOR32 *) calloc(CPIC_WIDTH * CPIC_HEIGHT, sizeof(COLOR32));
	if (!ReadImageScaled(path, in, CPIC_WIDTH, CPIC_HEIGHT)) {
		printf("Could not open '%S' for read access.\n", path);
		return 1;
	}

	//construct output image (80x88)
	COLOR32 *padded = (COLOR32 *) calloc(CPIC_WIDTH_FULL * CPIC_HEIGHT_FULL, sizeof(COLOR32));
	for (int y = 0; y < CPIC_HEIGHT; y++) {
		COLOR32 *rowDest = padded + (CPIC_XOFS % 8) + y * CPIC_WIDTH_FULL;
		COLOR32 *rowSrc = in + y * CPIC_WIDTH;
		memcpy(rowDest, rowSrc, CPIC_WIDTH * sizeof(COLOR32));
	}

	//data out
	COLOR palette[256] = { 0 };
	unsigned char *chars;
	uint16_t *screen;
	int paletteDataSize = 0, charDataSize = 0, screenDataSize = 0;

	//generate data
	float diffuse = 1.0;
	int paletteProgress, paletteProgressMax, charProgress, charProgressMax;
	
	BgGenerateParameters params = { 0 };
	params.balance.balance = BALANCE_DEFAULT;
	params.balance.colorBalance = BALANCE_DEFAULT;
	params.balance.enhanceColors = 1;

	params.dither.dither = diffuse > 0.0f;
	params.dither.diffuse = diffuse;

	params.paletteRegion.base = 0;
	params.paletteRegion.count = CPIC_PALETTE_COUNT;
	params.paletteRegion.offset = 0;
	params.paletteRegion.length = 16;

	params.characterSetting.base = 1;
	params.characterSetting.compress = 0;
	params.characterSetting.alignment = 1;

	params.bgType = BGGEN_BGTYPE_TEXT_16x16;

	BgGenerate(palette, &chars, &screen, &paletteDataSize, &charDataSize, &screenDataSize, 
		padded, CPIC_WIDTH_FULL, CPIC_HEIGHT_FULL, &params,
		&paletteProgress, &paletteProgressMax, &charProgress, &charProgressMax);

	//assemble out screen (pad)
	uint16_t outScreen[32 * 32] = { 0 };
	int outScreenDataSize = sizeof(outScreen);
	for (int y = 0; y < CPIC_HEIGHT_FULL / 8; y++) {
		int tileX = (CPIC_XOFS / 8);
		int tileY = (CPIC_YOFS / 8) + y;
		int rowSize = CPIC_WIDTH_FULL / 8;
		memcpy(outScreen + tileX + tileY * 32, screen + y * rowSize, rowSize * sizeof(uint16_t));
	}

	//write out palette
	unsigned char plttHeader[] = { 3, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 16, 0, 0, 0 };
	GetOutputPath(pathBuffer, path, L".NCLR");
	fp = _wfopen(pathBuffer, L"wb");
	NnsWriteFileHeader(fp, 'NCLR', 0x228, 1);
	NnsWriteBlock(fp, 'PLTT', plttHeader, sizeof(plttHeader), palette, sizeof(palette));
	fclose(fp);

	//write out character
	unsigned char charHeader[] = { 11, 0, 10, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xC0, 0x0D, 0, 0, 24, 0, 0, 0 };
	GetOutputPath(pathBuffer, path, L".NCGR");
	fp = _wfopen(pathBuffer, L"wb");
	NnsWriteFileHeader(fp, 'NCGR', 0xDF0, 1);
	NnsWriteBlock(fp, 'CHAR', charHeader, sizeof(charHeader), chars, charDataSize);
	fclose(fp);

	//write out screen
	unsigned char scrnHeader[] = { 0, 1, 0, 1, 0, 0, 0, 0, 0, 8, 0, 0 };
	GetOutputPath(pathBuffer, path, L".NSCR");
	fp = _wfopen(pathBuffer, L"wb");
	NnsWriteFileHeader(fp, 'NSCR', 0x824, 1);
	NnsWriteBlock(fp, 'SCRN', scrnHeader, sizeof(scrnHeader), outScreen, outScreenDataSize);
	fclose(fp);

	return 0;
}

#ifdef _MSC_VER

int entry(void *peb) {
	int argc, startInfo;
	wchar_t **argv, **envp;

	extern int __wgetmainargs(int *_Argc, wchar_t ***_Argv, wchar_t ***_Envp, int doWildCard, int *startInfo);
	__wgetmainargs(&argc, &argv, &envp, 1, &startInfo);

	ExitProcess(wmain(argc, argv));
	return 0; // not reach here
}

#endif
