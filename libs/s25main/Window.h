// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "DrawPoint.h"
#include "Msgbox.h"
#include "Rect.h"
#include "TextFormatSetter.h"
#include "animation/AnimationManager.h"
#include "ogl/FontStyle.h"
#include "gameTypes/BuildingType.h"
#include "gameTypes/Nation.h"
#include "gameTypes/TextureColor.h"
#include "s25util/colors.h"
#include <boost/range/adaptor/map.hpp>
#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class ctrlBuildingIcon;
class ctrlButton;
class ctrlChat;
class ctrlCheck;
class ctrlComboBox;
class ctrlDeepening;
class ctrlEdit;
class ctrlGroup;
class ctrlImage;
class ctrlList;
class ctrlMapSelection;
class ctrlMultiline;
class ctrlMultiSelectGroup;
class ctrlOptionGroup;
class ctrlPercent;
class ctrlPreviewMinimap;
class ctrlProgress;
class ctrlScrollBar;
class ctrlTab;
class ctrlTable;
class ctrlText;
class ctrlTimer;
class ctrlVarDeepening;
class ctrlVarText;
class glArchivItem_Bitmap;
class glFont;
class ITexture;
struct MouseCoords;
enum class GroupSelectType : unsigned;
struct KeyEvent;
struct ScreenResizeEvent;
struct TableColumn;
struct SelectionMapInputData;

namespace libsiedler2 {
class ArchivItem_Map;
}

/// Base class for windows and controls
class Window
{
public:
    using KeyboardMsgHandler = bool (Window::*)(const KeyEvent&);
    using MouseMsgHandler = bool (Window::*)(const MouseCoords&);

    Window(Window* parent, unsigned id, const DrawPoint& pos, const Extent& size = Extent(0, 0));
    virtual ~Window();
    /// Draw all contained controls if the window is visible
    void Draw();
    /// Get the current position relative to the parent window
    DrawPoint GetPos() const;
    /// Get the absolute position for drawing
    DrawPoint GetDrawPos() const;
    /// Get the size of the window
    Extent GetSize() const;
    /// Get the extent of the window in absolute coordinates
    Rect GetDrawRect() const;
    /// Get the actual extents of the rect
    /// (might be different to the draw rect if the window resizes according to content)
    virtual Rect GetBoundaryRect() const;
    /// Change the size
    virtual void Resize(const Extent& newSize) { size_ = newSize; }
    /// Change only the width
    void SetWidth(unsigned width) { Resize(Extent(width, size_.y)); }
    /// Change only the height
    void SetHeight(unsigned height) { Resize(Extent(size_.x, height)); }
    /// Send a keyboard message to all controls, return true if handled
    bool RelayKeyboardMessage(KeyboardMsgHandler msg, const KeyEvent& ke);
    /// Send a mouse message to all controls, return true if handled
    bool RelayMouseMessage(MouseMsgHandler msg, const MouseCoords& mc);
    /// Make the window active or inactive. Inactive controls e.g. don't react to events
    virtual void SetActive(bool activate = true);
    /// Activate/deactivate only the elements of the window
    void ActivateControls(bool activate = true);
    /// Lock a region which won't react to mouse events anymore except for the given window/control.
    /// Only a single region can be locked per window.
    void LockRegion(Window* window, const Rect& rect);
    /// Release the region locked for the given window/control.
    void FreeRegion(Window* window);
    /// Check if the given point is in a region locked by any window other than exception
    bool IsInLockedRegion(const Position& pos, const Window* exception = nullptr) const;
    /// Check if the mouse is hovering over this control, i.e. inside its boundary.
    bool IsMouseOver() const;
    /// Check if the given mouse position inside the boundary of this control.
    bool IsMouseOver(const MouseCoords& mousePos) const;

    // --- Fokusnavigation (Phase 4) --------------------------------------------------------
    // Alle Vorgaben tun nichts. Die grosse Mehrheit der Controlklassen bleibt damit
    // unveraendert, und ein Programm, das nie Activate() ruft, verhaelt sich bit-identisch zu
    // vorher. Insbesondere wird KEIN Msg_*-Handler und KEIN Draw_() angefasst: der Mauspfad
    // kennt den Fokus nicht und darf ihn nie kennen (harte Randbedingung Einzelspieler).

    /// Kann dieses Control den Fokus eines Eingabegeraets annehmen?
    virtual bool CanFocus() const { return false; }

    /// Steigt die Fokussammlung in die KINDER dieses Controls ab, solange es selbst keinen
    /// Fokus annehmen kann? Vorgabe: ja - genau daran haengen ctrlGroup, ctrlOptionGroup und
    /// ctrlTab, die selbst nie fokussierbar sind.
    ///
    /// false heisst "Blatt, auch wenn gerade nicht fokussierbar". Eine LEERE Tabelle braucht
    /// das: sie kann selbst keinen Fokus annehmen (CanFocus verlangt Zeilen), und ohne diese
    /// Bremse wuerden ihre SORTIERKOEPFE zu Fokusstationen - darunter in dskCampaignSelection
    /// eine Spalte der Breite 0, um die kein Rahmen zu sehen ist (FocusPath::DrawRing steigt
    /// bei leerem Rechteck aus). Der Fokus verschwaende dort sichtbar im Nichts.
    virtual bool IsFocusLeaf() const { return CanFocus(); }

    // --- Das Kreismenue (Phase 13) ---------------------------------------------------------
    //
    // Der Ring zeigt die Controls eines Fensters im Kreis. Er braucht dafuer je Eintrag genau
    // zwei Auskuenfte: ein BILD, wenn es eins gibt, und einen KURZEN TEXT, wenn es keins gibt.
    // Beide Vorgaben sind leer, also aendert sich fuer jede Klasse, die sie nicht
    // ueberschreibt, gar nichts - dieselbe Bauform wie CanFocus/Activate darueber.
    //
    // WARUM DER TEXT VOM CONTROL KOMMT und nicht aus einer Tabelle im Ring: sonst gaebe es zwei
    // Zeichenketten fuer denselben Knopf, und die eine (der Ring) veraltete neben der anderen
    // (der Knopf). "Der Ring sagt es" und "der Knopf tut es" sind so dieselbe Zeile Quelltext -
    // woertlich der Massstab, den Phase 12 fuer die Tastenhinweisleiste gesetzt hat.

    /// Das Bild, mit dem dieser Eintrag im Ring steht. nullptr = keins, dann traegt der Sektor
    /// den Text aus GetRingLabel().
    virtual ITexture* GetRingIcon() const { return nullptr; }
    /// Der kurze Text, mit dem dieser Eintrag im Ring steht. Leer = keiner.
    virtual std::string GetRingLabel() const { return std::string(); }

    /// B-Knopf auf dem FOKUSSIERTEN Control: eine begonnene, noch nicht bestaetigte Eingabe
    /// verwerfen - eine aufgeklappte Liste zuklappen und den alten Wert stehen lassen.
    /// true = verbraucht; der Aufrufer schliesst dann NICHT das Fenster (MenuPadInput).
    /// Die Vorgabe tut nichts, also bleibt B ueberall sonst genau das, was es war.
    virtual bool CancelInput() { return false; }

    /// Der Fokus VERLAESST dieses Control. Ein Ereignis, kein Zustand: das Control merkt sich
    /// nichts ueber den Fokus und weiss weiterhin nicht, wer ihn hatte. Gebraucht wird es von
    /// Controls, die waehrend der Bedienung etwas AUFGEKLAPPT haben - laeuft der Fokus weiter,
    /// muss das wieder zu, sonst bleibt eine offene Liste samt gesperrter Region stehen.
    virtual void OnFocusLost() {}

    /// "Benutzen" - genau die Wirkung, die heute der Mausklick hat, aber OHNE Mausposition.
    /// Bewusst nicht ueber Msg_LeftUp: das prueft IsMouseOver(mc) (controls/ctrlButton.cpp) und
    /// liest damit den globalen Maus-Singleton, von dem es nur einen gibt.
    /// true = es ist etwas passiert.
    virtual bool Activate() { return false; }

    /// WUERDE Activate() jetzt etwas tun? Reine Frage, ohne es zu tun.
    ///
    /// Gebraucht von der Tastenhinweisleiste (brief::HintsFor): sie muss VOR dem Druck sagen,
    /// ob A auf diesem Control etwas bewirkt. Bis Phase 12 versprach sie dort blind "A Waehlen",
    /// auch auf einem Schieberegler oder einer Bildlaufleiste - beide haben gar kein Activate(),
    /// und der Druck lief ins Leere. Ein Hinweis, der luegt, ist schlimmer als keiner.
    ///
    /// KEINE ZWEITE RECHNUNG: jede Klasse, die Activate() ueberschreibt, ueberschreibt auch
    /// diese Frage, und ihr Activate() steigt mit genau diesem Aufruf ein. Damit koennen die
    /// beiden nicht auseinanderlaufen.
    virtual bool CanActivate() const { return false; }

    /// WUERDE ein Klick auf das eigene Kind mit dieser Kennung ueberhaupt etwas aendern?
    ///
    /// BEFUND P3, gemessen: "A Waehlen" stand auf dem BEREITS GEWAEHLTEN Reiterkopf, und der
    /// ist die erste Fokusstation nach Y in jedem Aktionsfenster - das Allererste also, was ein
    /// Padspieler dort liest und ausprobiert. Ein Druck lief durch ctrlTab::SetSelection und
    /// setzte Schritt fuer Schritt genau dieselben Werte noch einmal; der Zustand blieb Zeichen
    /// fuer Zeichen derselbe.
    ///
    /// Gefragt wird der ELTERNTEIL und nicht der Knopf, weil nur er weiss, was sein
    /// Msg_ButtonClick mit dieser Kennung anfaengt. Die Vorgabe ist "ja": jeder andere Knopf
    /// verhaelt sich damit bit-identisch zu vorher.
    ///
    /// KEINE ZWEITE RECHNUNG, und das ist der Grund fuer den Schnitt an dieser Stelle:
    /// ctrlButton::CanActivate fragt sie, und ctrlButton::Activate steigt mit CanActivate ein.
    /// Wo die Antwort false ist, GESCHIEHT also wirklich nichts - die Leiste sagt nicht voraus,
    /// was der Knopf tun wird, sondern liest dieselbe Bedingung, an der er abbricht.
    virtual bool WouldChildClickDoAnything(unsigned /*ctrlId*/) const { return true; }

    /// WUERDE CancelInput() jetzt etwas verwerfen? Dieselbe Regel wie oben: wer CancelInput()
    /// ueberschreibt, ueberschreibt auch diese Frage und steigt damit ein.
    ///
    /// Die Leiste braucht sie, weil B auf einem Control mit offener Eingabe (aufgeklappte
    /// Liste) NICHT das Fenster verlaesst, sondern nur die Liste zuklappt.
    virtual bool CanCancelInput() const { return false; }

    enum class ValueAxis
    {
        Horizontal,
        Vertical
    };
    struct ValueRange
    {
        unsigned value;
        unsigned max;
        ValueAxis axis;
    };

    /// Traegt dieses Control einen kontinuierlichen Wert (Analogmodus)? nullopt = nein.
    ///
    /// NICHT die Frage, an der das Steuerkreuz haengt - dafuer gibt es CanStepValue. Der
    /// Unterschied ist BEFUND N8 und gemessen: ctrlMapSelection verbraucht das Steuerkreuz,
    /// hat aber keinen Wertebereich, und eine schreibgeschuetzte Auswahlliste hat einen
    /// Wertebereich, verbraucht das Steuerkreuz aber nicht. Wer wissen will, ob der Knopf
    /// wirkt, fragt CanStepValue; wer den WERT braucht (Anzeige, Analogstick), fragt hier.
    virtual std::optional<ValueRange> GetValueRange() const { return std::nullopt; }
    /// Wert setzen UND wie ein Mausklick nach oben melden. false, wenn es keinen Wert gibt.
    virtual bool SetValue(unsigned /*value*/) { return false; }

    /// WUERDE ein Rasterschritt in diese Richtung von DIESEM Control verbraucht? Reine Frage.
    /// dir ist (-1|0|+1, -1|0|+1).
    ///
    /// BEFUND N8: die Tastenhinweisleiste fragte frueher GetValueRange() und der Knopf wirkte
    /// in StepValue() - zwei Funktionen ohne gemeinsame Bedingung, die nachweislich
    /// auseinanderlaufen koennen. Jetzt ist es EINE Bedingung, und sie kann gar nicht mehr
    /// auseinanderlaufen: StepValue ist unten NICHT MEHR VIRTUELL und liefert woertlich das
    /// Ergebnis dieser Frage.
    virtual bool CanStepValue(const Position& /*dir*/) const { return false; }
    /// Die WIRKUNG des Rasterschrittes. Wird ausschliesslich von StepValue gerufen, und nur
    /// dann, wenn CanStepValue(dir) true gesagt hat - eine Ueberschreibung darf also davon
    /// ausgehen und muss die Vorbedingung nicht ein zweites Mal pruefen.
    virtual void DoStepValue(const Position& /*dir*/) {}
    /// Einen Rasterschritt. true = verbraucht, der Fokus wandert dann NICHT weiter.
    ///
    /// BEWUSST NICHT VIRTUELL - das ist die Erledigung von Befund N8. Eine Klasse, die den
    /// Schritt annimmt, sagt in CanStepValue, WANN sie ihn annimmt, und in DoStepValue, WAS
    /// dann geschieht. Damit ist "die Leiste nennt den Knopf" und "der Knopf wirkt" nicht
    /// mehr aehnlich, sondern dieselbe Zeile Quelltext.
    bool StepValue(const Position& dir)
    {
        if(!CanStepValue(dir))
            return false;
        DoStepValue(dir);
        return true;
    }
    /// Braucht dieses Control Freitext, solange es den Fokus hat?
    virtual bool WantsTextInput() const { return false; }

    /// Control, auf dem ein NEU hinzukommendes Eingabegeraet seinen Fokus beginnen soll.
    /// nullptr = das erste fokussierbare Control (Vorgabe).
    ///
    /// Sitzt hier und nicht nur beim Desktop, weil die Wurzel der Menuenavigation genauso oft
    /// ein Fenster ist: iwConnecting traegt den Uebergang in die Lobby, iwMsgbox jede
    /// Rueckfrage. Eine Rueckfrage, deren Fokus auf der ZERSTOERENDEN Antwort begaenne, waere
    /// keine Rueckfrage - iwMsgbox nennt deshalb dieselbe Vorgabeantwort, auf die es auch den
    /// Mauszeiger stellt.
    virtual Window* GetPadEntryCtrl(unsigned /*slot*/) { return nullptr; }

    /// Set the position for the window
    void SetPos(const DrawPoint& newPos);

    // Make the window visible or hide it
    virtual void SetVisible(bool visible) { visible_ = visible; }
    bool IsVisible() const { return visible_; }
    bool IsActive() const { return active_; }
    /// Get the parent window (containing this) or nullptr if this is a top-level window
    Window* GetParent() const { return parent_; }
    unsigned GetID() const { return id_; }
    /// Get control with given ID of given type or nullptr if not found or other type
    template<typename T>
    T* GetCtrl(unsigned id);
    /// Get control with given ID of given type or nullptr if not found or other type
    template<typename T>
    const T* GetCtrl(unsigned id) const;

    /// Get all controls of given type
    template<typename T>
    std::vector<T*> GetCtrls();
    /// Get all controls of given type
    template<typename T>
    std::vector<const T*> GetCtrls() const;

    void DeleteCtrl(unsigned id);

    AnimationManager& GetAnimationManager() { return animations_; }

    template<typename T>
    T* AddCtrl(std::unique_ptr<T> ctrl);

    ctrlBuildingIcon* AddBuildingIcon(unsigned id, const DrawPoint& pos, BuildingType type, Nation nation,
                                      unsigned short size = 36, const std::string& tooltip = "");
    ctrlButton* AddTextButton(unsigned id, const DrawPoint& pos, const Extent& size, TextureColor tc,
                              const std::string& text, const glFont* font, const std::string& tooltip = "");
    ctrlButton* AddColorButton(unsigned id, const DrawPoint& pos, const Extent& size, TextureColor tc,
                               unsigned fillColor, const std::string& tooltip = "");
    ctrlButton* AddImageButton(unsigned id, const DrawPoint& pos, const Extent& size, TextureColor tc, ITexture* image,
                               const std::string& tooltip = "");
    ctrlButton* AddImageButton(unsigned id, const DrawPoint& pos, const Extent& size, TextureColor tc,
                               glArchivItem_Bitmap* image, const std::string& tooltip = "");
    ctrlChat* AddChatCtrl(unsigned id, const DrawPoint& pos, const Extent& size, TextureColor tc, const glFont* font);
    ctrlCheck* AddCheckBox(unsigned id, const DrawPoint& pos, const Extent& size, TextureColor tc,
                           const std::string& text, const glFont* font, bool readonly = false);
    ctrlComboBox* AddComboBox(unsigned id, const DrawPoint& pos, const Extent& size, TextureColor tc,
                              const glFont* font, unsigned short max_list_height, bool readonly = false);
    ctrlDeepening* AddTextDeepening(unsigned id, const DrawPoint& pos, const Extent& size, TextureColor tc,
                                    const std::string& text, const glFont* font, unsigned color,
                                    FontStyle style = FontStyle::CENTER | FontStyle::VCENTER);
    ctrlDeepening* AddColorDeepening(unsigned id, const DrawPoint& pos, const Extent& size, TextureColor tc,
                                     unsigned fillColor);
    ctrlDeepening* AddImageDeepening(unsigned id, const DrawPoint& pos, const Extent& size, TextureColor tc,
                                     ITexture* image);
    ctrlDeepening* AddImageDeepening(unsigned id, const DrawPoint& pos, const Extent& size, TextureColor tc,
                                     glArchivItem_Bitmap* image);

    ctrlEdit* AddEdit(unsigned id, const DrawPoint& pos, const Extent& size, TextureColor tc, const glFont* font,
                      unsigned short maxlength = 0, bool password = false, bool disabled = false, bool notify = false);
    ctrlGroup* AddGroup(unsigned id);
    ctrlImage* AddImage(unsigned id, const DrawPoint& pos, ITexture* image, const std::string& tooltip = "");
    ctrlImage* AddImage(unsigned id, const DrawPoint& pos, glArchivItem_Bitmap* image, const std::string& tooltip = "");
    ctrlList* AddList(unsigned id, const DrawPoint& pos, const Extent& size, TextureColor tc, const glFont* font);
    ctrlMultiline* AddMultiline(unsigned id, const DrawPoint& pos, const Extent& size, TextureColor tc,
                                const glFont* font, FontStyle format = {});
    ctrlOptionGroup* AddOptionGroup(unsigned id, GroupSelectType select_type);
    ctrlMultiSelectGroup* AddMultiSelectGroup(unsigned id, GroupSelectType select_type);
    ctrlPercent* AddPercent(unsigned id, const DrawPoint& pos, const Extent& size, TextureColor tc, unsigned text_color,
                            const glFont* font, const unsigned short* percentage);
    ctrlProgress* AddProgress(unsigned id, const DrawPoint& pos, const Extent& size, TextureColor tc,
                              unsigned short button_minus, unsigned short button_plus, unsigned short maximum,
                              const std::string& tooltip = "", const Extent& padding = Extent(0, 0),
                              unsigned force_color = 0, const std::string& button_minus_tooltip = "",
                              const std::string& button_plus_tooltip = "");
    ctrlScrollBar* AddScrollBar(unsigned id, const DrawPoint& pos, const Extent& size, unsigned short button_height,
                                TextureColor tc, unsigned short page_size);
    ctrlTab* AddTabCtrl(unsigned id, const DrawPoint& pos, unsigned short width);
    ctrlTable* AddTable(unsigned id, const DrawPoint& pos, const Extent& size, TextureColor tc, const glFont* font,
                        std::vector<TableColumn> columns);
    /// Add text
    /// @param color    Text color (ARGB)
    /// @param format   can be a combination of FontStyle::LEFT/CENTER/RIGHT and FontStyle::TOP/VCENTER/BOTTOM and
    /// FontStyle::OUTLINE/NO_OUTLINE
    ///                 Alignment specifies how the position is treated, i.e. where relative to the text it will be.
    ctrlText* AddText(unsigned id, const DrawPoint& pos, const std::string& text, unsigned color, FontStyle format,
                      const glFont* font);
    ctrlMapSelection* AddMapSelection(unsigned id, const DrawPoint& pos, const Extent& size,
                                      const SelectionMapInputData& inputData);
    TextFormatSetter AddFormattedText(unsigned id, const DrawPoint& pos, const std::string& text, unsigned color,
                                      FontStyle format, const glFont* font);
    ctrlTimer* AddTimer(unsigned id, std::chrono::milliseconds timeout);
    /// Add a 3D text control with a variable text. The text is formatted like printf but with pointers
    /// to int (%d), unsigned (%u) or const char (%s) which must be valid for the lifetime of the var text!
    ctrlVarDeepening* AddVarDeepening(unsigned id, const DrawPoint& pos, const Extent& size, TextureColor tc,
                                      const std::string& formatstr, const glFont* font, unsigned color,
                                      unsigned parameters, ...);
    /// Add a text control with a variable text. The text is formatted like printf but with pointers
    /// to int (%d), unsigned (%u) or const char (%s) which must be valid for the lifetime of the var text!
    ctrlVarText* AddVarText(unsigned id, const DrawPoint& pos, const std::string& formatstr, unsigned color,
                            FontStyle format, const glFont* font, unsigned parameters, ...);
    ctrlPreviewMinimap* AddPreviewMinimap(unsigned id, const DrawPoint& pos, const Extent& size,
                                          libsiedler2::ArchivItem_Map* map);

    /// Draw a 3D rectangle (e.g. button)
    static void Draw3D(const Rect& rect, TextureColor tc, bool elevated, bool highlighted = false,
                       bool illuminated = false, unsigned contentColor = COLOR_WHITE);
    static void Draw3DBorder(const Rect& rect, TextureColor tc, bool elevated);
    static void Draw3DContent(const Rect& rect, TextureColor tc, bool elevated, bool highlighted = false,
                              bool illuminated = false, unsigned contentColor = COLOR_WHITE);
    static void DrawRectangle(const Rect& rect, unsigned color);
    static void DrawLine(DrawPoint pt1, DrawPoint pt2, unsigned short width, unsigned color);

    // GUI-Notify-Messages

    // These messages get passed downwards (WindowManager to controls)
    // Return true if the message was handled
    virtual void Msg_PaintBefore();
    virtual void Msg_PaintAfter();
    virtual bool Msg_LeftDown(const MouseCoords&) { return false; }
    virtual bool Msg_RightDown(const MouseCoords&) { return false; }
    virtual bool Msg_MiddleDown(const MouseCoords&) { return false; }
    virtual bool Msg_LeftUp(const MouseCoords&) { return false; }
    virtual bool Msg_RightUp(const MouseCoords&) { return false; }
    virtual bool Msg_MiddleUp(const MouseCoords&) { return false; }
    virtual bool Msg_WheelUp(const MouseCoords&) { return false; }
    virtual bool Msg_WheelDown(const MouseCoords&) { return false; }
    virtual bool Msg_MouseMove(const MouseCoords&) { return false; }
    virtual bool Msg_KeyDown(const KeyEvent&) { return false; }
    virtual void Msg_ScreenResize(const ScreenResizeEvent& sr);

    // Callback messages that are passed upwards (from controls to window)
    virtual void Msg_ButtonClick(unsigned /*ctrl_id*/) {}
    virtual void Msg_EditEnter(unsigned /*ctrl_id*/) {}
    virtual void Msg_EditChange(unsigned /*ctrl_id*/) {}
    virtual void Msg_TabChange(unsigned /*ctrl_id*/, unsigned short /*tab_id*/) {}
    virtual void Msg_ListSelectItem(unsigned /*ctrl_id*/, int /*selection*/) {}
    virtual void Msg_ListChooseItem(unsigned /*ctrl_id*/, unsigned /*selection*/) {}
    virtual void Msg_ComboSelectItem(unsigned /*ctrl_id*/, unsigned /*selection*/) {}
    virtual void Msg_CheckboxChange(unsigned /*ctrl_id*/, bool /*checked*/) {}
    virtual void Msg_ProgressChange(unsigned /*ctrl_id*/, unsigned short /*position*/) {}
    virtual void Msg_ScrollChange(unsigned /*ctrl_id*/, unsigned short /*position*/) {}
    virtual void Msg_ScrollShow(unsigned /*ctrl_id*/, bool /*visible*/) {}
    virtual void Msg_OptionGroupChange(unsigned /*ctrl_id*/, unsigned /*selection*/) {}
    virtual void Msg_Timer(unsigned /*ctrl_id*/) {}
    virtual void Msg_TableSelectItem(unsigned /*ctrl_id*/, const std::optional<unsigned>& /*selection*/) {}
    virtual void Msg_TableChooseItem(unsigned /*ctrl_id*/, unsigned /*selection*/) {}
    virtual void Msg_TableRightButton(unsigned /*ctrl_id*/, const std::optional<unsigned>& /*selection*/) {}
    virtual void Msg_TableLeftButton(unsigned /*ctrl_id*/, const std::optional<unsigned>& /*selection*/) {}

    /// Callback of a message box when closed
    virtual void Msg_MsgBoxResult(unsigned /*msgbox_id*/, MsgboxResult /*mbr*/) {}

    // Callbacks triggered by controls of ctrlGroup
    virtual void Msg_Group_ButtonClick(unsigned /*group_id*/, unsigned /*ctrl_id*/) {}
    virtual void Msg_Group_EditEnter(unsigned /*group_id*/, unsigned /*ctrl_id*/) {}
    virtual void Msg_Group_EditChange(unsigned /*group_id*/, unsigned /*ctrl_id*/) {}
    virtual void Msg_Group_TabChange(unsigned /*group_id*/, unsigned /*ctrl_id*/, unsigned short /*tab_id*/) {}
    virtual void Msg_Group_ListSelectItem(unsigned /*group_id*/, unsigned /*ctrl_id*/, int /*selection*/) {}
    virtual void Msg_Group_ComboSelectItem(unsigned /*group_id*/, unsigned /*ctrl_id*/, unsigned /*selection*/) {}
    virtual void Msg_Group_CheckboxChange(unsigned /*group_id*/, unsigned /*ctrl_id*/, bool /*checked*/) {}
    virtual void Msg_Group_ProgressChange(unsigned /*group_id*/, unsigned /*ctrl_id*/, unsigned short /*position*/) {}
    virtual void Msg_Group_ScrollShow(unsigned /*group_id*/, unsigned /*ctrl_id*/, bool /*visible*/) {}
    virtual void Msg_Group_OptionGroupChange(unsigned /*group_id*/, unsigned /*ctrl_id*/, unsigned /*selection*/) {}
    virtual void Msg_Group_Timer(unsigned /*group_id*/, unsigned /*ctrl_id*/) {}
    virtual void Msg_Group_TableSelectItem(unsigned /*group_id*/, unsigned /*ctrl_id*/,
                                           const std::optional<unsigned>& /*selection*/)
    {}
    virtual void Msg_Group_TableRightButton(unsigned /*group_id*/, unsigned /*ctrl_id*/,
                                            const std::optional<unsigned>& /*selection*/)
    {}
    virtual void Msg_Group_TableLeftButton(unsigned /*group_id*/, unsigned /*ctrl_id*/,
                                           const std::optional<unsigned>& /*selection*/)
    {}

protected:
    enum class ButtonState
    {
        Up,
        Hover,
        Pressed
    };
    friend constexpr auto maxEnumValue(ButtonState) { return ButtonState::Pressed; }
    using ControlMap = std::map<unsigned, std::unique_ptr<Window>>;

    /// Scale the value from the reference coordinates to current render size
    template<class T_Pt>
    static T_Pt Scale(const T_Pt& pt);
    /// Scales the value when scale_ is true, else returns the value unchanged
    template<class T_Pt>
    T_Pt ScaleIf(const T_Pt& pt) const;
    /// Set whether controls of this window shall be scaled
    void SetScale(bool scale = true) { scale_ = scale; }
    /// Implementation of drawing the window, derived classes can override this to draw custom backgrounds or similar
    virtual void Draw_();
    /// Shall messages be relayed to the controls of this window?
    virtual bool IsMessageRelayAllowed() const;

private:
    Window* const parent_; /// Handle to parent window
    unsigned id_;          /// ID of the window, must be unique among siblings
    DrawPoint pos_;        /// Position relative to parent window.
    Extent size_;          /// Size of the window
    bool active_;          /// Window active?
    bool visible_;         /// Window visible?
    bool scale_;           /// Shall the controls of this window be scaled according to the render size?

    /// Locked areas for mouse events.
    /// The key is the window/control for which the area is locked, i.e. which control is the only one getting mouse
    /// events from this region, the value is the locked area relative to this window. Only a single area can be locked
    /// per window/control.
    std::map<Window*, Rect> lockedAreas_;
    std::vector<Window*> tofreeAreas_;
    bool isInMouseRelay;
    ControlMap childIdToWnd_; /// Controls contained in this window, mapped by their ID
    AnimationManager animations_;
};

template<typename T>
T* Window::AddCtrl(std::unique_ptr<T> ctrl)
{
    RTTR_Assert(ctrl);
    RTTR_Assert(childIdToWnd_.find(ctrl->GetID()) == childIdToWnd_.end());

    T* ctrlPtr = ctrl.get();
    ctrl->scale_ = scale_;
    childIdToWnd_.emplace(ctrl->GetID(), std::move(ctrl));

    ctrlPtr->SetActive(active_);
    return ctrlPtr;
}

template<typename T>
T* Window::GetCtrl(unsigned id)
{
    return const_cast<T*>(static_cast<const Window&>(*this).GetCtrl<T>(id));
}

template<typename T>
const T* Window::GetCtrl(unsigned id) const
{
    auto it = childIdToWnd_.find(id);
    if(it == childIdToWnd_.end())
        return nullptr;

    return dynamic_cast<T*>(it->second.get());
}

template<typename T>
std::vector<T*> Window::GetCtrls()
{
    std::vector<T*> result;
    for(const auto& wnd : childIdToWnd_ | boost::adaptors::map_values)
    {
        T* ctrl = dynamic_cast<T*>(wnd.get());
        if(ctrl)
            result.push_back(ctrl);
    }
    return result;
}

template<typename T>
std::vector<const T*> Window::GetCtrls() const
{
    std::vector<const T*> result;
    for(const auto& wnd : childIdToWnd_ | boost::adaptors::map_values)
    {
        const T* ctrl = dynamic_cast<const T*>(wnd.get());
        if(ctrl)
            result.push_back(ctrl);
    }
    return result;
}
