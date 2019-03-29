/* vim:expandtab:ts=2 sw=2:
*/
/*  Grafx2 - The Ultimate 256-color bitmap paint program

    Copyright 2018 Thomas Bernard
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

#if defined(_MSC_VER)
#include <windows.h>
#if _MSC_VER < 1900
#define snprintf _snprintf
#endif
#endif
#if defined(USE_SDL2)
#include <SDL.h>
#endif
#include <stdio.h>
#include <string.h>
#include "gfx2log.h"

GFX2_Log_priority_T GFX2_verbosity_level = GFX2_INFO;

extern void GFX2_Log(GFX2_Log_priority_T priority, const char * fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  GFX2_LogV(priority, fmt, ap);
  va_end(ap);
}

extern void GFX2_LogV(GFX2_Log_priority_T priority, const char * fmt, va_list ap)
{
#if !defined(_DEBUG)
  if ((unsigned)GFX2_verbosity_level < (unsigned)priority)
    return;
#endif
#if defined(__EMSCRIPTEN__) || !defined(USE_SDL2)
  vfprintf((unsigned)priority >= GFX2_INFO ? stdout : stderr, fmt, ap);
#else
  {
    int sdl_priority;
    switch(priority)
    {
      case GFX2_ERROR:
        sdl_priority = SDL_LOG_PRIORITY_ERROR;
        break;
      case GFX2_WARNING:
        sdl_priority = SDL_LOG_PRIORITY_WARN;
        break;
      case GFX2_INFO:
        sdl_priority = SDL_LOG_PRIORITY_INFO;
        break;
      case GFX2_DEBUG:
        sdl_priority = SDL_LOG_PRIORITY_DEBUG;
        break;
      default:
        sdl_priority = SDL_LOG_PRIORITY_CRITICAL; // unknown
    }
    SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, sdl_priority, fmt, ap);
  }
#endif
#if defined(_MSC_VER) && defined(_DEBUG)
  {
    char message[1024];
    vsnprintf(message, sizeof(message), fmt, ap);
    OutputDebugStringA(message);
  }
#endif
}

extern void GFX2_LogHexDump(GFX2_Log_priority_T priority, const char * header, const byte * data, long offset, long count)
{
  char line[128];
  long i;
  while (count > 0)
  {
    int p = 0, r;
    r = snprintf(line + p, sizeof(line) - p, "%s%06lX:", header, offset);
    if (r < 0)
      return;
    p += r;
    for (i = 0; i < count && i < 16; i++)
    {
      r = snprintf(line + p, sizeof(line) - p, " %02x", data[offset+i]);
      if (r < 0)
        return;
      p += r;
    }
    if (i < 16)
    {
      memset(line + p, ' ', 3 * (16 - i));
      p += 3 * (16 - i);
    }
    line[p++] = ' ';
    line[p++] = '|';
    line[p++] = ' ';
    for (i = 0; i < count && i < 16; i++)
      line[p++] = data[offset+i]>=32 && data[offset+i]<127 ? data[offset+i] : '.';
    line[p++] = '\0';
    GFX2_Log(priority, "%s\n", line);
    count -= i;
    offset += i;
  }
}
