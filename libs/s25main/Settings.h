// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "DrawPoint.h"
#include "driver/VideoInterface.h"
#include "driver/VideoMode.h"
#include "s25util/ProxySettings.h"
#include "s25util/Singleton.h"
#include <array>
#include <cstdint>
#include <gameData/const_gui_ids.h>
#include <map>
#include <optional>
#include <string>

#undef interface

namespace validate {
std::optional<uint16_t> checkPort(const std::string& port);
bool checkPort(int port);
} // namespace validate

struct PersistentWindowSettings
{
    DrawPoint lastPos = DrawPoint::Invalid();
    DrawPoint restorePos = DrawPoint::Invalid();
    bool isOpen = false;
    bool isPinned = false;
    bool isMinimized = false;
};

enum class MapScrollMode
{
    ScrollSame,
    ScrollOpposite, // S2 Original
    GrabAndDrag
};

enum class SubmitDebugData : uint8_t
{
    AskAtStart = 0, // I.e. undecided
    Yes = 1,
    AlwaysAsk = 2
};
constexpr auto maxEnumValue(SubmitDebugData)
{
    return SubmitDebugData::AlwaysAsk;
}

/// Configuration class
class Settings : public Singleton<Settings, SingletonPolicies::WithLongevity>
{
public:
    static constexpr unsigned Longevity = 18;

    Settings();

    void Load();
    void Save();

protected:
    void LoadDefaults();
    void LoadIngameDefaults();

    void LoadIngame();
    void SaveIngame();

public:
    struct
    {
        SubmitDebugData submitDebugData;
        bool useUPNP, smartCursor, debugMode, showGFInfo;
    } global;

    struct
    {
        VideoMode fullscreenSize, windowedSize;
        signed short framerate; // <0 for unlimited, 0 for HW Vsync
        DisplayMode displayMode;
        bool vbo;
        bool sharedTextures;
        unsigned guiScale; ///< UI scaling in percent; 0 indicates automatic selection
        /// Fernsehmodus. Aus (Standard) heisst: die Darstellung ist BIT-IDENTISCH zu der vor
        /// dieser Aenderung - weder die empfohlene GUI-Skalierung noch der Startzoom noch die
        /// Fensterklemme aendern sich. An heisst: die Bedienoberflaeche wird gegen eine logische
        /// Leinwand von tv::UI_REFERENCE_HEIGHT Zeilen skaliert, die Karte startet auf einem
        /// dazu passenden Zoom, und Fenster halten den Safe-Area-Rand ein.
        bool tvMode;
        /// Safe-Area-Rand je Seite in Prozent (0..tv::SAFE_AREA_PERCENT_MAX). Wirkt nur bei
        /// eingeschaltetem Fernsehmodus.
        unsigned tvSafeAreaPercent;
    } video;

    struct
    {
        std::string language;
    } language;

    struct
    {
        std::string audio;
        std::string video;
    } driver;

    struct
    {
        bool musicEnabled;
        uint8_t musicVolume;
        bool effectsEnabled;
        uint8_t effectsVolume;
        bool birdsEnabled;
        std::string playlist; /// musicplayer playlist name
    } sound;

    struct
    {
        std::string name;
        unsigned portraitIndex;
        std::string password;
        bool savePassword;
    } lobby;

    struct
    {
        std::string lastIP; /// last entered ip or hostname
        uint16_t localPort;
        bool ipv6; /// listen/connect on ipv6 as default or not
    } server;

    ProxySettings proxy;

    struct
    {
        unsigned autosaveInterval;
        MapScrollMode mapScrollMode;
        bool enableWindowPinning;
        unsigned windowSnapDistance;
    } interface;

    struct
    {
        bool scaleStatistics;
        bool showNames;
        bool showProductivity;
        bool showBQ;
        bool minimapExtended;
    } ingame;

    struct
    {
        std::map<GUI_ID, PersistentWindowSettings> persistentSettings;
    } windows;

    struct
    {
        std::map<unsigned, unsigned> configuration;
    } addons;

    static const std::array<short, 13> SCREEN_REFRESH_RATES;

private:
    static const int VERSION;
    static const std::array<std::string, 10> SECTION_NAMES;
};

#define SETTINGS Settings::inst()
