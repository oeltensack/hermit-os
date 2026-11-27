# Vor dem Bauen: Weboberflaechen aus src/webui*.h gepackt nach src/webui*_gz.h (PlatformIO extra_script)
#   webui.h -> webui_gz.h (Klima), webui_disp.h -> webui_disp_gz.h (Heizungsanzeige)
import gzip, pathlib, re
Import("env")  # noqa: F821
src = pathlib.Path(env.subst("$PROJECT_DIR")) / "src"  # noqa: F821
for name in ("webui", "webui_disp"):
    f = src / f"{name}.h"
    if not f.exists():
        continue
    html = re.search(r'R"HTML\((.*)\)HTML"', f.read_text(encoding="utf-8"), re.S).group(1).encode()
    gz = gzip.compress(html, 9, mtime=0)
    rows = ",".join(f"0x{b:02x}" for b in gz)
    (src / f"{name}_gz.h").write_text(f"// automatisch erzeugt aus {name}.h – nicht bearbeiten\n#pragma once\n#include <Arduino.h>\n"
                                      f"const uint8_t INDEX_HTML_GZ[] PROGMEM = {{{rows}}};\nconst size_t INDEX_HTML_GZ_LEN = {len(gz)};\n")
