#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Comprueba que el proyecto de MSBuild y el instalador no apuntan a la nada.

Un <ClCompile Include> que no existe hace fallar el build de CI con un error
poco descriptivo; un .cpp en disco que el proyecto no compila se pierde en
silencio. Este script detecta las dos cosas antes de subir nada.

Uso:
    python tools/check_project.py
Devuelve 1 si encuentra algo roto.
"""

from __future__ import annotations

import pathlib
import re
import sys

PROJECT = pathlib.Path(__file__).resolve().parents[1]
REPO = PROJECT.parent
VCXPROJ = PROJECT / "daso.vcxproj"
ISS = PROJECT / "installer" / "daso.iss"

problems: list[str] = []


def check_sources() -> int:
    text = VCXPROJ.read_text(encoding="utf-8")
    listed = [m.group(1).replace(chr(92), "/")
              for m in re.finditer(r'<Cl(?:Compile|Include) Include="([^"]+)"', text)]

    for rel in listed:
        if not (PROJECT / rel).exists():
            problems.append(f"vcxproj lista un fichero inexistente: {rel}")

    on_disk = set()
    for p in (PROJECT / "src").rglob("*"):
        if p.suffix in (".cpp", ".hpp") and "tests" not in p.parts:
            on_disk.add(str(p.relative_to(PROJECT)).replace(chr(92), "/"))

    for rel in sorted(on_disk - set(listed)):
        problems.append(f"fuente en disco que el proyecto NO compila: {rel}")

    print(f"  vcxproj: {len(listed)} ficheros referenciados, "
          f"{len(on_disk)} fuentes de aplicacion en disco")
    return len(listed)


def check_installer() -> None:
    text = ISS.read_text(encoding="utf-8")

    # El codigo Pascal de [Code] contiene constantes {#...} de ISCC que se
    # resuelven al compilar el instalador, no rutas de disco. Solo interesan las
    # directivas de la seccion [Files] y las de configuracion.
    head = text.split("[Code]", 1)[0]
    for m in re.finditer(r'(?:SetupIconFile|Source):\s*"([^"]+)"', head):
        raw = m.group(1)
        if "*" in raw or "{#" in raw:
            continue  # comodin o constante de preprocesador
        candidate = (ISS.parent / raw).resolve()
        if not candidate.exists():
            problems.append(
                f"el instalador referencia un fichero inexistente: {raw}")


def check_workflow() -> None:
    workflow = REPO / ".github" / "workflows" / "windows.yml"
    if not workflow.exists():
        problems.append("no existe .github/workflows/windows.yml en la raiz del repo")
        return
    text = workflow.read_text(encoding="utf-8")
    for job in ("core:", "app:", "installer:", "release:"):
        if job not in text:
            problems.append(f"el workflow no define el job '{job}'")
    if "\t" in text:
        problems.append("el workflow contiene tabuladores (YAML no los admite)")


def main() -> int:
    print("Comprobando el proyecto:")
    check_sources()
    check_installer()
    check_workflow()

    if problems:
        print("\nProblemas encontrados:")
        for p in problems:
            print(f"  - {p}")
        return 1
    print("\nTodo coherente: proyecto, instalador y workflow.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
