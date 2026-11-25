# Vor dem Bauen: Weboberflaeche aus src/webui.h gepackt nach src/webui_gz.h (PlatformIO extra_script)
import gzip, pathlib, re
Import("env")  # noqa: F821
src = pathlib.Path(env.subst("$PROJECT_DIR")) / "src"  # noqa: F821
html = re.search(r'R"HTML\((.*)\)HTML"', (src / "webui.h").read_text(encoding="utf-8"), re.S).group(1).encode()
gz = gzip.compress(html, 9, mtime=0)
rows = ",".join(f"0x{b:02x}" for b in gz)
(src / "webui_gz.h").write_text("// automatisch erzeugt aus webui.h – nicht bearbeiten\n#pragma once\n#include <Arduino.h>\n"
                                f"const uint8_t INDEX_HTML_GZ[] PROGMEM = {{{rows}}};\nconst size_t INDEX_HTML_GZ_LEN = {len(gz)};\n")
