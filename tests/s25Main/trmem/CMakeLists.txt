# SPDX-License-Identifier: GPL-2.0-or-later

# Messwerkzeug fuer Phase 2: Speicher- und Laufzeitkosten mehrerer TerrainRenderer.
# Braucht testUIHelper, weil GenerateOpenGL ueber LOADER/VIDEODRIVER Texturen anlegt.
add_testcase(NAME trmem
    LIBS s25Main testHelpers testConfig testUIHelper rttr::vld
    COST 20
)
