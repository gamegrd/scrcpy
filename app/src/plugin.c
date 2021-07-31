#include "plugin.h"
#include "scrcpy.h"
#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include "controller.h"
#include "decoder.h"
#include "events.h"
#include "file_handler.h"
#include "input_manager.h"
#include "recorder.h"
#include "screen.h"
#include "server.h"
#include "stream.h"
#include "tiny_xpm.h"
#include "util/log.h"
#include "util/net.h"
#ifdef HAVE_V4L2
#include "v4l2_sink.h"
#endif

#ifdef _WIN32
// not needed here, but winsock2.h must never be included AFTER windows.h
#include <winsock2.h>
#include <windows.h>

struct scrcpy
{
	struct server server;
	struct screen screen;
	struct stream stream;
	struct decoder decoder;
	struct recorder recorder;
#ifdef HAVE_V4L2
	struct sc_v4l2_sink v4l2_sink;
#endif
	struct controller controller;
	struct file_handler file_handler;
	struct input_manager input_manager;
};

#endif

#ifdef _WIN32

//--------------------global-----------------
struct scrcpy *g_scrcpy = NULL;
bool bPauseCtrl = false;
bool bRecord = false;
#define WM_PLUGIN_BASE WM_USER
//-------------------------------------------

bool ScreenShot(struct scrcpy *s,char* name)
{
	int w,h=0;
	OutputDebugStringA(name);
	SDL_GetRendererOutputSize(s->screen.renderer,&w,&h);
	SDL_Surface *sshot = SDL_CreateRGBSurface(0, w, h, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000);
	SDL_RenderReadPixels(s->screen.renderer, NULL, SDL_PIXELFORMAT_ARGB8888, sshot->pixels, sshot->pitch);
	SDL_SaveBMP(sshot,  name);
	SDL_FreeSurface(sshot);
	return true;
}

bool AutoRecordClick(int x,int y){
	if (!bRecord){
		return false;
	}
	int w,h=0;
	SDL_GetRendererOutputSize(g_scrcpy->screen.renderer,&w,&h);
	char name[0x200];
	time_t seconds = time(NULL);
	double fx = (double)(x) / (double)(w);
	double fy = (double)(y) / (double)(h);
	for (int i=0;i<1;i++){
		sprintf(name,"%lld.%ld-%d-%.6f-%.6f.bmp",seconds,clock(),i,fx,fy);
		ScreenShot(g_scrcpy,name);
		Sleep(20);
	}
	return true;
}

bool CheckMutex(char *name)
{
	HANDLE hMutex;
	hMutex = OpenMutexA(MUTEX_ALL_ACCESS, FALSE, name);
	if (NULL == hMutex)
	{
		hMutex = CreateMutex(0, FALSE, name);
		return true;
	}
	return false;
}

char WorkDir[0x200] = "x";
void ExtractFilePath(char *FullPath)
{
	if (!FullPath)
		return;
	char *_filename = strrchr(FullPath, '\\');
	if (!_filename)
		return;
	if (strlen(_filename) > 1)
	{
		_filename[1] = '\0';
	}
}
void InitFunctionModule(void *hModule)
{
	if (strlen(WorkDir) < 2)
	{
		GetModuleFileNameA((HMODULE)hModule, WorkDir, sizeof(WorkDir));
	}
	ExtractFilePath(WorkDir);
}
typedef bool (*pSetInterface)(void *Interface);
void LoadPlugins(char *plugin_path, void *pData)
{
	HMODULE module = LoadLibrary(plugin_path);
	pSetInterface f = (pSetInterface)((void *)GetProcAddress(module, "SetInterface"));
	if (f)
	{
		f(pData);
	}
}

DWORD IterFiles()
{
	DWORD dwStatus = 0;
	WIN32_FIND_DATA findFileData;
	char filePath[] = "*.plugin";
	HANDLE hFind = FindFirstFile(filePath, &findFileData);
	if (hFind != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (strcmp(findFileData.cFileName, ".") == 0 || strcmp(findFileData.cFileName, "..") == 0)
			{
				continue;
			}
			else
			{
				LoadPlugins(findFileData.cFileName, NULL);
			}
			if (dwStatus != 0)
			{
				break;
			}
		} while (FindNextFile(hFind, &findFileData));
	}

	return dwStatus;
}

void on_window_message(void *userdata, void *hWnd, unsigned int message, Uint64 wParam, int64_t lParam)
{
	(void)(userdata);
	(void)(hWnd);
	char nr[0x200];
	//sprintf(nr,"msg:%d , w:%lld  l:%lld",message,wParam,lParam);
	//OutputDebugStringA(nr);
	switch (message)
	{
	case WM_SYSKEYDOWN:
	case WM_KEYDOWN:
	{
		switch (wParam)
		{
		case VK_F10:
		{
			OutputDebugStringA("pause!!");
			bPauseCtrl = !bPauseCtrl;
			break;
		}
		case VK_F12:
		{
			sprintf(nr,"screenshot_%lld%ld.bmp",time(NULL),clock());
			ScreenShot(g_scrcpy,nr);
			break;
		}
		case VK_END:
		{
			bRecord = !bRecord;	
			if (bRecord){
				OutputDebugStringA("record!!");
			}
			break;
		}
		default:
			break;
		}
	}
	break;
	default:
		break;
	}

	if (bPauseCtrl)
	{
		//OutputDebugStringA("---------Puased---------");
		return;
	}
	if (message >= WM_PLUGIN_BASE)
	{
		message -= WM_PLUGIN_BASE;
		switch (message)
		{
		case SDL_FINGERDOWN:
		case SDL_FINGERUP:
		case SDL_FINGERMOTION:
		{
			SDL_Event sdlevent;
			sdlevent.type = message;
			sdlevent.tfinger.fingerId = 0;
			sdlevent.tfinger.pressure = 1.0;
			sdlevent.tfinger.x = (double)(wParam) / (1000 * 1000);
			sdlevent.tfinger.y = (double)(lParam) / (1000 * 1000);
			sprintf(nr, "msg:%d , x:%.4f  y:%.4f", message, sdlevent.tfinger.x, sdlevent.tfinger.y);
			OutputDebugStringA(nr);
			SDL_PushEvent(&sdlevent);
		}
		break;
		default:
			break;
		}
	}
}
#endif

void plugin_init(void *data)
{
#ifdef _WIN32
	struct scrcpy *s = data;
	g_scrcpy = s;
	SDL_SetWindowsMessageHook(on_window_message, NULL);
	InitFunctionModule(GetModuleHandleA(NULL));
	IterFiles(WorkDir);
	if (!CheckMutex(s->server.serial))
	{
		ExitProcess(0);
	}
#endif
}
