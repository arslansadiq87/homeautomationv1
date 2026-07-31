from pathlib import Path

Import("env")

PROJECT_DIR = Path(env["PROJECT_DIR"])
DATA_DIR = PROJECT_DIR / "data"
OUTPUT = PROJECT_DIR / "include" / "web_assets_generated.h"

ASSETS = [
    "/index.html",
    "/styles.css",
    "/app.js",
    "/techpanda.png",
]


def emit_byte_array(name, data):
    lines = [f"const uint8_t {name}[] PROGMEM = {{"]
    for index in range(0, len(data), 16):
        chunk = data[index:index + 16]
        lines.append("  " + ", ".join(f"0x{value:02X}" for value in chunk) + ",")
    lines.append("};")
    return "\n".join(lines)


def generate_web_assets(source, target, env):
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    parts = [
        "#pragma once",
        "",
        "#include <Arduino.h>",
        "",
        "struct GeneratedWebAsset {",
        "  const char *path;",
        "  const uint8_t *data;",
        "  uint32_t length;",
        "};",
        "",
    ]
    table_entries = []

    for index, path in enumerate(ASSETS):
        asset_path = DATA_DIR / path.lstrip("/")
        data = asset_path.read_bytes()
        symbol = f"WEB_ASSET_{index}"
        parts.append(emit_byte_array(symbol, data))
        parts.append("")
        table_entries.append(f'  {{"{path}", {symbol}, sizeof({symbol})}}')

    parts.append(f"constexpr uint8_t GENERATED_WEB_ASSET_COUNT = {len(ASSETS)};")
    parts.append("const GeneratedWebAsset GENERATED_WEB_ASSETS[] PROGMEM = {")
    parts.append(",\n".join(table_entries))
    parts.append("};")
    parts.append("")

    content = "\n".join(parts)
    if not OUTPUT.exists() or OUTPUT.read_text() != content:
        OUTPUT.write_text(content)


generate_web_assets(None, None, env)
