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

#define BPIC_WIDTH        107  // width of used image
#define BPIC_HEIGHT        80  // height of used image
#define BPIC_SHEET_WIDTH  240  // width of full sheet
#define BPIC_SHEET_HEIGHT 240  // height of full sheet
#define BPIC_WIDTH_FULL   112  // width of padded image
#define BPIC_HEIGHT_FULL   80  // height of padded image
#define BPIC_XOFS1         18  // X coordinate of used image 1 left edge
#define BPIC_XOFS2        130  // X coordinate of used image 2 left edge
#define BPIC_YOFS          56  // Y coordinate of used image top edge
#define BPIC_PALETTE_COUNT 12  // number of palettes for battle pictures

int wmain(int argc, wchar_t **argv) {
	FILE *fp;
	
#ifdef WIN32
	CoInitialize(NULL); // for WIC
#endif

	//read args
	if (argc < 2) {
		printf("Usage: makebpic <source image 1..6>\n");
		return 1;
	}

	LPCWSTR *paths = argv + 1;
	if (argc < 7) {
		int nInputs = argc - 1;
		printf("Requires 6 images. Found %d.\n", nInputs);
		return 1;
	}

	//read & scale images
	COLOR32 *in[6] = { 0 };
	for (int i = 0; i < 6; i++) {
		LPCWSTR path = paths[i];

		in[i] = (COLOR32 *) calloc(BPIC_WIDTH * BPIC_HEIGHT, sizeof(COLOR32));
		if (!ReadImageScaled(path, in[i], BPIC_WIDTH, BPIC_HEIGHT)) {
			printf("Could not open '%S' for read access.\n", path);
			return 1;
		}
	}

	//construct output images
	COLOR32 *padded[6] = { 0 };
	for (int i = 0; i < 6; i++) {
		padded[i] = (COLOR32 *) calloc(BPIC_WIDTH_FULL * BPIC_HEIGHT_FULL, sizeof(COLOR32));

		//left and right image
		for (int y = 0; y < BPIC_HEIGHT; y++) {
			int xOfs = (i % 2) == 0 ? BPIC_XOFS1 : BPIC_XOFS2;
			COLOR32 *rowDest = padded[i] + (xOfs % 8) + y * BPIC_WIDTH_FULL;
			COLOR32 *rowSrc = in[i] + y * BPIC_WIDTH;
			memcpy(rowDest, rowSrc, BPIC_WIDTH * sizeof(COLOR32));
		}
	}

	//construct temporary image (to create palettes from)
	COLOR palette[256] = { 0 };
	COLOR32 *tempImage = (COLOR32 *) calloc(BPIC_SHEET_WIDTH * BPIC_SHEET_HEIGHT, sizeof(COLOR32));
	for (int i = 0; i < 6; i++) {
		COLOR32 *img = padded[i];
		int xDest = (i % 2) == 0 ? 8 : 120;
		int yDest = (i / 2) * BPIC_HEIGHT_FULL;

		for (int y = 0; y < BPIC_HEIGHT_FULL; y++) {
			memcpy(tempImage + (y + yDest) * BPIC_SHEET_WIDTH + xDest, img + y * BPIC_WIDTH_FULL, BPIC_WIDTH_FULL * sizeof(COLOR32));
		}
	}

	unsigned char *outChar = NULL;
	unsigned short *outScreen = NULL;
	int outPalSize, outCharSize, outScreenSize;

	float diffuse = 1.0;
	int progress1, progress1Max, progress2, progress2Max;

	BgGenerateParameters params = { 0 };
	params.balance.balance = BALANCE_DEFAULT;
	params.balance.colorBalance = BALANCE_DEFAULT;
	params.balance.enhanceColors = 1;

	params.dither.dither = diffuse > 0.0f;
	params.dither.diffuse = diffuse;

	params.paletteRegion.base = 0;
	params.paletteRegion.count = BPIC_PALETTE_COUNT;
	params.paletteRegion.offset = 0;
	params.paletteRegion.length = 16;

	params.characterSetting.base = 1;
	params.characterSetting.compress = 0;
	params.characterSetting.alignment = 1;

	params.bgType = BGGEN_BGTYPE_TEXT_16x16;

	BgGenerate(palette, &outChar, &outScreen, &outPalSize, &outCharSize, &outScreenSize,
		tempImage, BPIC_SHEET_WIDTH, BPIC_SHEET_HEIGHT, &params,
		&progress1, &progress1Max, &progress2, &progress2Max);

	//palette data out
	fp = fopen("BS_PIC_0.ncl.bin", "wb");
	fwrite(palette, BPIC_PALETTE_COUNT * 16, sizeof(COLOR), fp);
	fclose(fp);

	//write character data
	fp = fopen("BS_PIC_0.ncg.bin", "wb");
	fwrite(outChar, outCharSize, 1, fp);
	fclose(fp);
	
	//generate screen data
	uint16_t screenBuffer[32 * 32] = { 0 };
	const char *screenOutNames[] = { "BS_PIC_0_0.nsc.bin", "BS_PIC_0_1.nsc.bin", "BS_PIC_0_2.nsc.bin" };
	for (int i = 0; i < 3; i++) {
		//transfer two images
		uint16_t *src = outScreen + i * (BPIC_HEIGHT_FULL / 8) * (BPIC_SHEET_WIDTH / 8);
		for (int y = 0; y < BPIC_HEIGHT_FULL / 8; y++) {
			uint16_t *rowSrc = src + y * (BPIC_SHEET_WIDTH / 8);
			uint16_t *rowDest = screenBuffer + (y + (BPIC_YOFS / 8)) * 32 + ((BPIC_XOFS1 - 8) / 8);
			memcpy(rowDest, rowSrc, 2 * (2 * BPIC_WIDTH_FULL / 8 + 2));
		}

		//write
		fp = fopen(screenOutNames[i], "wb");
		fwrite(screenBuffer, sizeof(screenBuffer), 1, fp);
		fclose(fp);
	}

	free(tempImage);
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
