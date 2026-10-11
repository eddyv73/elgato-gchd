# Estrategia de modernización — elgato-gchd

**Fecha:** 2026-10-11
**Alcance:** evaluación de viabilidad previa a cualquier cambio de código.
**Veredicto corto:** técnicamente viable y de riesgo bajo en todo lo que no requiere el
dispositivo físico. El único bloqueo real no es el código: es el acceso al hardware.

---

## 1. Qué es este repositorio

Driver de espacio de usuario en C++ para la capturadora **Elgato Game Capture HD**
(hardware de 2012, discontinuado). Habla USB directo vía libusb, carga firmware
propietario en el dispositivo, y emite un **MPEG-TS** a disco, FIFO o socket UDP.

| Dato | Valor |
|---|---|
| LOC | 9.390 (24 `.cpp` + headers) |
| Estándar | C++11 |
| Dependencias | `libusb-1.0`, `pthread` (nada más) |
| Build | CMake, `cmake_minimum_required(2.8.11)` |
| Último commit | **2016-12-23** (50 commits en total) |
| Origen | fork de `tolga9009/elgato-gchd`, sin divergencia local aparente |
| Tests | **ninguno** |
| CI | **ninguna** |
| Licencia | MIT (el código; el firmware **no**) |

---

## 2. Verificación de viabilidad (ejecutada, no estimada)

Compilé el repo tal cual en Ubuntu 24.04 con GCC 13.3 / CMake 3.28 / libusb 1.0.27.

### 2.1. Estado del build: roto, por **una** línea

El build falla con **12 errores**, todos en `src/gchd/settings.cpp`, y todos de la misma
causa raíz: falta `#include <cstdint>`. El archivo usa `uint32_t` y en 2016 lo recibía por
include transitivo de libstdc++; las versiones modernas ya no lo arrastran.

Inyectando el header por flag (`-include cstdint`), **sin tocar ni un archivo**:

```
make = 0 errores,  binario gchd generado (970 KB),  tiempo de build: 3,5 s
./gchd -h        -> funciona
./gchd           -> "Unable to find a supported device."  (degrada limpio sin hardware)
```

**Es decir: el proyecto está a un `#include` de compilar y arrancar hoy.** Esto es la
mejor noticia del análisis y cambia el perfil de riesgo de todo el plan.

### 2.2. Portabilidad y salto de estándar: mejor de lo esperado

| Configuración | Resultado |
|---|---|
| GCC 13 + C++11 | OK |
| GCC 13 + C++17 / C++20 / C++23 | **OK, 0 errores** |
| Clang 18 + C++17 | **OK, 0 errores** |

No hay deuda que bloquee el salto de estándar. Hay **una** trampa: `utility.cpp` usa
`std::ptr_fun` y `std::not1`, **eliminados en C++17**. Compila sólo porque libstdc++ los
mantiene por compatibilidad; con libc++ estricto o `_GLIBCXX_USE_DEPRECATED=0` rompe.
Son 2 funciones de `trim()`, reemplazo trivial por lambda.

### 2.3. Auditoría de warnings

El `CMakeLists.txt` raíz silencia tres categorías
(`-Wno-unused-parameter -Wno-reorder -Wno-missing-field-initializers`).
Recompilando **sin** esas supresiones:

| Categoría | Nº | Severidad |
|---|---|---|
| `-Wunused-parameter` | 27 | cosmética |
| `-Wreorder` | 27 | cosmética (orden de init de miembros) |
| `-Wdeprecated-declarations` | 4 | la de `ptr_fun` de arriba |
| `-Wunused-but-set-variable` | 2 | cosmética |
| **Total** | **60** | — |

Cero warnings de UB, de `sign-compare`, de punteros o de aliasing. Para ser código de
ingeniería inversa de 2016 escrito sin CI, **está notablemente sano**: `std::vector`,
`std::array`, excepciones, RAII, `nullptr`. Sin `malloc`, sin `strcpy`, sin `goto`,
3 `new` en total. **No es un rescate de código legacy; es un repo sano y abandonado.**

### 2.4. Bomba de tiempo a corto plazo

`cmake_minimum_required(VERSION 2.8.11)` ya emite deprecation warning en CMake 3.28 y es
**error fatal en CMake 4.x**, que eliminó la compatibilidad por debajo de 3.5. En cuanto
los runners de CI o las imágenes base salten a CMake 4, el repo deja de configurar.
Esto solo ya justifica la Fase 0.

---

## 3. Arquitectura: dónde se puede trabajar sin hardware

Mapeé el acoplamiento a `libusb`/`gchd.hpp` archivo por archivo. El reparto es
inusualmente favorable:

**Capa pura — testeable hoy, sin dispositivo (~2.700 LOC):**
`psi_*.cpp` (7 archivos, tablas PAT/PMT/SIT/descriptores MPEG-TS, ~1.100 LOC),
`settings.cpp` (674), `utility.cpp` (151), `socket.cpp`, `fifo.cpp`, `disk.cpp`,
`streamer.cpp`, `process.cpp`.

**Capa acoplada a hardware (~3.700 LOC):**
`gchd.cpp`, `gchd/commands.cpp`, `gchd/configure*.cpp` (4 archivos), `gchd/transcoder.cpp`.

**Datos inmutables:** `gchd_hardware.hpp` (696 LOC) son definiciones de registros obtenidas
por ingeniería inversa. Es tabla de datos, no lógica. **No se refactoriza, se congela.**

**GUI:** `src/gui/` son **47 LOC** — un stub vacío de Qt Designer, ya desactivado
(comentado) en el CMake raíz. El README lo admite: *"not usable, work in progress"*.
Es peso muerto: borrar o declararlo fuera de alcance, no "modernizar".

El propio autor dejó el diagnóstico en `gchd.hpp:36`:

> `//TODO this is too complicated, needs to be factored into multiple classes.`

La clase `GCHD` tiene 60+ métodos privados, un `friend class`, y mezcla transporte USB,
máquina de estados, configuración por tipo de entrada y transcoder. Es el objetivo central
de cualquier refactor estructural — y el que **no se puede tocar a ciegas**.

---

## 4. Los límites reales de viabilidad

Esto es lo que hay que decidir **antes** de escribir código, no después.

### 4.1. Bloqueo nº1 — Validación funcional exige el dispositivo físico

Ninguna CI puede probar que este driver *funciona*. Puede probar que compila, que la lógica
pura es correcta, y (con la Fase 2) que una sesión grabada se reproduce igual. Pero
"captura vídeo correctamente" requiere una GCHD real con señal real de entrada.

**Esta es la pregunta go/no-go: ¿alguien del equipo tiene una Elgato Game Capture HD de
primera generación?** Hardware de 2012, discontinuado, con 4 product IDs soportados
(`0x0044`, `0x004e`, `0x0051`, `0x005d`). Las HD60 / HD60 S / HD60 Pro **no** están
soportadas y usan otra interfaz de software. Sin dispositivo, el resultado del proyecto es
un binario mantenible que nadie puede verificar.

### 4.2. Bloqueo nº2 — El firmware no es redistribuible

El dispositivo no arranca sin firmware propietario de Fujitsu/Elgato
(`MB86H57_H58_IDLE`, `MB86H57_H58_ENC_H`, y variantes `MB86M01_*` para la revisión nueva).
No está en el repo por licencia, y el usuario debe extraerlo del MSI de Windows
(`firmware_extract/extract_firmware_windows`, bash + 7z, **sin verificación de checksum**)
o del driver de macOS.

Consecuencias: no se puede publicar un paquete funcional, no hay end-to-end en CI sin
subir binarios propietarios a la infra, y cualquier pipeline de release debe documentar el
paso manual. Es una restricción **legal**, no técnica: no se resuelve con ingeniería.

### 4.3. Riesgos secundarios

- **Semántica desconocida.** Los registros son ingeniería inversa. Un refactor que
  "limpie" lo que no entiende rompe el dispositivo de formas no detectables sin hardware.
  Regla: cambiar estructura, nunca valores ni orden de las secuencias USB.
- **Soporte macOS no verificado.** El README promete macOS, pero hay **cero** guardas
  `__APPLE__`/`#ifdef` de plataforma y el código es POSIX puro
  (`getopt_long_only` GNU, `sigaction`, `flock`, `unistd.h`). Trátalo como *feature nueva*,
  no como regresión a arreglar.
- **Upstream muerto.** `tolga9009/elgato-gchd` también paró en 2016. No hay a quién
  mandar PRs ni de quién traer fixes: **este fork es tuyo para siempre.** No existe la
  opción "esperar a que upstream lo arregle".
- **Versión hardcodeada** `"0.2.0"` en `process.cpp:21`, sin relación con tags de git.

---

## 5. Plan por fases

Cada fase es entregable por sí sola y deja el repo en mejor estado que la anterior.
El orden no es negociable: **la red de seguridad va antes del refactor.**

### Fase 0 — Desbloquear y congelar línea base · ~1 día · sin hardware
1. `#include <cstdint>` en `src/gchd/settings.cpp` → el build vuelve a funcionar.
2. CMake moderno: mínimo `3.16`, `target_compile_features(cxx_std_17)`,
   `PkgConfig::LIBUSB` como target importado, quitar el link explícito a `stdc++`,
   sustituir `AUX_SOURCE_DIRECTORY` por lista explícita de fuentes (el globbing no
   detecta archivos nuevos y rompe builds incrementales).
3. GitHub Actions: matriz `ubuntu × {gcc, clang}` + `macos`, sólo build. El build dura
   3,5 s — la CI es prácticamente gratis.
4. Decisión explícita sobre `src/gui/` (recomendación: borrar).

**Resultado:** repo que compila, verde en CI, inmune a CMake 4. Riesgo ~nulo.

### Fase 1 — Red de seguridad · ~1-2 semanas · sin hardware
1. Tests unitarios sobre la capa pura (GoogleTest o Catch2 vía `FetchContent`):
   - `psi_*`: tests de golden file — las tablas MPEG-TS tienen salida binaria
     determinista y verificable contra la norma ITU/ISO. **El mejor ROI del proyecto.**
   - `settings.cpp`: parseo de resolución, bitrate (`max:avg:min`), niveles H.264.
   - `utility.cpp`: `byteify`/`debyteify` big-endian, trims.
2. Arreglar los 60 warnings, eliminar las supresiones del CMake, activar `-Werror` en CI.
3. Reemplazar `ptr_fun`/`not1` por lambdas (desbloquea libc++ y C++17 estricto).
4. `clang-tidy` + `cppcheck` + ASan/UBSan sobre el binario de tests en CI.

**Resultado:** ~2.700 LOC bajo test sin tocar el hardware, análisis estático en el pipeline.

### Fase 2 — Testabilidad del hardware · ~2 semanas · ⚠ REQUIERE EL DISPOSITIVO
Esta fase es la **puerta go/no-go** del proyecto.

1. Extraer una interfaz `UsbTransport`. La costura ya existe y es limpia:
   `read_config` / `write_config` / `scmd` / `sparam` / `mailRead` / `mailWrite` son el
   cuello de botella por el que pasa *todo* el tráfico USB.
2. Tres implementaciones: `LibUsbTransport` (real), `RecordingTransport` (graba la sesión),
   `ReplayTransport` (la reproduce).
3. **Grabar una vez** una sesión real completa con el dispositivo (init + carga de firmware
   + configuración HDMI/componente/compuesto + streaming) y versionar la traza.
4. La CI reproduce la traza y verifica que el refactor emite la **misma** secuencia USB.
   Alternativa si se prefiere no tocar el código: `umockdev`, que graba y reproduce
   dispositivos libusb a nivel de ioctl.

**Resultado:** se puede refactorizar la capa de hardware con verificación objetiva.
**Sin esta fase, el refactor de la Fase 3 es a ciegas y no lo recomiendo.**

### Fase 3 — Refactor estructural · ~3-4 semanas · apoyada en Fase 2
1. Descomponer la god-class `GCHD` según el TODO del propio autor:
   `UsbTransport` / `DeviceCommands` / `FirmwareLoader` / `InputConfigurator`
   (HDMI, componente, compuesto) / `Transcoder` / `StateMachine`.
2. Sustituir `getopt_long_only` por CLI11 (header-only) — elimina la dependencia GNU de
   `main.cpp` y hace realista el soporte macOS/BSD.
3. Unificar el manejo de errores: hoy convive `int`-de-retorno (`checkDevice`, `init`)
   con excepciones (`usb_error` en los templates). Elegir excepciones y ser consistente.
4. C++17/20 donde aporte: `std::span` para los buffers de transferencia, `string_view`,
   `enum class` en lugar de `#define`, `constexpr` para las tablas de registros.

### Fase 4 — Distribución y operación · ~1 semana · sin hardware
1. Empaquetado `.deb` / AUR / Nix, con target de instalación de las reglas udev
   (hoy el usuario las copia a mano).
2. Endurecer `extract_firmware_windows`: verificación de checksum, mensajes de error
   claros, y añadir la ruta de extracción desde macOS (hoy sólo existe la de Windows).
3. Versionado desde tags de git (elimina el `"0.2.0"` hardcodeado) + pipeline de release
   con artefactos.

---

## 6. Esfuerzo y recomendación

| Fase | Esfuerzo (1 dev) | Hardware | Riesgo |
|---|---|---|---|
| 0 — Desbloquear | ~1 día | no | mínimo |
| 1 — Red de seguridad | 1-2 semanas | no | bajo |
| 2 — Testabilidad HW | ~2 semanas | **sí** | medio |
| 3 — Refactor | 3-4 semanas | sí (validación) | alto sin Fase 2 |
| 4 — Distribución | ~1 semana | no | bajo |
| **Total** | **~7-9 semanas** | | |

**Recomendación:**

1. **Ejecutar Fases 0 y 1 ya.** Son 2-3 semanas, sin hardware, sin riesgo, y dejan el repo
   compilando, con CI verde, tests sobre un tercio del código y análisis estático. Tienen
   valor incluso si el proyecto se detiene ahí.
2. **Tratar la Fase 2 como gate formal de decisión.** Antes de comprometer las Fases 2-4,
   confirmar acceso a una Game Capture HD de primera generación y al firmware extraído.
3. **Si no hay dispositivo: parar al terminar la Fase 1** y declarar el repo como
   "compila, está testeado en su lógica pura y es mantenible" — que es un entregable
   honesto. No abordar el refactor de los 3.700 LOC de capa USB sin forma de verificarlo:
   produciría código más bonito con regresiones indetectables, que es peor que no tocarlo.

---

## 7. Anexo — comandos de reproducción

```bash
# dependencia (Ubuntu/Debian)
apt-get install -y libusb-1.0-0-dev

# reproducir el fallo actual (12 errores en settings.cpp)
mkdir build && cd build && cmake .. && make

# demostrar que es el único bloqueo, sin modificar archivos
cmake .. -DCMAKE_CXX_FLAGS="-std=c++11 -include cstdint" && make   # -> 0 errores

# auditoría de warnings sin las supresiones del CMakeLists
g++ -std=c++17 -include cstdint -Isrc -Wall -Wextra -fsyntax-only \
    $(find src -path src/gui -prune -o -name '*.cpp' -print)       # -> 60 warnings
```
