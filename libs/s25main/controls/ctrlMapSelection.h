// Copyright (C) 2024 - 2025  Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "DrawPoint.h"
#include "Window.h"
#include <gameData/SelectionMapInputData.h>
#include <optional>

class glArchivItem_Bitmap;
namespace libsiedler2 {
class baseArchivItem_Bitmap;
} // namespace libsiedler2

struct MissionStatus
{
    bool playable = false;
    bool conquered = false;
};

class ctrlMapSelection : public Window
{
public:
    ctrlMapSelection(Window* parent, unsigned id, const DrawPoint& pos, const Extent& size,
                     const SelectionMapInputData& inputData);
    ~ctrlMapSelection() override;

    void setMissionsStatus(const std::vector<MissionStatus>& status);
    void setSelection(size_t select);
    std::optional<unsigned> getSelection() const;
    void setPreview(bool previewOnly);

    bool Msg_LeftUp(const MouseCoords& mc) override;

    /// Fokusnavigation.
    ///
    /// CanFocus haengt AUSDRUECKLICH NICHT an einer bestehenden Auswahl. Genau das war die
    /// Sackgasse der Weltkampagne: eine Auswahl entstand nur aus Msg_LeftUp (Pixelfarbe unter
    /// dem Mauszeiger), also konnte ein Pad die Karte nie fokussieren, und ohne Auswahl blieb
    /// auch "Start" deaktiviert und damit ebenfalls unfokussierbar. Der Bildschirm hatte fuer
    /// ein Pad genau eine Station: "Zurueck".
    ///
    /// Die VORSCHAU (dskCampaignSelection) bleibt aussen vor - dort ist die Karte ein Bild und
    /// kein Bedienelement, sie nimmt auch mit der Maus keine Auswahl an.
    bool CanFocus() const override
    {
        return IsVisible() && !preview && !inputData.missionSelectionInfos.empty();
    }
    bool Activate() override;
    /// Dieselbe Vorbedingung, die Activate() prueft - siehe ctrlButton::CanActivate.
    bool CanActivate() const override { return IsVisible() && GetParent() && getSelection(); }
    /// BEFUND N8, gemessen: dieses Control verbraucht das Steuerkreuz, hat aber gar keinen
    /// Wertebereich (GetValueRange liefert nullopt). Wer die Leiste an GetValueRange haengt,
    /// schweigt hier ueber einen Knopf, der wirkt. Deshalb ist CanStepValue die Frage - und
    /// sie rechnet dieselbe Suche wie der Schritt selbst.
    bool CanStepValue(const Position& dir) const override { return findStepTarget(dir) >= 0; }
    void DoStepValue(const Position& dir) override;

protected:
    void Draw_() override;

    void updateEnabledMask();

    float getScaleFactor();
    template<class Type>
    Type scale(const Type& scaleIt)
    {
        return Type(scaleIt * getScaleFactor());
    }
    DrawPoint invertScale(const DrawPoint& scaleIt);

    DrawPoint getBackgroundPosition();
    DrawPoint getMapPosition();

    void drawImageOnMap(glArchivItem_Bitmap* image, const Position& drawPos);

    struct MapImages
    {
        MapImages(const SelectionMapInputData& data);

        glArchivItem_Bitmap* background;
        glArchivItem_Bitmap* map;
        glArchivItem_Bitmap* missionMapMask;
        glArchivItem_Bitmap* marker;
        glArchivItem_Bitmap* conquered;
        glArchivItem_Bitmap* enabledMask;
        std::unique_ptr<libsiedler2::baseArchivItem_Bitmap> enabledMaskMemory;
    };

    /// Die Marke, auf die ein Rasterschritt in diese Richtung fuehrt, oder -1.
    ///
    /// EINE Rechnung, zwei Aufrufer (CanStepValue und DoStepValue) - genau die Bauform, mit
    /// der FocusPath::Move und FocusPath::CanMove sich ihr Ziel teilen. Ohne sie koennte die
    /// Frage "wirkt das Steuerkreuz hier" etwas anderes sagen als der Schritt tut.
    int findStepTarget(const Position& dir) const;

    const MapImages mapImages;
    SelectionMapInputData inputData;
    std::vector<MissionStatus> missionStatus;
    Position currentSelectionPos;
    bool preview;
};
