#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#ifndef USE_IMAGE
extern "C" {
#include <png.h>
}
#endif

#include "System.h"
#include "NLS.h"
#include "Util.h"
#include "gba/Flash.h"
#include "gba/GBA.h"
#include "gba/Globals.h"
#include "gba/RTC.h"
#include "common/Port.h"

#ifdef USE_FEX
#include "fex/fex.h"
#endif

extern "C" {
#include "common/memgzio.h"
}

#include "gba/gbafilter.h"
#include "gb/gbGlobals.h"

#ifndef _MSC_VER
#define _stricmp strcasecmp
#endif // ! _MSC_VER

extern int systemColorDepth;
extern int systemRedShift;
extern int systemGreenShift;
extern int systemBlueShift;

extern uint16_t systemColorMap16[0x10000];
extern uint32_t systemColorMap32[0x10000];

static int (ZEXPORT *utilGzWriteFunc)(gzFile, const voidp, unsigned int) = NULL;
static int (ZEXPORT *utilGzReadFunc)(gzFile, voidp, unsigned int) = NULL;
static int (ZEXPORT *utilGzCloseFunc)(gzFile) = NULL;
static z_off_t (ZEXPORT *utilGzSeekFunc)(gzFile, z_off_t, int) = NULL;

#ifdef USE_IMAGE
bool utilWritePNGFile(const char *fileName, int w, int h, uint8_t *pix)
{
  uint8_t writeBuffer[512 * 3];

  FILE *fp = fopen(fileName,"wb");

  if(!fp) {
    systemMessage(MSG_ERROR_CREATING_FILE, N_("Error creating file %s"), fileName);
    return false;
  }

  png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING,
                                                NULL,
                                                NULL,
                                                NULL);
  if(!png_ptr) {
    fclose(fp);
    return false;
  }

  png_infop info_ptr = png_create_info_struct(png_ptr);

  if(!info_ptr) {
    png_destroy_write_struct(&png_ptr,NULL);
    fclose(fp);
    return false;
  }

  if(setjmp(png_jmpbuf(png_ptr))) {
    png_destroy_write_struct(&png_ptr,NULL);
    fclose(fp);
    return false;
  }

  png_init_io(png_ptr,fp);

  png_set_IHDR(png_ptr,
               info_ptr,
               w,
               h,
               8,
               PNG_COLOR_TYPE_RGB,
               PNG_INTERLACE_NONE,
               PNG_COMPRESSION_TYPE_DEFAULT,
               PNG_FILTER_TYPE_DEFAULT);

  png_write_info(png_ptr,info_ptr);

  uint8_t *b = writeBuffer;

  int sizeX = w;
  int sizeY = h;

  switch(systemColorDepth) {
  case 16:
    {
      uint16_t *p = (uint16_t *)(pix+(w+2)*2); // skip first black line
      for(int y = 0; y < sizeY; y++) {
         for(int x = 0; x < sizeX; x++) {
          uint16_t v = *p++;

          *b++ = ((v >> systemRedShift) & 0x001f) << 3; // R
          *b++ = ((v >> systemGreenShift) & 0x001f) << 3; // G
          *b++ = ((v >> systemBlueShift) & 0x01f) << 3; // B
        }
        p++; // skip black pixel for filters
        p++; // skip black pixel for filters
        png_write_row(png_ptr,writeBuffer);

        b = writeBuffer;
      }
    }
    break;
  case 24:
    {
      uint8_t *pixU8 = (uint8_t *)pix;
      for(int y = 0; y < sizeY; y++) {
        for(int x = 0; x < sizeX; x++) {
          if(systemRedShift < systemBlueShift) {
            *b++ = *pixU8++; // R
            *b++ = *pixU8++; // G
            *b++ = *pixU8++; // B
          } else {
            int blue = *pixU8++;
            int green = *pixU8++;
            int red = *pixU8++;

            *b++ = red;
            *b++ = green;
            *b++ = blue;
          }
        }
        png_write_row(png_ptr,writeBuffer);

        b = writeBuffer;
      }
    }
    break;
  case 32:
    {
      uint32_t *pixU32 = (uint32_t *)(pix+4*(w+1));
      for(int y = 0; y < sizeY; y++) {
        for(int x = 0; x < sizeX; x++) {
          uint32_t v = *pixU32++;

          *b++ = ((v >> systemRedShift) & 0x001f) << 3; // R
          *b++ = ((v >> systemGreenShift) & 0x001f) << 3; // G
          *b++ = ((v >> systemBlueShift) & 0x001f) << 3; // B
        }
        pixU32++;

        png_write_row(png_ptr,writeBuffer);

        b = writeBuffer;
      }
    }
    break;
  }

  png_write_end(png_ptr, info_ptr);

  png_destroy_write_struct(&png_ptr, &info_ptr);

  fclose(fp);

  return true;
}
#else
bool utilWritePNGFile(const char *fileName, int w, int h, uint8_t *pix)
{
  return false;
}
#endif
void utilPutDword(u8 *p, u32 value)
{
  *p++ = value & 255;
  *p++ = (value >> 8) & 255;
  *p++ = (value >> 16) & 255;
  *p = (value >> 24) & 255;
}

void utilPutWord(uint8_t *p, uint16_t value)
{
  *p++ = value & 255;
  *p = (value >> 8) & 255;
}

#ifdef USE_IMAGE
bool utilWriteBMPFile(const char *fileName, int w, int h, uint8_t *pix)
{
  uint8_t writeBuffer[512 * 3];

  FILE *fp = fopen(fileName,"wb");

  if(!fp) {
    systemMessage(MSG_ERROR_CREATING_FILE, N_("Error creating file %s"), fileName);
    return false;
  }

  struct {
	uint8_t ident[2];
	uint8_t filesize[4];
	uint8_t reserved[4];
	uint8_t dataoffset[4];
	uint8_t headersize[4];
	uint8_t width[4];
	uint8_t height[4];
	uint8_t planes[2];
	uint8_t bitsperpixel[2];
	uint8_t compression[4];
	uint8_t datasize[4];
	uint8_t hres[4];
	uint8_t vres[4];
	uint8_t colors[4];
	uint8_t importantcolors[4];
	//    uint8_t pad[2];
  } bmpheader;
  __builtin_memset(&bmpheader, 0, sizeof(bmpheader));

  bmpheader.ident[0] = 'B';
  bmpheader.ident[1] = 'M';

  uint32_t fsz = sizeof(bmpheader) + w*h*3;
  utilPutDword(bmpheader.filesize, fsz);
  utilPutDword(bmpheader.dataoffset, 0x36);
  utilPutDword(bmpheader.headersize, 0x28);
  utilPutDword(bmpheader.width, w);
  utilPutDword(bmpheader.height, h);
  utilPutDword(bmpheader.planes, 1);
  utilPutDword(bmpheader.bitsperpixel, 24);
  utilPutDword(bmpheader.datasize, 3*w*h);

  fwrite(&bmpheader, 1, sizeof(bmpheader), fp);

  uint8_t *b = writeBuffer;

  int sizeX = w;
  int sizeY = h;

  switch(systemColorDepth) {
  case 16:
    {
      uint16_t *p = (uint16_t *)(pix+(w+2)*(h)*2); // skip first black line
      for(int y = 0; y < sizeY; y++) {
        for(int x = 0; x < sizeX; x++) {
          uint16_t v = *p++;

          *b++ = ((v >> systemBlueShift) & 0x01f) << 3; // B
          *b++ = ((v >> systemGreenShift) & 0x001f) << 3; // G
          *b++ = ((v >> systemRedShift) & 0x001f) << 3; // R
        }
        p++; // skip black pixel for filters
        p++; // skip black pixel for filters
        p -= 2*(w+2);
        fwrite(writeBuffer, 1, 3*w, fp);

        b = writeBuffer;
      }
    }
    break;
  case 24:
    {
      uint8_t *pixU8 = (uint8_t *)pix+3*w*(h-1);
      for(int y = 0; y < sizeY; y++) {
        for(int x = 0; x < sizeX; x++) {
          if(systemRedShift > systemBlueShift) {
            *b++ = *pixU8++; // B
            *b++ = *pixU8++; // G
            *b++ = *pixU8++; // R
          } else {
            int red = *pixU8++;
            int green = *pixU8++;
            int blue = *pixU8++;

            *b++ = blue;
            *b++ = green;
            *b++ = red;
          }
        }
        pixU8 -= 2*3*w;
        fwrite(writeBuffer, 1, 3*w, fp);

        b = writeBuffer;
      }
    }
    break;
  case 32:
    {
      uint32_t *pixU32 = (uint32_t *)(pix+4*(w+1)*(h));
      for(int y = 0; y < sizeY; y++) {
        for(int x = 0; x < sizeX; x++) {
          uint32_t v = *pixU32++;

          *b++ = ((v >> systemBlueShift) & 0x001f) << 3; // B
          *b++ = ((v >> systemGreenShift) & 0x001f) << 3; // G
          *b++ = ((v >> systemRedShift) & 0x001f) << 3; // R
        }
        pixU32++;
        pixU32 -= 2*(w+1);

        fwrite(writeBuffer, 1, 3*w, fp);

        b = writeBuffer;
      }
    }
    break;
  }

  fclose(fp);

  return true;
}
#endif

extern bool cpuIsMultiBoot;

bool utilIsGBAImage(const char * file)
{
  cpuIsMultiBoot = false;
  if(strlen(file) > 4) {
    const char * p = strrchr(file,'.');

    if(p != NULL) {
      if((_stricmp(p, ".agb") == 0) ||
         (_stricmp(p, ".gba") == 0) ||
         (_stricmp(p, ".bin") == 0) ||
         (_stricmp(p, ".elf") == 0))
        return true;
      if(_stricmp(p, ".mb") == 0) {
        cpuIsMultiBoot = true;
        return true;
      }
    }
  }

  return false;
}

bool utilIsGBImage(const char * file)
{
  if(strlen(file) > 4) {
    const char * p = strrchr(file,'.');

    if(p != NULL) {
      if((_stricmp(p, ".dmg") == 0) ||
         (_stricmp(p, ".gb") == 0) ||
         (_stricmp(p, ".gbc") == 0) ||
         (_stricmp(p, ".cgb") == 0) ||
         (_stricmp(p, ".sgb") == 0))
        return true;
    }
  }

  return false;
}

bool utilIsGzipFile(const char *file)
{
  if(strlen(file) > 3) {
    const char * p = strrchr(file,'.');

    if(p != NULL) {
      if(_stricmp(p, ".gz") == 0)
        return true;
      if(_stricmp(p, ".z") == 0)
        return true;
    }
  }

  return false;
}

// disabled for now
#if 0
bool utilIsZipFile(const char * file)
{
  if(strlen(file) > 4) {
    const char * p = strrchr(file,'.');

    if(p != NULL) {
      if(_stricmp(p, ".zip") == 0)
        return true;
    }
  }

  return false;
}
#endif

// strip .gz or .z off end
void utilStripDoubleExtension(const char *file, char *buffer)
{
  if(buffer != file) // allows conversion in place
    strcpy(buffer, file);

  if(utilIsGzipFile(file)) {
    char *p = strrchr(buffer, '.');

    if(p)
      *p = 0;
  }
}

// Opens and scans archive using accept(). Returns fex_t if found.
// If error or not found, displays message and returns NULL.
#ifdef USE_FEX
static fex_t* scan_arc(const char *file, bool (*accept)(const char *),
		char (&buffer) [2048] )
{
	fex_t* fe;
	fex_err_t err = fex_open( &fe, file );
	if(!fe)
	{
		systemMessage(MSG_CANNOT_OPEN_FILE, N_("Cannot open file %s: %s"), file, err);
		return NULL;
	}

	// Scan filenames
	bool found=false;
	while(!fex_done(fe)) {
		strncpy(buffer,fex_name(fe),sizeof buffer);
		buffer [sizeof buffer-1] = '\0';

		utilStripDoubleExtension(buffer, buffer);

		if(accept(buffer)) {
			found = true;
			break;
		}

		fex_err_t err = fex_next(fe);
		if(err) {
			systemMessage(MSG_BAD_ZIP_FILE, N_("Cannot read archive %s: %s"), file, err);
			fex_close(fe);
			return NULL;
		}
	}

	if(!found) {
		systemMessage(MSG_NO_IMAGE_ON_ZIP,
									N_("No image found in file %s"), file);
		fex_close(fe);
		return NULL;
	}
	return fe;
}
#endif

static bool utilIsImage(const char *file)
{
	return utilIsGBAImage(file) || utilIsGBImage(file);
}

uint32_t utilFindType(const char *file)
{
	char buffer [2048];
	if ( !utilIsImage( file ) ) // TODO: utilIsArchive() instead?
	{
		#ifdef USE_FEX
		fex_t* fe = scan_arc(file,utilIsImage,buffer);
		if(!fe)
		#endif
			return IMAGE_UNKNOWN;
      #ifdef USE_FEX
		fex_close(fe);
		file = buffer;
      #endif
	}
	return utilIsGBAImage(file) ? IMAGE_GBA : IMAGE_GB;
}

static int utilGetSize(int size)
{
  int res = 1;
  while(res < size)
    res <<= 1;
  return res;
}

uint8_t *utilLoad(const char *file, bool (*accept)(const char *), uint8_t *data, int &size)
#ifdef USE_FEX
{
	// find image file
	char buffer [2048];
	fex_t *fe = scan_arc(file,accept,buffer);
	if(!fe)
		return NULL;

	// Allocate space for image
	fex_err_t err = fex_stat(fe);
	int fileSize = fex_size(fe);
	if(size == 0)
		size = fileSize;

	uint8_t *image = data;

	if(image == NULL) {
		// allocate buffer memory if none was passed to the function
		image = (uint8_t *)__builtin_malloc(utilGetSize(size));
		if(image == NULL) {
			fex_close(fe);
			systemMessage(MSG_OUT_OF_MEMORY, N_("Failed to allocate memory for %s"),
					"data");
			return NULL;
		}
		size = fileSize;
	}

	// Read image
	int read = fileSize <= size ? fileSize : size; // do not read beyond file
	err = fex_read(fe, image, read);
	fex_close(fe);
	if(err) {
		systemMessage(MSG_ERROR_READING_IMAGE,
				N_("Error reading image from %s: %s"), buffer, err);
		if(data == NULL)
			free(image);
		return NULL;
	}

	size = fileSize;

	return image;
}
#else
{
	FILE *fp = NULL;
	char *buf = NULL;

	fp = fopen(file,"rb");
	fseek(fp, 0, SEEK_END); //go to end
	size = ftell(fp); // get position at end (length)
	rewind(fp);

	uint8_t *image = data;
	if(image == NULL)
	{
		//allocate buffer memory if none was passed to the function
		image = (uint8_t *)__builtin_malloc(utilGetSize(size));
		if(image == NULL)
		{
			systemMessage(MSG_OUT_OF_MEMORY, N_("Failed to allocate memory for %s"),
					"data");
			return NULL;
		}
	}

   fread(image, 1, size, fp); // read into buffer
	fclose(fp);
	return image;
}
#endif

void utilWriteInt(gzFile gzFile, int i)
{
  utilGzWrite(gzFile, &i, sizeof(int));
}

int utilReadInt(gzFile gzFile)
{
  int i = 0;
  utilGzRead(gzFile, &i, sizeof(int));
  return i;
}

void utilReadData(gzFile gzFile, variable_desc* data)
{
  while(data->address) {
    utilGzRead(gzFile, data->address, data->size);
    data++;
  }
}

void utilReadDataSkip(gzFile gzFile, variable_desc* data)
{
  while(data->address) {
    utilGzSeek(gzFile, data->size, SEEK_CUR);
    data++;
  }
}

void utilWriteData(gzFile gzFile, variable_desc *data)
{
  while(data->address) {
    utilGzWrite(gzFile, data->address, data->size);
    data++;
  }
}

#ifndef __LIBRETRO__
gzFile utilGzOpen(const char *file, const char *mode)
{
  utilGzWriteFunc = (int (ZEXPORT *)(void *,void * const, unsigned int))gzwrite;
  utilGzReadFunc = gzread;
  utilGzCloseFunc = gzclose;
  utilGzSeekFunc = gzseek;

  return gzopen(file, mode);
}
#endif

gzFile utilMemGzOpen(char *memory, int available, const char *mode)
{
  utilGzWriteFunc = memgzwrite;
  utilGzReadFunc = memgzread;
  utilGzCloseFunc = memgzclose;
  utilGzSeekFunc = memgzseek;

  return memgzopen(memory, available, mode);
}

int utilGzWrite(gzFile file, const voidp buffer, unsigned int len)
{
  return utilGzWriteFunc(file, buffer, len);
}

int utilGzRead(gzFile file, voidp buffer, unsigned int len)
{
  return utilGzReadFunc(file, buffer, len);
}

int utilGzClose(gzFile file)
{
  return utilGzCloseFunc(file);
}

z_off_t utilGzSeek(gzFile file, z_off_t offset, int whence)
{
	return utilGzSeekFunc(file, offset, whence);
}

long utilGzMemTell(gzFile file)
{
  return memtell(file);
}

void utilGBAFindSave(const uint8_t *data, const int size)
{
  uint32_t *p = (uint32_t *)data;
  uint32_t *end = (uint32_t *)(data + size);
  int saveType = 0;
  int flashSize = 0x10000;
  bool rtcFound = false;

  while(p  < end) {
    uint32_t d = READ32LE(p);

    if(d == 0x52504545) {
      if(memcmp(p, "EEPROM_", 7) == 0) {
        if(saveType == 0)
          saveType = 3;
      }
    } else if (d == 0x4D415253) {
      if(memcmp(p, "SRAM_", 5) == 0) {
        if(saveType == 0)
          saveType = 1;
      }
    } else if (d == 0x53414C46) {
      if(memcmp(p, "FLASH1M_", 8) == 0) {
        if(saveType == 0) {
          saveType = 2;
          flashSize = 0x20000;
        }
      } else if(memcmp(p, "FLASH", 5) == 0) {
        if(saveType == 0) {
          saveType = 2;
          flashSize = 0x10000;
        }
      }
    } else if (d == 0x52494953) {
      if(memcmp(p, "SIIRTC_V", 8) == 0)
        rtcFound = true;
    }
    p++;
  }
  // if no matches found, then set it to NONE
  if(saveType == 0) {
    saveType = 5;
  }

  rtcEnable(rtcFound);
  cpuSaveType = saveType;
  flashSetSize(flashSize);
}

void utilUpdateSystemColorMaps()
{
 #if 0
  switch(systemColorDepth) {
  case 16:
    {
      for(int i = 0; i < 0x10000; i++) {
        systemColorMap16[i] = ((i & 0x1f) << systemRedShift) |
          (((i & 0x3e0) >> 5) << systemGreenShift) |
          (((i & 0x7c00) >> 10) << systemBlueShift);
      }
      if (lcd) gbafilter_pal(systemColorMap16, 0x10000);
    }
    break;
  case 24:
  case 32:
    {
      for(int i = 0; i < 0x10000; i++) {
        systemColorMap32[i] = ((i & 0x1f) << systemRedShift) |
          (((i & 0x3e0) >> 5) << systemGreenShift) |
          (((i & 0x7c00) >> 10) << systemBlueShift);
      }
      if (lcd) gbafilter_pal32(systemColorMap32, 0x10000);
    }
    break;
  }
#endif
      for(int i = 0; i < 0x10000; i++) {
        systemColorMap32[i] = ((i & 0x1f) << systemRedShift) |
          (((i & 0x3e0) >> 5) << systemGreenShift) |
          (((i & 0x7c00) >> 10) << systemBlueShift);
      }
}

// Check for existence of file.
bool utilFileExists( const char *filename )
{
	FILE *f = fopen( filename, "r" );
	if( f == NULL ) {
		return false;
	} else {
		fclose( f );
		return true;
	}
}

// Not endian safe, but VBA itself doesn't seem to care, so hey <_<
#ifdef __LIBRETRO__
void utilWriteIntMem(uint8_t *& data, int val)
{
   memcpy(data, &val, sizeof(int));
   data += sizeof(int);
}

void utilWriteMem(uint8_t *& data, const void *in_data, unsigned size)
{
   memcpy(data, in_data, size);
   data += size;
}

void utilWriteDataMem(uint8_t *& data, variable_desc *desc)
{
   while (desc->address) 
   {
      utilWriteMem(data, desc->address, desc->size);
      desc++;
   }
}

int utilReadIntMem(const uint8_t *& data)
{
   int res;
   memcpy(&res, data, sizeof(int));
   data += sizeof(int);
   return res;
}

void utilReadMem(void *buf, const uint8_t *& data, unsigned size)
{
   memcpy(buf, data, size);
   data += size;
}

void utilReadDataMem(const uint8_t *& data, variable_desc *desc)
{
   while (desc->address)
   {
      utilReadMem(desc->address, data, desc->size);
      desc++;
   }
}
#endif
