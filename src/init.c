/* vim:expandtab:ts=2 sw=2:
*/
/*  Grafx2 - The Ultimate 256-color bitmap paint program

    Copyright 2011 Pawel Góralski
    Copyright 2008 Peter Gordon
    Copyright 2008 Yves Rizoud
    Copyright 2009 Franck Charlet
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

// Signal handler: I activate it for the two platforms who certainly
// support them. Feel free to check with others.
#if defined(WIN32) || defined(__linux__)
  #define GRAFX2_CATCHES_SIGNALS
#endif

#if defined(__amigaos4__) || defined(__AROS__) || defined(__MORPHOS__) || defined(__amigaos__)
  #include <proto/exec.h>
  #include <proto/dos.h>
#endif

#include <stdio.h>
#include <string.h>
#include <strings.h>

#if !defined(__VBCC__) && !defined(_MSC_VER)
    #include <unistd.h>
#endif

#include <stdlib.h>
#include <errno.h>
#if defined(USE_SDL) || defined(USE_SDL2)
#include <SDL_image.h>
#endif
#if defined(USE_SDL)
#include <SDL_byteorder.h>
#endif
#if defined(WIN32)
  #include <windows.h> // GetLogicalDrives(), GetDriveType(), DRIVE_*
#endif
#if defined (__MINT__)
  #include <mint/osbind.h>
#endif
#ifdef GRAFX2_CATCHES_SIGNALS
  #include <signal.h>
#endif
#if defined(_MSC_VER)
#define strncasecmp _strnicmp
#define strdup _strdup
#if _MSC_VER < 1900
#define snprintf _snprintf
#endif
#endif

#include "buttons.h"
#include "const.h"
#include "errors.h"
#include "global.h"
#include "graph.h"
#include "init.h"
#include "io.h"
#include "factory.h"
#include "help.h"
#include "hotkeys.h"
#include "keyboard.h"
#include "loadsave.h" // Image_emergency_backup
#include "misc.h"
#include "mountlist.h" // read_file_system_list
#include "operatio.h"
#include "palette.h"
#include "screen.h"
#if defined(USE_SDL) || defined(USE_SDL2)
#include "sdlscreen.h"
#endif
#include "setup.h"
#include "struct.h"
#include "transform.h"
#include "windows.h"
#include "layers.h"
#include "special.h"
#include "gfx2surface.h"

char Gui_loading_error_message[512];

// Rechercher la liste et le type des lecteurs de la machine

#if defined(__amigaos4__) || defined(__AROS__) || defined(__MORPHOS__) || defined(__amigaos__)
void bstrtostr( BSTR in, STRPTR out, TEXT max );
#endif

// Fonctions de lecture dans la skin de l'interface graphique
static byte GUI_seek_down(T_GFX2_Surface *gui, int *start_x, int *start_y, byte neutral_color,char * section)
{
  byte color;
  int y;
  y=*start_y;
  *start_x=0;
  do
  {
    color=Get_GFX2_Surface_pixel(gui,*start_x,y);
    if (color!=neutral_color)
    {
      *start_y=y;
      return 0;
    }
    y++;
  } while (y<gui->h);
  
  sprintf(Gui_loading_error_message, "Error in skin file: Was looking down from %d,%d for a '%s', and reached the end of the image\n",
    *start_x, *start_y, section);
  return 1;
}

static byte GUI_seek_right(T_GFX2_Surface *gui, int *start_x, int start_y, byte neutral_color, char * section)
{
  byte color;
  int x;
  x=*start_x;
  
  do
  {
    color=Get_GFX2_Surface_pixel(gui,x,start_y);
    if (color!=neutral_color)
    {
      *start_x=x;
      return 0;
    }
    x++;
  } while (x<gui->w);
  
  sprintf(Gui_loading_error_message, "Error in skin file: Was looking right from %d,%d for a '%s', and reached the edege of the image\n",
    *start_x, start_y, section);
  return 1;
}

static byte Read_GUI_block(T_Gui_skin *gfx, T_GFX2_Surface *gui, int start_x, int start_y, void *dest, int width, int height, char * section, int type)
{
  // type: 0 = normal GUI element, only 4 colors allowed
  // type: 1 = mouse cursor, 4 colors allowed + transparent
  // type: 2 = brush icon or sieve pattern (only gui->Color[3] and gui->Color_trans)
  // type: 3 = raw bitmap (splash screen)
  
  byte * dest_ptr=dest;
  int x,y;
  byte color;

  // Verification taille
  if (start_y+height>=gui->h || start_x+width>=gui->w)
  {
    sprintf(Gui_loading_error_message, "Error in skin file: Was looking at %d,%d for a %d*%d object (%s) but it doesn't fit the image.\n",
      start_x, start_y, height, width, section);
    return 1;
  }

  for (y=start_y; y<start_y+height; y++)
  {
    for (x=start_x; x<start_x+width; x++)
    {
      color=Get_GFX2_Surface_pixel(gui,x,y);
      if (type==0 && (color != gfx->Color[0] && color != gfx->Color[1] && color != gfx->Color[2] && color != gfx->Color[3]))
      {
        sprintf(Gui_loading_error_message, "Error in skin file: Was looking at %d,%d for a %d*%d object (%s) but at %d,%d a pixel was found with color %d which isn't one of the GUI colors (which were detected as %d,%d,%d,%d.\n",
          start_x, start_y, height, width, section, x, y, color, gfx->Color[0], gfx->Color[1], gfx->Color[2], gfx->Color[3]);
        return 1;
      }
      if (type==1 && (color != gfx->Color[0] && color != gfx->Color[1] && color != gfx->Color[2] && color != gfx->Color[3] && color != gfx->Color_trans))
      {
        sprintf(Gui_loading_error_message, "Error in skin file: Was looking at %d,%d for a %d*%d object (%s) but at %d,%d a pixel was found with color %d which isn't one of the mouse colors (which were detected as %d,%d,%d,%d,%d.\n",
          start_x, start_y, height, width, section, x, y, color, gfx->Color[0], gfx->Color[1], gfx->Color[2], gfx->Color[3], gfx->Color_trans);
        return 1;
      }
      if (type==2)
      {
        if (color != gfx->Color[3] && color != gfx->Color_trans)
        {
          sprintf(Gui_loading_error_message, "Error in skin file: Was looking at %d,%d for a %d*%d object (%s) but at %d,%d a pixel was found with color %d which isn't one of the brush colors (which were detected as %d on %d.\n",
            start_x, start_y, height, width, section, x, y, color, gfx->Color[3], gfx->Color_trans);
          return 1;
        }
        // Conversion en 0/1 pour les brosses monochromes internes
        color = (color != gfx->Color_trans);
      }
      *dest_ptr=color;
      dest_ptr++;
    }
  }
  return 0;
}

static byte Read_GUI_pattern(T_Gui_skin *gfx, T_GFX2_Surface *gui, int start_x, int start_y, word *dest, char * section)
{
  byte buffer[256];
  int x,y;
  
  if (Read_GUI_block(gfx, gui, start_x, start_y, buffer, 16, 16, section, 2))
    return 1;

  for (y=0; y<16; y++)
  {
    *dest=0;
    for (x=0; x<16; x++)
    {
      *dest=*dest | buffer[y*16+x]<<x;
    }
    dest++;
  }
  return 0;
}

void Center_GUI_cursor(T_Gui_skin *gfx, byte *cursor_buffer, int cursor_number)
{
  int x,y;
  int start_x, start_y;
  byte found;

  // Locate first non-empty column
  found=0;
  for (start_x=0;start_x<14;start_x++)
  {
    for (y=0;y<29;y++)
    {
      if (cursor_buffer[y*29+start_x]!=gfx->Color_trans)
      {
        found=1;
        break;
      }
    }
    if (found)
      break;
  }
  // Locate first non-empty line
  found=0;
  for (start_y=0;start_y<14;start_y++)
  {
    for (x=0;x<29;x++)
    {
      if (cursor_buffer[start_y*29+x]!=gfx->Color_trans)
      {
        found=1;
        break;
      }
    }
    if (found)
      break;
  }
  gfx->Cursor_offset_X[cursor_number]=14-start_x;
  gfx->Cursor_offset_Y[cursor_number]=14-start_y;

  for (y=0;y<CURSOR_SPRITE_HEIGHT;y++)
    for (x=0;x<CURSOR_SPRITE_WIDTH;x++)
      gfx->Cursor_sprite[cursor_number][y][x]=cursor_buffer[(start_y+y)*29+start_x+x];
}

static byte Parse_skin(T_GFX2_Surface * gui, T_Gui_skin *gfx)
{
  int i,j;
  int cursor_x=0,cursor_y=0;
  byte color;
  byte neutral_color; // color neutre utilisée pour délimiter les éléments GUI
  int char_1=0;  // Indices utilisés pour les 4 "fontes" qui composent les
  int char_2=0;  // grands titres de l'aide. Chaque indice avance dans 
  int char_3=0;  // l'une des fontes dans l'ordre :  1 2
  int char_4=0;  //                                  3 4
  byte mouse_cursor_area[31][29];
  
  // Default palette
  memcpy(gfx->Default_palette, gui->palette, sizeof(T_Palette));

  // Carré "noir"
  gfx->Color[0] = Get_GFX2_Surface_pixel(gui,cursor_x,cursor_y);
  do
  {
    if (++cursor_x>=gui->w)
    {
      sprintf(Gui_loading_error_message, "Error in GUI skin file: should start with 5 consecutive squares for black, dark, light, white, transparent, then a neutral color\n");
      return 1;
    }
    color=Get_GFX2_Surface_pixel(gui,cursor_x,cursor_y);
  } while(color==gfx->Color[0]);
  // Carré "foncé"
  gfx->Color[1] = color;
  do
  {
    if (++cursor_x>=gui->w)
    {
      sprintf(Gui_loading_error_message, "Error in GUI skin file: should start with 5 consecutive squares for black, dark, light, white, transparent, then a neutral color\n");
      return 1;
    }
    color=Get_GFX2_Surface_pixel(gui,cursor_x,cursor_y);
  } while(color==gfx->Color[1]);
  // Carré "clair"
  gfx->Color[2] = color;
  do
  {
    if (++cursor_x>gui->w)
    {
      sprintf(Gui_loading_error_message, "Error in GUI skin file: should start with 5 consecutive squares for black, dark, light, white, transparent, then a neutral color\n");
      return 1;
    }
    color=Get_GFX2_Surface_pixel(gui,cursor_x,cursor_y);
  } while(color==gfx->Color[2]);
  // Carré "blanc"
  gfx->Color[3] = color;
  do
  {
    if (++cursor_x>=gui->w)
    {
      sprintf(Gui_loading_error_message, "Error in GUI skin file: should start with 5 consecutive squares for black, dark, light, white, transparent, then a neutral color\n");
      return 1;
    }
    color=Get_GFX2_Surface_pixel(gui,cursor_x,cursor_y);
  } while(color==gfx->Color[3]);
  // Carré "transparent"
  gfx->Color_trans=color;
  do
  {
    if (++cursor_x>=gui->w)
    {
      sprintf(Gui_loading_error_message, "Error in GUI skin file: should start with 5 consecutive squares for black, dark, light, white, transparent, then a neutral color\n");
      return 1;
    }
    color=Get_GFX2_Surface_pixel(gui,cursor_x,cursor_y);
  } while(color==gfx->Color_trans);
  // Reste : couleur neutre
  neutral_color=color;

  
  cursor_x=0;
  cursor_y=1;
  while ((color=Get_GFX2_Surface_pixel(gui,cursor_x,cursor_y))==gfx->Color[0])
  {
    cursor_y++;
    if (cursor_y>=gui->h)
    {
      sprintf(Gui_loading_error_message, "Error in GUI skin file: should start with 5 consecutive squares for black, dark, light, white, transparent, then a neutral color\n");
      return 1;
    }
  }
  
  // Menu
  if (GUI_seek_down(gui, &cursor_x, &cursor_y, neutral_color, "menu"))
    return 1;
  if (Read_GUI_block(gfx, gui, cursor_x, cursor_y, gfx->Menu_block[0], Menu_bars[MENUBAR_TOOLS].Skin_width, Menu_bars[MENUBAR_TOOLS].Height,"menu",0))
    return 1;

  // Preview
  cursor_x += Menu_bars[MENUBAR_TOOLS].Skin_width;
  if (GUI_seek_right(gui, &cursor_x, cursor_y, neutral_color, "preview"))
    return 1;
  if (Read_GUI_block(gfx, gui, cursor_x, cursor_y, gfx->Preview, 173, 16, "preview", 0))
    return 1;
  cursor_y+= Menu_bars[MENUBAR_TOOLS].Height;

  // Layerbar
  if (GUI_seek_down(gui, &cursor_x, &cursor_y, neutral_color, "layer bar"))
    return 1;
  if (Read_GUI_block(gfx, gui, cursor_x, cursor_y, gfx->Layerbar_block[0], 144, 10,"layer bar",0))
    return 1;
  cursor_y+= Menu_bars[MENUBAR_LAYERS].Height;

  // Animbar
  if (GUI_seek_down(gui, &cursor_x, &cursor_y, neutral_color, "anim bar"))
    return 1;
  if (Read_GUI_block(gfx, gui, cursor_x, cursor_y, gfx->Animbar_block[0], 236, 14,"anim bar",0))
    return 1;
  cursor_y+= Menu_bars[MENUBAR_ANIMATION].Height;

  // Status bar
  if (GUI_seek_down(gui, &cursor_x, &cursor_y, neutral_color, "status bar"))
    return 1;
  if (Read_GUI_block(gfx, gui, cursor_x, cursor_y, gfx->Statusbar_block[0], Menu_bars[MENUBAR_STATUS].Skin_width, Menu_bars[MENUBAR_STATUS].Height,"status bar",0))
    return 1;
  cursor_y+= Menu_bars[MENUBAR_STATUS].Height;

  // Menu (selected)
  if (GUI_seek_down(gui, &cursor_x, &cursor_y, neutral_color, "selected menu"))
    return 1;
  if (Read_GUI_block(gfx, gui, cursor_x, cursor_y, gfx->Menu_block[1], Menu_bars[MENUBAR_TOOLS].Skin_width, Menu_bars[MENUBAR_TOOLS].Height,"selected menu",0))
    return 1;
  cursor_y+= Menu_bars[MENUBAR_TOOLS].Height;

  // Layerbar (selected)
  if (GUI_seek_down(gui, &cursor_x, &cursor_y, neutral_color, "selected layer bar"))
    return 1;
  if (Read_GUI_block(gfx, gui, cursor_x, cursor_y, gfx->Layerbar_block[1], 144, 10,"selected layer bar",0))
    return 1;
  cursor_y+= Menu_bars[MENUBAR_LAYERS].Height;

  // Animbar (selected)
  if (GUI_seek_down(gui, &cursor_x, &cursor_y, neutral_color, "selected anim bar"))
    return 1;
  if (Read_GUI_block(gfx, gui, cursor_x, cursor_y, gfx->Animbar_block[1], 236, 14,"selected anim bar",0))
    return 1;
  cursor_y+= Menu_bars[MENUBAR_ANIMATION].Height;

  // Status bar (selected)
  if (GUI_seek_down(gui, &cursor_x, &cursor_y, neutral_color, "selected status bar"))
    return 1;
  if (Read_GUI_block(gfx, gui, cursor_x, cursor_y, gfx->Statusbar_block[1], Menu_bars[MENUBAR_STATUS].Skin_width, Menu_bars[MENUBAR_STATUS].Height,"selected status bar",0))
    return 1;
  cursor_y+= Menu_bars[MENUBAR_STATUS].Height;

  // Effects
  for (i=0; i<NB_EFFECTS_SPRITES; i++)
  {
    if (i==0)
    {
      if (GUI_seek_down(gui, &cursor_x, &cursor_y, neutral_color, "effect sprite"))
        return 1;
    }
    else
    {
      if (GUI_seek_right(gui, &cursor_x, cursor_y, neutral_color, "effect sprite"))
        return 1;
    }
    if (Read_GUI_block(gfx, gui, cursor_x, cursor_y, gfx->Effect_sprite[i], EFFECT_SPRITE_WIDTH, EFFECT_SPRITE_HEIGHT, "effect sprite",0))
      return 1;
    cursor_x+=EFFECT_SPRITE_WIDTH;
  }
  cursor_y+=EFFECT_SPRITE_HEIGHT;
  
  // Layer sprite
  for (j=0; j<3; j++)
  {
    for (i=0; i<16; i++)
    {
      if (i==0)
      {
        if (GUI_seek_down(gui, &cursor_x, &cursor_y, neutral_color, "layer sprite"))
          return 1;
      }
      else
      {
        if (GUI_seek_right(gui, &cursor_x, cursor_y, neutral_color, "layer sprite"))
          return 1;
      }
      if (Read_GUI_block(gfx, gui, cursor_x, cursor_y, gfx->Layer_sprite[j][i], LAYER_SPRITE_WIDTH, LAYER_SPRITE_HEIGHT, "layer sprite",1))
        return 1;
      cursor_x+=LAYER_SPRITE_WIDTH;
    }
    cursor_y+=LAYER_SPRITE_HEIGHT;
  }

  
  // Mouse cursors
  for (i=0; i<NB_CURSOR_SPRITES; i++)
  {
    if (i==0)
    {
      if (GUI_seek_down(gui, &cursor_x, &cursor_y, neutral_color, "mouse cursor"))
        return 1;
    }
    else
    {
      if (GUI_seek_right(gui, &cursor_x, cursor_y, neutral_color, "mouse cursor"))
        return 1;
    }
    if (Read_GUI_block(gfx, gui, cursor_x, cursor_y, mouse_cursor_area, 29, 31, "mouse cursor",1))
      return 1;
    Center_GUI_cursor(gfx, (byte *)mouse_cursor_area,i);
    cursor_x+=29;
  }
  cursor_y+=31;

  // Menu sprites
  for (i=0; i<NB_MENU_SPRITES; i++)
  {
    if (i==0)
    {
      if (GUI_seek_down(gui, &cursor_x, &cursor_y, neutral_color, "menu sprite"))
        return 1;
    }
    else
    {
      if (GUI_seek_right(gui, &cursor_x, cursor_y, neutral_color, "menu sprite"))
        return 1;
    }
    if (Read_GUI_block(gfx, gui, cursor_x, cursor_y, gfx->Menu_sprite[0][i], MENU_SPRITE_WIDTH, MENU_SPRITE_HEIGHT, "menu sprite",1))
      return 1;
    cursor_x+=MENU_SPRITE_WIDTH;
  }
  cursor_y+=MENU_SPRITE_HEIGHT;

  // Menu sprites (selected)
  for (i=0; i<NB_MENU_SPRITES; i++)
  {
    if (i==0)
    {
      if (GUI_seek_down(gui, &cursor_x, &cursor_y, neutral_color, "selected menu sprite"))
        return 1;
    }
    else
    {
      if (GUI_seek_right(gui, &cursor_x, cursor_y, neutral_color, "selected menu sprite"))
        return 1;
    }
    if (Read_GUI_block(gfx, gui, cursor_x, cursor_y, gfx->Menu_sprite[1][i], MENU_SPRITE_WIDTH, MENU_SPRITE_HEIGHT, "selected menu sprite",1))
      return 1;
    cursor_x+=MENU_SPRITE_WIDTH;
  }
  cursor_y+=MENU_SPRITE_HEIGHT;
  
  // Drive sprites
  for (i=0; i<NB_ICON_SPRITES; i++)
  {
    if (i==0)
    {
      if (GUI_seek_down(gui, &cursor_x, &cursor_y, neutral_color, "sprite drive"))
        return 1;
    }
    else
    {
      if (GUI_seek_right(gui, &cursor_x, cursor_y, neutral_color, "sprite drive"))
        return 1;
    }
    if (Read_GUI_block(gfx, gui, cursor_x, cursor_y, gfx->Icon_sprite[i], ICON_SPRITE_WIDTH, ICON_SPRITE_HEIGHT, "sprite drive",1))
      return 1;
    cursor_x+=ICON_SPRITE_WIDTH;
  }
  cursor_y+=ICON_SPRITE_HEIGHT;

  // Logo splash screen

  if (GUI_seek_down(gui, &cursor_x, &cursor_y, neutral_color, "logo menu"))
    return 1;
  if (Read_GUI_block(gfx, gui, cursor_x, cursor_y, gfx->Logo_grafx2, 231, 56, "logo menu",3))
    return 1;
  cursor_y+=56;
  
  // Trames
  for (i=0; i<NB_PRESET_SIEVE; i++)
  {
    if (i==0)
    {
      if (GUI_seek_down(gui, &cursor_x, &cursor_y, neutral_color, "sieve pattern"))
        return 1;
    }
    else
    {
      if (GUI_seek_right(gui, &cursor_x, cursor_y, neutral_color, "sieve pattern"))
        return 1;
    }
    if (Read_GUI_pattern(gfx, gui, cursor_x, cursor_y, gfx->Sieve_pattern[i],"sieve pattern"))
      return 1;
    cursor_x+=16;
  }
  cursor_y+=16;

  // Help font: Normal
  for (i=0; i<256; i++)
  {
    // Each line holds 32 symbols
    if ((i%32)==0)
    {
      if (i!=0)
        cursor_y+=8;
      if (GUI_seek_down(gui, &cursor_x, &cursor_y, neutral_color, "help font (norm)"))
        return 1;
    }
    else
    {
      if (GUI_seek_right(gui, &cursor_x, cursor_y, neutral_color, "help font (norm)"))
        return 1;
    }
    if (Read_GUI_block(gfx, gui, cursor_x, cursor_y, &(gfx->Help_font_norm[i][0][0]), 6, 8, "help font (norm)",0))
      return 1;
    cursor_x+=6;
  }
  cursor_y+=8;
  
  // Help font: Bold
  for (i=0; i<256; i++)
  {
    // Each line holds 32 symbols
    if ((i%32)==0)
    {
      if (i!=0)
        cursor_y+=8;
      if (GUI_seek_down(gui, &cursor_x, &cursor_y, neutral_color, "help font (bold)"))
        return 1;
    }
    else
    {
      if (GUI_seek_right(gui, &cursor_x, cursor_y, neutral_color, "help font (bold)"))
        return 1;
    }
    if (Read_GUI_block(gfx, gui, cursor_x, cursor_y, &(gfx->Bold_font[i][0][0]), 6, 8, "help font (bold)",0))
      return 1;
    cursor_x+=6;
  }
  cursor_y+=8;

  // Help font: Title
  for (i=0; i<256; i++)
  {
    byte * dest;
    // Each line holds 64 symbols
    if ((i%64)==0)
    {
      if (i!=0)
        cursor_y+=8;
      if (GUI_seek_down(gui, &cursor_x, &cursor_y, neutral_color, "help font (title)"))
        return 1;
    }
    else
    {
      if (GUI_seek_right(gui, &cursor_x, cursor_y, neutral_color, "help font (title)"))
        return 1;
    }
    
    if (i&1)
      if (i&64)
        dest=&(gfx->Help_font_t4[char_4++][0][0]);
      else
        dest=&(gfx->Help_font_t2[char_2++][0][0]);
    else
      if (i&64)
        dest=&(gfx->Help_font_t3[char_3++][0][0]);
      else
        dest=&(gfx->Help_font_t1[char_1++][0][0]);
    
    if (Read_GUI_block(gfx, gui, cursor_x, cursor_y, dest, 6, 8, "help font (title)",0))
      return 1;
    cursor_x+=6;
  }
  cursor_y+=8;
  
  // Copy unselected bitmaps to current ones
  memcpy(gfx->Menu_block[2], gfx->Menu_block[0], 
    Menu_bars[MENUBAR_TOOLS].Skin_width*Menu_bars[MENUBAR_TOOLS].Height);
  memcpy(gfx->Layerbar_block[2], gfx->Layerbar_block[0],
    sizeof(gfx->Layerbar_block[0]));
  memcpy(gfx->Animbar_block[2], gfx->Animbar_block[0],
    sizeof(gfx->Animbar_block[0]));
  memcpy(gfx->Statusbar_block[2], gfx->Statusbar_block[0],
    Menu_bars[MENUBAR_STATUS].Skin_width*Menu_bars[MENUBAR_STATUS].Height);
  
  
  return 0;
}

T_Gui_skin * Load_graphics(const char * skin_file, T_Gradient_array *gradients)
{
  T_Gui_skin * gfx;
  char filename[MAX_PATH_CHARACTERS];
  T_GFX2_Surface * gui;

  if (skin_file[0] == '\0')
  {
    sprintf(Gui_loading_error_message, "Wrong skin file name \"\"\n");
    return NULL;
  }

  gfx = (T_Gui_skin *)malloc(sizeof(T_Gui_skin));
  if (gfx == NULL)
  {
    sprintf(Gui_loading_error_message, "Not enough memory to read skin file\n");
    return NULL;
  }
  
  // Read the "skin" file
  snprintf(filename, sizeof(filename), "%s%s%s%s", Data_directory,SKINS_SUBDIRECTORY, PATH_SEPARATOR, skin_file);
  
  gui=Load_surface(filename, gradients);
  if (!gui)
  {
    sprintf(Gui_loading_error_message, "Unable to load the skin image (missing? not an image file?)\n");
    free(gfx);
    gfx = NULL;
    return NULL;
  }
  if (Parse_skin(gui, gfx) != 0)
  {
    free(gfx);
    gfx = NULL;
  }
  Free_GFX2_Surface(gui);
  return gfx;
}

// ---- font loading -----

static byte * Parse_font(T_GFX2_Surface * image, int is_main)
{
  byte * font;
  int character;
  byte color;
  int x, y;
  int chars_per_line;
  int character_count;
  
  // Check image size
  if (image->w % 8)
  {
    sprintf(Gui_loading_error_message, "Error in font file: Image width is not a multiple of 8.\n");
    return NULL;
  }
  character_count = (image->w * image->h) / (8*8);
  if (is_main && character_count < 256)
  {
    sprintf(Gui_loading_error_message, "Error in font file: Image is too small to be a 256-character 8x8 font.\n");
    return NULL;
  }
  font = (byte *)malloc(8*8*character_count);
  if (font == NULL)
  {
    sprintf(Gui_loading_error_message, "Not enough memory to read font file\n");
    return NULL;
  }
  chars_per_line = image->w/8;

  for (character=0; character < character_count; character++)
  {
    for (y=0; y<8; y++)
    {
      for (x=0;x<8; x++)
      {
        // Pick pixel
        color = Get_GFX2_Surface_pixel(image, (character % chars_per_line)*8+x, (character / chars_per_line)*8+y);
        if (color > 1)
        {
          sprintf(Gui_loading_error_message, "Error in font file: Only colors 0 and 1 can be used for the font.\n");
          free(font);
          return NULL;
        }
        // Put it in font. 0 = BG, 1 = FG.
        font[character*64 + y*8 + x]=color;
      }
    }
  }
  return font;
}

byte * Load_font(const char * font_name, int is_main)
{
  byte * font = NULL;
  char filename[MAX_PATH_CHARACTERS];
  T_GFX2_Surface * image;

  if (font_name[0] == '\0')
  {
    sprintf(Gui_loading_error_message, "Wrong font name \"\"\n");
    return NULL;
  }

  // Read the file containing the image
  snprintf(filename, sizeof(filename), "%s%s%s%s", Data_directory, SKINS_SUBDIRECTORY, PATH_SEPARATOR, font_name);
  
  image=Load_surface(filename, NULL);
  if (!image)
  {
    sprintf(Gui_loading_error_message, "Unable to load the skin image (missing? not an image file?)\n");
    return NULL;
  }
  font = Parse_font(image, is_main);
  Free_GFX2_Surface(image);
  return font;
}

static void Load_Unicode_font(const char * fullname, const char * filename)
{
  T_Unicode_Font * ufont;
  byte * font;
  unsigned int first, last;
  (void)fullname;

  if (strncasecmp(filename, "unicode_", 8) != 0)
    return;
  if (sscanf(filename + 8, "%04X-%04X.", &first, &last) == 2)
  {
    font = Load_font(filename, 0);
    if (font)
    {
      ufont = malloc(sizeof(T_Unicode_Font));
      ufont->FirstChar = first;
      ufont->LastChar = last;
      ufont->FontData = font;
      ufont->Next = Unicode_fonts;
      Unicode_fonts = ufont;
    }
  }
  else
    Warning_with_format("Could not parse filename %s", filename);
}

void Load_Unicode_fonts(void)
{
  char directory[MAX_PATH_CHARACTERS];

  snprintf(directory,sizeof(directory), "%s%s", Data_directory, SKINS_SUBDIRECTORY);
  For_each_file(directory, Load_Unicode_font);
}

// Initialisation des boutons:

  // Action factice:

static void Do_nothing(int btn)
{
  (void)btn;
}

  // Initialiseur d'un bouton:

static
void Init_button(byte        btn_number,
                 const char* tooltip,
                 word        x_offset, word   y_offset,
                 word        width,    word   height,
                 byte        shape,
                 Func_btn_action left_action, Func_btn_action right_action,
                 byte        left_instant,    byte        right_instant,
                 Func_btn_action unselect_action,
                 byte        family)
{
  Buttons_Pool[btn_number].X_offset        =x_offset;
  Buttons_Pool[btn_number].Y_offset        =y_offset;
  Buttons_Pool[btn_number].Width           =width-1;
  Buttons_Pool[btn_number].Height          =height-1;
  Buttons_Pool[btn_number].Pressed         =0;
  Buttons_Pool[btn_number].Icon            =-1;
  Buttons_Pool[btn_number].Shape           =shape;
  Buttons_Pool[btn_number].Tooltip         =tooltip;
  Buttons_Pool[btn_number].Left_action     =left_action;
  Buttons_Pool[btn_number].Right_action    =right_action;
  Buttons_Pool[btn_number].Left_instant    =left_instant;
  Buttons_Pool[btn_number].Right_instant   =right_instant;
  Buttons_Pool[btn_number].Unselect_action =unselect_action;
  Buttons_Pool[btn_number].Family          =family;
}


  // Initiliseur de tous les boutons:

void Init_buttons(void)
{
  byte button_index;

  for (button_index=0;button_index<NB_BUTTONS;button_index++)
  {
    Buttons_Pool[button_index].Left_shortcut[0]=0;
    Buttons_Pool[button_index].Left_shortcut[1]=0;
    Buttons_Pool[button_index].Right_shortcut[0]=0;
    Buttons_Pool[button_index].Right_shortcut[1]=0;
    Init_button(button_index,
                "",
                0,0,
                1,1,
                BUTTON_SHAPE_RECTANGLE,
                Do_nothing,Do_nothing,
                0,0,
                Do_nothing,
                0);
  }

  // Ici viennent les déclarations des boutons que l'on sait gérer

  Init_button(BUTTON_PAINTBRUSHES,
              "Paintbrush choice       ",
              0,1,
              16,16,
              BUTTON_SHAPE_RECTANGLE,
              Button_Paintbrush_menu,Button_Brush_monochrome,
              0,0,
              Do_nothing,
              FAMILY_INSTANT);

  Init_button(BUTTON_ADJUST,
              "Adjust / Transform menu ",
              0,18,
              16,16,
              BUTTON_SHAPE_RECTANGLE,
              Button_Adjust,Button_Transform_menu,
              0,0,
              Do_nothing,
              FAMILY_TOOL);

  Init_button(BUTTON_DRAW,
              "Freehand draw. / Toggle ",
              17,1,
              16,16,
              BUTTON_SHAPE_RECTANGLE,
              Button_Draw,Button_Draw_switch_mode,
              0,1,
              Do_nothing,
              FAMILY_TOOL);

  Init_button(BUTTON_CURVES,
              "Splines / Toggle        ",
              17,18,
              16,16,
              BUTTON_SHAPE_RECTANGLE,
              Button_Curves,Button_Curves_switch_mode,
              0,0,
              Do_nothing,
              FAMILY_TOOL);

  Init_button(BUTTON_LINES,
              "Lines / Toggle          ",
              34,1,
              16,16,
              BUTTON_SHAPE_RECTANGLE,
              Button_Lines,Button_Lines_switch_mode,
              0,0,
              Do_nothing,
              FAMILY_TOOL);

  Init_button(BUTTON_AIRBRUSH,
              "Spray / Menu            ",
              34,18,
              16,16,
              BUTTON_SHAPE_RECTANGLE,
              Button_Airbrush,Button_Airbrush_menu,
              0,0,
              Do_nothing,
              FAMILY_TOOL);

  Init_button(BUTTON_FLOODFILL,
              "Floodfill / Replace col.",
              51,1,
              16,16,
              BUTTON_SHAPE_RECTANGLE,
              Button_Fill,Button_Replace,
              0,0,
              Button_Unselect_fill,
              FAMILY_TOOL);

  Init_button(BUTTON_POLYGONS,
              "Polylines / Polyforms   ",
              51,18,
              15,15,
              BUTTON_SHAPE_TRIANGLE_TOP_LEFT,
              Button_polygon,Button_Polyform,
              0,0,
              Do_nothing,
              FAMILY_TOOL);

  Init_button(BUTTON_POLYFILL,
              "Polyfill / Filled Pforms",
              52,19,
              15,15,
              BUTTON_SHAPE_TRIANGLE_BOTTOM_RIGHT,
              Button_Polyfill,Button_Filled_polyform,
              0,0,
              Do_nothing,
              FAMILY_TOOL);

  Init_button(BUTTON_RECTANGLES,
              "Empty rectangles        ",
              68,1,
              15,15,
              BUTTON_SHAPE_TRIANGLE_TOP_LEFT,
              Button_Empty_rectangle,Button_Empty_rectangle,
              0,0,
              Do_nothing,
              FAMILY_TOOL);

  Init_button(BUTTON_FILLRECT,
              "Filled rectangles       ",
              69,2,
              15,15,
              BUTTON_SHAPE_TRIANGLE_BOTTOM_RIGHT,
              Button_Filled_rectangle,Button_Filled_rectangle,
              0,0,
              Do_nothing,
              FAMILY_TOOL);

  Init_button(BUTTON_CIRCLES,
              "Empty circles / Toggle  ",
              68,18,
              15,15,
              BUTTON_SHAPE_TRIANGLE_TOP_LEFT,
              Button_circle_ellipse,Button_Circle_switch_mode,
              0,1,
              Do_nothing,
              FAMILY_TOOL);

  Init_button(BUTTON_FILLCIRC,
              "Filled circles / Toggle ",
              69,19,
              15,15,
              BUTTON_SHAPE_TRIANGLE_BOTTOM_RIGHT,
              Button_circle_ellipse,Button_Circle_switch_mode,
              0,1,
              Do_nothing,
              FAMILY_TOOL);

  Init_button(BUTTON_GRADRECT,
              "Grad. rect / Grad. menu ",
              85,1,
              16,16,
              BUTTON_SHAPE_RECTANGLE,
              Button_Grad_rectangle,Button_Gradients,
              0,0,
              Do_nothing,
              FAMILY_TOOL);

  Init_button(BUTTON_SPHERES,
              "Grad. spheres / Toggle. ",
              85,18,
              16,16,
              BUTTON_SHAPE_RECTANGLE,
              Button_circle_ellipse,Button_Circle_switch_mode,
              0,1,
              Do_nothing,
              FAMILY_TOOL);

  Init_button(BUTTON_BRUSH,
              "Brush grab. / Restore   ",
              106,1,
              15,15,
              BUTTON_SHAPE_TRIANGLE_TOP_LEFT,
              Button_Brush,Button_Restore_brush,
              0,0,
              Button_Unselect_brush,
              FAMILY_INTERRUPTION);

  Init_button(BUTTON_POLYBRUSH,
              "Lasso / Restore brush   ",
              107,2,
              15,15,
              BUTTON_SHAPE_TRIANGLE_BOTTOM_RIGHT,
              Button_Lasso,Button_Restore_brush,
              0,0,
              Button_Unselect_lasso,
              FAMILY_INTERRUPTION);

  Init_button(BUTTON_BRUSH_EFFECTS,
#ifdef __ENABLE_LUA__
              "Brush effects / factory ",
#else
              "Brush effects           ",
#endif
              106, 18,
              16, 16,
              BUTTON_SHAPE_RECTANGLE,
#ifdef __ENABLE_LUA__
              Button_Brush_FX, Button_Brush_Factory,
#else
              Button_Brush_FX, Button_Brush_FX,
#endif
              0,0,
              Do_nothing,
              FAMILY_INSTANT);

  Init_button(BUTTON_EFFECTS,
              "Drawing modes (effects) ",
              123,1,
              16,16,
              BUTTON_SHAPE_RECTANGLE,
              Button_Effects,Button_Effects,
              0,0,
              Do_nothing,
              FAMILY_EFFECTS);

  Init_button(BUTTON_TEXT,
              "Text                    ",
              123,18,
              16,16,
              BUTTON_SHAPE_RECTANGLE,
              Button_Text,Button_Text,
              0,0,
              Do_nothing,
              FAMILY_INSTANT);

  Init_button(BUTTON_MAGNIFIER,
              "Magnify mode / Menu     ",
              140,1,
              16,16,
              BUTTON_SHAPE_RECTANGLE,
              Button_Magnify,Button_Magnify_menu,
              0,1,
              Button_Unselect_magnifier,
              FAMILY_INTERRUPTION);

  Init_button(BUTTON_COLORPICKER,
              "Pipette / Invert colors ",
              140,18,
              16,16,
              BUTTON_SHAPE_RECTANGLE,
              Button_Colorpicker,Button_Invert_foreback,
              0,0,
              Button_Unselect_colorpicker,
              FAMILY_INTERRUPTION);

  Init_button(BUTTON_RESOL,
              "Screen size / Safe. res.",
              161,1,
              16,16,
              BUTTON_SHAPE_RECTANGLE,
              Button_Resolution,Button_Safety_resolution,
              0,0,
              Do_nothing,
              FAMILY_INSTANT);
  
  Init_button(BUTTON_PAGE,
              "Go / Copy to other page ",
              161,18,
              16,16,
              BUTTON_SHAPE_RECTANGLE,
              Button_Page,Button_Copy_page,
              0,0,
              Do_nothing,
              FAMILY_INSTANT);

  Init_button(BUTTON_SAVE,
              "Save as / Save          ",
              178,1,
              15,15,
              BUTTON_SHAPE_TRIANGLE_TOP_LEFT,
              Button_Save,Button_Autosave,
              0,0,
              Do_nothing,
              FAMILY_INSTANT);

  Init_button(BUTTON_LOAD,
              "Load / Re-load          ",
              179,2,
              15,15,
              BUTTON_SHAPE_TRIANGLE_BOTTOM_RIGHT,
              Button_Load,Button_Reload,
              0,0,
              Do_nothing,
              FAMILY_INSTANT);

  Init_button(BUTTON_SETTINGS,
              "Settings / Skins        ",
              178,18,
              16,16,
              BUTTON_SHAPE_RECTANGLE,
              Button_Settings,Button_Skins,
              0,0,
              Do_nothing,
              FAMILY_INSTANT);

  Init_button(BUTTON_CLEAR,
              "Clear / with backcolor  ",
              195,1,
              17,16,
              BUTTON_SHAPE_RECTANGLE,
              Button_Clear,Button_Clear_with_backcolor,
              0,0,
              Do_nothing,
              FAMILY_INSTANT);

  Init_button(BUTTON_HELP,
              "Help / Statistics       ",
              195,18,
              17,16,
              BUTTON_SHAPE_RECTANGLE,
              Button_Help,Button_Stats,
              0,0,
              Do_nothing,
              FAMILY_INSTANT);

  Init_button(BUTTON_UNDO,
              "Undo / Redo             ",
              213,1,
              19,12,
              BUTTON_SHAPE_RECTANGLE,
              Button_Undo,Button_Redo,
              0,0,
              Do_nothing,
              FAMILY_INSTANT);

  Init_button(BUTTON_KILL,
              "Kill current page       ",
              213,14,
              19,7,
              BUTTON_SHAPE_RECTANGLE,
              Button_Kill,Button_Kill,
              0,0,
              Do_nothing,
              FAMILY_INSTANT);

  Init_button(BUTTON_QUIT,
              "Quit                    ",
              213,22,
              19,12,
              BUTTON_SHAPE_RECTANGLE,
              Button_Quit,Button_Quit,
              0,0,
              Do_nothing,
              FAMILY_INSTANT);

  Init_button(BUTTON_PALETTE,
              "Palette editor / setup  ",
              237,9,
              16,8,
              BUTTON_SHAPE_RECTANGLE,
              Button_Palette,Button_Secondary_palette,
              0,0,
              Do_nothing,
              FAMILY_INSTANT);

  Init_button(BUTTON_PAL_LEFT,
              "Scroll pal. bkwd / Fast ",
              237,18,
              15,15,
              BUTTON_SHAPE_TRIANGLE_TOP_LEFT,
              Button_Pal_left,Button_Pal_left_fast,
              0,0,
              Do_nothing,
              FAMILY_INSTANT);

  Init_button(BUTTON_PAL_RIGHT,
              "Scroll pal. fwd / Fast  ",
              238,19,
              15,15,
              BUTTON_SHAPE_TRIANGLE_BOTTOM_RIGHT,
              Button_Pal_right,Button_Pal_right_fast,
              0,0,
              Do_nothing,
              FAMILY_INSTANT);

  Init_button(BUTTON_CHOOSE_COL,
              "Color #"                 ,
              MENU_WIDTH+1,1,
              1,32, // La largeur est mise à jour à chq chngmnt de mode
              BUTTON_SHAPE_NO_FRAME,
              Button_Select_forecolor,Button_Select_backcolor,
              1,1,
              Do_nothing,
              FAMILY_INSTANT);

  // Layer bar

  Init_button(BUTTON_LAYER_MENU,
              "Layers manager          ",
              0,0,
              57,9,
              BUTTON_SHAPE_RECTANGLE,
              Button_Layer_menu, Button_Layer_menu,
              0,0,
              Do_nothing,
              FAMILY_INSTANT);
  Init_button(BUTTON_LAYER_COLOR,
              "Get/Set transparent col.",
              58,0,
              13,9,
              BUTTON_SHAPE_RECTANGLE,
              Button_Layer_get_transparent, Button_Layer_set_transparent,
              0,0,
              Do_nothing,
              FAMILY_INSTANT);
  Init_button(BUTTON_LAYER_MERGE,
              "Merge layer             ",
              72,0,
              13,9,
              BUTTON_SHAPE_RECTANGLE,
              Button_Layer_merge, Button_Layer_merge,
              0,0,
              Do_nothing,
              FAMILY_INSTANT);
  Init_button(BUTTON_LAYER_ADD,
              "Add/Duplicate  layer    ",
              86,0,
              13,9,
              BUTTON_SHAPE_RECTANGLE,
              Button_Layer_add, Button_Layer_duplicate,
              0,0,
              Do_nothing,
              FAMILY_INSTANT);
  Init_button(BUTTON_LAYER_REMOVE,
              "Drop layer              ",
              100,0,
              13,9,
              BUTTON_SHAPE_RECTANGLE,
              Button_Layer_remove, Button_Layer_remove,
              0,0,
              Do_nothing,
              FAMILY_INSTANT);
  Init_button(BUTTON_LAYER_DOWN,
              "Lower layer             ",
              114,0,
              13,9,
              BUTTON_SHAPE_RECTANGLE,
              Button_Layer_down, Button_Layer_down,
              0,0,
              Do_nothing,
              FAMILY_INSTANT);
  Init_button(BUTTON_LAYER_UP,
              "Raise layer             ",
              128,0,
              13,9,
              BUTTON_SHAPE_RECTANGLE,
              Button_Layer_up, Button_Layer_up,
              0,0,
              Do_nothing,
              FAMILY_INSTANT);
  Init_button(BUTTON_LAYER_SELECT,
              "Layer select / toggle   ",
              142,0,
              13,9, // Will be updated according to actual number of layers
              BUTTON_SHAPE_NO_FRAME,
              Button_Layer_select, Button_Layer_toggle,
              1,1,
              Do_nothing,
              FAMILY_INSTANT);

 // Anim bar

  Init_button(BUTTON_LAYER_MENU2,
              "Layers manager          ",
              0,0,
              44,13,
              BUTTON_SHAPE_RECTANGLE,
              Button_Layer_menu, Button_Layer_menu,
              0,0,
              Do_nothing,
              FAMILY_INSTANT);
  Init_button(BUTTON_ANIM_TIME,
              "Set frame time          ",
              45,0,
              13,13,
              BUTTON_SHAPE_RECTANGLE,
              Button_Anim_time, Button_Anim_time,
              0,0,
              Do_nothing,
              FAMILY_INSTANT);
  Init_button(BUTTON_ANIM_FIRST_FRAME,
              "Go to first frame       ",
              116,0,
              13,13,
              BUTTON_SHAPE_RECTANGLE,
              Button_Anim_first_frame, Button_Anim_first_frame,
              0,0,
              Do_nothing,
              FAMILY_INSTANT);
  Init_button(BUTTON_ANIM_PREV_FRAME,
              "Go to prev. frame/Rewind",
              130,0,
              13,13,
              BUTTON_SHAPE_RECTANGLE,
              Button_Anim_prev_frame, Button_Anim_continuous_prev,
              0,1,
              Do_nothing,
              FAMILY_INSTANT);
  Init_button(BUTTON_ANIM_NEXT_FRAME,
              "Go to next frame / Play ",
              144,0,
              13,13,
              BUTTON_SHAPE_RECTANGLE,
              Button_Anim_next_frame, Button_Anim_continuous_next,
              0,1,
              Do_nothing,
              FAMILY_INSTANT);
  Init_button(BUTTON_ANIM_LAST_FRAME,
              "Go to last frame        ",
              158,0,
              13,13,
              BUTTON_SHAPE_RECTANGLE,
              Button_Anim_last_frame, Button_Anim_last_frame,
              0,0,
              Do_nothing,
              FAMILY_INSTANT);
  Init_button(BUTTON_ANIM_ADD_FRAME,
              "Add frame               ",
              177,0,
              13,13,
              BUTTON_SHAPE_RECTANGLE,
              Button_Layer_duplicate, Button_Layer_duplicate,
              0,0,
              Do_nothing,
              FAMILY_INSTANT);
  Init_button(BUTTON_ANIM_REMOVE_FRAME,
              "Drop frame              ",
              191,0,
              13,13,
              BUTTON_SHAPE_RECTANGLE,
              Button_Layer_remove, Button_Layer_remove,
              0,0,
              Do_nothing,
              FAMILY_INSTANT);
  Init_button(BUTTON_ANIM_DOWN_FRAME,
              "Move frame back         ",
              205,0,
              13,13,
              BUTTON_SHAPE_RECTANGLE,
              Button_Layer_down, Button_Layer_down,
              0,0,
              Do_nothing,
              FAMILY_INSTANT);
  Init_button(BUTTON_ANIM_UP_FRAME,
              "Move frame forwards     ",
              219,0,
              13,13,
              BUTTON_SHAPE_RECTANGLE,
              Button_Layer_up, Button_Layer_up,
              0,0,
              Do_nothing,
              FAMILY_INSTANT);

  // Status bar
  Init_button(BUTTON_HIDE,
              "Hide toolbars / Select  ",
              0,0,
              16,9,
              BUTTON_SHAPE_RECTANGLE,
              Button_Toggle_all_toolbars, Button_Toggle_toolbar,
              0,1,
              Do_nothing,
              FAMILY_TOOLBAR);
}



// Initialisation des opérations:

  // Initialiseur d'une opération:

void Init_operation(byte operation_number,
                           byte mouse_button,
                           byte stack_index,
                           Func_action action,
                           byte hide_cursor,
                           byte fast_mouse)
{
  Operation[operation_number][mouse_button]
           [stack_index].Action=action;
  Operation[operation_number][mouse_button]
           [stack_index].Hide_cursor=hide_cursor;
  Operation[operation_number][mouse_button]
           [stack_index].Fast_mouse=fast_mouse;
}


  // Initiliseur de toutes les opérations:

void Init_operations(void)
{
  byte number; // Numéro de l'option en cours d'auto-initialisation
  byte Button; // Button souris en cours d'auto-initialisation
  byte stack_index; // Taille de la pile en cours d'auto-initialisation
  #define HIDE_CURSOR 1
  #define FAST_MOUSE 1

  // Auto-initialisation des opérations (vers des actions inoffensives)

  for (number=0;number<NB_OPERATIONS;number++)
    for (Button=0;Button<3;Button++)
      for (stack_index=0;stack_index<OPERATION_STACK_SIZE;stack_index++)
        Init_operation(number,Button,stack_index,Print_coordinates,0,FAST_MOUSE);


  // Ici viennent les déclarations détaillées des opérations
  Init_operation(OPERATION_CONTINUOUS_DRAW,1,0,
                        Freehand_mode1_1_0,HIDE_CURSOR,0);
  Init_operation(OPERATION_CONTINUOUS_DRAW,1,2,
                        Freehand_mode1_1_2,0,0);
  Init_operation(OPERATION_CONTINUOUS_DRAW,0,2,
                        Freehand_mode12_0_2,0,0);
  Init_operation(OPERATION_CONTINUOUS_DRAW,2,0,
                        Freehand_mode1_2_0,HIDE_CURSOR,0);
  Init_operation(OPERATION_CONTINUOUS_DRAW,2,2,
                        Freehand_mode1_2_2,0,0);

  Init_operation(OPERATION_DISCONTINUOUS_DRAW,1,0,
                        Freehand_mode2_1_0,HIDE_CURSOR,0);
  Init_operation(OPERATION_DISCONTINUOUS_DRAW,1,2,
                        Freehand_mode2_1_2,0,0);
  Init_operation(OPERATION_DISCONTINUOUS_DRAW,0,2,
                        Freehand_mode12_0_2,0,0);
  Init_operation(OPERATION_DISCONTINUOUS_DRAW,2,0,
                        Freehand_mode2_2_0,HIDE_CURSOR,0);
  Init_operation(OPERATION_DISCONTINUOUS_DRAW,2,2,
                        Freehand_mode2_2_2,0,0);

  Init_operation(OPERATION_POINT_DRAW,1,0,
                        Freehand_mode3_1_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_POINT_DRAW,2,0,
                        Freehand_Mode3_2_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_POINT_DRAW,0,1,
                        Freehand_mode3_0_1,0,FAST_MOUSE);

  Init_operation(OPERATION_LINE,1,0,
                        Line_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_LINE,1,5,
                        Line_12_5,0,FAST_MOUSE);
  Init_operation(OPERATION_LINE,0,5,
                        Line_0_5,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_LINE,2,0,
                        Line_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_LINE,2,5,
                        Line_12_5,0,FAST_MOUSE);

  Init_operation(OPERATION_K_LINE,1,0,
                        K_line_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_K_LINE,1,6,
                        K_line_12_6,0,FAST_MOUSE);
  Init_operation(OPERATION_K_LINE,1,7,
                        K_line_12_7,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_K_LINE,2,FAST_MOUSE,
                        K_line_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_K_LINE,2,6,
                        K_line_12_6,0,FAST_MOUSE);
  Init_operation(OPERATION_K_LINE,2,7,
                        K_line_12_7,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_K_LINE,0,6,
                        K_line_0_6,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_K_LINE,0,7,
                        K_line_12_6,0,FAST_MOUSE);

  Init_operation(OPERATION_EMPTY_RECTANGLE,1,0,
                        Rectangle_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_EMPTY_RECTANGLE,2,0,
                        Rectangle_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_EMPTY_RECTANGLE,1,5,
                        Rectangle_12_5,0,FAST_MOUSE);
  Init_operation(OPERATION_EMPTY_RECTANGLE,2,5,
                        Rectangle_12_5,0,FAST_MOUSE);
  Init_operation(OPERATION_EMPTY_RECTANGLE,0,5,
                        Empty_rectangle_0_5,HIDE_CURSOR,FAST_MOUSE);

  Init_operation(OPERATION_FILLED_RECTANGLE,1,0,
                        Rectangle_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_FILLED_RECTANGLE,2,0,
                        Rectangle_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_FILLED_RECTANGLE,1,5,
                        Rectangle_12_5,0,FAST_MOUSE);
  Init_operation(OPERATION_FILLED_RECTANGLE,2,5,
                        Rectangle_12_5,0,FAST_MOUSE);
  Init_operation(OPERATION_FILLED_RECTANGLE,0,5,
                        Filled_rectangle_0_5,HIDE_CURSOR,FAST_MOUSE);

  Init_operation(OPERATION_EMPTY_CIRCLE_CTR,1,0,
                        Circle_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_EMPTY_CIRCLE_CTR,2,0,
                        Circle_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_EMPTY_CIRCLE_CTR,1,5,
                        Circle_12_5,0,FAST_MOUSE);
  Init_operation(OPERATION_EMPTY_CIRCLE_CTR,2,5,
                        Circle_12_5,0,FAST_MOUSE);
  Init_operation(OPERATION_EMPTY_CIRCLE_CTR,0,5,
                        Empty_circle_0_5,HIDE_CURSOR,FAST_MOUSE);

  Init_operation(OPERATION_EMPTY_CIRCLE_CRN,1,0,
                        Ellipse_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_EMPTY_CIRCLE_CRN,2,0,
                        Ellipse_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_EMPTY_CIRCLE_CRN,1,5,
                        Ellipse_12_5,0,FAST_MOUSE);
  Init_operation(OPERATION_EMPTY_CIRCLE_CRN,2,5,
                        Ellipse_12_5,0,FAST_MOUSE);
  Init_operation(OPERATION_EMPTY_CIRCLE_CRN,0,5,
                        Empty_ellipse_0_5,HIDE_CURSOR,FAST_MOUSE);

  Init_operation(OPERATION_FILLED_CIRCLE_CTR,1,0,
                        Circle_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_FILLED_CIRCLE_CTR,2,0,
                        Circle_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_FILLED_CIRCLE_CTR,1,5,
                        Circle_12_5,0,FAST_MOUSE);
  Init_operation(OPERATION_FILLED_CIRCLE_CTR,2,5,
                        Circle_12_5,0,FAST_MOUSE);
  Init_operation(OPERATION_FILLED_CIRCLE_CTR,0,5,
                        Filled_circle_0_5,HIDE_CURSOR,FAST_MOUSE);

  Init_operation(OPERATION_FILLED_CIRCLE_CRN,1,0,
                        Ellipse_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_FILLED_CIRCLE_CRN,2,0,
                        Ellipse_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_FILLED_CIRCLE_CRN,1,5,
                        Ellipse_12_5,0,FAST_MOUSE);
  Init_operation(OPERATION_FILLED_CIRCLE_CRN,2,5,
                        Ellipse_12_5,0,FAST_MOUSE);
  Init_operation(OPERATION_FILLED_CIRCLE_CRN,0,5,
                        Filled_ellipse_0_5,HIDE_CURSOR,FAST_MOUSE);

  Init_operation(OPERATION_EMPTY_ELLIPSE_CTR,1,0,
                        Ellipse_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_EMPTY_ELLIPSE_CTR,2,0,
                        Ellipse_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_EMPTY_ELLIPSE_CTR,1,5,
                        Ellipse_12_5,0,FAST_MOUSE);
  Init_operation(OPERATION_EMPTY_ELLIPSE_CTR,2,5,
                        Ellipse_12_5,0,FAST_MOUSE);
  Init_operation(OPERATION_EMPTY_ELLIPSE_CTR,0,5,
                        Empty_ellipse_0_5,HIDE_CURSOR,FAST_MOUSE);

  Init_operation(OPERATION_EMPTY_ELLIPSE_CRN,1,0,
                        Ellipse_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_EMPTY_ELLIPSE_CRN,2,0,
                        Ellipse_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_EMPTY_ELLIPSE_CRN,1,5,
                        Ellipse_12_5,0,FAST_MOUSE);
  Init_operation(OPERATION_EMPTY_ELLIPSE_CRN,2,5,
                        Ellipse_12_5,0,FAST_MOUSE);
  Init_operation(OPERATION_EMPTY_ELLIPSE_CRN,0,5,
                        Empty_ellipse_0_5,HIDE_CURSOR,FAST_MOUSE);

  Init_operation(OPERATION_FILLED_ELLIPSE_CTR,1,0,
                        Ellipse_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_FILLED_ELLIPSE_CTR,2,0,
                        Ellipse_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_FILLED_ELLIPSE_CTR,1,5,
                        Ellipse_12_5,0,FAST_MOUSE);
  Init_operation(OPERATION_FILLED_ELLIPSE_CTR,2,5,
                        Ellipse_12_5,0,FAST_MOUSE);
  Init_operation(OPERATION_FILLED_ELLIPSE_CTR,0,5,
                        Filled_ellipse_0_5,HIDE_CURSOR,FAST_MOUSE);

  Init_operation(OPERATION_FILLED_ELLIPSE_CRN,1,0,
                        Ellipse_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_FILLED_ELLIPSE_CRN,2,0,
                        Ellipse_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_FILLED_ELLIPSE_CRN,1,5,
                        Ellipse_12_5,0,FAST_MOUSE);
  Init_operation(OPERATION_FILLED_ELLIPSE_CRN,2,5,
                        Ellipse_12_5,0,FAST_MOUSE);
  Init_operation(OPERATION_FILLED_ELLIPSE_CRN,0,5,
                        Filled_ellipse_0_5,HIDE_CURSOR,FAST_MOUSE);

  Init_operation(OPERATION_FILL,1,0,
                        Fill_1_0,0,FAST_MOUSE);
  Init_operation(OPERATION_FILL,2,0,
                        Fill_2_0,0,FAST_MOUSE);

  Init_operation(OPERATION_REPLACE,1,0,
                        Replace_1_0,0,FAST_MOUSE);
  Init_operation(OPERATION_REPLACE,2,0,
                        Replace_2_0,0,FAST_MOUSE);

  Init_operation(OPERATION_GRAB_BRUSH,1,0,
                        Brush_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_GRAB_BRUSH,2,0,
                        Brush_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_GRAB_BRUSH,1,5,
                        Brush_12_5,0,FAST_MOUSE);
  Init_operation(OPERATION_GRAB_BRUSH,2,5,
                        Brush_12_5,0,FAST_MOUSE);
  Init_operation(OPERATION_GRAB_BRUSH,0,5,
                        Brush_0_5,HIDE_CURSOR,FAST_MOUSE);

  Init_operation(OPERATION_STRETCH_BRUSH,1,0,
                        Stretch_brush_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_STRETCH_BRUSH,2,0,
                        Stretch_brush_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_STRETCH_BRUSH,1,7,
                        Stretch_brush_1_7,0,FAST_MOUSE);
  Init_operation(OPERATION_STRETCH_BRUSH,0,7,
                        Stretch_brush_0_7,0,FAST_MOUSE);
  Init_operation(OPERATION_STRETCH_BRUSH,2,7,
                        Stretch_brush_2_7,HIDE_CURSOR,FAST_MOUSE);

  Init_operation(OPERATION_ROTATE_BRUSH,1,0,
                        Rotate_brush_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_ROTATE_BRUSH,2,0,
                        Rotate_brush_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_ROTATE_BRUSH,1,5,
                        Rotate_brush_1_5,0,FAST_MOUSE);
  Init_operation(OPERATION_ROTATE_BRUSH,0,5,
                        Rotate_brush_0_5,0,FAST_MOUSE);
  Init_operation(OPERATION_ROTATE_BRUSH,2,5,
                        Rotate_brush_2_5,HIDE_CURSOR,FAST_MOUSE);

  Init_operation(OPERATION_DISTORT_BRUSH,0,0,
                        Distort_brush_0_0,0,FAST_MOUSE);
  Init_operation(OPERATION_DISTORT_BRUSH,1,0,
                        Distort_brush_1_0,0,FAST_MOUSE);
  Init_operation(OPERATION_DISTORT_BRUSH,1,8,
                        Distort_brush_1_8,0,FAST_MOUSE);
  Init_operation(OPERATION_DISTORT_BRUSH,2,8,
                        Distort_brush_2_8,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_DISTORT_BRUSH,2,0,
                        Distort_brush_2_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_DISTORT_BRUSH,1,9,
                        Distort_brush_1_9,0,FAST_MOUSE);
  Init_operation(OPERATION_DISTORT_BRUSH,0,9,
                        Distort_brush_0_9,0,FAST_MOUSE);


  Init_operation(OPERATION_POLYBRUSH,1,0,
                        Filled_polyform_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_POLYBRUSH,2,0,
                        Filled_polyform_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_POLYBRUSH,1,8,
                        Polybrush_12_8,0,0);
  Init_operation(OPERATION_POLYBRUSH,2,8,
                        Polybrush_12_8,0,0);
  Init_operation(OPERATION_POLYBRUSH,0,8,
                        Filled_polyform_0_8,0,FAST_MOUSE);

  Colorpicker_color=-1;
  Init_operation(OPERATION_COLORPICK,1,0,
                        Colorpicker_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_COLORPICK,2,0,
                        Colorpicker_12_0,0,FAST_MOUSE);
  Init_operation(OPERATION_COLORPICK,1,1,
                        Colorpicker_1_1,0,FAST_MOUSE);
  Init_operation(OPERATION_COLORPICK,2,1,
                        Colorpicker_2_1,0,FAST_MOUSE);
  Init_operation(OPERATION_COLORPICK,0,1,
                        Colorpicker_0_1,HIDE_CURSOR,FAST_MOUSE);

  Init_operation(OPERATION_MAGNIFY,1,0,
                        Magnifier_12_0,0,FAST_MOUSE);
  Init_operation(OPERATION_MAGNIFY,2,0,
                        Magnifier_12_0,0,FAST_MOUSE);

  Init_operation(OPERATION_4_POINTS_CURVE,1,0,
                        Curve_34_points_1_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_4_POINTS_CURVE,2,0,
                        Curve_34_points_2_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_4_POINTS_CURVE,1,5,
                        Curve_34_points_1_5,0,FAST_MOUSE);
  Init_operation(OPERATION_4_POINTS_CURVE,2,5,
                        Curve_34_points_2_5,0,FAST_MOUSE);
  Init_operation(OPERATION_4_POINTS_CURVE,0,5,
                        Curve_4_points_0_5,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_4_POINTS_CURVE,1,9,
                        Curve_4_points_1_9,0,FAST_MOUSE);
  Init_operation(OPERATION_4_POINTS_CURVE,2,9,
                        Curve_4_points_2_9,0,FAST_MOUSE);

  Init_operation(OPERATION_3_POINTS_CURVE,1,0,
                        Curve_34_points_1_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_3_POINTS_CURVE,2,0,
                        Curve_34_points_2_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_3_POINTS_CURVE,1,5,
                        Curve_34_points_1_5,0,FAST_MOUSE);
  Init_operation(OPERATION_3_POINTS_CURVE,2,5,
                        Curve_34_points_2_5,0,FAST_MOUSE);
  Init_operation(OPERATION_3_POINTS_CURVE,0,5,
                        Curve_3_points_0_5,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_3_POINTS_CURVE,0,11,
                        Curve_3_points_0_11,0,FAST_MOUSE);
  Init_operation(OPERATION_3_POINTS_CURVE,1,11,
                        Curve_3_points_12_11,0,FAST_MOUSE);
  Init_operation(OPERATION_3_POINTS_CURVE,2,11,
                        Curve_3_points_12_11,0,FAST_MOUSE);

  Init_operation(OPERATION_AIRBRUSH,1,0,
                        Airbrush_1_0,0,0);
  Init_operation(OPERATION_AIRBRUSH,2,0,
                        Airbrush_2_0,0,0);
  Init_operation(OPERATION_AIRBRUSH,1,2,
                        Airbrush_12_2,0,0);
  Init_operation(OPERATION_AIRBRUSH,2,2,
                        Airbrush_12_2,0,0);
  Init_operation(OPERATION_AIRBRUSH,0,2,
                        Airbrush_0_2,0,0);

  Init_operation(OPERATION_POLYGON,1,0,
                        Polygon_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_POLYGON,2,0,
                        Polygon_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_POLYGON,1,8,
                        K_line_12_6,0,FAST_MOUSE);
  Init_operation(OPERATION_POLYGON,2,8,
                        K_line_12_6,0,FAST_MOUSE);
  Init_operation(OPERATION_POLYGON,1,9,
                        Polygon_12_9,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_POLYGON,2,9,
                        Polygon_12_9,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_POLYGON,0,8,
                        K_line_0_6,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_POLYGON,0,9,
                        K_line_12_6,0,FAST_MOUSE);

  Init_operation(OPERATION_POLYFILL,1,0,
                        Polyfill_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_POLYFILL,2,0,
                        Polyfill_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_POLYFILL,1,8,
                        Polyfill_12_8,0,FAST_MOUSE);
  Init_operation(OPERATION_POLYFILL,2,8,
                        Polyfill_12_8,0,FAST_MOUSE);
  Init_operation(OPERATION_POLYFILL,1,9,
                        Polyfill_12_9,0,FAST_MOUSE);
  Init_operation(OPERATION_POLYFILL,2,9,
                        Polyfill_12_9,0,FAST_MOUSE);
  Init_operation(OPERATION_POLYFILL,0,8,
                        Polyfill_0_8,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_POLYFILL,0,9,
                        Polyfill_12_8,0,FAST_MOUSE);

  Init_operation(OPERATION_POLYFORM,1,0,
                        Polyform_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_POLYFORM,2,0,
                        Polyform_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_POLYFORM,1,8,
                        Polyform_12_8,0,0);
  Init_operation(OPERATION_POLYFORM,2,8,
                        Polyform_12_8,0,0);
  Init_operation(OPERATION_POLYFORM,0,8,
                        Polyform_0_8,0,FAST_MOUSE);

  Init_operation(OPERATION_FILLED_POLYFORM,1,0,
                        Filled_polyform_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_FILLED_POLYFORM,2,0,
                        Filled_polyform_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_FILLED_POLYFORM,1,8,
                        Filled_polyform_12_8,0,0);
  Init_operation(OPERATION_FILLED_POLYFORM,2,8,
                        Filled_polyform_12_8,0,0);
  Init_operation(OPERATION_FILLED_POLYFORM,0,8,
                        Filled_polyform_0_8,0,FAST_MOUSE);

  Init_operation(OPERATION_FILLED_CONTOUR,1,0,
                        Filled_polyform_12_0,HIDE_CURSOR,0);
  Init_operation(OPERATION_FILLED_CONTOUR,2,0,
                        Filled_polyform_12_0,HIDE_CURSOR,0);
  Init_operation(OPERATION_FILLED_CONTOUR,1,8,
                        Filled_polyform_12_8,0,0);
  Init_operation(OPERATION_FILLED_CONTOUR,2,8,
                        Filled_polyform_12_8,0,0);
  Init_operation(OPERATION_FILLED_CONTOUR,0,8,
                        Filled_contour_0_8,0,0);

  Init_operation(OPERATION_SCROLL,1,0,
                        Scroll_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_SCROLL,2,0,
                        Scroll_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_SCROLL,1,5,
                        Scroll_12_5,0,FAST_MOUSE);
  Init_operation(OPERATION_SCROLL,2,5,
                        Scroll_12_5,0,FAST_MOUSE);
  Init_operation(OPERATION_SCROLL,0,5,
                        Scroll_0_5,HIDE_CURSOR,FAST_MOUSE);

  Init_operation(OPERATION_GRAD_CIRCLE_CTR,1,0,Grad_circle_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_GRAD_CIRCLE_CTR,2,0,Grad_circle_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_GRAD_CIRCLE_CTR,1,6,Grad_circle_12_6,0,FAST_MOUSE);
  Init_operation(OPERATION_GRAD_CIRCLE_CTR,2,6,Grad_circle_12_6,0,FAST_MOUSE);
  Init_operation(OPERATION_GRAD_CIRCLE_CTR,0,6,Grad_circle_0_6,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_GRAD_CIRCLE_CTR,1,8,Grad_circle_12_8,0,FAST_MOUSE);
  Init_operation(OPERATION_GRAD_CIRCLE_CTR,2,8,Grad_circle_12_8,0,FAST_MOUSE);
  Init_operation(OPERATION_GRAD_CIRCLE_CTR,0,8,Grad_circle_or_ellipse_0_8,0,FAST_MOUSE);

  Init_operation(OPERATION_GRAD_CIRCLE_CRN,1,0,Grad_ellipse_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_GRAD_CIRCLE_CRN,2,0,Grad_ellipse_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_GRAD_CIRCLE_CRN,1,6,Grad_ellipse_12_6,0,FAST_MOUSE);
  Init_operation(OPERATION_GRAD_CIRCLE_CRN,2,6,Grad_ellipse_12_6,0,FAST_MOUSE);
  Init_operation(OPERATION_GRAD_CIRCLE_CRN,0,6,Grad_ellipse_0_6,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_GRAD_CIRCLE_CRN,1,8,Grad_ellipse_12_8,0,FAST_MOUSE);
  Init_operation(OPERATION_GRAD_CIRCLE_CRN,2,8,Grad_ellipse_12_8,0,FAST_MOUSE);
  Init_operation(OPERATION_GRAD_CIRCLE_CRN,0,8,Grad_circle_or_ellipse_0_8,0,FAST_MOUSE);

  Init_operation(OPERATION_GRAD_ELLIPSE_CTR,1,0,Grad_ellipse_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_GRAD_ELLIPSE_CTR,2,0,Grad_ellipse_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_GRAD_ELLIPSE_CTR,1,6,Grad_ellipse_12_6,0,FAST_MOUSE);
  Init_operation(OPERATION_GRAD_ELLIPSE_CTR,2,6,Grad_ellipse_12_6,0,FAST_MOUSE);
  Init_operation(OPERATION_GRAD_ELLIPSE_CTR,0,6,Grad_ellipse_0_6,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_GRAD_ELLIPSE_CTR,1,8,Grad_ellipse_12_8,0,FAST_MOUSE);
  Init_operation(OPERATION_GRAD_ELLIPSE_CTR,2,8,Grad_ellipse_12_8,0,FAST_MOUSE);
  Init_operation(OPERATION_GRAD_ELLIPSE_CTR,0,8,Grad_circle_or_ellipse_0_8,0,FAST_MOUSE);

  Init_operation(OPERATION_GRAD_ELLIPSE_CRN,1,0,Grad_ellipse_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_GRAD_ELLIPSE_CRN,2,0,Grad_ellipse_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_GRAD_ELLIPSE_CRN,1,6,Grad_ellipse_12_6,0,FAST_MOUSE);
  Init_operation(OPERATION_GRAD_ELLIPSE_CRN,2,6,Grad_ellipse_12_6,0,FAST_MOUSE);
  Init_operation(OPERATION_GRAD_ELLIPSE_CRN,0,6,Grad_ellipse_0_6,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_GRAD_ELLIPSE_CRN,1,8,Grad_ellipse_12_8,0,FAST_MOUSE);
  Init_operation(OPERATION_GRAD_ELLIPSE_CRN,2,8,Grad_ellipse_12_8,0,FAST_MOUSE);
  Init_operation(OPERATION_GRAD_ELLIPSE_CRN,0,8,Grad_circle_or_ellipse_0_8,0,FAST_MOUSE);

  Init_operation(OPERATION_GRAD_RECTANGLE,1,0,Grad_rectangle_12_0,0,FAST_MOUSE);
  Init_operation(OPERATION_GRAD_RECTANGLE,1,5,Grad_rectangle_12_5,0,FAST_MOUSE);
  Init_operation(OPERATION_GRAD_RECTANGLE,0,5,Grad_rectangle_0_5,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_GRAD_RECTANGLE,0,7,Grad_rectangle_0_7,0,FAST_MOUSE);
  Init_operation(OPERATION_GRAD_RECTANGLE,1,7,Grad_rectangle_12_7,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_GRAD_RECTANGLE,2,7,Grad_rectangle_12_7,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_GRAD_RECTANGLE,1,9,Grad_rectangle_12_9,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_GRAD_RECTANGLE,0,9,Grad_rectangle_0_9,0,FAST_MOUSE);


  Init_operation(OPERATION_CENTERED_LINES,1,0,
                        Centered_lines_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_CENTERED_LINES,2,0,
                        Centered_lines_12_0,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_CENTERED_LINES,1,3,
                        Centered_lines_12_3,0,FAST_MOUSE);
  Init_operation(OPERATION_CENTERED_LINES,2,3,
                        Centered_lines_12_3,0,FAST_MOUSE);
  Init_operation(OPERATION_CENTERED_LINES,0,3,
                        Centered_lines_0_3,HIDE_CURSOR,FAST_MOUSE);
  Init_operation(OPERATION_CENTERED_LINES,1,7,
                        Centered_lines_12_7,0,FAST_MOUSE);
  Init_operation(OPERATION_CENTERED_LINES,2,7,
                        Centered_lines_12_7,0,FAST_MOUSE);
  Init_operation(OPERATION_CENTERED_LINES,0,7,
                        Centered_lines_0_7,0,FAST_MOUSE);
  Init_operation(OPERATION_RMB_COLORPICK,0,1,
                        Rightclick_colorpick_0_1,0,FAST_MOUSE);
  Init_operation(OPERATION_PAN_VIEW,0,0,
                        Pan_view_0_0,0,FAST_MOUSE);
  Init_operation(OPERATION_PAN_VIEW,1,0,
                        Pan_view_12_0,0,FAST_MOUSE);
  Init_operation(OPERATION_PAN_VIEW,2,0,
                        Pan_view_12_0,0,FAST_MOUSE);
  Init_operation(OPERATION_PAN_VIEW,1,2,
                        Pan_view_12_2,0,FAST_MOUSE);
  Init_operation(OPERATION_PAN_VIEW,2,2,
                        Pan_view_12_2,0,FAST_MOUSE);
  Init_operation(OPERATION_PAN_VIEW,0,2,
                        Pan_view_0_2,0,FAST_MOUSE);
                        
}



//-- Définition des modes vidéo: --------------------------------------------

  // Définition d'un mode:

static void Set_video_mode(short  width,
                           short  height,
                           byte   mode,
                           word   fullscreen)
{
  byte supported = 0;
#if defined(USE_SDL2)
  SDL_DisplayMode dm;
#endif

  if (Nb_video_modes >= MAX_VIDEO_MODES-1)
  {
    GFX2_Log(GFX2_ERROR, "Attempt to create too many videomodes. Maximum is: %d\n", MAX_VIDEO_MODES);
    return;
  }
  if (!fullscreen)
    supported = 128; // Prefere, non modifiable
#if defined(USE_SDL)
  else if (SDL_VideoModeOK(width, height, 8, SDL_FULLSCREEN))
    supported = 1; // supported
#elif defined(USE_SDL2)
  else if (SDL_GetDisplayMode(0, mode, &dm) == 0) {
    if (width == dm.w && height == dm.h)
      supported = 1;
  }
#endif

  if (!supported) // Not supported : skip this mode
    return;

  Video_mode[Nb_video_modes].Width      = width;
  Video_mode[Nb_video_modes].Height     = height;
  Video_mode[Nb_video_modes].Mode       = mode;
  Video_mode[Nb_video_modes].Fullscreen = fullscreen;
  Video_mode[Nb_video_modes].State      = supported;
  Nb_video_modes ++;
}

// Utilisé pour trier les modes retournés par SDL
int Compare_video_modes(const void *p1, const void *p2)
{
  const T_Video_mode *mode1 = (const T_Video_mode *)p1;
  const T_Video_mode *mode2 = (const T_Video_mode *)p2;

  // Tris par largeur
  if(mode1->Width - mode2->Width)
    return mode1->Width - mode2->Width;

  // Tri par hauteur
  return mode1->Height - mode2->Height;
}


// Initializes the list of available video modes
void Set_all_video_modes(void)
{
#if defined(USE_SDL)
  SDL_Rect** Modes;
#endif
  Nb_video_modes=0;
  
  // The first mode will have index number 0.
  // It will be the default mode if an unsupported one
  // is requested in gfx2.ini
  #if defined(__GP2X__) || defined(__WIZ__) || defined(__CAANOO__) || defined(GCWZERO)
  // Native GP2X resolution
  Set_video_mode( 320,240,0, 1);
  #else
  // Window mode, with default size of 640x480
  Set_video_mode( 640,480,0, 0);
  #endif

  Set_video_mode( 320,200,0, 1);
  Set_video_mode( 320,224,0, 1);
  #if !defined(__GP2X__) && !defined(__WIZ__) && !defined(__CAANOO__) && !defined(GCWZERO)
  // For the GP2X, this one is already declared above.
  Set_video_mode( 320,240,0, 1);
  #endif
  Set_video_mode( 320,256,0, 1);
  Set_video_mode( 320,270,0, 1);
  Set_video_mode( 320,282,0, 1);
  Set_video_mode( 320,300,0, 1);
  Set_video_mode( 320,360,0, 1);
  Set_video_mode( 320,400,0, 1);
  Set_video_mode( 320,448,0, 1);
  Set_video_mode( 320,480,0, 1);
  Set_video_mode( 320,512,0, 1);
  Set_video_mode( 320,540,0, 1);
  Set_video_mode( 320,564,0, 1);
  Set_video_mode( 320,600,0, 1);
  Set_video_mode( 360,200,0, 1);
  Set_video_mode( 360,224,0, 1);
  Set_video_mode( 360,240,0, 1);
  Set_video_mode( 360,256,0, 1);
  Set_video_mode( 360,270,0, 1);
  Set_video_mode( 360,282,0, 1);
  Set_video_mode( 360,300,0, 1);
  Set_video_mode( 360,360,0, 1);
  Set_video_mode( 360,400,0, 1);
  Set_video_mode( 360,448,0, 1);
  Set_video_mode( 360,480,0, 1);
  Set_video_mode( 360,512,0, 1);
  Set_video_mode( 360,540,0, 1);
  Set_video_mode( 360,564,0, 1);
  Set_video_mode( 360,600,0, 1);
  Set_video_mode( 400,200,0, 1);
  Set_video_mode( 400,224,0, 1);
  Set_video_mode( 400,240,0, 1);
  Set_video_mode( 400,256,0, 1);
  Set_video_mode( 400,270,0, 1);
  Set_video_mode( 400,282,0, 1);
  Set_video_mode( 400,300,0, 1);
  Set_video_mode( 400,360,0, 1);
  Set_video_mode( 400,400,0, 1);
  Set_video_mode( 400,448,0, 1);
  Set_video_mode( 400,480,0, 1);
  Set_video_mode( 400,512,0, 1);
  Set_video_mode( 400,540,0, 1);
  Set_video_mode( 400,564,0, 1);
  Set_video_mode( 400,600,0, 1);
  Set_video_mode( 640,224,0, 1);
  Set_video_mode( 640,240,0, 1);
  Set_video_mode( 640,256,0, 1);
  Set_video_mode( 640,270,0, 1);
  Set_video_mode( 640,300,0, 1);
  Set_video_mode( 640,350,0, 1);
  Set_video_mode( 640,400,0, 1);
  Set_video_mode( 640,448,0, 1);
  Set_video_mode( 640,480,0, 1);
  Set_video_mode( 640,512,0, 1);
  Set_video_mode( 640,540,0, 1);
  Set_video_mode( 640,564,0, 1);
  Set_video_mode( 640,600,0, 1);
  Set_video_mode( 800,600,0, 1);
  Set_video_mode(1024,768,0, 1);

#if defined(USE_SDL)
  Modes = SDL_ListModes(NULL, SDL_FULLSCREEN);
  if ((Modes != (SDL_Rect**)0) && (Modes!=(SDL_Rect**)-1))
  {
    int index;
    for (index=0; Modes[index]; index++)
    {
      int index2;
#if defined(__GP2X__) || defined(__WIZ__) || defined(__CAANOO__) || defined(GCWZERO)
      // On the GP2X the first mode is not windowed, so include it in the search.
      index2=0;
#else
      index2=1;
#endif
      for (/**/; index2 < Nb_video_modes; index2++)
        if (Modes[index]->w == Video_mode[index2].Width &&
            Modes[index]->h == Video_mode[index2].Height)
        {
          // Was already in the hard-coded list: ok, don't add.
          break;
        }
      if (index2 >= Nb_video_modes && Modes[index]->w>=320 && Modes[index]->h>=200)
      {
        // New mode to add to the list
        Set_video_mode(Modes[index]->w,Modes[index]->h,0, 1);
      }
    }
    // Sort the modes : those found by SDL were listed at the end.
    // Note that we voluntarily omit the first entry: the default mode.
    qsort(&Video_mode[1], Nb_video_modes - 1, sizeof(T_Video_mode), Compare_video_modes);
  }
#elif defined(USE_SDL2)
  {
    SDL_DisplayMode dm;
    int num_modes, mode;
    int display = 0;


    num_modes = SDL_GetNumDisplayModes(display);
    for (mode = num_modes; mode >= 0; mode--) // reverse order. from small resolution to big resolution
    {
      if (SDL_GetDisplayMode(display, mode, &dm) == 0)
        Set_video_mode(dm.w, dm.h, mode, 1);
    }

    if (SDL_GetDesktopDisplayMode(display, &dm) == 0)
    {
      // Set the native desktop video mode
      Set_video_mode(dm.w, dm.h, num_modes + 1, 1);
    }
  }
#elif defined(WIN32)
  {
    int width = GetSystemMetrics(SM_CXSCREEN);
    int height = GetSystemMetrics(SM_CYSCREEN);
    if (width > 0 && height > 0)
    {
      Video_mode[Nb_video_modes].Width      = width;
      Video_mode[Nb_video_modes].Height     = height;
      Video_mode[Nb_video_modes].Mode       = 0;
      Video_mode[Nb_video_modes].Fullscreen = 1;
      Video_mode[Nb_video_modes].State      = 1;
      Nb_video_modes ++;
    }
  }
#endif
}

//---------------------------------------------------------------------------

int Load_CFG(int reload_all)
{
  FILE*  Handle;
  char filename[MAX_PATH_CHARACTERS];
  long file_size;
  int  index,index2;
  T_Config_header       cfg_header;
  T_Config_chunk        Chunk;
  T_Config_shortcut_info cfg_shortcut_info;
  T_Config_video_mode   cfg_video_mode;
  int key_conversion = 0;

  snprintf(filename, sizeof(filename), "%s%s", Config_directory, CONFIG_FILENAME);

  GFX2_Log(GFX2_DEBUG, "Load_CFG() trying to load %s\n", filename);

  if ((Handle=fopen(filename,"rb"))==NULL)
    return ERROR_CFG_MISSING;

  file_size=File_length_file(Handle);

  if ( (file_size<7)
    || (!Read_bytes(Handle, &cfg_header.Signature, 3))
    || memcmp(cfg_header.Signature,"CFG",3)
    || (!Read_byte(Handle, &cfg_header.Version1))
    || (!Read_byte(Handle, &cfg_header.Version2))
    || (!Read_byte(Handle, &cfg_header.Beta1))
    || (!Read_byte(Handle, &cfg_header.Beta2)) )
      goto Erreur_lecture_config;

  // Version DOS de Robinson et X-Man
  if ( (cfg_header.Version1== 2)
    && (cfg_header.Version2== 0)
    && (cfg_header.Beta1== 96))
  {
    // Les touches (scancodes) sont à convertir)
    key_conversion = 1;
  }
  // Version SDL jusqu'a 98%
  else if ( (cfg_header.Version1== 2)
    && (cfg_header.Version2== 0)
    && (cfg_header.Beta1== 97))
  {
    // Les touches 00FF (pas de touche) sont a comprendre comme 0x0000
    key_conversion = 2;
  }
  // Version SDL
  else if ( (cfg_header.Version1!=VERSION1)
    || (cfg_header.Version2!=VERSION2)
    || (cfg_header.Beta1!=BETA1)
    || (cfg_header.Beta2!=BETA2) )
    goto Erreur_config_ancienne;

  // - Lecture des infos contenues dans le fichier de config -
  while (Read_byte(Handle, &Chunk.Number))
  {
    Read_word_le(Handle, &Chunk.Size);
    switch (Chunk.Number)
    {
      case CHUNK_KEYS: // Touches
        if (reload_all)
        {
          for (index=0; index<(long)(Chunk.Size/6); index++)
          {
            if (!Read_word_le(Handle, &cfg_shortcut_info.Number) ||
                !Read_word_le(Handle, &cfg_shortcut_info.Key) ||
                !Read_word_le(Handle, &cfg_shortcut_info.Key2) )
              goto Erreur_lecture_config;
            else
            {
              if (key_conversion==1)
              {
                cfg_shortcut_info.Key = Key_for_scancode(cfg_shortcut_info.Key);
              }
              else if (key_conversion==2)
              {
                if (cfg_shortcut_info.Key == 0x00FF)
                  cfg_shortcut_info.Key = 0x0000;
                if (cfg_shortcut_info.Key2 == 0x00FF)
                  cfg_shortcut_info.Key2 = 0x0000;
              }
              
              for (index2=0;
                 ((index2<NB_SHORTCUTS) && (ConfigKey[index2].Number!=cfg_shortcut_info.Number));
                 index2++);
              if (index2<NB_SHORTCUTS)
              {
                switch(Ordering[index2]>>8)
                {
                  case 0 :
                    Config_Key[Ordering[index2]&0xFF][0]=cfg_shortcut_info.Key;
                    Config_Key[Ordering[index2]&0xFF][1]=cfg_shortcut_info.Key2;
                    break;
                  case 1 :
                    Buttons_Pool[Ordering[index2]&0xFF].Left_shortcut[0] = cfg_shortcut_info.Key;
                    Buttons_Pool[Ordering[index2]&0xFF].Left_shortcut[1] = cfg_shortcut_info.Key2;
                    break;
                  case 2 :
                    Buttons_Pool[Ordering[index2]&0xFF].Right_shortcut[0] = cfg_shortcut_info.Key;
                    Buttons_Pool[Ordering[index2]&0xFF].Right_shortcut[1] = cfg_shortcut_info.Key2;
                    break;
                }
              }
              else
                goto Erreur_lecture_config;
            }
          }
        }
        else
        {
          if (fseek(Handle,Chunk.Size,SEEK_CUR)==-1)
            goto Erreur_lecture_config;
        }
        break;
      case CHUNK_VIDEO_MODES: // Modes vidéo
        for (index=0; index<(long)(Chunk.Size/5); index++)
        {
          if (!Read_byte(Handle, &cfg_video_mode.State) ||
              !Read_word_le(Handle, &cfg_video_mode.Width) ||
              !Read_word_le(Handle, &cfg_video_mode.Height) )
            goto Erreur_lecture_config;

#if defined(__GP2X__) || defined(__WIZ__) || defined(__CAANOO__)
          index2=0;
#else
          index2=1;
#endif
          for (/**/; index2<Nb_video_modes; index2++)
          {
            if (Video_mode[index2].Width==cfg_video_mode.Width &&
                Video_mode[index2].Height==cfg_video_mode.Height)
            {
              // On ne prend le paramètre utilisateur que si la résolution
              // est effectivement supportée par SDL
              // Seules les deux petits bits sont récupérés, car les anciens fichiers
              // de configuration (DOS 96.5%) utilisaient d'autres bits.
              if (! (Video_mode[index2].State & 128))
                Video_mode[index2].State=cfg_video_mode.State&3;
              break;
            }
          }
        }
        break;
      case CHUNK_SHADE: // Shade
        if (reload_all)
        {
          if (! Read_byte(Handle, &Shade_current) )
            goto Erreur_lecture_config;

          for (index=0; index<8; index++)
          {
            for (index2=0; index2<512; index2++)
            {
              if (! Read_word_le(Handle, &Shade_list[index].List[index2]))
                goto Erreur_lecture_config;
            }
            if (! Read_byte(Handle, &Shade_list[index].Step) ||
              ! Read_byte(Handle, &Shade_list[index].Mode) )
            goto Erreur_lecture_config;
          }
          Shade_list_to_lookup_tables(Shade_list[Shade_current].List,
            Shade_list[Shade_current].Step,
            Shade_list[Shade_current].Mode,
            Shade_table_left,Shade_table_right);
        }
        else
        {
          if (fseek(Handle,Chunk.Size,SEEK_CUR)==-1)
            goto Erreur_lecture_config;
        }
        break;
      case CHUNK_MASK: // Masque
        if (reload_all)
        {
          if (!Read_bytes(Handle, Mask_table, 256))
            goto Erreur_lecture_config;
        }
        else
        {
          if (fseek(Handle,Chunk.Size,SEEK_CUR)==-1)
            goto Erreur_lecture_config;
        }
        break;
      case CHUNK_STENCIL: // Stencil
        if (reload_all)
        {
          if (!Read_bytes(Handle, Stencil, 256))
            goto Erreur_lecture_config;
        }
        else
        {
          if (fseek(Handle,Chunk.Size,SEEK_CUR)==-1)
            goto Erreur_lecture_config;
        }
        break;
      case CHUNK_GRADIENTS: // Infos sur les dégradés
        // The gradients chunk is deprecated since the data
        // is now loaded/saved in GIF and IFF formats.
        // The chunk will be completely ignored.
        /*if (reload_all)
        {
          if (! Read_byte(Handle, &Current_gradient))
            goto Erreur_lecture_config;
          for(index=0;index<16;index++)
          {
            if (!Read_byte(Handle, &Gradient_array[index].Start) ||
                !Read_byte(Handle, &Gradient_array[index].End) ||
                !Read_dword_le(Handle, &Gradient_array[index].Inverse) ||
                !Read_dword_le(Handle, &Gradient_array[index].Mix) ||
                !Read_dword_le(Handle, &Gradient_array[index].Technique) )
            goto Erreur_lecture_config;
          }
          Load_gradient_data(Current_gradient);
        }
        else*/
        {
          if (fseek(Handle,Chunk.Size,SEEK_CUR)==-1)
            goto Erreur_lecture_config;
        }
        break;
      case CHUNK_SMOOTH: // Matrice du smooth
        if (reload_all)
        {
          for (index=0; index<3; index++)
            for (index2=0; index2<3; index2++)
              if (!Read_byte(Handle, &(Smooth_matrix[index][index2])))
                goto Erreur_lecture_config;
        }
        else
        {
          if (fseek(Handle,Chunk.Size,SEEK_CUR)==-1)
            goto Erreur_lecture_config;
        }
        break;
      case CHUNK_EXCLUDE_COLORS: // Exclude_color
        if (reload_all)
        {
          if (!Read_bytes(Handle, Exclude_color, 256))
            goto Erreur_lecture_config;
        }
        else
        {
          if (fseek(Handle,Chunk.Size,SEEK_CUR)==-1)
            goto Erreur_lecture_config;
        }
        break;
      case CHUNK_QUICK_SHADE: // Quick-shade
        if (reload_all)
        {
          if (!Read_byte(Handle, &Quick_shade_step))
            goto Erreur_lecture_config;
          if (!Read_byte(Handle, &Quick_shade_loop))
            goto Erreur_lecture_config;
        }
        else
        {
          if (fseek(Handle,Chunk.Size,SEEK_CUR)==-1)
            goto Erreur_lecture_config;
        }
        break;
        case CHUNK_GRID: // Grille
        if (reload_all)
        {
          if (!Read_word_le(Handle, &Snap_width))
            goto Erreur_lecture_config;
          if (!Read_word_le(Handle, &Snap_height))
            goto Erreur_lecture_config;
          if (!Read_word_le(Handle, &Snap_offset_X))
            goto Erreur_lecture_config;
          if (!Read_word_le(Handle, &Snap_offset_Y))
            goto Erreur_lecture_config;
        }
        else
        {
          if (fseek(Handle,Chunk.Size,SEEK_CUR)==-1)
            goto Erreur_lecture_config;
        }
        break;
        
      case CHUNK_BRUSH:
        if (reload_all)
        {
          int index;
          for (index=0; index<NB_PAINTBRUSH_SPRITES; index++)
          {
            int i;
            byte current_byte=0;
            word width,height;

            if (!Read_byte(Handle, &Paintbrush[index].Shape))
              goto Erreur_lecture_config;

            if (!Read_word_le(Handle, &width))
              goto Erreur_lecture_config;
            if (!Read_word_le(Handle, &height))
              goto Erreur_lecture_config;
      
            Paintbrush[index].Width=width;
            Paintbrush[index].Height=height;

            if (!Read_word_le(Handle, &Paintbrush[index].Offset_X))
              goto Erreur_lecture_config;
            if (!Read_word_le(Handle, &Paintbrush[index].Offset_Y))
              goto Erreur_lecture_config;
            
            // Decode binary
            for (i=0;i<width*height;i++)
            {
              if ((i&7) == 0)
              {
                // Read one byte
                if (!Read_byte(Handle, &current_byte))
                  goto Erreur_lecture_config;
              }
              Paintbrush[index].Sprite[i/width][i%width] =
                ((current_byte & (0x80 >> (i&7))) != 0);              
            }
          }
        }
        else
        {
          if (fseek(Handle,Chunk.Size,SEEK_CUR)==-1)
            goto Erreur_lecture_config;
        }
        break;
        
        
      case CHUNK_SCRIPTS:
        if (reload_all)
        {
          int current_size=0;
          int current_script=0;
          
          while(current_size<Chunk.Size)
          {
            byte size;
            
            // Free (old) string
            free(Bound_script[current_script]);
            Bound_script[current_script]=NULL;
            
            if (!Read_byte(Handle, &size))
              goto Erreur_lecture_config;
              
            if (size!=0)
            {
              // Alloc string
              Bound_script[current_script]=malloc(size+1);
              if (Bound_script[current_script]==NULL)
                return ERROR_MEMORY;
              
              // Init and load string
              memset(Bound_script[current_script], 0, size+1);
              if (!Read_bytes(Handle, Bound_script[current_script], size))
                goto Erreur_lecture_config;
            }
            current_size+=size+1;
            current_script++;
            
            // Do not load more strings than hard-coded limit
            if (current_script>=10)
              break;
          }
          
          
        }
        break;
        
      default: // Chunk inconnu
        goto Erreur_lecture_config;
    }
  }

  if (fclose(Handle))
    return ERROR_CFG_CORRUPTED;

  return 0;

Erreur_lecture_config:
  fclose(Handle);
  return ERROR_CFG_CORRUPTED;
Erreur_config_ancienne:
  fclose(Handle);
  return ERROR_CFG_OLD;
}


int Save_CFG(void)
{
  FILE*  Handle;
  int  index;
  int  index2;
  int modes_to_save;
  char filename[MAX_PATH_CHARACTERS];
  T_Config_header cfg_header;
  T_Config_chunk Chunk;
  T_Config_shortcut_info cfg_shortcut_info={0,0,0};
  T_Config_video_mode   cfg_video_mode={0,0,0};

  strcpy(filename,Config_directory);
  strcat(filename,CONFIG_FILENAME);

  if ((Handle=fopen(filename,"wb"))==NULL)
    return ERROR_SAVING_CFG;

  // Ecriture du header
  memcpy(cfg_header.Signature,"CFG",3);
  cfg_header.Version1=VERSION1;
  cfg_header.Version2=VERSION2;
  cfg_header.Beta1   =BETA1;
  cfg_header.Beta2   =BETA2;
  if (!Write_bytes(Handle, &cfg_header.Signature,3) ||
      !Write_byte(Handle, cfg_header.Version1) ||
      !Write_byte(Handle, cfg_header.Version2) ||
      !Write_byte(Handle, cfg_header.Beta1) ||
      !Write_byte(Handle, cfg_header.Beta2) )
    goto Erreur_sauvegarde_config;

  // Enregistrement des touches
  Chunk.Number=CHUNK_KEYS;
  Chunk.Size=NB_SHORTCUTS*6;

  if (!Write_byte(Handle, Chunk.Number) ||
      !Write_word_le(Handle, Chunk.Size) )
    goto Erreur_sauvegarde_config;
  for (index=0; index<NB_SHORTCUTS; index++)
  {
    cfg_shortcut_info.Number = ConfigKey[index].Number;
    switch(Ordering[index]>>8)
    {
      case 0 :
        cfg_shortcut_info.Key =Config_Key[Ordering[index]&0xFF][0]; 
        cfg_shortcut_info.Key2=Config_Key[Ordering[index]&0xFF][1]; 
        break;
      case 1 :
        cfg_shortcut_info.Key =Buttons_Pool[Ordering[index]&0xFF].Left_shortcut[0]; 
        cfg_shortcut_info.Key2=Buttons_Pool[Ordering[index]&0xFF].Left_shortcut[1]; 
        break;
      case 2 : 
        cfg_shortcut_info.Key =Buttons_Pool[Ordering[index]&0xFF].Right_shortcut[0]; 
        cfg_shortcut_info.Key2=Buttons_Pool[Ordering[index]&0xFF].Right_shortcut[1]; 
        break;
    }
    if (!Write_word_le(Handle, cfg_shortcut_info.Number) ||
        !Write_word_le(Handle, cfg_shortcut_info.Key) ||
        !Write_word_le(Handle, cfg_shortcut_info.Key2) )
      goto Erreur_sauvegarde_config;
  }

  // D'abord compter les modes pour lesquels l'utilisateur a mis une préférence
  modes_to_save=0;
#if defined(__GP2X__) || defined (__WIZ__) || defined (__CAANOO__)
  index = 0;
#else
  index = 1;
#endif
  for (/**/; index<Nb_video_modes; index++)
    if (Video_mode[index].State==0 || Video_mode[index].State==2 || Video_mode[index].State==3)
      modes_to_save++;

  // Sauvegarde de l'état de chaque mode vidéo
  Chunk.Number=CHUNK_VIDEO_MODES;
  Chunk.Size=modes_to_save * 5;

  if (!Write_byte(Handle, Chunk.Number) ||
      !Write_word_le(Handle, Chunk.Size) )
    goto Erreur_sauvegarde_config;
#if defined(__GP2X__) || defined (__WIZ__) || defined (__CAANOO__)
  index = 0;
#else
  index = 1;
#endif
  for (/**/; index<Nb_video_modes; index++)
    if (Video_mode[index].State==0 || Video_mode[index].State==2 || Video_mode[index].State==3)
    {
      cfg_video_mode.State   =Video_mode[index].State;
      cfg_video_mode.Width=Video_mode[index].Width;
      cfg_video_mode.Height=Video_mode[index].Height;

      if (!Write_byte(Handle, cfg_video_mode.State) ||
        !Write_word_le(Handle, cfg_video_mode.Width) ||
        !Write_word_le(Handle, cfg_video_mode.Height) )
        goto Erreur_sauvegarde_config;
    }

  // Ecriture des données du Shade (précédées du shade en cours)
  Chunk.Number=CHUNK_SHADE;
  Chunk.Size=8209;
  if (!Write_byte(Handle, Chunk.Number) ||
      !Write_word_le(Handle, Chunk.Size) )
    goto Erreur_sauvegarde_config;
  if (!Write_byte(Handle, Shade_current))
    goto Erreur_sauvegarde_config;
  for (index=0; index<8; index++)
  {
    for (index2=0; index2<512; index2++)
    {
      if (! Write_word_le(Handle, Shade_list[index].List[index2]))
        goto Erreur_sauvegarde_config;
    }
    if (! Write_byte(Handle, Shade_list[index].Step) ||
      ! Write_byte(Handle, Shade_list[index].Mode) )
    goto Erreur_sauvegarde_config;
  }

  // Sauvegarde des informations du Masque
  Chunk.Number=CHUNK_MASK;
  Chunk.Size=256;
  if (!Write_byte(Handle, Chunk.Number) ||
      !Write_word_le(Handle, Chunk.Size) )
    goto Erreur_sauvegarde_config;
  if (!Write_bytes(Handle, Mask_table,256))
    goto Erreur_sauvegarde_config;

  // Sauvegarde des informations du Stencil
  Chunk.Number=CHUNK_STENCIL;
  Chunk.Size=256;
  if (!Write_byte(Handle, Chunk.Number) ||
      !Write_word_le(Handle, Chunk.Size) )
    goto Erreur_sauvegarde_config;
  if (!Write_bytes(Handle, Stencil,256))
    goto Erreur_sauvegarde_config;

  // Sauvegarde des informations des dégradés
  // The gradients chunk is deprecated since the data
  // is now loaded/saved in GIF and IFF formats.
  /*
  Chunk.Number=CHUNK_GRADIENTS;
  Chunk.Size=14*16+1;
  if (!Write_byte(Handle, Chunk.Number) ||
      !Write_word_le(Handle, Chunk.Size) )
    goto Erreur_sauvegarde_config;
  if (!Write_byte(Handle, Current_gradient))
    goto Erreur_sauvegarde_config;
  for(index=0;index<16;index++)
  {
    if (!Write_byte(Handle,Gradient_array[index].Start) ||
        !Write_byte(Handle,Gradient_array[index].End) ||
        !Write_dword_le(Handle, Gradient_array[index].Inverse) ||
        !Write_dword_le(Handle, Gradient_array[index].Mix) ||
        !Write_dword_le(Handle, Gradient_array[index].Technique) )
        goto Erreur_sauvegarde_config;
  }
  */

  // Sauvegarde de la matrice du Smooth
  Chunk.Number=CHUNK_SMOOTH;
  Chunk.Size=9;
  if (!Write_byte(Handle, Chunk.Number) ||
      !Write_word_le(Handle, Chunk.Size) )
    goto Erreur_sauvegarde_config;
  for (index=0; index<3; index++)
    for (index2=0; index2<3; index2++)
      if (!Write_byte(Handle, Smooth_matrix[index][index2]))
        goto Erreur_sauvegarde_config;

  // Sauvegarde des couleurs à exclure
  Chunk.Number=CHUNK_EXCLUDE_COLORS;
  Chunk.Size=256;
  if (!Write_byte(Handle, Chunk.Number) ||
      !Write_word_le(Handle, Chunk.Size) )
    goto Erreur_sauvegarde_config;
 if (!Write_bytes(Handle, Exclude_color, 256))
    goto Erreur_sauvegarde_config;

  // Sauvegarde des informations du Quick-shade
  Chunk.Number=CHUNK_QUICK_SHADE;
  Chunk.Size=2;
  if (!Write_byte(Handle, Chunk.Number) ||
      !Write_word_le(Handle, Chunk.Size) )
    goto Erreur_sauvegarde_config;
  if (!Write_byte(Handle, Quick_shade_step))
    goto Erreur_sauvegarde_config;
  if (!Write_byte(Handle, Quick_shade_loop))
    goto Erreur_sauvegarde_config;

  // Sauvegarde des informations de la grille
  Chunk.Number=CHUNK_GRID;
  Chunk.Size=8;
  if (!Write_byte(Handle, Chunk.Number) ||
      !Write_word_le(Handle, Chunk.Size) )
    goto Erreur_sauvegarde_config;
  if (!Write_word_le(Handle, Snap_width))
    goto Erreur_sauvegarde_config;
  if (!Write_word_le(Handle, Snap_height))
    goto Erreur_sauvegarde_config;
  if (!Write_word_le(Handle, Snap_offset_X))
    goto Erreur_sauvegarde_config;
  if (!Write_word_le(Handle, Snap_offset_Y))
    goto Erreur_sauvegarde_config;

  // Save brush data
  {
    long total_size=0;
    int index;
    // Compute size: monochrome paintbrushes
    for (index=0; index<NB_PAINTBRUSH_SPRITES; index++)
    {
      total_size+=9+(Paintbrush[index].Width*Paintbrush[index].Height+7)/8;
    }
    /*
    // Compute size: brush container
    for (index=0; index<BRUSH_CONTAINER_COLUMNS*BRUSH_CONTAINER_ROWS; index++)
    {
      
    }
    */
    Chunk.Number=CHUNK_BRUSH;
    Chunk.Size=total_size;
    if (!Write_byte(Handle, Chunk.Number) ||
        !Write_word_le(Handle, Chunk.Size) )
      goto Erreur_sauvegarde_config;
    for (index=0; index<NB_PAINTBRUSH_SPRITES; index++)
    {
      int i;
      byte current_byte=0;
      word width,height;

      width=Paintbrush[index].Width;
      height=Paintbrush[index].Height;
      
      
      if (!Write_byte(Handle, Paintbrush[index].Shape))
        goto Erreur_sauvegarde_config;
      if (!Write_word_le(Handle, width))
        goto Erreur_sauvegarde_config;
      if (!Write_word_le(Handle, height))
        goto Erreur_sauvegarde_config;
      if (!Write_word_le(Handle, Paintbrush[index].Offset_X))
        goto Erreur_sauvegarde_config;
      if (!Write_word_le(Handle, Paintbrush[index].Offset_Y))
        goto Erreur_sauvegarde_config;
      // Encode in binary
      for (i=0;i<width*height;i++)
      {
        if (Paintbrush[index].Sprite[i/width][i%width])
          current_byte |= 0x80 >> (i&7);
        if ((i&7) == 7)
        {
          // Write one byte
          if (!Write_byte(Handle, current_byte))
            goto Erreur_sauvegarde_config;
          current_byte=0;
        }
      }
      // Remainder
      if ((i&7) != 0)
      {
        // Write one byte
        if (!Write_byte(Handle, current_byte))
          goto Erreur_sauvegarde_config;
      }
    }
  }
  
  // Save script shortcuts
  {
    int i;
    Chunk.Number=CHUNK_SCRIPTS;
    // Compute size : Data stored as 10 pascal strings
    Chunk.Size=0;
    for (i=0; i<10; i++)    
    {
      if (Bound_script[i]==NULL)
        Chunk.Size+=1;
      else
        Chunk.Size+=strlen(Bound_script[i])+1;
    }
    // Header
    if (!Write_byte(Handle, Chunk.Number) ||
        !Write_word_le(Handle, Chunk.Size) )
      goto Erreur_sauvegarde_config;
      
    // Strings
    for (i=0; i<10; i++)    
    {
      byte size=0;      
      if (Bound_script[i]!=NULL)
        size=strlen(Bound_script[i]);
        
      if (!Write_byte(Handle, size))
        goto Erreur_sauvegarde_config;
        
      if (size)
        if (!Write_bytes(Handle, Bound_script[i], size))
          goto Erreur_sauvegarde_config;
    }
  }
  
  if (fclose(Handle))
    return ERROR_SAVING_CFG;

  return 0;

Erreur_sauvegarde_config:
  fclose(Handle);
  return ERROR_SAVING_CFG;
}

// (Ré)assigne toutes les valeurs de configuration par défaut
void Set_config_defaults(void)
{
  int index, index2;

  // Keyboard shortcuts
  for (index=0; index<NB_SHORTCUTS; index++)
  {
    switch(Ordering[index]>>8)
    {
      case 0 :
        Config_Key[Ordering[index]&0xFF][0]=ConfigKey[index].Key;
        Config_Key[Ordering[index]&0xFF][1]=ConfigKey[index].Key2;
        break;
      case 1 :
        Buttons_Pool[Ordering[index]&0xFF].Left_shortcut[0] = ConfigKey[index].Key;
        Buttons_Pool[Ordering[index]&0xFF].Left_shortcut[1] = ConfigKey[index].Key2;
        break;
      case 2 :
        Buttons_Pool[Ordering[index]&0xFF].Right_shortcut[0] = ConfigKey[index].Key;
        Buttons_Pool[Ordering[index]&0xFF].Right_shortcut[1] = ConfigKey[index].Key2;
        break;
    }
  }
  // Shade
  Shade_current=0;
  for (index=0; index<8; index++)
  {
    Shade_list[index].Step=1;
    Shade_list[index].Mode=0;
    for (index2=0; index2<512; index2++)
      Shade_list[index].List[index2]=256;
  }
  // Shade par défaut pour la palette standard
  for (index=0; index<7; index++)
    for (index2=0; index2<16; index2++)
      Shade_list[0].List[index*17+index2]=index*16+index2+16;

  Shade_list_to_lookup_tables(Shade_list[Shade_current].List,
            Shade_list[Shade_current].Step,
            Shade_list[Shade_current].Mode,
            Shade_table_left,Shade_table_right);

  // Mask
  for (index=0; index<256; index++)
    Mask_table[index]=0;

  // Stencil
  for (index=0; index<256; index++)
    Stencil[index]=1;
  
  // Smooth
  Smooth_matrix[0][0]=1;
  Smooth_matrix[0][1]=2;
  Smooth_matrix[0][2]=1;
  Smooth_matrix[1][0]=2;
  Smooth_matrix[1][1]=4;
  Smooth_matrix[1][2]=2;
  Smooth_matrix[2][0]=1;
  Smooth_matrix[2][1]=2;
  Smooth_matrix[2][2]=1;

  // Exclude colors
  for (index=0; index<256; index++)
    Exclude_color[index]=0;

  // Quick shade
  Quick_shade_step=1;
  Quick_shade_loop=0;

  // Grid
  Snap_width=Snap_height=8;
  Snap_offset_X=Snap_offset_Y=0;

}

#ifdef GRAFX2_CATCHES_SIGNALS

#if defined(WIN32)
#if defined(_MSC_VER)
typedef void (*__p_sig_fn_t)(int);
#endif
  #define SIGHANDLER_T __p_sig_fn_t
#elif defined(__macosx__)
  typedef void (*sig_t) (int);
  #define SIGHANDLER_T sig_t
#else
  #define SIGHANDLER_T __sighandler_t
#endif

// Memorize the signal handlers of SDL
SIGHANDLER_T Handler_TERM=SIG_DFL;
SIGHANDLER_T Handler_INT=SIG_DFL;
SIGHANDLER_T Handler_ABRT=SIG_DFL;
SIGHANDLER_T Handler_SEGV=SIG_DFL;
SIGHANDLER_T Handler_FPE=SIG_DFL;

void Sig_handler(int sig)
{
  // Restore default behaviour
  signal(SIGTERM, Handler_TERM);
  signal(SIGINT, Handler_INT);
  signal(SIGABRT, Handler_ABRT);
  signal(SIGSEGV, Handler_SEGV);
  signal(SIGFPE, Handler_FPE);
  
  switch(sig)
  {
    case SIGTERM:
    case SIGINT:
    case SIGABRT:
    case SIGSEGV:
      Image_emergency_backup();
    default:
    break;
   }
}
#endif

void Init_sighandler(void)
{
#ifdef GRAFX2_CATCHES_SIGNALS
  Handler_TERM=signal(SIGTERM,Sig_handler);
  Handler_INT =signal(SIGINT,Sig_handler);
  Handler_ABRT=signal(SIGABRT,Sig_handler);
  Handler_SEGV=signal(SIGSEGV,Sig_handler);
  Handler_FPE =signal(SIGFPE,Sig_handler);
#endif
}

void Init_brush_container(void)
{
  int i;
  
  for (i=0; i<BRUSH_CONTAINER_COLUMNS*BRUSH_CONTAINER_ROWS; i++)
  {
    int x,y,c;
    
    Brush_container[i].Paintbrush_shape=PAINTBRUSH_SHAPE_MAX;
    Brush_container[i].Width=0;
    Brush_container[i].Height=0;
    memset(Brush_container[i].Palette,0,sizeof(T_Palette));
    Brush_container[i].Transp_color=0;  
    for (y=0; y<BRUSH_CONTAINER_PREVIEW_WIDTH; y++)
      for (x=0; x<BRUSH_CONTAINER_PREVIEW_HEIGHT; x++)
        Brush_container[i].Thumbnail[y][x]=0;
    for (c=0; c<256; c++)
        Brush_container[i].Colormap[c]=c;
        
    Brush_container[i].Brush = NULL;
  }
}

void Set_current_skin(const char *skinfile, T_Gui_skin *gfx)
{
  int i;
  
  // Free previous one
  free(Gfx);
  
  // Assign main skin pointer
  Gfx = gfx;

  // Change config  
  if(Config.Skin_file != skinfile) // Happens when loading the initial skin
  {
    free(Config.Skin_file);
    Config.Skin_file = strdup(skinfile);
  }

  //Config.Fav_menu_colors[0] = gfx->Default_palette[gfx->Color[0]];
  //Config.Fav_menu_colors[1] = gfx->Default_palette[gfx->Color[1]];
  //Config.Fav_menu_colors[2] = gfx->Default_palette[gfx->Color[2]];
  //Config.Fav_menu_colors[3] = gfx->Default_palette[gfx->Color[3]];
  
  // Reassign GUI color indices
  MC_Black = gfx->Color[0];
  MC_Dark =  gfx->Color[1];
  MC_Light = gfx->Color[2];
  MC_White = gfx->Color[3];
  MC_Trans = gfx->Color_trans;
  MC_OnBlack=MC_Dark;
  MC_Window=MC_Light;
  MC_Lighter=MC_White;
  MC_Darker=MC_Dark;
  

  // Set menubars to point to the new data
  for (i=0; i<3; i++)
  {
    Menu_bars[MENUBAR_TOOLS ].Skin[i] = (byte*)&(gfx->Menu_block[i]);
    Menu_bars[MENUBAR_LAYERS].Skin[i] = (byte*)&(gfx->Layerbar_block[i]);
    Menu_bars[MENUBAR_ANIMATION].Skin[i] = (byte*)&(gfx->Animbar_block[i]);
    Menu_bars[MENUBAR_STATUS].Skin[i] = (byte*)&(gfx->Statusbar_block[i]);
  }
}

void Init_paintbrush(int index, int width, int height, byte shape, const char * bitmap)
{
  if (bitmap!=NULL)
  {
    int i;
    
    Paintbrush[index].Shape=shape;
    Paintbrush[index].Width=width;
    Paintbrush[index].Height=height;
    Paintbrush[index].Offset_X=width>>1;
    Paintbrush[index].Offset_Y=height>>1;
  
    // Decode pixels
    for (i=0;i<width*height;i++)
    {
      Paintbrush[index].Sprite[i/width][i%width] =
        ((bitmap[i/8] & (0x80 >> (i&7))) != 0);              
    }
  }
  else
  {
    Paintbrush_shape=shape;
    Set_paintbrush_size(width, height);
    Store_paintbrush(index);
  }

}


void Init_paintbrushes(void)
{
  int index;
 
  Init_paintbrush( 0, 1, 1,PAINTBRUSH_SHAPE_SQUARE, NULL);
  Init_paintbrush( 1, 2, 2,PAINTBRUSH_SHAPE_SQUARE, NULL);
  Init_paintbrush( 2, 3, 3,PAINTBRUSH_SHAPE_SQUARE, NULL);
  Init_paintbrush( 3, 4, 4,PAINTBRUSH_SHAPE_SQUARE, NULL);
  Init_paintbrush( 4, 5, 5,PAINTBRUSH_SHAPE_SQUARE, NULL);
  Init_paintbrush( 5, 7, 7,PAINTBRUSH_SHAPE_SQUARE, NULL);
  Init_paintbrush( 6, 8, 8,PAINTBRUSH_SHAPE_SQUARE, NULL);
  Init_paintbrush( 7,12,12,PAINTBRUSH_SHAPE_SQUARE, NULL);
  Init_paintbrush( 8,16,16,PAINTBRUSH_SHAPE_SQUARE, NULL);
  Init_paintbrush( 9,16,16,PAINTBRUSH_SHAPE_SIEVE_SQUARE, NULL);
  Init_paintbrush(10,15,15,PAINTBRUSH_SHAPE_DIAMOND, NULL);
  Init_paintbrush(11, 5, 5,PAINTBRUSH_SHAPE_DIAMOND, NULL);
  Init_paintbrush(12, 3, 3,PAINTBRUSH_SHAPE_ROUND, NULL);
  Init_paintbrush(13, 4, 4,PAINTBRUSH_SHAPE_ROUND, NULL);
  Init_paintbrush(14, 5, 5,PAINTBRUSH_SHAPE_ROUND, NULL);
  Init_paintbrush(15, 6, 6,PAINTBRUSH_SHAPE_ROUND, NULL);
  Init_paintbrush(16, 8, 8,PAINTBRUSH_SHAPE_ROUND, NULL);
  Init_paintbrush(17,10,10,PAINTBRUSH_SHAPE_ROUND, NULL);
  Init_paintbrush(18,12,12,PAINTBRUSH_SHAPE_ROUND, NULL);
  Init_paintbrush(19,14,14,PAINTBRUSH_SHAPE_ROUND, NULL);
  Init_paintbrush(20,16,16,PAINTBRUSH_SHAPE_ROUND, NULL);
  Init_paintbrush(21,15,15,PAINTBRUSH_SHAPE_SIEVE_ROUND, NULL);
  Init_paintbrush(22,11,11,PAINTBRUSH_SHAPE_SIEVE_ROUND, NULL);
  Init_paintbrush(23, 5, 5,PAINTBRUSH_SHAPE_SIEVE_ROUND, NULL);
  Init_paintbrush(24, 2, 1,PAINTBRUSH_SHAPE_HORIZONTAL_BAR, NULL);
  Init_paintbrush(25, 3, 1,PAINTBRUSH_SHAPE_HORIZONTAL_BAR, NULL);
  Init_paintbrush(26, 4, 1,PAINTBRUSH_SHAPE_HORIZONTAL_BAR, NULL);
  Init_paintbrush(27, 8, 1,PAINTBRUSH_SHAPE_HORIZONTAL_BAR, NULL);
  Init_paintbrush(28, 1, 2,PAINTBRUSH_SHAPE_VERTICAL_BAR, NULL);
  Init_paintbrush(29, 1, 3,PAINTBRUSH_SHAPE_VERTICAL_BAR, NULL);
  Init_paintbrush(30, 1, 4,PAINTBRUSH_SHAPE_VERTICAL_BAR, NULL);
  Init_paintbrush(31, 1, 8,PAINTBRUSH_SHAPE_VERTICAL_BAR, NULL);
  Init_paintbrush(32, 3, 3,PAINTBRUSH_SHAPE_CROSS, NULL);
  Init_paintbrush(33, 5, 5,PAINTBRUSH_SHAPE_CROSS, NULL);
  Init_paintbrush(34, 5, 5,PAINTBRUSH_SHAPE_PLUS, NULL);
  Init_paintbrush(35,15,15,PAINTBRUSH_SHAPE_PLUS, NULL);
  Init_paintbrush(36, 2, 2,PAINTBRUSH_SHAPE_SLASH, NULL);
  Init_paintbrush(37, 4, 4,PAINTBRUSH_SHAPE_SLASH, NULL);
  Init_paintbrush(38, 8, 8,PAINTBRUSH_SHAPE_SLASH, NULL);
  Init_paintbrush(39, 2, 2,PAINTBRUSH_SHAPE_ANTISLASH, NULL);
  Init_paintbrush(40, 4, 4,PAINTBRUSH_SHAPE_ANTISLASH, NULL);
  Init_paintbrush(41, 8, 8,PAINTBRUSH_SHAPE_ANTISLASH, NULL);
  
  Init_paintbrush(42, 4, 4,PAINTBRUSH_SHAPE_RANDOM, "\x20\x81");
  Init_paintbrush(43, 8, 8,PAINTBRUSH_SHAPE_RANDOM, "\x44\x00\x11\x00\x88\x01\x40\x08");
  Init_paintbrush(44,13,13,PAINTBRUSH_SHAPE_RANDOM, "\x08\x00\x08\x90\x00\x10\x42\x10\x02\x06\x02\x02\x04\x02\x08\x42\x10\x44\x00\x00\x44\x00");
  
  Init_paintbrush(45, 3, 3,PAINTBRUSH_SHAPE_MISC, "\x7f\x00");
  Init_paintbrush(46, 3, 3,PAINTBRUSH_SHAPE_MISC, "\xdd\x80");
  Init_paintbrush(47, 7, 7,PAINTBRUSH_SHAPE_MISC, "\x06\x30\x82\x04\x10\x20\x00");

  for (index=0;index<NB_PAINTBRUSH_SPRITES;index++)
  {
    Paintbrush[index].Offset_X=(Paintbrush[index].Width>>1);
    Paintbrush[index].Offset_Y=(Paintbrush[index].Height>>1);
  }
}
