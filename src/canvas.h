#pragma once
// A retained RGBA pixel canvas drawn on the CPU. Panels upload it as a texture;
// the dashboard submits it to SteamVR directly. Text uses FreeType.
#include <string>
#include <vector>
struct Canvas {int width=0,height=0;std::vector<unsigned char> rgba;std::string fontPath;bool dirty=true;};
bool canvasInitLibrary();
// The SF_FONT font, else the first installed monospace font found.
std::string canvasDefaultFont();
void canvasInit(Canvas &,int width,int height,const std::string &font);
void canvasClear(Canvas &,unsigned char,unsigned char,unsigned char,unsigned char);
void canvasRect(Canvas &,int,int,int,int,unsigned char,unsigned char,unsigned char,unsigned char,bool);
void canvasLine(Canvas &,int,int,int,int,unsigned char,unsigned char,unsigned char,unsigned char);
void canvasCircle(Canvas &,int,int,int,unsigned char,unsigned char,unsigned char,unsigned char,bool);
bool canvasText(Canvas &,int,int,const std::string &,unsigned char,unsigned char,unsigned char,unsigned char,int);
// Width in pixels of one line of text at this size, or -1 without a usable font.
int canvasTextWidth(const Canvas &,const std::string &,int pixels);
// Blends RGBA pixels onto the canvas; the source rows are tightly packed.
void canvasBlit(Canvas &,int x,int y,int width,int height,const unsigned char *rgba);
