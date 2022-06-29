/*
    vncserver.hpp - source code

    =================================
      vnc class for PIwebVNC in C++
    =================================

    Free to use, free to modify, free to redistribute.
    Created by : Jishan Ali Mondal

    This is a header-only library.
    created for only PIwebVNC
    * This code was created entirely for the most optimized performance for PIwebVNC *
    * May not be suitable for other projects *
    version 1.0.1
*/

#ifndef WEBVNC_HPP
#define WEBVNC_HPP

#include <iostream>
#include <unistd.h>
#include <string>
#include <lz4.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/XShm.h>
#include <sys/shm.h>
#include "display.hpp"
#include "input.hpp"

class VNCServer
{
    public:
        XDisplay xdisplay;
        XInputs *inputs;
        VNCServer();
        ~VNCServer();
        void start_service(Websocket &ws);
        void stop_service();
        void send_first_frame(int sd); // send the first frame to the client
    private:
        void threadSleep();
        Display * display;
        Damage damage;
        ScreenInfo screenInfo;
        char config[500] = {0}; // 500 byte not too much
        bool isRunning = true;
        bool sendFirstFrame = false;
        int clientSD = 0;
        int sleepDelay = 1000000 / FPS;
        int sleepLoop = (1000000 / FPS)/this->sleepDelay;
        int bufferSize = 1000000;
        char *buffer;
        XShmSegmentInfo shminfo;
        XImage *image;
};

VNCServer::VNCServer()
{
    int damage_event, damage_error;   
    this->display = this->xdisplay.getDisplay();
    this->screenInfo = this->xdisplay.getScreenInfo();
    strcpy(this->config,this->xdisplay.getDisplayConfig().c_str());

    XDamageQueryExtension(display, &damage_event, &damage_error);
    this->damage = XDamageCreate(display, this->screenInfo.root, XDamageReportNonEmpty);

    this->bufferSize = (this->screenInfo.height * this->xdisplay.getBitPerLine()) + (2 * this->screenInfo.width);
    this->buffer = (char *)malloc(this->bufferSize * sizeof(char));
    if (this->buffer == NULL)
    {
        printf("[ERROR] Memory not allocated for VNC buffer.\n");
        exit(0);
    }
}

VNCServer::~VNCServer()
{
    // free image
    XDestroyImage(this->image);
    XShmDetach(this->display, &shminfo);
    free(this->buffer);
    printf("[INFO] Memory released for VNC buffer.\n");
    XDamageDestroy(display, damage);
    this->xdisplay.close();
}

void VNCServer::stop_service()
{
    this->isRunning = false;
}
void VNCServer::send_first_frame(int sd)
{
    this->clientSD = sd;
    this->sendFirstFrame = true;
}
void VNCServer::start_service(Websocket &ws)
{
    image = XShmCreateImage(display, DefaultVisual(display, DefaultScreen(display)), xdisplay.depth, ZPixmap, NULL, &shminfo, this->screenInfo.width, this->screenInfo.height);
    shminfo.shmid = shmget(IPC_PRIVATE,
                           image->bytes_per_line * image->height,
                           IPC_CREAT | 0777);

    image->data = (char *)shmat(shminfo.shmid, 0, 0);
    shminfo.shmaddr = image->data;
    shminfo.readOnly = False;
    XShmAttach(display, &shminfo);
    const XserverRegion xregion = XFixesCreateRegion(this->display, NULL, 0);
    while(this->isRunning)
    {
        threadSleep();
        if (ws.clients > 0)
        {
            if (this->sendFirstFrame)
            {
                XShmGetImage(this->display, this->screenInfo.root, image, 0, 0, AllPlanes);
                int frameSize = ((this->screenInfo.height - 1) * image->bytes_per_line + (this->screenInfo.width - 1));
                int compressedSize = LZ4_compress_default(image->data, this->buffer, frameSize, this->bufferSize);
                std::string data = "UPD" + std::to_string(0) + " " + std::to_string(0) + " " 
                    + std::to_string(this->screenInfo.width) + " " + std::to_string(this->screenInfo.height) + " " 
                    + std::to_string(image->bytes_per_line) + " " + std::to_string(compressedSize) + " \n";
                char *info = (char *)data.c_str();
                int infoSize = strlen(info);
                ws.sendText(this->config, this->clientSD);
                ws.sendFrame(info, buffer, infoSize, compressedSize, this->clientSD);
                this->sendFirstFrame = false;
                usleep(100000);
            }
            else
            {
                int partCounts = 0;
                XDamageSubtract(this->display, this->damage, None, xregion);
                XRectangle *rect = XFixesFetchRegion(this->display, xregion, &partCounts);
                rect->x = 200;
                rect->y = 200;
                rect->width = 100;
                rect->height = 100;
                for (int i = 0; i < partCounts; i++)
                {
                    XShmGetImage(this->display, this->screenInfo.root, image, 0, 0, AllPlanes);
                    int cropx = ((rect[i].y *image->bytes_per_line) + rect[i].x);
                    int frameSize = (rect[i].height * image->bytes_per_line) - cropx;
                    int compressedSize = LZ4_compress_default(image->data + cropx, this->buffer, frameSize, this->bufferSize);
                    std::string data = "UPD" + std::to_string(rect[i].x) + " " + std::to_string(rect[i].y) + " " 
                        + std::to_string(rect[i].width) + " " + std::to_string(rect[i].height) + " " 
                        + std::to_string(image->bytes_per_line) + " " + std::to_string(compressedSize) + " \n";
                    char *info = (char *)data.c_str();
                    int infoSize = strlen(info);
                    ws.sendFrame(info, this->buffer, infoSize, compressedSize);
                }
                XFree(rect);
            }
        }
        else{
            while(ws.clients == 0){
                sleep(1);
            }
        }
    }
}

void VNCServer::threadSleep()
{
    // loop and sleep for 50ms
    int i = this->sleepLoop;
    while(i--)
    {
        this->inputs->dispatchEvents();
        usleep(this->sleepDelay);
    }
}

#endif