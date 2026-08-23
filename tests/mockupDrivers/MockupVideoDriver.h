// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "driver/VideoDriver.h"

class MockupVideoDriver : public VideoDriver
{
public:
    MockupVideoDriver(VideoDriverLoaderInterface* CallBack);
    ~MockupVideoDriver() override;
    const char* GetName() const override;
    bool Initialize() override;
    bool CreateScreen(const std::string& title, VideoMode newSize, DisplayMode displayMode) override;
    bool ResizeScreen(VideoMode newSize, DisplayMode displayMode) override;
    void DestroyScreen() override {}
    bool SwapBuffers() override { return true; }
    bool MessageLoop() override;
    unsigned long GetTickCount() const override;
    OpenGL_Loader_Proc GetLoaderFunction() const override;
    std::vector<VideoMode> ListVideoModes() const override;
    void SetMousePos(Position pos) override;
    KeyEvent GetModKeyState() const override;
    void* GetMapPointer() const override;
    bool IsOpenGL() const override { return false; }
    void ShowErrorMessage(const std::string& title, const std::string& message) override;
    using VideoDriver::FindClosestVideoMode;
    using VideoDriver::SetNewSize;
    bool IsTouchEvent() const override;

    /// Genau die Naht, an der der echte Treiber seine SDL-Ereignisse herausreicht
    /// (IVideoDriver::FetchPadEvents). Ein Test fuellt padEvents_ und der Produktivcode holt sie
    /// mit demselben Aufruf ab wie vom SDL2-Treiber - ohne SDL, ohne DLL, ohne Hardware.
    void FetchPadEvents(std::vector<PadEvent>& out) override
    {
        out = padEvents_;
        padEvents_.clear();
    }
    /// Was der echte Treiber aus SDL geholt haette
    std::vector<PadEvent> padEvents_;

    KeyEvent modKeyState_;
    unsigned long tickCount_;
    unsigned numTfinger_;
    std::vector<VideoMode> video_modes_;
};
