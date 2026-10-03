#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Genera src/core/include/daso/catalog_data.hpp a partir de los datos del repo.

Fuentes de verdad:
  ../../Contenido             -> orden canonico de los paquetes (bucle `for %%A in (...)`)
  ../../Catalogo_Paquetes.md  -> categoria de cada paquete (secciones `=== ... ===`)

El orden del batch se preserva. Los duplicados que el batch arrastra (278 entradas
para 144 paquetes reales) se eliminan conservando la primera aparicion.

Uso:
    python tools/generate_catalog.py            # escribe el .hpp
    python tools/generate_catalog.py --check    # falla si el .hpp esta desactualizado
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

# tools/ -> DASO-WinUI3/ -> repo root
PROJECT_ROOT = pathlib.Path(__file__).resolve().parents[1]
REPO_ROOT = PROJECT_ROOT.parent
BATCH = REPO_ROOT / "Contenido"
CATALOG_MD = REPO_ROOT / "Catalogo_Paquetes.md"
OUT = PROJECT_ROOT / "src" / "core" / "include" / "daso" / "catalog_data.hpp"

# --------------------------------------------------------------------------
# Seccion del catalogo markdown -> categoria C++
# --------------------------------------------------------------------------
SECTION_TO_CATEGORY = {
    "Iconos y Temas": "Themes",
    "SystemUI": "SystemUi",
    "ROM Personalizadas": "CustomRom",
    "Otros": "Other",
    "AOSP": "Aosp",
    "Overlays": "Overlay",
    "Vendor / OEM": "Vendor",
    "Google / GMS": "Google",
}

# --------------------------------------------------------------------------
# Reglas de riesgo. Gana la primera que coincide.
#   Vital   -> nunca llega al plan, ni aunque el usuario lo fuerce.
#   Caution -> exige salir del modo seguro.
# --------------------------------------------------------------------------
VITAL_RULES: list[tuple[str, str]] = [
    ("com.android.systemui", "Barra de estado y panel de notificaciones."),
    ("com.android.settings", "Ajustes del sistema. Sin esto no hay configuración."),
    ("com.android.shell", "Consola ADB. Sin esto D.A.S.O no puede ejecutarse."),
    ("com.android.providers.settings", "Proveedor de Ajustes; las apps se apagan al quitarlo."),
    ("com.android.packageinstaller", "Instalador de paquetes del sistema."),
    ("com.android.permissioncontroller", "Controlador de permisos."),
    ("com.android.phone", "Telephonía."),
    ("com.android.server.telecom", "Servicio telefónico."),
    ("com.android.bluetooth", "Pila de Bluetooth."),
    ("com.android.nfc", "NFC."),
    ("com.android.emergency", "Llamadas de emergencia."),
    ("com.android.externalstorage", "Almacenamiento externo (SD)."),
    ("com.android.providers.contacts", "Proveedor de contactos."),
    ("com.android.providers.calendar", "Proveedor de calendario."),
    ("com.android.inputmethod.*", "Teclado del sistema. Sin teclado no se puede escribir."),
    ("com.google.android.inputmethod.*", "Teclado de Gboard."),
    ("com.android.launcher*", "Launcher del sistema: sin icono no se abre ninguna app."),
    ("com.android.launcher3*", "Launcher del sistema: sin icono no se abre ninguna app."),
    ("org.lineageos.launcher*", "Launcher del sistema: sin icono no se abre ninguna app."),
    ("com.miui.home", "Launcher del sistema: sin icono no se abre ninguna app."),
    ("com.sec.android.app.launcher*", "Launcher del sistema: sin icono no se abre ninguna app."),
    ("com.google.android.gms", "Servicios de Google. Quitarlos rompe notificaciones y cuentas."),
    ("android", "El propio framework de Android."),
]

CAUTION_RULES: list[tuple[str, str]] = [
    ("*.systemui.clocks.*", "Desaparece la opción de reloj en Ajustes."),
    ("*.systemui.plugin.*", "Desaparece un módulo de las acciones rápidas."),
    ("*.internal.systemui.*", "Desaparece una variante de la barra de navegación."),
    ("com.android.internal.display.cutout.emulation*", "Cambia cómo se dibuja el recorte de pantalla."),
    ("com.android.customization.themes", "Desaparece toda la sección Temas de Ajustes."),
    ("com.android.wallpaper*", "Desaparece el selector de fondos de pantalla."),
    ("com.android.dreams.*", "Desaparece los protectores de pantalla."),
    ("*.auto_generated_rro_product__", "Puede alterar el tema; es un overlay de la ROM."),
    ("android.overlay.*", "Puede alterar el tema; es un overlay base del sistema."),
    ("*.overlay.*", "Puede alterar el tema; es un overlay."),
    ("com.google.android.marvin.talkback", "Se pierde la accesibilidad (TalkBack)."),
    ("com.google.android.apps.wellbeing", "Desaparece Bienestar Digital."),
    ("com.google.android.googlequicksearchbox", "Desaparece el buscador de Google del launcher."),
    ("com.android.companiondevicemanager", "Desaparece el enlace con reloj y auriculares."),
    ("com.android.appsearch", "Desaparece el buscador de aplicaciones."),
    ("com.android.adservices", "Se pierde el filtrado de publicidad por red."),
    ("com.android.federatedcompute.services", "Desaparece el aprendizaje federado."),
    ("com.android.scheduling", "Se afecta al planificador de tareas del sistema."),
    ("com.android.virtualmachine.res", "Desaparece la virtualización de aplicaciones."),
    ("com.android.health.connect.backuprestore", "Desaparece la copia de seguridad de Salud Conectada."),
    ("com.android.egg", "Desaparece el huevo de Pascua de Android."),
    ("com.stevesoltys.seedvault", "Se pierde la copia de seguridad del sistema (SeedVault)."),
    ("com.android.uwb.resources", "Desaparece la interfaz de Ultra Wideband."),
    ("vendor.qti.hardware.cacert.server", "Desaparece el servidor de certificados del fabricante."),
    ("org.lineageos.lineagesettings", "Desaparece el módulo de Ajustes de la ROM."),
    ("*.columbus*", "Desaparece los gestos de pantalla de la ROM."),
    ("*.omnijaws*", "Desaparece el fondo de pantalla por defecto de la ROM."),
    ("org.risingos.backgrounds", "Desaparece los fondos de pantalla de la ROM."),
    ("com.libremobileos.sidebar", "Desaparece la barra lateral del launcher."),
]

# --------------------------------------------------------------------------
# Familias por prefijo. Gana el prefijo mas largo.
# --------------------------------------------------------------------------
FAMILIES: list[tuple[str, str, str, str, str, bool]] = [
    ("com.android.theme.icon_pack.", "Pack de iconos: ", "Icon pack: ",
     "Pack de iconos adicional. Solo desaparece de la galería de temas.",
     "Additional icon pack. It only disappears from the theme gallery.", True),
    ("com.android.theme.icon.", "Icono temático: ", "Themed icon: ",
     "Un icono concreto de la galería de temas.",
     "A single icon from the theme gallery.", True),
    ("com.android.theme.font.", "Tipografía: ", "Font: ",
     "Una tipografía concreta de la galería de temas.",
     "A single font from the theme gallery.", True),
    ("com.android.systemui.clocks.", "Reloj de SystemUI: ", "SystemUI clock: ",
     "Un diseño de reloj para la barra de estado.",
     "One status-bar clock design.", True),
    ("com.android.internal.display.cutout.emulation", "Emulación de recorte: ",
     "Cutout emulation: ",
     "Simula un recorte de pantalla distinto al real.",
     "Fakes a display cutout different from the real one.", True),
    ("org.lineageos.overlay.customization.", "Overlay de personalización: ",
     "Customisation overlay: ", "Ajuste de personalización de la ROM.",
     "A ROM customisation tweak.", True),
    ("android.overlay.", "Overlay base: ", "Base overlay: ",
     "Overlay del sistema que ajusta recursos comunes.",
     "System overlay that adjusts common resources.", True),
]

# Descripciones puntuales por nombre exacto.
OVERRIDES: dict[str, tuple[str, str, str, str]] = {}


def ov(name: str, es: str, en: str, desc_es: str, desc_en: str) -> None:
    OVERRIDES[name] = (es, en, desc_es, desc_en)


ov("com.android.avatarpicker", "Selector de avatar", "Avatar picker",
   "Elige la foto de perfil de la cuenta. Nadie lo usa en la práctica.",
   "Picks the account profile photo. Nobody uses it in practice.")
ov("com.android.htmlviewer", "Visor HTML", "HTML viewer",
   "Visor de HTML antiguo, ya innecesario en los navegadores actuales.",
   "Legacy HTML viewer, already unnecessary in current browsers.")
ov("com.android.cts.priv.ctsshim", "Shim de pruebas CTS", "CTS test shim",
   "Andamiaje de pruebas de compatibilidad. Solo lo usan las compilaciones de QA.",
   "Compatibility test shim. Only QA builds use it.")
ov("com.android.egg", "Huevo de Pascua", "Easter egg",
   "El huevo de Pascua de Android.", "The Android easter egg.")
ov("com.android.uwb.resources", "Recursos UWB", "UWB resources",
   "Textos e iconos de Ultra Wideband.",
   "Ultra Wideband strings and icons.")
ov("com.android.companiondevicemanager", "Gestor de dispositivos vinculados",
   "Companion device manager",
   "Empareja el reloj y los auriculares con el móvil.",
   "Pairs a watch and headphones with the phone.")
ov("com.android.appsearch", "Buscador de aplicaciones", "App search",
   "Buscador global de aplicaciones.", "Global application search.")
ov("com.android.adservices", "Servicios de publicidad", "Advertising services",
   "Plataforma de anuncios y filtrado de publicidad por red.",
   "Ad platform and network-based ad filtering.")
ov("com.android.federatedcompute.services", "Computación federada",
   "Federated compute", "Aprendizaje federado en segundo plano.",
   "Background federated learning.")
ov("com.android.scheduling", "Planificador de tareas", "Job scheduler",
   "Planificador de tareas en segundo plano del sistema.",
   "The system background job scheduler.")
ov("com.android.virtualmachine.res", "Recursos de máquina virtual",
   "Virtual machine resources",
   "Recursos para ejecutar aplicaciones virtualizadas.",
   "Resources for running virtualised apps.")
ov("com.android.photopicker", "Selector de fotos", "Photo picker",
   "El selector de fotos del sistema. Android 13 ya lo trae integrado.",
   "The system photo picker. Android 13 already ships it built in.")
ov("com.android.health.connect.backuprestore", "Copia de Salud Conectada",
   "Health Connect backup", "Copia de seguridad de los datos de salud.",
   "Backup of Health Connect data.")
ov("com.android.customization.themes", "Catálogo de temas", "Theme catalogue",
   "El catálogo de temas del sistema.", "The system theme catalogue.")
ov("com.stevesoltys.seedvault", "SeedVault", "SeedVault",
   "Copia de seguridad del sistema. Android la usa por debajo.",
   "System backup. Android relies on it underneath.")
ov("vendor.qti.hardware.cacert.server", "Servidor CA de Qualcomm",
   "Qualcomm CA server", "Servidor de certificados raíz del fabricante.",
   "Vendor root certificate server.")
ov("com.google.android.googlequicksearchbox", "Buscador de Google",
   "Google search box", "Caja de búsqueda de Google en el launcher.",
   "Google search box in the launcher.")
ov("com.google.android.marvin.talkback", "TalkBack", "TalkBack",
   "Lector de pantalla de accesibilidad.", "The TalkBack screen reader.")
ov("com.google.android.apps.wellbeing", "Bienestar Digital",
   "Digital Wellbeing",
   "Controles de tiempo de pantalla y modos de concentración.",
   "Screen time controls and focus modes.")
ov("com.android.wallpaper.livepicker", "Selector de fondos",
   "Live wallpaper picker", "Selector de fondos de pantalla animados.",
   "Live wallpaper picker.")
ov("com.android.wallpaperbackup", "Copia de fondos", "Wallpaper backup",
   "Copia de seguridad del fondo de pantalla actual.",
   "Backup of the current wallpaper.")
ov("com.android.wallpaper", "Fondos de pantalla", "Wallpapers",
   "Servicio de fondos de pantalla del sistema.",
   "The system wallpaper service.")
ov("com.android.dreams.basic", "Protectores básicos", "Basic dreams",
   "Protectores de pantalla integrados.", "Built-in screensavers.")
ov("com.android.dreams.phototable", "Protector de fotos", "Photo table dream",
   "Protector de pantalla con fotos.", "Photo screensaver.")
ov("io.chaldeaprjkt.gamespace.auto_generated_rro_product__",
   "Overlay de GameSpace", "GameSpace overlay",
   "Espacio de juego de la ROM.", "The ROM's game space.")
ov("com.mtg.gmssettingsoverlay", "Overlay de Ajustes GMS",
   "GMS settings overlay", "Ajustes de servicios de Google de la ROM.",
   "The ROM's Google services settings.")
ov("com.android.settings.overlay.miami", "Overlay de Ajustes",
   "Settings overlay", "Recurso de overlay aplicado a Ajustes.",
   "Overlay resource applied to Settings.")
ov("org.lineageos.sm6375.EuiccOverlay", "Overlay de eSIM (SM6375)",
   "eSIM overlay (SM6375)",
   "Overlay de eSIM específico del chip SM6375.",
   "eSIM overlay specific to the SM6375 chipset.")
ov("com.android.systemui.plugin.globalactions.wallet",
   "Acción rápida de Wallet", "Wallet quick action",
   "Acceso al monedero desde el menú de apagado.",
   "Wallet access from the power menu.")
ov("com.android.internal.systemui.navbar.threebutton",
   "Barra de 3 botones", "Three-button navbar",
   "Variante de la barra de navegación con 3 botones.",
   "Navigation bar variant with three buttons.")
ov("com.libremobileos.sidebar", "Barra lateral", "Sidebar",
   "Barra lateral del launcher.", "The launcher's sidebar.")
ov("com.android.theme.font.sanfrancisco", "San Francisco", "San Francisco",
   "La tipografía de sistema de iOS.", "The iOS system font.")
ov("com.android.theme.font.jetbrainsmono", "JetBrains Mono", "JetBrains Mono",
   "Tipografía monoespaciada para programadores.",
   "Monospaced font aimed at programmers.")

GENERIC_HEADS = {
    "com.android.theme.icon_pack.": (
        "Pack de iconos", "Icon pack",
        "Pack de iconos del catálogo de temas.",
        "Icon pack from the theme catalogue."),
    "com.android.theme.icon.": (
        "Icono", "Icon",
        "Icono del catálogo de temas.", "Icon from the theme catalogue."),
    "com.android.theme.font.": (
        "Tipografía", "Font",
        "Tipografía del catálogo de temas.", "Font from the theme catalogue."),
    "com.android.systemui.clocks.": (
        "Reloj de SystemUI", "SystemUI clock",
        "Diseño de reloj para la barra de estado.",
        "Clock design for the status bar."),
}

CATEGORY_LABEL_ES = {
    "Aosp": "Componente de AOSP",
    "Vendor": "Componente del fabricante",
    "Other": "Componente opcional",
    "CustomRom": "Componente de la ROM",
}
CATEGORY_LABEL_EN = {
    "Aosp": "AOSP component",
    "Vendor": "Vendor component",
    "Other": "Optional component",
    "CustomRom": "ROM component",
}

OVERLAY_SUFFIX = ".auto_generated_rro_product__"


def glob_to_re(pattern: str) -> re.Pattern[str]:
    return re.compile("^" + ".*".join(re.escape(p) for p in pattern.split("*")) + "$",
                      re.IGNORECASE)


VITAL_COMPILED = [(glob_to_re(p), w) for p, w in VITAL_RULES]
CAUTION_COMPILED = [(glob_to_re(p), w) for p, w in CAUTION_RULES]


def tier_of(name: str) -> tuple[str, str]:
    for rx, why in VITAL_COMPILED:
        if rx.match(name):
            return "Vital", why
    for rx, why in CAUTION_COMPILED:
        if rx.match(name):
            return "Caution", why
    return "Safe", ""


def tail_token(name: str) -> str:
    """Parte del nombre que identifica al elemento concreto."""
    if name.endswith(OVERLAY_SUFFIX):
        name = name[: -len(OVERLAY_SUFFIX)]
    for sep in (".", "__", "_", "-"):
        if sep in name:
            cand = name.rsplit(sep, 1)[-1]
            if len(cand) >= 3:
                return cand
    return name


def prettify(token: str) -> str:
    words = [w for w in re.split(r"[._\-]", token) if w]
    if not words:
        return token
    return " ".join(w if w.isupper() else w.capitalize() for w in words)


def describe(name: str, category: str) -> tuple[str, str, str, str]:
    """Devuelve (nombre_es, nombre_en, descripcion_es, descripcion_en)."""
    if name in OVERRIDES:
        return OVERRIDES[name]

    best: tuple[str, str, str, str, str, bool] | None = None
    best_len = -1
    for fam in FAMILIES:
        if name.startswith(fam[0]) and len(fam[0]) > best_len:
            best, best_len = fam, len(fam[0])
    if best is not None:
        _p, es, en, desc_es, desc_en, use_token = best
        token = prettify(tail_token(name))
        if use_token:
            return (es + token, en + token, desc_es, desc_en)
        return (token, token, desc_es, desc_en)

    for head, (es, en, desc_es, desc_en) in GENERIC_HEADS.items():
        if name.startswith(head):
            token = prettify(tail_token(name))
            return (f"{es}: {token}", f"{en}: {token}", desc_es, desc_en)

    if name.endswith(OVERLAY_SUFFIX):
        token = prettify(tail_token(name))
        return (f"Overlay de ROM: {token}", f"ROM overlay: {token}",
                "Recurso generado automáticamente por la ROM para un producto concreto.",
                "Resource auto-generated by the ROM for one specific product.")

    token = prettify(tail_token(name))
    es = CATEGORY_LABEL_ES.get(category, "Componente del sistema")
    en = CATEGORY_LABEL_EN.get(category, "System component")
    return (f"{es}: {token}", f"{en}: {token}",
            "Componente que no aporta nada en el uso diario del móvil.",
            "Component that adds nothing to everyday phone use.")


def cpp_escape(s: str) -> str:
    return s.replace("\\", "\\\\").replace('"', '\\"')


def parse_batch() -> list[str]:
    text = BATCH.read_text(encoding="utf-8", errors="ignore")
    m = re.search(r"for\s+%%A\s+in\s*\((.*?)\)\s*do", text, re.S | re.I)
    if not m:
        sys.exit("No se encontro el bucle `for %%A in (...)` en Contenido")
    seen: set[str] = set()
    out: list[str] = []
    for line in m.group(1).replace("\r", "").split("\n"):
        s = line.strip()
        if s and not s.upper().startswith("REM") and s not in seen:
            seen.add(s)
            out.append(s)
    return out


SECTION_RE = re.compile(r"^={2,}\s*(.+?)(?:\s*\(\d+\))?\s*={2,}$")


def parse_catalog_md() -> dict[str, str]:
    text = CATALOG_MD.read_text(encoding="utf-8", errors="ignore")
    section: str | None = None
    mapping: dict[str, str] = {}
    for line in text.splitlines():
        s = line.strip()
        if not s:
            continue
        # Los encabezados llevan el recuento: `=== Iconos y Temas (89) ===`
        m = SECTION_RE.match(s)
        if m:
            section = SECTION_TO_CATEGORY.get(m.group(1).strip())
            continue
        if section and "." in s and re.fullmatch(r"[A-Za-z0-9_.]+", s):
            mapping.setdefault(s, section)
    return mapping


HEADER = """// -----------------------------------------------------------------------------
//  GENERATED FILE - DO NOT EDIT BY HAND
//  Regenerate with:  python tools/generate_catalog.py
//  Sources:          Contenido (order) + Catalogo_Paquetes.md (categories)
//
//  {count} unique packages. The batch script lists 278 entries for these same
//  {count} packages; the duplicates are dropped here, first occurrence wins.
//
//  Note: Catalogo_Paquetes.md claims 145 unique packages; the actual union of
//  the batch list and the markdown list is {count} (the "Otros" section header
//  says 4 but lists 3). This header follows the real data.
// -----------------------------------------------------------------------------
#pragma once

// Nota: este header NO incluye "daso/catalog.hpp". Lo incluye el propio
// catalog.hpp al final, despues de haber definido PackageEntry / RiskTier /
// Category, para que las capas superiores vean el catalogo sin incluir datos.

#include <array>
#include <cstddef>

namespace daso::detail {{

using CatalogRecord = PackageEntry;

inline constexpr std::array<CatalogRecord, {count}> kCatalogRecords{{{{
"""


def render(count: int, records: list[dict]) -> str:
    out = [HEADER.format(count=count)]
    for r in records:
        out.append(
            '    {{ "{name}",\n      "{des}", "{den}",\n      "{desc}",\n      "{descn}",\n'
            "      RiskTier::{tier}, Category::{cat} }},\n".format(
                name=cpp_escape(r["name"]),
                des=cpp_escape(r["display_es"]),
                den=cpp_escape(r["display_en"]),
                desc=cpp_escape(r["desc_es"]),
                descn=cpp_escape(r["desc_en"]),
                tier=r["tier"],
                cat=r["category"],
            )
        )
    out.append("}};\n\n")
    out.append("// Reglas de nivel vital, con el mismo patron `*` que usa el generador.\n")
    out.append("// Se emiten aqui para que C++ y Python compartan una unica fuente.\n")
    out.append("struct VitalRule {\n"
               "  std::string_view pattern;\n"
               "  std::string_view reason;\n"
               "};\n\n")
    out.append("inline constexpr std::array<VitalRule, %d> kVitalRules{{\n" % len(VITAL_RULES))
    for pattern, why in VITAL_RULES:
        out.append('    {{ "{p}", "{w}" }},\n'.format(p=cpp_escape(pattern), w=cpp_escape(why)))
    out.append("}};\n\n")
    out.append("}  // namespace daso::detail\n")
    return "".join(out)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="salir con 1 si el .hpp generado no coincide")
    args = ap.parse_args()

    packages = parse_batch()
    categories = parse_catalog_md()

    records = []
    for name in packages:
        category = categories.get(name, "Other")
        tier, _why = tier_of(name)
        des, den, ds_es, ds_en = describe(name, category)
        records.append({
            "name": name, "display_es": des, "display_en": den,
            "desc_es": ds_es, "desc_en": ds_en,
            "tier": tier, "category": category,
        })

    orphans = [p for p in categories if p not in set(packages)]
    text = render(len(records), records)
    OUT.parent.mkdir(parents=True, exist_ok=True)

    if args.check:
        current = OUT.read_text(encoding="utf-8") if OUT.exists() else ""
        if current != text:
            print(f"DESACTUALIZADO: {OUT}")
            return 1
        print(f"OK: {OUT.name} esta al dia ({len(records)} paquetes)")
        return 0

    OUT.write_text(text, encoding="utf-8")

    by_tier: dict[str, int] = {}
    by_cat: dict[str, int] = {}
    for r in records:
        by_tier[r["tier"]] = by_tier.get(r["tier"], 0) + 1
        by_cat[r["category"]] = by_cat.get(r["category"], 0) + 1
    print(f"Escrito {OUT.relative_to(PROJECT_ROOT).as_posix()} ({len(records)} paquetes)")
    print("  por nivel : " + ", ".join(f"{k}={v}" for k, v in sorted(by_tier.items())))
    print("  por categ.: " + ", ".join(f"{k}={v}" for k, v in sorted(by_cat.items())))
    if orphans:
        print(f"  AVISO: {len(orphans)} del markdown no estan en el batch: {orphans}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
