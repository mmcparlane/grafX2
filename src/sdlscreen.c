/* vim:expandtab:ts=2 sw=2:
*/
/*  Grafx2 - The Ultimate 256-color bitmap paint program

    Copyright 2014 Sergii Pylypenko
    Copyright 2011 Pawel Góralski
    Copyright 2008 Yves Rizoud
    Copyright 2008 Franck Charlet
    Copyright 2007 Adrien Destugues
    Copyright 1996-2001 Sunset Design (Guillaume Dorme & Karl Maritaud)

    Grafx2 is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; version 2
    of the License.

    Grafx2 is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Grafx2; if not, see <http://www.gnu.org/licenses/>
*/
#include <string.h>
#include <stdlib.h>
#include <SDL.h>
#include <SDL_endian.h>
#if defined(__WIN32__)
    #include <windows.h>
#endif
// There is no WM on the GP2X...
#ifndef __GP2X__
    #include <SDL_syswm.h>
#endif

#include "global.h"
#include "sdlscreen.h"
#include "errors.h"
#include "misc.h"

// Update method that does a large number of small rectangles, aiming
// for a minimum number of total pixels updated.
#define UPDATE_METHOD_MULTI_RECTANGLE 1
// Intermediate update method, does only one update with the smallest
// rectangle that includes all modified pixels.
#define UPDATE_METHOD_CUMULATED       2
// Total screen update, for platforms that impose a Vsync on each SDL update.
#define UPDATE_METHOD_FULL_PAGE       3

// UPDATE_METHOD can be set from makefile, otherwise it's selected here
// depending on the platform :
#ifndef UPDATE_METHOD
  #if defined(__macosx__)
    #define UPDATE_METHOD     UPDATE_METHOD_FULL_PAGE
  #elif defined(__MINT__)
    #define UPDATE_METHOD     UPDATE_METHOD_CUMULATED
  #elif defined(GCWZERO)
    #define UPDATE_METHOD     UPDATE_METHOD_FULL_PAGE
  #elif defined(__ANDROID__)
    #define UPDATE_METHOD     UPDATE_METHOD_FULL_PAGE
  #else
    #define UPDATE_METHOD     UPDATE_METHOD_CUMULATED
  #endif
#endif

static SDL_Surface * Screen_SDL = NULL;
#if defined(USE_SDL2)
static SDL_Window * Window_SDL = NULL;
static SDL_Renderer * Renderer_SDL = NULL;
static SDL_Texture * Texture_SDL = NULL;
#endif

volatile int Allow_colorcycling=1;

byte Get_Screen_pixel(int x, int y)
{
  if (y < 0 || x < 0 || y >= Screen_SDL->h || x >= Screen_SDL->w)
  {
    Warning("Get_Screen_pixel() coordinates out of bound");
    return 0;
  }
  return ((byte *)Screen_SDL->pixels)[x + y*(Screen_SDL->pitch)];
}

void Set_Screen_pixel(int x, int y, byte value)
{
  if (y < 0 || x < 0 || y >= Screen_SDL->h || x >= Screen_SDL->w)
  {
    Warning("Set_Screen_pixel() coordinates out of bound");
    return;
  }
  ((byte *)Screen_SDL->pixels)[x + y*(Screen_SDL->pitch)] = value;
}

byte* Get_Screen_pixel_ptr(int x, int y)
{
  if (y < 0 || x < 0 || y >= Screen_SDL->h || x >= Screen_SDL->w)
  {
    Warning("Get_Screen_pixel_ptr() coordinates out of bound");
    return NULL;
  }
  return (byte *)Screen_SDL->pixels + x + y*(Screen_SDL->pitch);
}

void Screen_FillRect(int x, int y, int w, int h, byte color)
{
  SDL_Rect rectangle;
  rectangle.x = x;
  rectangle.y = y;
  rectangle.w = w;
  rectangle.h = h;
  SDL_FillRect(Screen_SDL,&rectangle,color);
}

/// Sets the new screen/window dimensions.
void Set_mode_SDL(int *width, int *height, int fullscreen)
{
  static SDL_Cursor* cur = NULL;
  static byte cursorData = 0;

#if defined(USE_SDL)
#ifdef GCWZERO
  Screen_SDL=SDL_SetVideoMode(*width,*height,8,SDL_HWSURFACE|SDL_TRIPLEBUF|(fullscreen?SDL_FULLSCREEN:0)|SDL_RESIZABLE);
#else
  Screen_SDL=SDL_SetVideoMode(*width,*height,8,(fullscreen?SDL_FULLSCREEN:0)|SDL_RESIZABLE);
#endif
  if(Screen_SDL != NULL)
  {
    // Check the mode we got, in case it was different from the one we requested.
    if (Screen_SDL->w != *width || Screen_SDL->h != *height)
    {
      DEBUG("Error: Got a different video mode than the requested one!",0);
      *width = Screen_SDL->w;
      *height = Screen_SDL->h;
    }
    //Screen_pixels=Screen_SDL->pixels;
  }
  else
  {
    DEBUG("Error: Unable to change video mode!",0);
  }
#else
  // SDL2
  Window_SDL = SDL_CreateWindow("GrafX2", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                *width, *height, (fullscreen?SDL_WINDOW_FULLSCREEN:SDL_WINDOW_RESIZABLE));
  Renderer_SDL = SDL_CreateRenderer(Window_SDL, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  Texture_SDL = SDL_CreateTexture(Renderer_SDL, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, *width, *height);
  Screen_SDL = SDL_CreateRGBSurface(0, *width, *height, 8, 0, 0, 0, 0);
#endif

  // Trick borrowed to Barrage (http://www.mail-archive.com/debian-bugs-dist@lists.debian.org/msg737265.html) :
  // Showing the cursor but setting it to fully transparent allows us to get absolute mouse coordinates,
  // this means we can use tablet in fullscreen mode.

  SDL_FreeCursor(cur);
  cur = SDL_CreateCursor(&cursorData, &cursorData, 1,1,0,0);
  if (cur != NULL)
  {
    SDL_SetCursor(cur);
    SDL_ShowCursor(SDL_ENABLE); // show the SDL 1 pixel transparent cursor
  }
  else
  {
    // failed to create the 1 pixel transparent cursor
    SDL_ShowCursor(SDL_DISABLE); // Hide the SDL mouse cursor, we use our own internal one
  }
}

#if defined(USE_SDL2)
static void GFX2_UpdateRect(int x, int y, int width, int height)
{
  byte * pixels;
  int pitch;
  int line;
  static SDL_Surface *RGBcopy = NULL;
  SDL_Rect source_rect;

  source_rect.x = x;
  source_rect.y = y;
  source_rect.w = width;
  source_rect.h = height;

  if  (RGBcopy == NULL)
  {
    RGBcopy = SDL_CreateRGBSurface(0,
    Screen_SDL->w, Screen_SDL->h,
    32, 0, 0, 0, 0);
  }
  // conversion ARGB
  SDL_BlitSurface(Screen_SDL, &source_rect, RGBcopy, &source_rect);
  // upload texture
  SDL_LockTexture(Texture_SDL, &source_rect, (void **)(&pixels), &pitch );
  for (line = 0; line < source_rect.h; line++)
  {
     memcpy(pixels + line * pitch, RGBcopy->pixels + source_rect.x * 4 + (source_rect.y+line)* RGBcopy->pitch, source_rect.w * 4 );
  }
  SDL_UnlockTexture(Texture_SDL);
  SDL_RenderCopy(Renderer_SDL, Texture_SDL, &source_rect, &source_rect);
  SDL_RenderPresent(Renderer_SDL);
}
#endif

#if (UPDATE_METHOD == UPDATE_METHOD_CUMULATED)
short Min_X=0;
short Min_Y=0;
short Max_X=10000;
short Max_Y=10000;
short Status_line_dirty_begin=0;
short Status_line_dirty_end=0;
#endif

#if (UPDATE_METHOD == UPDATE_METHOD_FULL_PAGE)
  int update_is_required=0;
#endif

void Flush_update(void)
{
#if (UPDATE_METHOD == UPDATE_METHOD_FULL_PAGE)
  // Do a full screen update
  if (update_is_required)
  {
#ifdef GCWZERO
    SDL_Flip(Screen_SDL);
#elif defined(USE_SDL)
    SDL_UpdateRect(Screen_SDL, 0, 0, 0, 0);
#else
    GFX2_UpdateRect(0, 0, 0, 0);
#endif
    update_is_required=0;
  }
#endif
  #if (UPDATE_METHOD == UPDATE_METHOD_CUMULATED)
  if (Min_X>=Max_X || Min_Y>=Max_Y)
  {
    ; // Nothing to do
  }
  else
  {
    if (Min_X<0)
      Min_X=0;
    if (Min_Y<0)
      Min_Y=0;
#if defined(USE_SDL)
    SDL_UpdateRect(Screen_SDL, Min_X*Pixel_width, Min_Y*Pixel_height, Min(Screen_width-Min_X, Max_X-Min_X)*Pixel_width, Min(Screen_height-Min_Y, Max_Y-Min_Y)*Pixel_height);
#else
    GFX2_UpdateRect(Min_X*Pixel_width, Min_Y*Pixel_height, Min(Screen_width-Min_X, Max_X-Min_X)*Pixel_width, Min(Screen_height-Min_Y, Max_Y-Min_Y)*Pixel_height);
#endif

    Min_X=Min_Y=10000;
    Max_X=Max_Y=0;
  }
  if (Status_line_dirty_end)
  {
#if defined(USE_SDL)
    SDL_UpdateRect(Screen_SDL, (18+(Status_line_dirty_begin*8))*Menu_factor_X*Pixel_width,Menu_status_Y*Pixel_height,(Status_line_dirty_end-Status_line_dirty_begin)*8*Menu_factor_X*Pixel_width,8*Menu_factor_Y*Pixel_height);
#else
    GFX2_UpdateRect((18+(Status_line_dirty_begin*8))*Menu_factor_X*Pixel_width,Menu_status_Y*Pixel_height,(Status_line_dirty_end-Status_line_dirty_begin)*8*Menu_factor_X*Pixel_width,8*Menu_factor_Y*Pixel_height);
#endif
  }
  Status_line_dirty_begin=25;
  Status_line_dirty_end=0;
  
    #endif

}

void Update_rect(short x, short y, unsigned short width, unsigned short height)
{
  #if (UPDATE_METHOD == UPDATE_METHOD_MULTI_RECTANGLE)
    #if defined(USE_SDL)
    SDL_UpdateRect(Screen_SDL, x*Pixel_width, y*Pixel_height, width*Pixel_width, height*Pixel_height);
    #else
    GFX2_UpdateRect(x*Pixel_width, y*Pixel_height, width*Pixel_width, height*Pixel_height);
    #endif
  #endif

  #if (UPDATE_METHOD == UPDATE_METHOD_CUMULATED)
  if (width==0 || height==0)
  {
    Min_X=Min_Y=0;
    Max_X=Max_Y=10000;
  }
  else
  {
    if (x < Min_X)
      Min_X = x;
    if (y < Min_Y)
      Min_Y = y;
    if (x+width>Max_X)
      Max_X=x+width;
    if (y+height>Max_Y)
      Max_Y=y+height;
  }
  #endif

  #if (UPDATE_METHOD == UPDATE_METHOD_FULL_PAGE)
  (void)x; (void)y; (void)width; (void)height; // unused
  update_is_required=1;
  #endif

}

void Update_status_line(short char_pos, short width)
{
  #if (UPDATE_METHOD == UPDATE_METHOD_MULTI_RECTANGLE)
  #if defined(USE_SDL)
  SDL_UpdateRect(Screen_SDL, (18+char_pos*8)*Menu_factor_X*Pixel_width,Menu_status_Y*Pixel_height,width*8*Menu_factor_X*Pixel_width,8*Menu_factor_Y*Pixel_height);
  #else
  GFX2_UpdateRect((18+char_pos*8)*Menu_factor_X*Pixel_width,Menu_status_Y*Pixel_height,width*8*Menu_factor_X*Pixel_width,8*Menu_factor_Y*Pixel_height);
  #endif
  #endif

  #if (UPDATE_METHOD == UPDATE_METHOD_CUMULATED)
  // Merge the ranges
  if (Status_line_dirty_end < char_pos+width)
    Status_line_dirty_end=char_pos+width;
  if (Status_line_dirty_begin > char_pos)
    Status_line_dirty_begin=char_pos;
  #endif

  #if (UPDATE_METHOD == UPDATE_METHOD_FULL_PAGE)
  (void)char_pos; // unused parameter
  (void)width; // unused parameter
  update_is_required=1;
  #endif

}

///
/// Converts a SDL_Surface (indexed colors or RGB) into an array of bytes
/// (indexed colors).
/// If dest is NULL, it's allocated by malloc(). Otherwise, be sure to
/// pass a buffer of the right dimensions.
byte * Surface_to_bytefield(SDL_Surface *source, byte * dest)
{
  byte *src;
  byte *dest_ptr;
  int y;
  int remainder;

  // Support seulement des images 256 couleurs
  if (source->format->BytesPerPixel != 1)
    return NULL;

  if (source->w & 3)
    remainder=4-(source->w&3);
  else
    remainder=0;

  if (dest==NULL)
    dest=(byte *)malloc(source->w*source->h);

  dest_ptr=dest;
  src=(byte *)(source->pixels);
  for(y=0; y < source->h; y++)
  {
    memcpy(dest_ptr, src,source->w);
    dest_ptr += source->w;
    src += source->w + remainder;
  }
  return dest;

}

/// Gets the RGB 24-bit color currently associated with a palette index.
SDL_Color Color_to_SDL_color(byte index)
{
  SDL_Color color;
  color.r = Main.palette[index].R;
  color.g = Main.palette[index].G;
  color.b = Main.palette[index].B;
#if defined(USE_SDL)
  color.unused = 255;
#else
  color.a = 255;
#endif
  return color;
}

/// Reads a pixel in a 8-bit SDL surface.
byte Get_SDL_pixel_8(const SDL_Surface *bmp, int x, int y)
{
  return ((byte *)(bmp->pixels))[(y*bmp->pitch+x)];
}

/// Writes a pixel in a 8-bit SDL surface.
void Set_SDL_pixel_8(SDL_Surface *bmp, int x, int y, byte color)
{
  ((byte *)(bmp->pixels))[(y*bmp->pitch+x)]=color;
}


/// Reads a pixel in a multi-byte SDL surface.
dword Get_SDL_pixel_hicolor(const SDL_Surface *bmp, int x, int y)
{
  byte * ptr;
  
  switch(bmp->format->BytesPerPixel)
  {
    case 4:
    default:
      return *((dword *)((byte *)bmp->pixels+(y*bmp->pitch+x*4)));
    case 3:
      // Reading a 4-byte number starting at an address that isn't a multiple
      // of 2 (or 4?) is not supported on Caanoo console at least (ARM CPU)
      // So instead, we will read the 3 individual bytes, and re-construct the
      // "dword" expected by SDL.
      ptr = ((byte *)bmp->pixels)+(y*bmp->pitch+x*3);
      #ifdef SDL_LIL_ENDIAN
      // Read ABC, output _CBA : Most Significant Byte is zero.
      return (*ptr) | (*(ptr+1)<<8) | (*(ptr+2)<<16);
      #else
      // Read ABC, output ABC_ : Least Significant Byte is zero.
      return ((*ptr)<<24) | (*(ptr+1)<<16) | (*(ptr+2)<<8);
      #endif
    case 2:
      return *((word *)((byte *)bmp->pixels+(y*bmp->pitch+x*2)));
  }
}

/// Convert a SDL Palette to a grafx2 palette
void Get_SDL_Palette(const SDL_Palette * sdl_palette, T_Palette palette)
{
  int i;
  
  for (i=0; i<256; i++)
  {
    palette[i].R=sdl_palette->colors[i].r;
    palette[i].G=sdl_palette->colors[i].g;
    palette[i].B=sdl_palette->colors[i].b;
  }

}

int SetPalette(const T_Components * colors, int firstcolor, int ncolors)
{
  int i;
  SDL_Color PaletteSDL[256];

  for (i = 0; i < ncolors; i++) {
    PaletteSDL[i].r = colors[i].R;
    PaletteSDL[i].g = colors[i].G;
    PaletteSDL[i].b = colors[i].B;
  }
#if defined(USE_SDL)
  return SDL_SetPalette(Screen_SDL, SDL_PHYSPAL | SDL_LOGPAL, PaletteSDL, firstcolor, ncolors);
#else
  return SDL_SetPaletteColors(Screen_SDL->format->palette, PaletteSDL, firstcolor, ncolors);
#endif
}

void Clear_border(byte color)
{
  int width;
  int height;
  
  // This function can be called before the graphics mode is set.
  // Nothing to do then.
  if (!Screen_SDL)
    return;
  
  width = Screen_SDL->w - Screen_width*Pixel_width;
  height = Screen_SDL->h - Screen_height*Pixel_height;
  if (width)
  {
    SDL_Rect r;
    r.x=Screen_SDL->w - width;
    r.y=0;
    r.h=Screen_SDL->h;
    r.w=width;
    SDL_FillRect(Screen_SDL,&r,color);
#if defined(USE_SDL)
    SDL_UpdateRect(Screen_SDL, r.x, r.y, r.w, r.h);
#else
    //SDL_RenderPresent(
#endif
  }
  if (height)
  {
    SDL_Rect r;
    r.x=0;
    r.y=Screen_SDL->h - height;
    r.h=height;
    r.w=Screen_SDL->w - height;
    SDL_FillRect(Screen_SDL,&r,color);
#if defined(USE_SDL)
    SDL_UpdateRect(Screen_SDL, r.x, r.y, r.w, r.h);
#else
// TODO
#endif
  }  
}

/// Activates or desactivates file drag-dropping in program window.
void Allow_drag_and_drop(int flag)
{
  // Inform Windows that we accept drag-n-drop events or not
  #ifdef __WIN32__
  SDL_SysWMinfo wminfo;
  HWND hwnd;
  
  SDL_VERSION(&wminfo.version);
  SDL_GetWMInfo(&wminfo);
  hwnd = wminfo.window;
  DragAcceptFiles(hwnd,flag?TRUE:FALSE);
  SDL_EventState (SDL_SYSWMEVENT,flag?SDL_ENABLE:SDL_DISABLE );
  #else
  (void)flag; // unused
  #endif
}
