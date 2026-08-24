// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "dskGameLobby.h"
#include "GameLobby.h"
#include "GameLobbyController.h"
#include "ILobbyClient.hpp"
#include "JoinPlayerInfo.h"
#include "Loader.h"
#include "RTTR_Assert.h"
#include "WindowManager.h"
#include "animation/BlinkButtonAnim.h"
#include "controls/ctrlBaseColor.h"
#include "controls/ctrlChat.h"
#include "controls/ctrlCheck.h"
#include "controls/ctrlComboBox.h"
#include "controls/ctrlEdit.h"
#include "controls/ctrlGroup.h"
#include "controls/ctrlImageButton.h"
#include "controls/ctrlOptionGroup.h"
#include "controls/ctrlPreviewMinimap.h"
#include "controls/ctrlText.h"
#include "controls/ctrlTextButton.h"
#include "controls/ctrlVarDeepening.h"
#include "desktops/dskDirectIP.h"
#include "desktops/dskGameLoader.h"
#include "desktops/dskLAN.h"
#include "desktops/dskLobby.h"
#include "desktops/dskSinglePlayer.h"
#include "helpers/containerUtils.h"
#include "input/MenuPadInput.h"
#include "input/PadRouter.h"
#include "helpers/format.hpp"
#include "ingameWindows/iwAddons.h"
#include "ingameWindows/iwMsgbox.h"
#include "lua/LuaInterfaceSettings.h"
#include "network/ClientError.h"
#include "network/GameClient.h"
#include "ogl/FontStyle.h"
#include "gameData/GameConsts.h"
#include "gameData/PortraitConsts.h"
#include "gameData/const_gui_ids.h"
#include "world/ViewportLayout.h"
#include "liblobby/LobbyPlayerInfo.h"
#include "libsiedler2/ArchivItem_Map.h"
#include "libsiedler2/ErrorCodes.h"
#include "libsiedler2/prototypen.h"
#include "s25util/Log.h"
#include "s25util/MyTime.h"
#include <array>
#include <memory>
#include <mygettext/mygettext.h>
#include <set>

namespace {
enum CtrlIds
{
    ID_btStartGame,
    ID_btReturn,
    ID_chkLockTeams,
    ID_chkSharedView,
    ID_chkRandomSpawn,
    ID_txtAddons,
    ID_btSettings,
    ID_txtColPastPlayer,
    ID_txtColSwap,
    ID_txtColName,
    ID_txtColRace,
    ID_txtColColor,
    ID_txtColTeam,
    ID_txtColReady,
    ID_txtColPing,
    ID_txtGameName,
    ID_txtExploration,
    ID_cbExploration,
    ID_txtGoods,
    ID_cbGoods,
    ID_txtGoals,
    ID_cbGoals,
    ID_txtSpeed,
    ID_cbSpeed,
    ID_txtNoPreview,
    ID_txtMapName,
    ID_miniMap,
    ID_btPlayerState,
    ID_btNation,
    ID_btPortrait,
    ID_btColor,
    ID_btTeam,
    ID_chkReady,
    ID_txtPing,
    ID_cbMove,
    ID_mbLuaLoadError,
    ID_mbLuaVersionError,
    ID_mbMapLoadError,
    ID_mbError,
    ID_mbStartErrror,
    ID_mbQuestionEconomy,
    ID_mbQuestionPeaceful,
    ID_chatGame,
    ID_chatLobby,
    ID_edtChatMsg,
    ID_optChatTab,
    ID_btChatGame,
    ID_btChatLobby,
    ID_grpPlayerStart,                           // up to and including ID_grpPlayerStart + MAX_PLAYERS - 1
    ID_btSwap = ID_grpPlayerStart + MAX_PLAYERS, // up to and including ID_btSwap + MAX_PLAYERS - 1
    /// Der Zuordnungsbildschirm: eine Karte je Sitzplatz vor dem Fernseher.
    ID_btSeat = ID_btSwap + MAX_PLAYERS, // up to and including ID_btSeat + MAX_VIEWPORTS - 1
    ID_txtSeats = ID_btSeat + MAX_VIEWPORTS,
    /// Bewusst hier hinten und nicht bei den anderen ID_mb*: die Aufzaehlung traegt mit
    /// ID_grpPlayerStart eine Basis, auf die sich Gruppen- und Sitzids beziehen. Ein Einschub
    /// weiter oben verschoebe sie alle.
    ID_mbQuestionLeave,
    /// Die Meldung aus PrepareSeatsForStart. Sie hat ABSICHTLICH keinen Fall in
    /// Msg_MsgBoxResult: der Zustand ist bereits repariert, es gibt nichts zu bestaetigen -
    /// anders als ID_mbError, das Stop() und GoBack() ausloest.
    ID_mbSeatsDropped,
};
template<typename T>
constexpr T nextEnumValue(T value)
{
    return T((rttr::enum_cast(value) + 1) % helpers::NumEnumValues_v<T>);
}

std::array NATION_ORDER = {
  Nation::Romans, Nation::Vikings, Nation::Japanese, Nation::Africans, Nation::Babylonians,
};
static_assert(NATION_ORDER.size() == helpers::NumEnumValues_v<Nation>);

Nation nextNation(const Nation value)
{
    // NOLINTNEXTLINE(readability-qualified-auto)
    auto it = helpers::find(NATION_ORDER, value);
    RTTR_Assert(it != NATION_ORDER.end());
    if(++it == NATION_ORDER.end())
        it = NATION_ORDER.begin();
    return *it;
}
} // namespace

dskGameLobby::dskGameLobby(ServerType serverType, std::shared_ptr<GameLobby> gameLobby, unsigned playerId,
                           std::unique_ptr<ILobbyClient> lobbyClient)
    : Desktop(LOADER.GetImageN("setup015", 0)), serverType(serverType), gameLobby_(std::move(gameLobby)),
      localPlayerId_(playerId), lobbyClient_(std::move(lobbyClient)), hasCountdown_(false), wasActivated(false),
      gameChat(nullptr), lobbyChat(nullptr), lobbyChatTabAnimId(0), localChatTabAnimId(0)
{
    // If no lobby don't do anything else
    if(!gameLobby_)
        return;

    const bool loadLua = !GAMECLIENT.GetLuaFilePath().empty();

    // The lobby controller for clients is only used by lua
    if(gameLobby_->isHost() || loadLua)
        lobbyController = std::make_unique<GameLobbyController>(gameLobby_, GAMECLIENT.GetMainPlayer());

    if(loadLua)
    {
        lua = std::make_unique<LuaInterfaceSettings>(*lobbyController, GAMECLIENT);
        if(!lua->loadScript(GAMECLIENT.GetLuaFilePath()))
        {
            WINDOWMANAGER.ShowAfterSwitch(std::make_unique<iwMsgbox>(
              _("Error"), _("Lua script was found but failed to load. Map might not work as expected!"), this,
              MsgboxButton::Ok, MsgboxIcon::ExclamationRed, ID_mbLuaLoadError));
            lua.reset();
        } else if(!lua->CheckScriptVersion())
        {
            WINDOWMANAGER.ShowAfterSwitch(std::make_unique<iwMsgbox>(
              _("Error"), _("Lua script uses a different version and cannot be used. Map might not work as expected!"),
              this, MsgboxButton::Ok, MsgboxIcon::ExclamationRed, ID_mbLuaVersionError));
            lua.reset();
        } else if(!lua->EventSettingsInit(serverType == ServerType::Local, gameLobby_->isSavegame()))
        {
            // This should have been detected for the host so others won't even see the script
            RTTR_Assert(gameLobby_->isHost());
            LOG.write(_("Lua was disabled by the script itself\n"));
            lua.reset();
        }
        if(!lua && gameLobby_->isHost())
            lobbyController->RemoveLuaScript();
    }

    const bool readonlySettings = !gameLobby_->isHost() || gameLobby_->isSavegame() || !IsChangeAllowed("general");
    allowAddonChange = gameLobby_->isHost() && !gameLobby_->isSavegame()
                       && (IsChangeAllowed("addonsAll") || IsChangeAllowed("addonsSome"));

    AddText(ID_txtGameName, DrawPoint(400, 5), GAMECLIENT.GetGameName(), COLOR_YELLOW, FontStyle::CENTER, LargeFont);

    AddText(ID_txtColName, DrawPoint(125, 40), _("Player Name"), COLOR_YELLOW, FontStyle::CENTER, NormalFont);
    AddText(ID_txtColRace, DrawPoint(262, 40), _("Race"), COLOR_YELLOW, FontStyle::CENTER, NormalFont);
    AddText(ID_txtColColor, DrawPoint(369, 40), _("Color"), COLOR_YELLOW, FontStyle::CENTER, NormalFont);
    AddText(ID_txtColTeam, DrawPoint(419, 40), _("Team"), COLOR_YELLOW, FontStyle::CENTER, NormalFont);

    if(!IsSinglePlayer())
    {
        AddText(ID_txtColReady, DrawPoint(479, 40), _("Ready?"), COLOR_YELLOW, FontStyle::CENTER, NormalFont);
        AddText(ID_txtColPing, DrawPoint(530, 40), _("Ping"), COLOR_YELLOW, FontStyle::CENTER, NormalFont);
    }
    if(gameLobby_->isHost() && !gameLobby_->isSavegame())
        AddText(ID_txtColSwap, DrawPoint(0, 40), _("Swap"), COLOR_YELLOW, FontStyle::LEFT, NormalFont);
    if(gameLobby_->isSavegame())
        AddText(ID_txtColPastPlayer, DrawPoint(645, 40), _("Past player"), COLOR_YELLOW, FontStyle::CENTER, NormalFont);

    if(!IsSinglePlayer())
    {
        // Enable lobby chat when we are logged in
        if(lobbyClient_ && lobbyClient_->IsLoggedIn())
        {
            ctrlOptionGroup* chatTab = AddOptionGroup(ID_optChatTab, GroupSelectType::Check);
            chatTab->AddTextButton(ID_btChatGame, DrawPoint(20, 320), Extent(178, 22), TextureColor::Green2,
                                   _("Game Chat"), NormalFont);
            chatTab->AddTextButton(ID_btChatLobby, DrawPoint(202, 320), Extent(178, 22), TextureColor::Green2,
                                   _("Lobby Chat"), NormalFont);
            gameChat =
              AddChatCtrl(ID_chatGame, DrawPoint(20, 345), Extent(360, 218 - 25), TextureColor::Grey, NormalFont);
            lobbyChat =
              AddChatCtrl(ID_chatLobby, DrawPoint(20, 345), Extent(360, 218 - 25), TextureColor::Grey, NormalFont);
            chatTab->SetSelection(ID_btChatGame, true);
        } else
        {
            gameChat = AddChatCtrl(ID_chatGame, DrawPoint(20, 320), Extent(360, 218), TextureColor::Grey, NormalFont);
        }
        AddEdit(ID_edtChatMsg, DrawPoint(20, 540), Extent(360, 22), TextureColor::Grey, NormalFont);
    }

    AddTextButton(ID_btStartGame, DrawPoint(600, 560), Extent(180, 22), TextureColor::Green2,
                  (gameLobby_->isHost() ? _("Start game") : _("Ready")), NormalFont);

    AddTextButton(ID_btReturn, DrawPoint(400, 560), Extent(180, 22), TextureColor::Red1, _("Return"), NormalFont);

    AddCheckBox(ID_chkLockTeams, DrawPoint(400, 460), Extent(180, 26), TextureColor::Grey, _("Lock teams:"), NormalFont,
                readonlySettings);
    AddCheckBox(ID_chkSharedView, DrawPoint(600, 460), Extent(180, 26), TextureColor::Grey, _("Shared team view"),
                NormalFont, readonlySettings);
    AddCheckBox(ID_chkRandomSpawn, DrawPoint(600, 430), Extent(180, 26), TextureColor::Grey,
                _("Random start locations"), NormalFont, readonlySettings);

    AddText(ID_txtAddons, DrawPoint(400, 499), _("Addons:"), COLOR_YELLOW, FontStyle{}, NormalFont);
    AddTextButton(ID_btSettings, DrawPoint(600, 495), Extent(180, 22), TextureColor::Green2,
                  allowAddonChange ? _("Change Settings...") : _("View Settings..."), NormalFont);

    ctrlComboBox* combo;

    // umgedrehte Reihenfolge, damit die Listen nicht dahinter sind

    AddText(ID_txtExploration, DrawPoint(400, 405), _("Exploration:"), COLOR_YELLOW, FontStyle{}, NormalFont);
    combo = AddComboBox(ID_cbExploration, DrawPoint(600, 400), Extent(180, 20), TextureColor::Grey, NormalFont, 100,
                        readonlySettings);
    combo->AddItem(_("Off (all visible)"));
    combo->AddItem(_("Classic (Settlers 2)"));
    combo->AddItem(_("Fog of War"));
    combo->AddItem(_("FoW - all explored"));

    AddText(ID_txtGoods, DrawPoint(400, 375), _("Goods at start:"), COLOR_YELLOW, FontStyle{}, NormalFont);
    combo = AddComboBox(ID_cbGoods, DrawPoint(600, 370), Extent(180, 20), TextureColor::Grey, NormalFont, 100,
                        readonlySettings);
    combo->AddItem(_("Very Low"));
    combo->AddItem(_("Low"));
    combo->AddItem(_("Normal"));
    combo->AddItem(_("A lot"));

    AddText(ID_txtGoals, DrawPoint(400, 345), _("Goals:"), COLOR_YELLOW, FontStyle{}, NormalFont);
    combo = AddComboBox(ID_cbGoals, DrawPoint(600, 340), Extent(180, 20), TextureColor::Grey, NormalFont, 100,
                        readonlySettings);
    combo->AddItem(_("None"));
    combo->AddItem(_("Conquer 3/4 of map"));
    combo->AddItem(_("Total domination"));
    combo->AddItem(_("Economy mode"));

    // Lobby game?
    if(lobbyClient_ && lobbyClient_->IsLoggedIn())
    {
        // Then add tournament modes as possible "objectives"
        for(const auto duration : TOURNAMENT_MODES_DURATION)
            combo->AddItem(helpers::format(_("Tournament: %u minutes"), duration / 1min));
    }

    AddText(ID_txtSpeed, DrawPoint(400, 315), _("Speed:"), COLOR_YELLOW, FontStyle{}, NormalFont);
    combo = AddComboBox(ID_cbSpeed, DrawPoint(600, 310), Extent(180, 20), TextureColor::Grey, NormalFont, 100,
                        !gameLobby_->isHost());
    combo->AddItem(_("Very slow"));
    combo->AddItem(_("Slow"));
    combo->AddItem(_("Normal"));
    combo->AddItem(_("Fast"));
    combo->AddItem(_("Very fast"));

    // Karte laden, um Kartenvorschau anzuzeigen
    if(!gameLobby_->isSavegame())
    {
        const bool isMapPreviewEnabled = !lua || lua->IsMapPreviewEnabled();
        if(!isMapPreviewEnabled)
        {
            AddTextDeepening(ID_txtNoPreview, DrawPoint(560, 40), Extent(220, 220), TextureColor::Grey, _("No preview"),
                             LargeFont, COLOR_YELLOW);
            AddText(ID_txtMapName, DrawPoint(670, 40 + 220 + 10), _("Map: ") + GAMECLIENT.GetMapTitle(), COLOR_YELLOW,
                    FontStyle::CENTER, NormalFont);
        } else
        {
            // Map laden
            libsiedler2::Archiv mapArchiv;
            // Karteninformationen laden
            if(int ec = libsiedler2::loader::LoadMAP(GAMECLIENT.GetMapPath(), mapArchiv))
            {
                WINDOWMANAGER.ShowAfterSwitch(
                  std::make_unique<iwMsgbox>(_("Error"), _("Could not load map:\n") + libsiedler2::getErrorString(ec),
                                             this, MsgboxButton::Ok, MsgboxIcon::ExclamationRed, ID_mbMapLoadError));
            } else
            {
                auto* map = static_cast<libsiedler2::ArchivItem_Map*>(mapArchiv.get(0));
                ctrlPreviewMinimap* preview = AddPreviewMinimap(ID_miniMap, DrawPoint(560, 40), Extent(220, 220), map);

                // Titel der Karte, Y-Position relativ je nach Höhe der Minimap festlegen, daher nochmals danach
                // verschieben, da diese Position sonst skaliert wird!
                ctrlText* text = AddText(ID_txtMapName, DrawPoint(670, 0), _("Map: ") + GAMECLIENT.GetMapTitle(),
                                         COLOR_YELLOW, FontStyle::CENTER, NormalFont);
                text->SetPos(DrawPoint(text->GetPos().x, preview->GetPos().y + preview->GetMapArea().bottom + 10));
            }
        }
    }

    if(GAMECLIENT.IsAIBattleModeOn())
    {
        const auto& aiBattlePlayers = GAMECLIENT.GetAIBattlePlayers();

        // Initialize AI battle players
        for(unsigned i = 0; i < gameLobby_->getNumPlayers(); i++)
        {
            if(i < aiBattlePlayers.size())
                lobbyController->SetPlayerState(i, PlayerState::AI, aiBattlePlayers[i]);
            else
                lobbyController->CloseSlot(i); // Close remaining slots
        }

        // Set name of host to the corresponding AI for local player
        if(localPlayerId_ < aiBattlePlayers.size())
            lobbyController->SetName(localPlayerId_,
                                     JoinPlayerInfo::MakeAIName(aiBattlePlayers[localPlayerId_], localPlayerId_));
    } else if(IsSinglePlayer() && !gameLobby_->isSavegame())
    {
        // Setze initial auf KI
        for(unsigned i = 0; i < gameLobby_->getNumPlayers(); i++)
        {
            if(!gameLobby_->getPlayer(i).isHost)
                lobbyController->SetPlayerState(i, PlayerState::AI, AI::Info(AI::Type::Default, AI::Level::Easy));
        }
    }

    // Angeforderte zusaetzliche lokale Spieler (Splitscreen) festnageln. NACH der
    // Standardbelegung oben, damit die Default-KI ueberschrieben wird - und auch fuer
    // Savegames, die der Block oben ueberspringt.
    if(!GAMECLIENT.GetAdditionalLocalPlayers().empty())
    {
        const std::vector<uint8_t> localPlayers = GAMECLIENT.GetAdditionalLocalPlayers();
        std::string err;
        if(!lobbyController)
            err = _("Additional local players require hosting the game");
        else
            err = GameClient::ValidateAdditionalLocalPlayers(*gameLobby_, localPlayerId_, localPlayers,
                                                             GAMECLIENT.IsAIBattleModeOn());
        if(err.empty())
            GameClient::ApplyAdditionalLocalPlayers(*lobbyController, localPlayers);
        else
        {
            // Harter Abbruch statt stiller Degradierung zum Einzelspieler.
            // ID_mbError fuehrt in Msg_MsgBoxResult zu GAMECLIENT.Stop() + GoBack().
            LOG.write("dskGameLobby: local player setup failed: %1%\n") % err;
            WINDOWMANAGER.ShowAfterSwitch(std::make_unique<iwMsgbox>(
              _("Error"), _("Could not set up local players:\n") + err, this, MsgboxButton::Ok,
              MsgboxIcon::ExclamationRed, ID_mbError));
        }
    }

    // Alle Spielercontrols erstellen
    for(unsigned i = 0; i < gameLobby_->getNumPlayers(); i++)
        UpdatePlayerRow(i);
    // swap buttons erstellen
    if(gameLobby_->isHost() && !gameLobby_->isSavegame() && IsChangeAllowed("swapping"))
    {
        for(unsigned i = 0; i < gameLobby_->getNumPlayers(); i++)
        {
            int rowPos = GetCtrl<Window>(ID_grpPlayerStart + i)->GetCtrl<Window>(ID_btPlayerState)->GetPos().y;
            ctrlButton* bt =
              AddTextButton(ID_btSwap + i, DrawPoint(5, 0), Extent(22, 22), TextureColor::Red1, _("-"), NormalFont);
            bt->SetPos(DrawPoint(bt->GetPos().x, rowPos));
        }
    }
    // Der Zuordnungsbildschirm. NACH den Spielerreihen, damit er ihre Beschriftung schon
    // aktualisieren kann, und nach der Uebernahme von --local-players oben, damit er deren
    // Sitze anzeigt statt sie zu ueberschreiben.
    CreateSeatPanel();

    CI_GGSChanged(gameLobby_->getSettings());

    if(serverType == ServerType::Lobby && lobbyClient_ && lobbyClient_->IsLoggedIn())
    {
        lobbyClient_->AddListener(this);
        lobbyClient_->SendServerJoinRequest();
    }

    GAMECLIENT.SetInterface(this);
}

dskGameLobby::~dskGameLobby()
{
    if(lobbyClient_)
        lobbyClient_->RemoveListener(this);
    GAMECLIENT.RemoveInterface(this);
}

/**
 *  Größe ändern-Reaktionen die nicht vom Skaling-Mechanismus erfasst werden.
 */
void dskGameLobby::Resize(const Extent& newSize)
{
    Window::Resize(newSize);

    // Text unter der PreviewMinimap verschieben, dessen Höhe von der Höhe der
    // PreviewMinimap abhängt, welche sich gerade geändert hat.
    auto* preview = GetCtrl<ctrlPreviewMinimap>(ID_miniMap);
    auto* text = GetCtrl<ctrlText>(ID_txtMapName);
    if(preview && text)
    {
        DrawPoint txtPos = text->GetPos();
        txtPos.y = preview->GetPos().y + preview->GetMapArea().bottom + 10;
        text->SetPos(txtPos);
    }
}

void dskGameLobby::SetActive(bool activate /*= true*/)
{
    Desktop::SetActive(activate);
    if(activate && !wasActivated && lua && gameLobby_->isHost())
    {
        wasActivated = true;
        try
        {
            lua->EventSettingsReady();
        } catch(const LuaExecutionError&)
        {
            WINDOWMANAGER.Show(std::make_unique<iwMsgbox>(
              _("Error"), _("Lua script was found but failed to load. Map might not work as expected!"), this,
              MsgboxButton::Ok, MsgboxIcon::ExclamationRed, ID_mbLuaLoadError));
            lua.reset();
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Der Zuordnungsbildschirm: wer sitzt auf welchem Platz vor dem Fernseher?
//
// Es fehlt hier KEIN Spielmechanismus. GameClient::SetAdditionalLocalPlayers,
// ValidateAdditionalLocalPlayers und ApplyAdditionalLocalPlayers gibt es seit Phase 1; bis
// hierher konnte sie nur die Kommandozeile fuellen (--local-players). Was fehlte, war die
// Bedienung - und damit die Erreichbarkeit vom Sofa aus.
// ---------------------------------------------------------------------------------------------

bool dskGameLobby::AreLocalSeatsAvailable() const
{
    // Genau die Bedingungen, unter denen ValidateAdditionalLocalPlayers zustimmen kann.
    // Savegames sind bewusst ausgenommen: dort haengt an jedem Slot ein frueherer Spieler, und
    // ohne eine Anzeige "welcher Sitz war frueher wer" (ID_cbMove) waere die Zuordnung geraten.
    return lobbyController && IsSinglePlayer() && gameLobby_ && gameLobby_->isHost() && !gameLobby_->isSavegame()
           && !GAMECLIENT.IsAIBattleModeOn();
}

void dskGameLobby::CreateSeatPanel()
{
    seats_.clear();
    if(!AreLocalSeatsAvailable())
        return;

    const unsigned numPlayers = gameLobby_->getNumPlayers();
    const unsigned numSeats = std::min<unsigned>(MAX_VIEWPORTS, numPlayers);
    if(numSeats < 2)
        return; // eine Karte fuer einen Spieler - da gibt es nichts zu verteilen

    // Sitz 1 ist der Hostslot. Er ist immer besetzt und kann nicht verlassen werden; Maus und
    // Tastatur bleiben fuer ihn zustaendig.
    seats_.push_back(LocalSeat{localPlayerId_, InvalidPadDevice, true});

    // Danach zuerst die Slots, die --local-players schon benannt hat - sonst haette die
    // Kommandozeile Sitze, die auf keiner Karte auftauchen -, dann die uebrigen aufsteigend.
    const std::vector<uint8_t> fromCmdLine = GAMECLIENT.GetAdditionalLocalPlayers();
    const auto canSeat = [&](const unsigned id) {
        if(id == localPlayerId_ || id >= numPlayers)
            return false;
        const PlayerState ps = gameLobby_->getPlayer(id).ps;
        return ps != PlayerState::Occupied && ps != PlayerState::Locked;
    };
    const auto alreadySeated = [&](const unsigned id) {
        return helpers::contains_if(seats_, [id](const LocalSeat& s) { return s.playerId == id; });
    };
    for(const uint8_t id : fromCmdLine)
    {
        if(seats_.size() >= numSeats)
            break;
        if(canSeat(id) && !alreadySeated(id))
        {
            // applied = true, und der Rueckgabewert ist die GEWOEHNLICHE KI, nicht das, was
            // gerade dort steht: dort steht bereits der Dummy, den der Konstruktor fuer genau
            // diesen lokalen Spieler geschrieben hat (GameClient::ApplyAdditionalLocalPlayers).
            // Ihn beim Aufstehen wiederherzustellen hiesse, eine untaetige KI zu hinterlassen.
            LocalSeat seat{id, InvalidPadDevice, true};
            seat.applied = true;
            seats_.push_back(seat);
        }
    }
    for(unsigned id = 0; id < numPlayers && seats_.size() < numSeats; ++id)
    {
        if(canSeat(id) && !alreadySeated(id))
            seats_.push_back(LocalSeat{id, InvalidPadDevice, false});
    }
    if(seats_.size() < 2)
    {
        seats_.clear();
        return;
    }

    // Der Platz unter der Spielertabelle ist im Einzelspieler frei: die Chatcontrols entstehen
    // nur, wenn !IsSinglePlayer(), und den Zuordnungsbildschirm gibt es nur dort.
    AddText(ID_txtSeats, DrawPoint(20, 318), _("Splitscreen seats (gamepad):"), COLOR_YELLOW, FontStyle{}, NormalFont);
    for(unsigned i = 0; i < seats_.size(); ++i)
    {
        const DrawPoint pos(20 + static_cast<int>(i % 2) * 190, 340 + static_cast<int>(i / 2) * 30);
        AddTextButton(ID_btSeat + i, pos, Extent(180, 22), TextureColor::Green2, "", NormalFont);
    }
    UpdateSeatPanel();
}

void dskGameLobby::UpdateSeatPanel()
{
    if(seats_.empty())
        return;
    // DER ZUORDNUNGSBILDSCHIRM GEHOERT DEM PAD. Steckt keins, blieben hier Knoepfe stehen, die
    // auf einen Mausklick ABSICHTLICH nichts tun - OnSeatButton verwirft jede Betaetigung ohne
    // handelndes Geraet, weil ein Sitzplatz ohne Pad ein Spieler ohne Eingabegeraet waere.
    // Sichtbar und wirkungslos ist davon die schlechtere Haelfte: es laedt zu der Annahme ein,
    // hier sei etwas kaputt. Die Karten erscheinen also, sobald ein Pad steckt, und
    // verschwinden wieder, wenn das letzte abgezogen wird.
    //
    // Gefragt wird "steckt" und nicht "wird benutzt": wer sein Pad gerade erst in die Hand
    // nimmt, soll die Karten schon sehen und sie nicht erst durch einen Knopfdruck ins Leere
    // herbeirufen muessen. Die Sichtbarkeit wird ausserdem in GetPadEntryCtrl nachgezogen -
    // der Einstiegsfokus eines Pads laeuft ueber FocusPath::Collect, und das ueberspringt
    // unsichtbare Controls (input/FocusPath.cpp).
    const bool visible = WINDOWMANAGER.GetPadInput().GetRouter().GetNumDevices() > 0;
    if(auto* txt = GetCtrl<Window>(ID_txtSeats))
        txt->SetVisible(visible);
    for(unsigned i = 0; i < seats_.size(); ++i)
    {
        auto* bt = GetCtrl<ctrlTextButton>(ID_btSeat + i);
        if(!bt)
            continue;
        bt->SetVisible(visible);
        LocalSeat& seat = seats_[i];
        SeatCardKind kind;
        unsigned value = 0;
        if(i == 0)
            kind = SeatCardKind::Host;
        else if(!IsSeatJoinable(i))
            kind = SeatCardKind::Closed;
        else if(!seat.taken)
            kind = SeatCardKind::Free;
        else if(seat.device == InvalidPadDevice)
        {
            kind = SeatCardKind::Slot;
            value = seat.playerId;
        } else
        {
            kind = SeatCardKind::Pad;
            value = static_cast<unsigned>(seat.device);
        }
        // Msg_PaintBefore laeuft je Frame. Solange sich daran nichts aendert, entsteht hier
        // keine Zeichenkette.
        if(seat.cardValid && seat.cardKind == kind && seat.cardValue == value)
            continue;
        seat.cardKind = kind;
        seat.cardValue = value;
        seat.cardValid = true;
        std::string text;
        switch(kind)
        {
            case SeatCardKind::Host: text = helpers::format(_("Seat %1%: host"), i + 1); break;
            case SeatCardKind::Closed: text = helpers::format(_("Seat %1%: closed"), i + 1); break;
            case SeatCardKind::Free: text = helpers::format(_("Seat %1%: press A to join"), i + 1); break;
            case SeatCardKind::Slot: text = helpers::format(_("Seat %1%: slot %2%"), i + 1, value + 1); break;
            case SeatCardKind::Pad: text = helpers::format(_("Seat %1%: pad %2%"), i + 1, value); break;
        }
        bt->SetText(text);
    }
}

bool dskGameLobby::IsSeatJoinable(const unsigned seat) const
{
    if(seat == 0 || seat >= seats_.size() || !gameLobby_)
        return false;
    const PlayerState ps = gameLobby_->getPlayer(seats_[seat].playerId).ps;
    // Occupied: dort sitzt eine echte Netzwerkverbindung. Locked: der Host hat den Slot
    // geschlossen. In beiden Faellen ist der Platz keiner mehr, den dieser Bildschirm vergibt.
    // Ein Sitz, den wir schon in der Hand haben, bleibt natuerlich seiner (Aufstehen).
    return seats_[seat].applied || (ps != PlayerState::Occupied && ps != PlayerState::Locked);
}

unsigned dskGameLobby::SeatOfDevice(const PadDeviceId device) const
{
    if(device == InvalidPadDevice)
        return static_cast<unsigned>(seats_.size());
    for(unsigned i = 0; i < seats_.size(); ++i)
    {
        if(seats_[i].device == device)
            return i;
    }
    return static_cast<unsigned>(seats_.size());
}

void dskGameLobby::OnSeatButton(const unsigned seat, const unsigned slot)
{
    if(seat == 0 || seat >= seats_.size())
        return; // Sitz 1 ist der Host und wird nicht vergeben
    const PadDeviceId device = WINDOWMANAGER.GetPadInput().GetActingDevice();
    if(device == InvalidPadDevice)
        return; // Maus und Tastatur setzen sich hier nicht hin
    if(slot == 0)
        return; // wer den Hostplatz steuert, sitzt schon

    const unsigned mySeat = SeatOfDevice(device);
    if(mySeat != seat && !IsSeatJoinable(seat))
        return; // der Host hat diesen Slot geschlossen oder ein Netzwerkspieler sitzt darauf
    if(mySeat == seat)
    {
        // Aufstehen. ApplyLocalSeats stellt auf dem Slot GENAU DAS wieder her, was vor dem
        // Hinsetzen dort stand - nicht den Dummy aus ApplyAdditionalLocalPlayers, sonst
        // hinterliesse jeder Aussteiger eine untaetige KI, und nicht eine pauschale Vorgabe,
        // sonst verlaere der Host seine Einstellung (BEFUND 2).
        seats_[seat].taken = false;
        seats_[seat].device = InvalidPadDevice;
    } else if(mySeat < seats_.size())
        return; // ein Pad sitzt auf genau einem Platz
    else if(!seats_[seat].taken)
    {
        seats_[seat].taken = true;
        seats_[seat].device = device;
    } else if(seats_[seat].device == InvalidPadDevice)
    {
        // Ein Sitz, den --local-players belegt hat, bekommt jetzt sein Pad.
        seats_[seat].device = device;
    } else
        return; // besetzt

    ApplyLocalSeats();
}

void dskGameLobby::ApplyLocalSeats()
{
    if(seats_.empty() || !lobbyController)
        return;

    std::vector<uint8_t> ids;
    for(unsigned i = 1; i < seats_.size(); ++i)
    {
        if(seats_[i].taken)
            ids.push_back(static_cast<uint8_t>(seats_[i].playerId));
    }
    const std::string err =
      GameClient::ValidateAdditionalLocalPlayers(*gameLobby_, localPlayerId_, ids, GAMECLIENT.IsAIBattleModeOn());
    if(!err.empty())
    {
        // Kann von hier aus nicht vorkommen - die Sitze werden aus genau den Slots gebildet, die
        // die Pruefung zulaesst. Falls doch: den Spielzustand NICHT anfassen.
        LOG.write("dskGameLobby: seat assignment rejected: %1%\n") % err;
        return;
    }

    GAMECLIENT.SetAdditionalLocalPlayers(ids);
    // BEFUND 2: NUR die Slots anfassen, die dieser Bildschirm selbst in der Hand hat.
    //
    // Hier stand einmal eine Schleife, die JEDEN nicht eingenommenen Sitz auf die Standard-KI
    // schrieb. Sie war als Rueckabwicklung des Aufstehens gedacht, traf aber auch jeden Slot,
    // den der Host inzwischen mit der Maus eingestellt hatte - ein geschlossener Slot wurde
    // wieder KI, eine schwere KI wieder leicht, und zwar bei jedem einzelnen Beitritt.
    //
    // Stattdessen: beim Hinsetzen merken, was dort stand, beim Aufstehen genau das
    // wiederherstellen. Ein Sitz, auf dem nie jemand sass, wird nie angefasst.
    for(unsigned i = 1; i < seats_.size(); ++i)
    {
        LocalSeat& seat = seats_[i];
        if(seat.taken && !seat.applied)
        {
            const JoinPlayerInfo& before = gameLobby_->getPlayer(seat.playerId);
            seat.savedPs = before.ps;
            seat.savedAi = before.aiInfo;
            seat.applied = true;
        } else if(!seat.taken && seat.applied)
        {
            lobbyController->SetPlayerState(seat.playerId, seat.savedPs, seat.savedAi);
            seat.applied = false;
        }
    }
    // Erst die Rueckgabe oben, dann die belegten festnageln - ApplyAdditionalLocalPlayers muss
    // NACH der Standardbelegung laufen (network/GameClient.cpp:1948-1949).
    GameClient::ApplyAdditionalLocalPlayers(*lobbyController, ids);

    // Und die Padzuordnung LUECKENLOS nachziehen: Ansicht i gehoert zu ids[i-1], der Padslot ist
    // die Ansichtsnummer. Ohne diese Zeilen entschiede in der Partie wieder die Reihenfolge der
    // ersten Benutzung, und wer hier Sitz 3 genommen hat, saesse dort auf Sitz 2.
    PadRouter& router = WINDOWMANAGER.GetPadInput().GetRouter();
    unsigned viewIdx = 1;
    for(unsigned i = 1; i < seats_.size(); ++i)
    {
        if(!seats_[i].taken)
            continue;
        if(seats_[i].device != InvalidPadDevice)
            router.AssignSlot(seats_[i].device, viewIdx);
        ++viewIdx;
    }
    // Wer dabei verdraengt wurde - ein Pad, das nur navigiert hat und zufaellig auf diesem Slot
    // sass -, bekommt einen freien zurueck. Ohne diesen Ausgleich koennte es selbst nie mehr
    // beitreten (PadRouter::RebalanceUnassigned).
    router.RebalanceUnassigned();

    for(const LocalSeat& seat : seats_)
        UpdatePlayerRow(seat.playerId);
    UpdateSeatPanel();
}

void dskGameLobby::SyncSeatsWithGameState()
{
    if(seats_.empty())
        return;
    // Bewusst zwei Anweisungen und kein ||: beide Pruefungen muessen laufen, auch wenn die
    // erste schon etwas gefunden hat.
    const bool unplugged = DropDisconnectedSeats();
    const bool closed = DropSeatsClosedByTheHost();
    if(unplugged || closed)
        ApplyLocalSeats();
}

bool dskGameLobby::DropDisconnectedSeats()
{
    const PadRouter& router = WINDOWMANAGER.GetPadInput().GetRouter();
    bool changed = false;
    for(unsigned i = 1; i < seats_.size(); ++i)
    {
        if(seats_[i].device == InvalidPadDevice || router.HasDevice(seats_[i].device))
            continue;
        seats_[i].device = InvalidPadDevice;
        seats_[i].taken = false;
        changed = true;
    }
    return changed;
}

bool dskGameLobby::DropSeatsClosedByTheHost()
{
    if(!gameLobby_)
        return false;
    bool changed = false;
    for(unsigned i = 1; i < seats_.size(); ++i)
    {
        LocalSeat& seat = seats_[i];
        if(!seat.taken)
            continue;
        const PlayerState ps = gameLobby_->getPlayer(seat.playerId).ps;
        // Locked: der Host hat den Slot geschlossen. Occupied: eine echte Netzwerkverbindung
        // sitzt darauf. Genau die beiden Zustaende, die IsSeatJoinable auch beim BEITRETEN
        // abweist - nur schuetzte das bisher nie einen Sitz, den schon jemand haelt.
        //
        // WARUM NICHT "ps != AI", also genau das Kriterium, an dem GameClient::SetupLocalPlayers
        // haengt: unsere eigene Umstellung auf die Dummy-KI laeuft ueber den Server und steht
        // erst ein paar Frames spaeter im gameLobby_. In diesem Fenster saehe ein gerade
        // eingenommener Sitz wie ein weggenommener aus, und der Spieler floege aus dem Platz,
        // den er im selben Frame genommen hat. Diese beiden Zustaende dagegen kann unsere
        // eigene Schreibung nie erzeugen. Was hier durchrutscht, faengt PrepareSeatsForStart.
        if(ps != PlayerState::Locked && ps != PlayerState::Occupied)
            continue;
        seat.taken = false;
        seat.device = InvalidPadDevice;
        // DER HOST HAT ENTSCHIEDEN. `applied` faellt HIER und nicht erst in ApplyLocalSeats,
        // damit dort nicht der Zustand von VOR dem Hinsetzen zurueckgeschrieben wird: das
        // machte die Schliessung wortlos rueckgaengig - derselbe Datenverlust wie in BEFUND 2,
        // nur in der anderen Richtung.
        seat.applied = false;
        changed = true;
    }
    return changed;
}

bool dskGameLobby::PrepareSeatsForStart()
{
    if(seats_.empty() || !gameLobby_)
        return true;
    // Erst die gewoehnliche Nachfuehrung. Sie laeuft ohnehin je Frame; hier noch einmal, damit
    // die Startfaehigkeit nicht daran haengt, ob zwischen der letzten Aenderung des Hosts und
    // dem Startknopf ueberhaupt ein Frame lag.
    SyncSeatsWithGameState();
    // Und dann GENAU DAS Kriterium, an dem GameClient::SetupLocalPlayers den Spielstart
    // scheitern laesst: ein zusaetzlicher lokaler Slot muss eine KI sein. Was es nicht besteht,
    // wird geraeumt und GENANNT - statt den Start mit OnError(LocalPlayerSetup) -> Stop()
    // abzubrechen und die ganze Partievorbereitung mitzunehmen.
    bool dropped = false;
    for(unsigned i = 1; i < seats_.size(); ++i)
    {
        LocalSeat& seat = seats_[i];
        if(!seat.taken || gameLobby_->getPlayer(seat.playerId).ps == PlayerState::AI)
            continue;
        seat.taken = false;
        seat.device = InvalidPadDevice;
        seat.applied = false;
        dropped = true;
    }
    if(!dropped)
        return true;
    ApplyLocalSeats();
    // Der Zustand ist damit repariert: derselbe Knopf startet beim naechsten Druck. Die
    // Meldung sagt nur, warum ein Sitz leer geworden ist.
    WINDOWMANAGER.Show(std::make_unique<iwMsgbox>(_("Error"), ClientErrorToStr(ClientError::LocalPlayerSetup), this,
                                                  MsgboxButton::Ok, MsgboxIcon::ExclamationRed, ID_mbSeatsDropped));
    return false;
}

unsigned dskGameLobby::GetNumPadSlots() const
{
    // Nur hier duerfen mehrere Pads gleichzeitig navigieren - anderswo waere das ein Wettlauf
    // um den naechsten Desktopwechsel.
    return seats_.empty() ? 1u : static_cast<unsigned>(seats_.size());
}

bool dskGameLobby::Msg_PadCommand(const unsigned slot, const PadButton button)
{
    if(button == PadButton::Start)
    {
        // BEFUND B. Start handelt fuer ALLE: er startet die Partie und bricht einen laufenden
        // Countdown ab. Geprueft wurde bisher nur der CLIENT (isHost) - also ob DIESER RECHNER
        // hostet -, nie der handelnde SLOT. Damit konnte jedes Gastpad die Partie fuer alle
        // starten.
        //
        // Der Knopf gehoert dem Pad des HOSTPLATZES, und das ist Slot 0: Sitz 1 ist immer der
        // Hostslot (CreateSeatPanel), und ApplyLocalSeats vergibt die uebrigen Ansichten erst
        // ab 1. Dieselbe Klammer, mit der B ausdruecklich verhindert, dass ein sitzloses
        // Zweitpad die Lobby fuer alle verlaesst - und Starten ist mindestens so folgenreich
        // wie Verlassen.
        //
        // Ausserhalb des Zuordnungsbildschirms gibt es nur einen Slot (Desktop::GetNumPadSlots),
        // dort ist slot immer 0: fuer jede andere Lobby aendert sich nichts.
        if(slot == 0 && gameLobby_ && gameLobby_->isHost())
        {
            Msg_ButtonClick(ID_btStartGame);
            return true;
        }
        return false;
    }
    if(button != PadButton::B)
        return false;

    const unsigned mySeat = SeatOfDevice(WINDOWMANAGER.GetPadInput().GetActingDevice());
    if(mySeat > 0 && mySeat < seats_.size())
    {
        OnSeatButton(mySeat, slot); // eigener Sitz -> aufstehen
        return true;
    }
    if(slot != 0)
        return false; // ein sitzloses Zweitpad soll die Partie nicht fuer alle verlassen
    AskToLeave();
    return true;
}

void dskGameLobby::AskToLeave()
{
    // BEFUND 3. "Return" haelt den Client an und raeumt die ganze Partievorbereitung ab -
    // Karte, Addons, Voelker, Sitzverteilung. Es ist der einzige unwiderrufliche Knopf dieses
    // Bildschirms.
    //
    // WARUM EINE RUECKFRAGE UND KEINE ANDERE BELEGUNG: B ist am Pad der Zurueckknopf, und er
    // ist es ueberall - er schliesst Fenster, und wer sitzt, steht damit auf. Ihm hier
    // ausgerechnet gar keine Bedeutung zu geben, waere die einzige Stelle, an der der gelernte
    // Griff ins Leere geht; ihn wie bisher ohne Nachfrage durchzureichen, macht aus dem
    // meistgedrueckten Knopf am Pad den gefaehrlichsten. Die Rueckfrage behaelt beides: die
    // gelernte Bedeutung und den Weg nach draussen, der rein mit dem Pad begehbar bleibt.
    //
    // Der Fokus liegt dabei auf "Nein" (iwMsgbox::GetPadEntryCtrl, dieselbe Vorgabeantwort, auf
    // die iwMsgbox auch den Mauszeiger stellt). Ein reflexhaftes B-dann-A bleibt damit in der
    // Lobby; wer heraus will, muss den Fokus ausdruecklich bewegen.
    //
    // DER MAUSWEG BLEIBT UNVERAENDERT: der rote Knopf "Return" tut, was er immer getan hat.
    // Er verlangt schon durch Zielen und Klicken eine Absicht, die ein Daumen auf B nicht hat.
    // Eine zweite Frage kann von hier aus nicht entstehen: solange ein Fenster offen ist, geht
    // B dorthin und erreicht Msg_PadCommand gar nicht (MenuPadInput::OnPadButton).
    WINDOWMANAGER.Show(std::make_unique<iwMsgbox>(_("Return"), _("Do you really want to leave this game?"), this,
                                                  MsgboxButton::YesNo, MsgboxIcon::QuestionRed, ID_mbQuestionLeave));
}

Window* dskGameLobby::GetPadEntryCtrl(const unsigned slot)
{
    if(seats_.empty() || slot == 0)
        return nullptr; // der Hostspieler faengt wie bisher beim ersten Control an
    // Die Sitzkarten sind unsichtbar, solange kein Pad steckt (UpdateSeatPanel). Hier steckt
    // gerade eins - und der Aufrufer sucht seinen Einstiegsfokus ueber FocusPath::Collect, das
    // unsichtbare Controls ueberspringt. Msg_PaintBefore zieht die Sichtbarkeit erst NACH
    // diesem Aufruf nach (WindowManager::Draw ruft PumpPadInput davor), also hier.
    UpdateSeatPanel();
    // Ein neu hinzukommendes Pad sieht seinen Beitritt dort, wo er stattfindet. Das ist die
    // Uebersetzung von "Press A to join" (XR-115) in diese Oberflaeche - ohne sie laege der
    // Fokus eines Beitretenden auf "Spiel starten".
    //
    // ZUERST ein Sitz, den --local-players belegt hat, der aber noch kein Pad steuert: die
    // Kommandozeile hat ausdruecklich zwei lokale Spieler verlangt, also gehoert das erste Pad
    // dorthin und nicht auf einen zusaetzlichen Platz.
    for(unsigned i = 1; i < seats_.size(); ++i)
    {
        if(seats_[i].taken && seats_[i].device == InvalidPadDevice)
            return GetCtrl<Window>(ID_btSeat + i);
    }
    for(unsigned i = 1; i < seats_.size(); ++i)
    {
        // Ein vom Host geschlossener Slot ist kein Beitrittsangebot mehr.
        if(!seats_[i].taken && IsSeatJoinable(i))
            return GetCtrl<Window>(ID_btSeat + i);
    }
    return GetCtrl<Window>(ID_btSeat + 1);
}

void dskGameLobby::UpdatePlayerRow(const unsigned row)
{
    const JoinPlayerInfo& player = gameLobby_->getPlayer(row);

    unsigned cy = 80 + row * 30;
    TextureColor tc = (row & 1 ? TextureColor::Grey : TextureColor::Green2);

    // Alle Controls erstmal zerstören (die ganze Gruppe)
    DeleteCtrl(ID_grpPlayerStart + row);
    // und neu erzeugen
    ctrlGroup* group = AddGroup(ID_grpPlayerStart + row);

    std::string name;
    switch(player.ps)
    {
        default: name.clear(); break;
        case PlayerState::Occupied:
        case PlayerState::AI: name = player.name; break;
        case PlayerState::Free: name = _("Open"); break;
        case PlayerState::Locked: name = _("Closed"); break;
    }

    if(GetCtrl<ctrlPreviewMinimap>(ID_miniMap))
    {
        if(player.isUsed())
            // Nur KIs und richtige Spieler haben eine Farbe auf der Karte
            GetCtrl<ctrlPreviewMinimap>(ID_miniMap)->SetPlayerColor(row, player.color);
        else
            // Keine richtigen Spieler --> Startposition auf der Karte ausblenden
            GetCtrl<ctrlPreviewMinimap>(ID_miniMap)->SetPlayerColor(row, 0);
    }

    // Spielername, beim Hosts Spielerbuttons, aber nich beim ihm selber, er kann sich ja nich selber kicken!
    if(gameLobby_->isHost() && !player.isHost && IsChangeAllowed("playerState"))
        group->AddTextButton(ID_btPlayerState, DrawPoint(30, cy), Extent(180, 22), tc, name, NormalFont);
    else
        group->AddTextDeepening(ID_btPlayerState, DrawPoint(30, cy), Extent(180, 22), tc, name, NormalFont,
                                COLOR_YELLOW);
    auto* text = group->GetCtrl<ctrlBaseText>(ID_btPlayerState);

    // Is das der Host? Dann farblich markieren
    if(player.isHost)
        text->SetTextColor(0xFF00FF00);

    // Bei geschlossenem nicht sichtbar
    if(player.isUsed())
    {
        // If not in savegame -> Player can change own row and host can change AIs
        const bool allowPlayerChange = ((gameLobby_->isHost() && player.ps == PlayerState::AI) || localPlayerId_ == row)
                                       && !gameLobby_->isSavegame();
        bool allowNationChange = allowPlayerChange;
        bool allowColorChange = allowPlayerChange;
        bool allowTeamChange = allowPlayerChange;
        bool allowPortraitChange = allowPlayerChange;
        if(lua)
        {
            if(localPlayerId_ == row)
            {
                allowNationChange &= lua->IsChangeAllowed("ownNation", true);
                allowColorChange &= lua->IsChangeAllowed("ownColor", true);
                allowTeamChange &= lua->IsChangeAllowed("ownTeam", true);
                allowPortraitChange &= lua->IsChangeAllowed("ownPortrait", true);
            } else
            {
                allowNationChange &= lua->IsChangeAllowed("aiNation", true);
                allowColorChange &= lua->IsChangeAllowed("aiColor", true);
                allowTeamChange &= lua->IsChangeAllowed("aiTeam", true);
                allowPortraitChange &= lua->IsChangeAllowed("aiPortrait", true);
            }
        }

        if(allowNationChange)
            group->AddTextButton(ID_btNation, DrawPoint(215, cy), Extent(95, 22), tc, _(NationNames[NATION_ORDER[0]]),
                                 NormalFont);
        else
            group->AddTextDeepening(ID_btNation, DrawPoint(215, cy), Extent(95, 22), tc,
                                    _(NationNames[NATION_ORDER[0]]), NormalFont, COLOR_YELLOW);

        const auto& portrait = Portraits[player.portraitIndex];
        if(allowPortraitChange)
            group->AddImageButton(ID_btPortrait, DrawPoint(315, cy), Extent(34, 22), tc,
                                  LOADER.GetImageN(portrait.resourceId, portrait.resourceIndex), _(portrait.name));
        else
            group->AddImageDeepening(ID_btPortrait, DrawPoint(315, cy), Extent(34, 22), tc,
                                     LOADER.GetImageN(portrait.resourceId, portrait.resourceIndex));

        if(allowColorChange)
            group->AddColorButton(ID_btColor, DrawPoint(354, cy), Extent(30, 22), tc, 0);
        else
            group->AddColorDeepening(ID_btColor, DrawPoint(354, cy), Extent(30, 22), tc, 0);

        if(allowTeamChange)
            group->AddTextButton(ID_btTeam, DrawPoint(394, cy), Extent(50, 22), tc, _("-"), NormalFont);
        else
            group->AddTextDeepening(ID_btTeam, DrawPoint(394, cy), Extent(50, 22), tc, _("-"), NormalFont,
                                    COLOR_YELLOW);

        // Ready (not for AIs and Host)
        if(player.ps == PlayerState::Occupied && !player.isHost)
            group->AddCheckBox(ID_chkReady, DrawPoint(464, cy), Extent(22, 22), tc, "", nullptr,
                               (localPlayerId_ != row));

        ctrlVarDeepening* ping = group->AddVarDeepening(ID_txtPing, DrawPoint(505, cy), Extent(50, 22), tc, _("%d"),
                                                        NormalFont, COLOR_YELLOW, 1, &player.ping); //-V111

        // Move (not for Save games and Host)
        if(gameLobby_->isSavegame() && player.ps == PlayerState::Occupied)
        {
            ctrlComboBox* combo = group->AddComboBox(ID_cbMove, DrawPoint(560, cy), Extent(160, 22), tc, NormalFont,
                                                     150, !gameLobby_->isHost());

            // Mit den alten Namen füllen
            for(unsigned i = 0; i < gameLobby_->getNumPlayers(); ++i)
            {
                if(!gameLobby_->getPlayer(i).originName.empty())
                {
                    combo->AddItem(gameLobby_->getPlayer(i).originName);
                    if(i == row)
                        combo->SetSelection(combo->GetNumItems() - 1u);
                }
            }
        }

        // Hide ping for AIs or on single player games
        if(player.ps == PlayerState::AI || IsSinglePlayer())
            ping->SetVisible(false);

        // Fill fields
        ChangeNation(row, player.nation);
        ChangePortrait(row, player.portraitIndex);
        ChangeTeam(row, player.team);
        ChangePing(row);
        ChangeReady(row, player.isReady);
        ChangeColor(row, player.color);
    }
    group->SetActive(IsActive());
}

/**
 *  Methode vor dem Zeichnen
 */
void dskGameLobby::Msg_PaintBefore()
{
    Desktop::Msg_PaintBefore();
    // Der Host kann einen Slot mit der Maus geschlossen oder umgestellt haben, ohne dass diese
    // Sitzkarte davon erfahren haette, und ein Pad kann abgezogen worden sein.
    SyncSeatsWithGameState();
    UpdateSeatPanel();
    // Chatfenster Fokus geben
    if(!IsSinglePlayer())
        GetCtrl<ctrlEdit>(ID_edtChatMsg)->SetFocus();
}

void dskGameLobby::Msg_Group_ButtonClick(const unsigned group_id, const unsigned ctrl_id)
{
    unsigned playerId = group_id - ID_grpPlayerStart;

    switch(ctrl_id)
    {
        case ID_btPlayerState:
        {
            if(gameLobby_->isHost())
                lobbyController->TogglePlayerState(playerId);
        }
        break;

        case ID_btNation:
        {
            SetPlayerReady(playerId, false);

            if(playerId == localPlayerId_ || gameLobby_->isHost())
            {
                JoinPlayerInfo& player = gameLobby_->getPlayer(playerId);
                player.nation = nextNation(player.nation);
                if(gameLobby_->isHost())
                    lobbyController->SetNation(playerId, player.nation);
                else
                    GAMECLIENT.Command_SetNation(player.nation);
                ChangeNation(playerId, player.nation);
            }
        }
        break;

        case ID_btPortrait:
        {
            SetPlayerReady(playerId, false);

            if(playerId == localPlayerId_ || gameLobby_->isHost())
            {
                JoinPlayerInfo& player = gameLobby_->getPlayer(playerId);
                player.portraitIndex = (player.portraitIndex + 1) % Portraits.size();
                if(gameLobby_->isHost())
                    lobbyController->SetPortrait(playerId, player.portraitIndex);
                else
                    GAMECLIENT.Command_SetPortrait(player.portraitIndex);
                ChangePortrait(playerId, player.portraitIndex);
            }
        }
        break;

        case ID_btColor:
        {
            SetPlayerReady(playerId, false);

            if(playerId == localPlayerId_ || gameLobby_->isHost())
            {
                // Get colors used by other players
                std::set<unsigned> takenColors;
                for(unsigned p = 0; p < gameLobby_->getNumPlayers(); ++p)
                {
                    // Skip self
                    if(p == playerId)
                        continue;

                    const JoinPlayerInfo& otherPlayer = gameLobby_->getPlayer(p);
                    if(otherPlayer.isUsed())
                        takenColors.insert(otherPlayer.color);
                }

                // Look for a unique color
                JoinPlayerInfo& player = gameLobby_->getPlayer(playerId);
                int newColorIdx = JoinPlayerInfo::GetColorIdx(player.color);
                do
                {
                    player.color = PLAYER_COLORS[(++newColorIdx) % PLAYER_COLORS.size()];
                } while(helpers::contains(takenColors, player.color));

                if(gameLobby_->isHost())
                    lobbyController->SetColor(playerId, player.color);
                else
                    GAMECLIENT.Command_SetColor(player.color);
                ChangeColor(playerId, player.color);
            }

            // Start-Farbe der Minimap ändern
        }
        break;

        case ID_btTeam:
        {
            SetPlayerReady(playerId, false);

            if(playerId == localPlayerId_ || gameLobby_->isHost())
            {
                JoinPlayerInfo& player = gameLobby_->getPlayer(playerId);
                player.team = nextEnumValue(player.team);
                if(gameLobby_->isHost())
                    lobbyController->SetTeam(playerId, player.team);
                else
                    GAMECLIENT.Command_SetTeam(player.team);
                ChangeTeam(playerId, player.team);
            }
        }
        break;
    }
}

void dskGameLobby::Msg_Group_CheckboxChange(const unsigned group_id, const unsigned /*ctrl_id*/, const bool checked)
{
    unsigned playerId = group_id - ID_grpPlayerStart;

    // Bereit
    if(playerId < MAX_PLAYERS)
        SetPlayerReady(playerId, checked);
}

void dskGameLobby::Msg_Group_ComboSelectItem(const unsigned group_id, const unsigned /*ctrl_id*/,
                                             const unsigned selection)
{
    if(!gameLobby_->isHost())
        return;
    // Swap players
    const unsigned playerId = group_id - ID_grpPlayerStart;

    int player2 = -1;
    for(unsigned i = 0, playerCtr = 0; i < gameLobby_->getNumPlayers(); ++i)
    {
        if(!gameLobby_->getPlayer(i).originName.empty() && playerCtr++ == selection)
        {
            player2 = i;
            break;
        }
    }

    if(player2 < 0)
        LOG.write("dskHostGame: ERROR: Selected player not found, stop swapping!\n");
    else
        lobbyController->SwapPlayers(playerId, static_cast<unsigned>(player2));
}

void dskGameLobby::GoBack()
{
    if(IsSinglePlayer())
        WINDOWMANAGER.Switch(std::make_unique<dskSinglePlayer>());
    else if(serverType == ServerType::LAN)
        WINDOWMANAGER.Switch(std::make_unique<dskLAN>());
    else if(serverType == ServerType::Lobby && lobbyClient_ && lobbyClient_->IsLoggedIn())
        WINDOWMANAGER.Switch(std::make_unique<dskLobby>());
    else
        WINDOWMANAGER.Switch(std::make_unique<dskDirectIP>());
}

bool dskGameLobby::IsChangeAllowed(const std::string& setting) const
{
    return !lua || lua->IsChangeAllowed(setting);
}

void dskGameLobby::Msg_ButtonClick(const unsigned ctrl_id)
{
    if(ctrl_id >= ID_btSeat && ctrl_id < ID_btSeat + MAX_VIEWPORTS)
    {
        // Der HANDELNDE SLOT kommt aus der Klammer, unter der der WindowManager den Knopfdruck
        // zustellt (MenuPadInput::GetActingSlot) - dieselbe Bauform, mit der auch der
        // Fensterbesitz ohne Aenderung an 100+ Erzeugungsstellen auskommt. Ein MAUSKLICK auf
        // eine Sitzkarte laeuft ohne diese Klammer und wird in OnSeatButton verworfen: ein
        // Sitzplatz ohne Pad waere ein Spieler ohne Eingabegeraet.
        OnSeatButton(ctrl_id - ID_btSeat, WINDOWMANAGER.GetPadInput().GetActingSlot());
        return;
    }
    if(ctrl_id >= ID_btSwap && ctrl_id < ID_btSwap + MAX_PLAYERS)
    {
        unsigned targetPlayer = ctrl_id - ID_btSwap;
        if(targetPlayer != localPlayerId_ && gameLobby_->isHost())
            lobbyController->SwapPlayers(localPlayerId_, targetPlayer);
        return;
    }
    switch(ctrl_id)
    {
        case ID_btReturn:
            GAMECLIENT.Stop();
            GoBack();
            break;

        case ID_btStartGame:
        {
            auto* ready = GetCtrl<ctrlTextButton>(ID_btStartGame);
            if(gameLobby_->isHost())
            {
                if(!checkOptions())
                    return;
                if(!PrepareSeatsForStart())
                    return;

                SetPlayerReady(localPlayerId_, true);
                if(lua)
                    lua->EventPlayerReady(localPlayerId_);
                if(ready->GetText() == _("Start game"))
                    lobbyController->StartCountdown(5);
                else
                    lobbyController->CancelCountdown();
            } else
            {
                if(ready->GetText() == _("Ready"))
                    SetPlayerReady(localPlayerId_, true);
                else
                    SetPlayerReady(localPlayerId_, false);
            }
        }
        break;
        case ID_btSettings: // Addons
        {
            // Lobbyfenster gehoeren keiner Ansicht - hier gibt es noch keine Partie und keine
            // lokalen Spieler.
            if(auto* wnd = WINDOWMANAGER.FindNonModalWindow(CGI_ADDONS, SHARED_WINDOW_OWNER))
                wnd->Close();
            else
            {
                std::unique_ptr<iwAddons> w;
                if(!allowAddonChange)
                    w = std::make_unique<iwAddons>(gameLobby_->getSettings(), this, AddonChangeAllowed::None);
                else if(IsChangeAllowed("addonsAll"))
                    w = std::make_unique<iwAddons>(gameLobby_->getSettings(), this, AddonChangeAllowed::All);
                else
                {
                    RTTR_Assert(lua); // Otherwise all changes would be allowed
                    w = std::make_unique<iwAddons>(gameLobby_->getSettings(), this, AddonChangeAllowed::WhitelistOnly,
                                                   lua->GetAllowedAddons());
                }
                WINDOWMANAGER.Show(std::move(w));
            }
        }
        break;
    }
}

void dskGameLobby::Msg_EditEnter(const unsigned ctrl_id)
{
    if(ctrl_id != ID_edtChatMsg)
        return;
    auto* edit = GetCtrl<ctrlEdit>(ctrl_id);
    const std::string msg = edit->GetText();
    edit->SetText("");
    if(gameChat->IsVisible())
        GAMECLIENT.Command_Chat(msg, ChatDestination::All);
    else if(lobbyClient_ && lobbyClient_->IsLoggedIn() && lobbyChat->IsVisible())
        lobbyClient_->SendChat(msg);
}

void dskGameLobby::CI_Countdown(unsigned remainingTimeInSec)
{
    if(IsSinglePlayer())
        return;

    if(!hasCountdown_)
    {
        const std::string startMsg = helpers::format(_("You have %u seconds until game starts"), remainingTimeInSec);
        gameChat->AddMessage("", "", 0, startMsg, COLOR_RED);
        gameChat->AddMessage("", "", 0, _("Don't forget to check the addon configuration!"), 0xFFFFDD00);
        gameChat->AddMessage("", "", 0, "", 0xFFFFCC00);
        hasCountdown_ = true;
    }

    const std::string message =
      (remainingTimeInSec > 0) ? " " + std::to_string(remainingTimeInSec) : _("Starting game, please wait");

    gameChat->AddMessage("", "", 0, message, 0xFFFFBB00);
}

void dskGameLobby::CI_CancelCountdown(bool error)
{
    if(hasCountdown_)
    {
        hasCountdown_ = false;
        gameChat->AddMessage("", "", 0xFFCC2222, _("Start aborted"), 0xFFFFCC00);
        FlashGameChat();
    }

    if(gameLobby_->isHost())
    {
        if(error)
        {
            WINDOWMANAGER.Show(std::make_unique<iwMsgbox>(
              _("Error"),
              _("Game can only be started as soon as everybody has a unique color,everyone is "
                "ready and all free slots are closed."),
              this, MsgboxButton::Ok, MsgboxIcon::ExclamationRed, ID_mbStartErrror));
        }

        ChangeReady(localPlayerId_, true);
    }
}

void dskGameLobby::FlashGameChat()
{
    if(!gameChat->IsVisible())
    {
        auto* tab = GetCtrl<Window>(ID_optChatTab);
        auto* bt = tab->GetCtrl<ctrlButton>(ID_btChatGame);
        if(!localChatTabAnimId)
            localChatTabAnimId = tab->GetAnimationManager().addAnimation(new BlinkButtonAnim(bt));
    }
}

void dskGameLobby::Msg_MsgBoxResult(const unsigned msgbox_id, const MsgboxResult mbr)
{
    switch(msgbox_id)
    {
        case ID_mbMapLoadError:
        case ID_mbError:
        {
            GAMECLIENT.Stop();

            GoBack();
        }
        break;
        case ID_mbQuestionLeave:
        {
            if(mbr == MsgboxResult::Yes)
                Msg_ButtonClick(ID_btReturn);
        }
        break;
        case CGI_ADDONS: // addon-window applied settings?
        {
            if(mbr == MsgboxResult::Yes)
                UpdateGGS();
        }
        break;
        case ID_mbQuestionEconomy: // Economy Mode - change Addon Setttings
        {
            if(mbr == MsgboxResult::Yes)
            {
                gameLobby_->getSettings().setSelection(AddonId::PEACEFULMODE, true);
                gameLobby_->getSettings().setSelection(AddonId::NO_COINS_DEFAULT, true);
                gameLobby_->getSettings().setSelection(AddonId::NO_ARMOR_DEFAULT, true);
                gameLobby_->getSettings().setSelection(AddonId::LIMIT_CATAPULTS, 2);
                UpdateGGS();
            } else if(mbr == MsgboxResult::No)
            {
                forceOptions = true;
                Msg_ButtonClick(ID_btStartGame);
            }
        }
        break;
        case ID_mbQuestionPeaceful: // Peaceful mode still active but we have an attack based victory condition
        {
            if(mbr == MsgboxResult::Yes)
            {
                gameLobby_->getSettings().setSelection(AddonId::PEACEFULMODE, false);
            } else if(mbr == MsgboxResult::No)
            {
                forceOptions = true;
                Msg_ButtonClick(ID_btStartGame);
            }
        }
        break;
    }
}

void dskGameLobby::Msg_ComboSelectItem(const unsigned ctrl_id, const unsigned /*selection*/)
{
    switch(ctrl_id)
    {
        default: break;

        case ID_cbSpeed:
        case ID_cbGoals:
        case ID_cbGoods:
        case ID_cbExploration:
        {
            // GameSettings wurden verändert, resetten
            UpdateGGS();
        }
        break;
    }
}

void dskGameLobby::Msg_CheckboxChange(const unsigned ctrl_id, const bool /*checked*/)
{
    switch(ctrl_id)
    {
        default: break;
        case ID_chkSharedView:
        case ID_chkLockTeams:
        case ID_chkRandomSpawn:
        {
            // GameSettings wurden verändert, resetten
            UpdateGGS();
        }
        break;
    }
}

void dskGameLobby::Msg_OptionGroupChange(const unsigned ctrl_id, const unsigned selection)
{
    if(ctrl_id == ID_optChatTab)
    {
        gameChat->SetVisible(selection == ID_btChatGame);
        lobbyChat->SetVisible(selection == ID_btChatLobby);
        auto* tab = GetCtrl<Window>(ID_optChatTab);
        tab->GetCtrl<ctrlButton>(selection)->SetTexture(TextureColor::Green2);
        if(selection == ID_btChatGame)
        {
            tab->GetAnimationManager().finishAnimation(localChatTabAnimId, false);
            localChatTabAnimId = 0;
        } else
        {
            tab->GetAnimationManager().finishAnimation(lobbyChatTabAnimId, false);
            lobbyChatTabAnimId = 0;
        }
    }
}

void dskGameLobby::UpdateGGS()
{
    RTTR_Assert(gameLobby_->isHost());

    GlobalGameSettings& ggs = gameLobby_->getSettings();

    ggs.speed = static_cast<GameSpeed>(GetCtrl<ctrlComboBox>(ID_cbSpeed)->GetSelection().value());
    ggs.objective = static_cast<GameObjective>(GetCtrl<ctrlComboBox>(ID_cbGoals)->GetSelection().value());
    ggs.startWares = static_cast<StartWares>(GetCtrl<ctrlComboBox>(ID_cbGoods)->GetSelection().value());
    ggs.exploration = static_cast<Exploration>(GetCtrl<ctrlComboBox>(ID_cbExploration)->GetSelection().value());
    ggs.lockedTeams = GetCtrl<ctrlCheck>(ID_chkLockTeams)->isChecked();
    ggs.teamView = GetCtrl<ctrlCheck>(ID_chkSharedView)->isChecked();
    ggs.randomStartPosition = GetCtrl<ctrlCheck>(ID_chkRandomSpawn)->isChecked();

    // An Server übermitteln
    lobbyController->ChangeGlobalGameSettings(ggs);
}

void dskGameLobby::ChangeTeam(const unsigned player, const Team team)
{
    constexpr helpers::EnumArray<const char*, Team> teams = {"-", "?", "1", "2", "3", "4", "1-2", "1-3", "1-4"};

    GetCtrl<ctrlGroup>(ID_grpPlayerStart + player)->GetCtrl<ctrlBaseText>(ID_btTeam)->SetText(teams[team]);
}

void dskGameLobby::ChangeReady(const unsigned player, const bool ready)
{
    auto* check = GetCtrl<ctrlGroup>(ID_grpPlayerStart + player)->GetCtrl<ctrlCheck>(ID_chkReady);
    if(check)
        check->setChecked(ready);

    if(player == localPlayerId_)
    {
        auto* start = GetCtrl<ctrlTextButton>(ID_btStartGame);
        if(gameLobby_->isHost())
            start->SetText(hasCountdown_ ? _("Cancel start") : _("Start game"));
        else
            start->SetText(ready ? _("Not Ready") : _("Ready"));
    }
}

void dskGameLobby::ChangeNation(const unsigned player, const Nation nation)
{
    GetCtrl<ctrlGroup>(ID_grpPlayerStart + player)->GetCtrl<ctrlBaseText>(ID_btNation)->SetText(_(NationNames[nation]));
}

void dskGameLobby::ChangePortrait(const unsigned player, const unsigned portraitIndex)
{
    RTTR_Assert(portraitIndex < Portraits.size());
    const auto& portrait = Portraits[portraitIndex];
    auto* ctrl = GetCtrl<ctrlGroup>(ID_grpPlayerStart + player)->GetCtrl<ctrlBaseImage>(ID_btPortrait);
    ctrl->SetImage(LOADER.GetImageN(portrait.resourceId, portrait.resourceIndex));
    auto* ctrlButton = GetCtrl<ctrlGroup>(ID_grpPlayerStart + player)->GetCtrl<ctrlImageButton>(ID_btPortrait);
    if(ctrlButton)
        ctrlButton->SetTooltip(_(portrait.name));
}

void dskGameLobby::ChangePing(unsigned playerId)
{
    unsigned color = COLOR_RED;

    // Farbe bestimmen
    if(gameLobby_->getPlayer(playerId).ping < 300)
        color = COLOR_GREEN;
    else if(gameLobby_->getPlayer(playerId).ping < 800)
        color = COLOR_YELLOW;

    // und setzen
    GetCtrl<ctrlGroup>(ID_grpPlayerStart + playerId)->GetCtrl<ctrlVarDeepening>(ID_txtPing)->SetTextColor(color);
}

void dskGameLobby::ChangeColor(const unsigned player, const unsigned color)
{
    GetCtrl<ctrlGroup>(ID_grpPlayerStart + player)->GetCtrl<ctrlBaseColor>(ID_btColor)->SetColor(color);

    // Minimap-Startfarbe ändern
    if(GetCtrl<ctrlPreviewMinimap>(ID_miniMap))
        GetCtrl<ctrlPreviewMinimap>(ID_miniMap)->SetPlayerColor(player, color);
}

void dskGameLobby::SetPlayerReady(unsigned char player, bool ready)
{
    if(player != localPlayerId_)
        return;
    if(gameLobby_->isHost())
        ready = true;
    if(gameLobby_->getPlayer(player).isReady != ready)
    {
        gameLobby_->getPlayer(player).isReady = ready;
        GAMECLIENT.Command_SetReady(ready);
    }
    ChangeReady(player, ready);
}

void dskGameLobby::CI_NewPlayer(const unsigned playerId)
{
    UpdatePlayerRow(playerId);

    if(lua && gameLobby_->isHost())
        lua->EventPlayerJoined(playerId);
}

void dskGameLobby::CI_PlayerLeft(const unsigned playerId)
{
    UpdatePlayerRow(playerId);
    if(lua && gameLobby_->isHost())
        lua->EventPlayerLeft(playerId);
}

void dskGameLobby::CI_GameLoading(std::shared_ptr<Game> game)
{
    WINDOWMANAGER.Switch(std::make_unique<dskGameLoader>(std::move(game)));
}

void dskGameLobby::CI_PlayerDataChanged(unsigned playerId)
{
    UpdatePlayerRow(playerId);
}

void dskGameLobby::CI_PingChanged(const unsigned playerId, const unsigned short /*ping*/)
{
    ChangePing(playerId);
}

void dskGameLobby::CI_ReadyChanged(const unsigned playerId, const bool ready)
{
    ChangeReady(playerId, ready);
    // Event only called for other players (host ready is done in start game)
    // Also only for host and non-savegames
    if(ready && lua && gameLobby_->isHost() && playerId != localPlayerId_)
        lua->EventPlayerReady(playerId);
}

void dskGameLobby::CI_PlayersSwapped(const unsigned player1, const unsigned player2)
{
    if(player1 == localPlayerId_)
        localPlayerId_ = player2;
    else if(localPlayerId_ == player2)
        localPlayerId_ = player1;
    // BEFUND D: Ein Sitz zeigt auf einen SLOT, und ein Tausch bewegt die Spieler zwischen den
    // Slots. Der Sitz folgt deshalb seinem Spieler - mitsamt savedPs/savedAi, denn was vor dem
    // Hinsetzen auf seinem Slot stand, ist mit ihm gewandert
    // (GameClient::OnGameMessage(GameMessage_Player_Swap) tauscht die JoinPlayerInfos und zieht
    // die Liste der zusaetzlichen lokalen Spieler genau so mit).
    //
    // Ohne diese Zeilen zeigte das Aufstehen auf den falschen Slot: der Rueckgabewert liefe ins
    // Leere - der Server laesst einen Slot, der zugleich der Absender ist, unveraendert -, und
    // auf dem tatsaechlich verlassenen Slot bliebe die Dummy-KI stehen. Also genau die
    // untaetige KI, die ApplyLocalSeats vermeiden soll.
    //
    // Die REIHENFOLGE von seats_ bleibt unangetastet: sie ist die Reihenfolge der Ansichten auf
    // dem Bildschirm (dskGameInterface::CreateViews), und GameClient benennt seine Liste
    // ebenfalls an Ort und Stelle um. Ein Tausch verschiebt Spieler, keine Sitzplaetze.
    for(LocalSeat& seat : seats_)
    {
        if(seat.playerId == player1)
            seat.playerId = player2;
        else if(seat.playerId == player2)
            seat.playerId = player1;
    }
    // Spieler wurden vertauscht, beide Reihen updaten
    UpdatePlayerRow(player1);
    UpdatePlayerRow(player2);
}

void dskGameLobby::CI_GGSChanged(const GlobalGameSettings& /*ggs*/)
{
    const GlobalGameSettings& ggs = gameLobby_->getSettings();

    GetCtrl<ctrlComboBox>(ID_cbSpeed)->SetSelection(static_cast<unsigned short>(ggs.speed));
    GetCtrl<ctrlComboBox>(ID_cbGoals)->SetSelection(static_cast<unsigned short>(ggs.objective));
    GetCtrl<ctrlComboBox>(ID_cbGoods)->SetSelection(static_cast<unsigned short>(ggs.startWares));
    GetCtrl<ctrlComboBox>(ID_cbExploration)->SetSelection(static_cast<unsigned short>(ggs.exploration));
    GetCtrl<ctrlCheck>(ID_chkLockTeams)->setChecked(ggs.lockedTeams);
    GetCtrl<ctrlCheck>(ID_chkSharedView)->setChecked(ggs.teamView);
    GetCtrl<ctrlCheck>(ID_chkRandomSpawn)->setChecked(ggs.randomStartPosition);

    SetPlayerReady(localPlayerId_, false);
}

void dskGameLobby::CI_Chat(const unsigned playerId, const ChatDestination /*cd*/, const std::string& msg)
{
    if((playerId != 0xFFFFFFFF) && !IsSinglePlayer())
    {
        std::string time = s25util::Time::FormatTime("(%H:%i:%s)");

        gameChat->AddMessage(time, gameLobby_->getPlayer(playerId).name, gameLobby_->getPlayer(playerId).color, msg,
                             0xFFFFFF00); //-V810
        FlashGameChat();
    }
}

void dskGameLobby::CI_Error(const ClientError ce)
{
    WINDOWMANAGER.Show(std::make_unique<iwMsgbox>(_("Error"), ClientErrorToStr(ce), this, MsgboxButton::Ok,
                                                  MsgboxIcon::ExclamationRed, ID_mbError));
}

/**
 *  (Lobby-)Status: Benutzerdefinierter Fehler (kann auch Conn-Loss o.ä sein)
 */
void dskGameLobby::LC_Status_Error(const std::string& error)
{
    WINDOWMANAGER.Show(
      std::make_unique<iwMsgbox>(_("Error"), error, this, MsgboxButton::Ok, MsgboxIcon::ExclamationRed, ID_mbError));
}

void dskGameLobby::LC_Chat(const std::string& player, const std::string& text)
{
    if(!lobbyChat)
        return;
    lobbyChat->AddMessage("", player, ctrlChat::CalcUniqueColor(player), text, COLOR_YELLOW);
    if(!lobbyChat->IsVisible())
    {
        auto* tab = GetCtrl<Window>(ID_optChatTab);
        auto* bt = tab->GetCtrl<ctrlButton>(ID_btChatLobby);
        if(!lobbyChatTabAnimId)
            lobbyChatTabAnimId = tab->GetAnimationManager().addAnimation(new BlinkButtonAnim(bt));
    }
}

bool dskGameLobby::checkOptions()
{
    if(forceOptions)
        return true;
    const GlobalGameSettings& ggs = gameLobby_->getSettings();
    if(ggs.objective == GameObjective::EconomyMode && !ggs.isEnabled(AddonId::PEACEFULMODE))
    {
        WINDOWMANAGER.Show(std::make_unique<iwMsgbox>(
          _("Economy mode"),
          _("You chose the economy mode. In economy mode the player or team that collects the most of certain goods "
            "wins (check the Economic progress window in game).\n\n"
            "Some players like to play this objective in peaceful mode. Would you like to adjust settings for a "
            "peaceful game?\n"
            "Choosing Yes will activate peaceful mode, ban catapults and disable buildings receiving coins by default. "
            "After clicking Yes you will be able to review the changes and then start the game by clicking the Start "
            "game button again.\n"
            "Choosing No will start the game without any changes."),
          this, MsgboxButton::YesNoCancel, MsgboxIcon::QuestionGreen, ID_mbQuestionEconomy));
        return false;
    } else if(ggs.isEnabled(AddonId::PEACEFULMODE)
              && (ggs.objective == GameObjective::Conquer3_4 || ggs.objective == GameObjective::TotalDomination))
    {
        WINDOWMANAGER.Show(std::make_unique<iwMsgbox>(
          _("Peaceful mode"),
          _("You chose a war based victory condition but peaceful mode is still active. Would you like to deactivate "
            "peaceful mode before you start? Choosing No will start the game, Yes will let you review the changes."),
          this, MsgboxButton::YesNoCancel, MsgboxIcon::QuestionRed, ID_mbQuestionPeaceful));
        return false;
    }
    return true;
}
