// Single translation unit for the stb_image implementation.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_GIF
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_PSD
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
