// Copyright 2014 Isis Innovation Limited and the authors of InfiniTAM

#include "FileUtils.h"

#include <stdio.h>
#include <fstream>

#ifdef USE_LIBPNG
#include <png.h>
#endif

using namespace std;

static const char *pgm_ascii_id = "P2";
static const char *ppm_ascii_id = "P3";
static const char *pgm_id = "P5";
static const char *ppm_id = "P6";

typedef enum { PNM_PGM, PNM_PPM, PNM_PGM_16u, PNM_PGM_16s, PNM_UNKNOWN = -1 } PNMtype;

struct PNGReaderData {
#ifdef USE_LIBPNG
	png_structp png_ptr;
	png_infop info_ptr;

	PNGReaderData(void)
	{ png_ptr = NULL; info_ptr = NULL; }
	~PNGReaderData(void)
	{ 
		if (info_ptr != NULL) png_destroy_info_struct(png_ptr, &info_ptr);
		if (png_ptr != NULL) png_destroy_read_struct(&png_ptr, (png_infopp)NULL, (png_infopp)NULL);
	}
#endif
};

static PNMtype png_readheader(FILE *fp, int & width, int & height, PNGReaderData & internal)
{
	PNMtype type = PNM_UNKNOWN;

#ifdef USE_LIBPNG
	png_byte color_type;
	png_byte bit_depth;

	unsigned char header[8];    // 8 is the maximum size that can be checked

	fread(header, 1, 8, fp);
	if (png_sig_cmp(header, 0, 8)) {
		//"not a PNG file"
		return type;
	}

	/* initialize stuff */
	internal.png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

	if (!internal.png_ptr) {
		//"png_create_read_struct failed"
		return type;
	}

	internal.info_ptr = png_create_info_struct(internal.png_ptr);
	if (!internal.info_ptr) {
		//"png_create_info_struct failed"
		return type;
	}

	if (setjmp(png_jmpbuf(internal.png_ptr))) {
		//"setjmp failed"
		return type;
	}

	png_init_io(internal.png_ptr, fp);
	png_set_sig_bytes(internal.png_ptr, 8);

	png_read_info(internal.png_ptr, internal.info_ptr);

	width = png_get_image_width(internal.png_ptr, internal.info_ptr);
	height = png_get_image_height(internal.png_ptr, internal.info_ptr);
	color_type = png_get_color_type(internal.png_ptr, internal.info_ptr);
	bit_depth = png_get_bit_depth(internal.png_ptr, internal.info_ptr);

	if (color_type == PNG_COLOR_TYPE_GRAY) {
		if (bit_depth == 8) type = PNM_PGM;
		else if (bit_depth == 16) type = PNM_PGM_16u;
		// bit depths 1, 2 and 4 are not accepted
	} else if (color_type == PNG_COLOR_TYPE_RGB) {
		if (bit_depth == 8) type = PNM_PPM;
		// bit depth 16 is not accepted
	}
	// other color types are not accepted
#endif

	return type;
}

static bool png_readdata(FILE *f, int xsize, int ysize, PNGReaderData & internal, void *data_ext)
{
#ifdef USE_LIBPNG
	if (setjmp(png_jmpbuf(internal.png_ptr))) return false;

	png_read_update_info(internal.png_ptr, internal.info_ptr);

	/* read file */
	if (setjmp(png_jmpbuf(internal.png_ptr))) return false;

	int bytesPerRow = png_get_rowbytes(internal.png_ptr, internal.info_ptr);

	png_byte *data = (png_byte*)data_ext;
	png_bytep *row_pointers = new png_bytep[ysize];
	for (int y=0; y<ysize; y++) row_pointers[y] = &(data[bytesPerRow*y]);

	png_read_image(internal.png_ptr, row_pointers);
	png_read_end(internal.png_ptr, NULL);

	delete[] row_pointers;

	return true;
#else
	return false;
#endif
}

static PNMtype pnm_readheader(FILE *f, int *xsize, int *ysize, bool *binary)
{
	char tmp[1024];
	PNMtype type = PNM_UNKNOWN;
	int xs = 0, ys = 0, max_i = 0;
	bool isBinary = true;

	/* read identifier */
	if (fscanf(f, "%[^ \n\t]", tmp) != 1) return type;
	if (!strcmp(tmp, pgm_id)) type = PNM_PGM;
	else if (!strcmp(tmp, pgm_ascii_id)) { type = PNM_PGM; isBinary = false; }
	else if (!strcmp(tmp, ppm_id)) type = PNM_PPM;
	else if (!strcmp(tmp, ppm_ascii_id)) { type = PNM_PPM; isBinary = false; }
	else return type;

	/* read size */
	if (!fscanf(f, "%i", &xs)) return PNM_UNKNOWN;
	if (!fscanf(f, "%i", &ys)) return PNM_UNKNOWN;

	if (!fscanf(f, "%i", &max_i)) return PNM_UNKNOWN;
	if (max_i < 0) return PNM_UNKNOWN;
	else if (max_i <= (1 << 8)) {}
	else if ((max_i <= (1 << 15)) && (type == PNM_PGM)) type = PNM_PGM_16s;
	else if ((max_i <= (1 << 16)) && (type == PNM_PGM)) type = PNM_PGM_16u;
	else return PNM_UNKNOWN;
	fgetc(f);

	if (xsize) *xsize = xs;
	if (ysize) *ysize = ys;
	if (binary) *binary = isBinary;

	return type;
}

template<class T>
static bool pnm_readdata_ascii_helper(FILE *f, int xsize, int ysize, int channels, T *data)
{
	for (int y = 0; y < ysize; ++y) for (int x = 0; x < xsize; ++x) for (int c = 0; c < channels; ++c) {
		int v;
		if (!fscanf(f, "%i", &v)) return false;
		*data++ = v;
	}
	return true;
}

static bool pnm_readdata_ascii(FILE *f, int xsize, int ysize, PNMtype type, void *data)
{
	int channels = 0;
	switch (type)
	{
	case PNM_PGM:
		channels = 1;
		return pnm_readdata_ascii_helper(f, xsize, ysize, channels, (unsigned char*)data);
	case PNM_PPM:
		channels = 3;
		return pnm_readdata_ascii_helper(f, xsize, ysize, channels, (unsigned char*)data);
	case PNM_PGM_16s:
		channels = 1;
		return pnm_readdata_ascii_helper(f, xsize, ysize, channels, (short*)data);
	case PNM_PGM_16u:
		channels = 1;
		return pnm_readdata_ascii_helper(f, xsize, ysize, channels, (unsigned short*)data);
	case PNM_UNKNOWN: break;
	}
	return false;
}

static bool pnm_readdata_binary(FILE *f, int xsize, int ysize, PNMtype type, void *data)
{
	int channels = 0;
	int bytesPerSample = 0;
	switch (type)
	{
	case PNM_PGM: bytesPerSample = sizeof(unsigned char); channels = 1; break;
	case PNM_PPM: bytesPerSample = sizeof(unsigned char); channels = 3; break;
	case PNM_PGM_16s: bytesPerSample = sizeof(short); channels = 1; break;
	case PNM_PGM_16u: bytesPerSample = sizeof(unsigned short); channels = 1; break;
	case PNM_UNKNOWN: break;
	}
	if (bytesPerSample == 0) return false;

	size_t tmp = fread(data, bytesPerSample, xsize*ysize*channels, f);
	if (tmp != (size_t)xsize*ysize*channels) return false;
	return (data != NULL);
}

static bool pnm_writeheader(FILE *f, int xsize, int ysize, PNMtype type)
{
	const char *pnmid = NULL;
	int max = 0;
	switch (type) {
	case PNM_PGM: pnmid = pgm_id; max = 256; break;
	case PNM_PPM: pnmid = ppm_id; max = 255; break;
	case PNM_PGM_16s: pnmid = pgm_id; max = 32767; break;
	case PNM_PGM_16u: pnmid = pgm_id; max = 65535; break;
	case PNM_UNKNOWN: return false;
	}
	if (pnmid == NULL) return false;

	fprintf(f, "%s\n", pnmid);
	fprintf(f, "%i %i\n", xsize, ysize);
	fprintf(f, "%i\n", max);

	return true;
}

static bool pnm_writedata(FILE *f, int xsize, int ysize, PNMtype type, const void *data)
{
	int channels = 0;
	int bytesPerSample = 0;
	switch (type)
	{
	case PNM_PGM: bytesPerSample = sizeof(unsigned char); channels = 1; break;
	case PNM_PPM: bytesPerSample = sizeof(unsigned char); channels = 3; break;
	case PNM_PGM_16s: bytesPerSample = sizeof(short); channels = 1; break;
	case PNM_PGM_16u: bytesPerSample = sizeof(unsigned short); channels = 1; break;
	case PNM_UNKNOWN: break;
	}
	fwrite(data, bytesPerSample, channels*xsize*ysize, f);
	return true;
}

void SaveImageToFile(const ITMUChar4Image* image, const char* fileName, bool flipVertical)
{
	FILE *f = fopen(fileName, "wb");
	if (!pnm_writeheader(f, image->noDims.x, image->noDims.y, PNM_PPM)) {
		fclose(f); return;
	}

	unsigned char *data = new unsigned char[image->noDims.x*image->noDims.y * 3];

	Vector2i noDims = image->noDims;

	if (flipVertical)
	{
		for (int y = 0; y < noDims.y; y++) for (int x = 0; x < noDims.x; x++)
		{
			int locId_src, locId_dst;
			locId_src = x + y * noDims.x;
			locId_dst = x + (noDims.y - y - 1) * noDims.x;

			data[locId_dst * 3 + 0] = image->GetData(MEMORYDEVICE_CPU)[locId_src].x;
			data[locId_dst * 3 + 1] = image->GetData(MEMORYDEVICE_CPU)[locId_src].y;
			data[locId_dst * 3 + 2] = image->GetData(MEMORYDEVICE_CPU)[locId_src].z;
		}
	}
	else
	{
		for (int i = 0; i < noDims.x * noDims.y; ++i) {
			data[i * 3 + 0] = image->GetData(MEMORYDEVICE_CPU)[i].x;
			data[i * 3 + 1] = image->GetData(MEMORYDEVICE_CPU)[i].y;
			data[i * 3 + 2] = image->GetData(MEMORYDEVICE_CPU)[i].z;
		}
	}

	pnm_writedata(f, image->noDims.x, image->noDims.y, PNM_PPM, data);
	delete[] data;
	fclose(f);
}

void SaveImageToFile(const ITMShortImage* image, const char* fileName)
{
	short *data = (short*)malloc(sizeof(short) * image->dataSize);
	const short *dataSource = image->GetData(MEMORYDEVICE_CPU);
	for (int i = 0; i < image->dataSize; i++) data[i] = (dataSource[i] << 8) | ((dataSource[i] >> 8) & 255);

	FILE *f = fopen(fileName, "wb");
	if (!pnm_writeheader(f, image->noDims.x, image->noDims.y, PNM_PGM_16u)) {
		fclose(f); return;
	}
	pnm_writedata(f, image->noDims.x, image->noDims.y, PNM_PGM_16u, data);
	fclose(f);

	delete data;
}

void SaveImageToFile(const ITMFloatImage* image, const char* fileName)
{
	unsigned short *data = new unsigned short[image->dataSize];
	for (int i = 0; i < image->dataSize; i++)
	{
		float localData = image->GetData(MEMORYDEVICE_CPU)[i];
		data[i] = localData >= 0 ? (unsigned short)(localData * 1000.0f) : 0;
	}

	FILE *f = fopen(fileName, "wb");
	if (!pnm_writeheader(f, image->noDims.x, image->noDims.y, PNM_PGM_16u)) {
		fclose(f); return;
	}
	pnm_writedata(f, image->noDims.x, image->noDims.y, PNM_PGM_16u, data);
	fclose(f);

	delete[] data;
}

bool ReadImageFromFile(ITMUChar4Image* image, const char* fileName)
{
	PNGReaderData pngData;
	bool usepng = false;

	int xsize, ysize;
	bool binary;
	FILE *f = fopen(fileName, "rb");
	if (f == NULL) return false;
	if (pnm_readheader(f, &xsize, &ysize, &binary) != PNM_PPM) {
		fclose(f);
		f = fopen(fileName, "rb");
		if (png_readheader(f, xsize, ysize, pngData) != PNM_PPM) {
			fclose(f);
			return false;
		}
		usepng = true;
	}

	unsigned char *data = new unsigned char[xsize*ysize * 3];
	if (usepng) {
		if (!png_readdata(f, xsize, ysize, pngData, data)) { fclose(f); delete[] data; return false; }
	} else if (binary) {
		if (!pnm_readdata_binary(f, xsize, ysize, PNM_PPM, data)) { fclose(f); delete[] data; return false; }
	} else {
		if (!pnm_readdata_ascii(f, xsize, ysize, PNM_PPM, data)) { fclose(f); delete[] data; return false; }
	}
	fclose(f);

	Vector2i newSize(xsize, ysize);
	image->ChangeDims(newSize);
	Vector4u *dataPtr = image->GetData(MEMORYDEVICE_CPU);
	for (int i = 0; i < image->noDims.x*image->noDims.y; ++i)
	{
		dataPtr[i].x = data[i * 3 + 0]; dataPtr[i].y = data[i * 3 + 1];
		dataPtr[i].z = data[i * 3 + 2]; dataPtr[i].w = 255;
	}

	delete[] data;

	return true;
}

bool ReadImageFromFile(ITMShortImage *image, const char *fileName)
{
	PNGReaderData pngData;
	bool usepng = false;

	int xsize, ysize;
	bool binary;
	FILE *f = fopen(fileName, "rb");
	if (f == NULL) return false;
	PNMtype type = pnm_readheader(f, &xsize, &ysize, &binary);
	if ((type != PNM_PGM_16s) && (type != PNM_PGM_16u)) {
		fclose(f);
		f = fopen(fileName, "rb");
		type = png_readheader(f, xsize, ysize, pngData);
		if ((type != PNM_PGM_16s) && (type != PNM_PGM_16u)) {
			fclose(f);
			return false;
		}
		usepng = true;
		binary = true;
	}

	short *data = new short[xsize*ysize];
	if (usepng) {
		if (!png_readdata(f, xsize, ysize, pngData, data)) { fclose(f); delete[] data; return false; }
	} else if (binary) {
		if (!pnm_readdata_binary(f, xsize, ysize, type, data)) { fclose(f); delete[] data; return false; }
	} else {
		if (!pnm_readdata_ascii(f, xsize, ysize, type, data)) { fclose(f); delete[] data; return false; }
	}
	fclose(f);

	Vector2i newSize(xsize, ysize);
	image->ChangeDims(newSize);
	if (binary) {
		for (int i = 0; i < image->noDims.x*image->noDims.y; ++i) {
			image->GetData(MEMORYDEVICE_CPU)[i] = (data[i] << 8) | ((data[i] >> 8) & 255);
		}
	} else {
		for (int i = 0; i < image->noDims.x*image->noDims.y; ++i) {
			image->GetData(MEMORYDEVICE_CPU)[i] = data[i];
		}
	}
	delete[] data;

	return true;
}

