// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// 9: IVideoDriver::FetchPadEvents (Gamepad-Eingabe). Neue virtuelle Methode = neuer
//    vtable-Eintrag im Plugin -> ABI-Bruch. Geprueft wird auf exakte Gleichheit
//    (drivers/DriverWrapper.cpp:104); ein veraltetes Plugin wird mit "Invalid API version!"
//    abgelehnt und taucht nicht in der Treiberliste auf - sauberer Abbruch statt Absturz.
//    ACHTUNG: dieselbe Konstante gilt fuer Video UND Audio.
#define DRIVERAPIVERSION 10
