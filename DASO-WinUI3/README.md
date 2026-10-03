# D.A.S.O Nativo — WinUI 3 + C++26

Reescritura nativa del debloater de D.A.S.O: **WinUI 3 sobre Win32**, todo en
C++26, sin dependencias externas y sin pedir permisos de administrador.

---

## Qué mejora sobre la versión anterior

`debloat_gui.py` (Tkinter) lanzaba **un proceso de adb por paquete**: 145
paquetes ≈ 145 viajes, del orden de minuto y medio.

| | Antes | Ahora |
|---|---|---|
| Viajes a adb | 1 por paquete (145) | 1 por lote (2–3) |
| Revertir | No existía | Manifiesto previo + «Revertir último cambio» |
| Paquete que falla | Todo el lote a medias | Bisección: aísla el culpable |
| Paquetes críticos | Sin protección | Bloqueados por patrón |
| Idiomas | Uno | Español e inglés |
| Estado real del móvil | Desconocido | `pm list packages -d` + `packages` |

---

## Arquitectura

```
src/core/      ISO C++ puro. Sin Windows SDK. Compila y se prueba en cualquier sitio.
src/platform/  Win32: CreateProcessW, IOCP, pipes solapados, manifiestos en disco.
src/ui/        WinUI 3 SIN /ZW y SIN XAML: el árbol visual se construye en C++.
src/app/       Punto de entrada y arranque del runtime.
tools/         Generador del catálogo.
installer/     Script de Inno Setup.
```

La separación no es estética: permite que la mayor parte de la lógica se
verifique sin Visual Studio y sin un móvil conectado.

---

## Compilar y probar

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Sin CMake:

```bash
g++ -std=c++26 -O2 -Isrc/core/include -Isrc/core/tests \
    src/core/src/*.cpp src/core/tests/*.cpp -o tests && ./tests
```

Compilar la aplicación completa necesita **Visual Studio 2022** con la carga de
trabajo «Desarrollo de aplicaciones WinUI» y Windows SDK:

```bash
msbuild daso.sln -p:Configuration=Release -p:Platform=x64
```

Deja `x64\Release\daso.exe` autocontenido: una carpeta que se copia y funciona,
sin instalador de runtime y sin firma MSIX.

---

## Instalar (Inno Setup)

```bash
ISCC.exe installer\daso.iss
```

O indicando otra carpeta de publicación:

```bash
ISCC.exe /DPublishDir="x64\Release" installer\daso.iss
```

Salida: `installer\output\D.A.S.O-1.0.0-setup.exe`.

El script **empaqueta, no compila**. Si el ejecutable no está, falla con un
mensaje que dice exactamente qué compilar antes.

---

## Por qué C++26 y a la vez WinUI 3

WinUI 3 exige C++/WinRT, y C++/WinRT (`/ZW`) llega hasta C++17: son
incompatibles en la misma unidad de traducción.

La solución adoptada:

- **Ningún fichero XAML.** El compilador de XAML nunca se invoca, así que nunca
  genera código que exija `/ZW`. Un objetivo de MSBuild comprueba esto y falla
  si alguien añade un `.xaml`.
- **El árbol visual se construye en C++** con las clases controlables de WinUI 3.
- **Eventos en código** (`Button::Click` con lambda), no atributos `Click="..."`.
- **`INotifyPropertyChanged`** implementado con `winrt::implements`, que es C++
  puro y no necesita `/ZW`.

Coste: no hay vista de diseño de Visual Studio.

---

## Estado real

### Verificado ejecutando

| Qué | Con | Resultado |
|---|---|---|
| Núcleo + Win32 + manifiesto | GCC 16.2, `-std=c++26`, `-Wall -Wextra` | compila, **0 avisos** |
| Suite completa | 83 casos | **6615 comprobaciones, 0 fallos** |
| Solo núcleo | `-std=c++23` | idéntico |
| Catálogo | `--check` | 144 paquetes al día |

### Sin verificar

- **La capa WinUI 3** no se ha compilado: esta máquina no tiene el Windows App
  SDK. El primer build en CI es la prueba de fuego. Si `/std:c++26preview` y
  C++/WinRT en modo solo-cabecero no congenian, el plan B es bajar la capa UI a
  C++23 dejando el núcleo en C++26; el diseño ya lo permite.
- **El `.iss`** no se ha ejecutado: requiere Inno Setup 6, que no está instalado.
- **CMake** no está instalado aquí, así que los `CMakeLists.txt` están escritos
  pero sin ejecutar; CI los valida en el primer push.

---

## Sobre el catálogo

`Catalogo_Paquetes.md` dice 145 paquetes únicos. **Son 144** (la sección «Otros»
declara 4 y lista 3). El generador sigue los datos, no la documentación.

```bash
python tools/generate_catalog.py          # regenerar
python tools/generate_catalog.py --check  # fallar si está desactualizado
```

---

## Publicar en Releases

El flujo está en `.github/workflows/windows.yml`, en tres jobs:

1. `core` — valida el catálogo, compila el núcleo y pasa las pruebas. Rápido.
2. `app` — compila la aplicación WinUI y sube el ejecutable como artefacto.
3. `installer` — compila el `.iss` contra ese artefacto.

Falta un job de publicación que cree la Release y adjunte el instalador; se
puede añadir cuando haya una etiqueta (`v*`) y credenciales de GitHub en el
repositorio.
