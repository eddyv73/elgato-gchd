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

### 2.4. Superficie real de seguridad de memoria

Medición explícita, porque es el dato que decide la pregunta "¿reescribir en Rust?".

**Lo que NO hay** (deuda típica de C++ legacy, ausente aquí): 0 `malloc`, 0 `free`,
0 `memcpy`/`memmove`, 0 `strcpy`/`strncpy`/`snprintf`, 0 indexación cruda con índice
calculado, 1 solo `reinterpret_cast` (`disk.cpp:37`, benigno: `vector<uint8_t>` a
`char*` para `ostream::write`). 3 `new` y 1 `delete` en 9.390 LOC.

**Lo que SÍ hay** — un único patrón, sistemático y real:
`Utility::byteify` / `debyteify` (`utility.hpp`) son plantillas que serializan en
big-endian **a través de un puntero/iterador crudo sin cota**:

```cpp
template<typename T, typename I>
static void byteify(I iterator, T value, unsigned limit=UINT_MAX)
```

De **36 call sites, 25 no pasan `limit`** → el límite por defecto es `UINT_MAX` y la
escritura de `sizeof(T)` bytes no está acotada por nada. Los punteros se obtienen como
`&(*offset)`, `&data[0]`, `send+4` — ninguno lleva consigo su tamaño.

Peor: en la capa PSI la corrección depende de un contrato mantenido a mano. Cada clase
implementa `calculateSize()` y `pack()` por separado (ej. `psi_syntax.cpp:15`: devuelve
un `5` literal) y `pack()` escribe avanzando el puntero a mano. **Si `calculateSize()` y
`pack()` se desincronizan en un byte, hay desbordamiento de heap**, en 7 clases PSI.

Esta es una clase de bug genuina, y es exactamente la que Rust elimina por construcción.
**Pero el cierre en C++ es mecánico y lo fuerza el compilador:** cambiar la firma a
`std::span<uint8_t>` obliga a los 36 call sites a aportar una cota, y el compilador los
localiza todos. Añadiendo un `assert` de que `pack()` avanzó exactamente
`calculateSize()` bytes, más los tests de golden file de la Fase 1, la clase queda
cerrada **y verificada sin hardware**, porque la capa PSI es pura.

Coste estimado: días. Es la tarea de mayor retorno de seguridad de todo el plan y entra
en la Fase 1 como prioridad.

### 2.5. Bomba de tiempo a corto plazo

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
2. **Migrar `byteify`/`debyteify` a `std::span`** (ver §2.4). Cierra la clase de bug de
   desbordamiento de heap en los 25 call sites sin cota, con el compilador localizando
   cada uno. Añadir `assert` de que `pack()` avanza exactamente `calculateSize()` bytes.
   Máxima prioridad de la fase.
3. Arreglar los 60 warnings, eliminar las supresiones del CMake, activar `-Werror` en CI.
4. Reemplazar `ptr_fun`/`not1` por lambdas (desbloquea libc++ y C++17 estricto).
5. `clang-tidy` + `cppcheck` + ASan/UBSan sobre el binario de tests en CI.

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

## 7. Decisión de lenguaje: ¿Rust o seguimos en C/C++?

**Recomendación: seguir en C++, y no cerrar la decisión todavía — aún no toca.**

### 7.1. Descartado de entrada: C

Pasar de C++ a C es una regresión. El código ya usa `vector`, `array`, plantillas,
excepciones y RAII; en C habría que reimplementar todo eso a mano y reintroducir
precisamente la gestión manual de memoria que este repositorio **no** tiene. No hay caso.

### 7.2. Lo que Rust compraría de verdad

No es un argumento vacío; hay tres ventajas reales:

1. **La clase de bug de §2.4 desaparece por construcción.** Los slices llevan su longitud;
   escribir fuera de rango es un panic determinista, no corrupción de heap silenciosa.
2. **Multiplataforma real.** `nusb` es USB en Rust puro, sin dependencia de C, con soporte
   Linux / Windows / macOS / Android y transferencias control, bulk e interrupt — justo lo
   que usa este driver. Eliminaría `libusb` + `pkg-config` + `getopt_long_only` (GNU) +
   POSIX a pelo, que es lo que hoy hace que el soporte macOS prometido en el README sea
   ficción (cero guardas de plataforma en todo el árbol).
3. **Cadena de herramientas.** `cargo test` / `clippy` / `cargo build` es una historia
   mucho más amable para un equipo con perfil .NET/devops que CMake + CTest + clang-tidy
   + pkg-config.

### 7.3. Por qué igualmente gana C++

1. **El activo del repositorio no es el código: es el conocimiento de ingeniería inversa.**
   696 LOC de definiciones de registros, secuencias USB y transiciones de máquina de
   estados obtenidas sondeando hardware. Una reescritura transcribe esos valores a mano
   **sin forma de verificar la transcripción**. Un dígito hexadecimal mal copiado no da
   error de compilación: da un dispositivo que no arranca, y sólo se detecta con el
   aparato físico delante.
2. **La ganancia de seguridad se captura al 90% por ~1% del coste.** La migración a
   `std::span` de §2.4 cierra la misma clase de bug en días, y ya verifiqué que el árbol
   compila limpio en C++20, así que `span` está disponible hoy.
3. **Coste desproporcionado.** Reescribir 9.390 LOC equivale a 2-3x el plan completo de
   Fases 0-4, para llegar al **mismo** resultado funcional.
4. **Mal primer proyecto en Rust.** Si el objetivo colateral es que el equipo aprenda
   Rust, un driver USB de ingeniería inversa, sin tests y sin hardware para validar, es
   el peor vehículo posible para hacerlo.

### 7.4. La razón por la que la decisión no es urgente

**Las Fases 0, 1 y 2 son idénticas en ambos escenarios.** Y la Fase 2 produce exactamente
el artefacto que haría responsable una reescritura: el arnés de *record/replay* es un
**oráculo de conformidad**. Con una traza USB real grabada, cualquier reimplementación
—en Rust o en C++ refactorizado— se valida comparando su tráfico USB byte a byte contra
la grabación.

Sin ese oráculo, una reescritura en Rust es código no verificable: cambia bugs de memoria
que el código probablemente no está sufriendo por bugs de transcripción que no puedes
detectar. Con el oráculo, pasa a ser una opción legítima.

Por tanto: **construir el oráculo primero (va en el plan de todos modos) y revisitar la
pregunta al final de la Fase 2**, con datos en lugar de preferencias.

### 7.5. Cuándo sí reescribiría en Rust

Un único escenario lo justifica de salida: **si el objetivo real es soporte multiplataforma
de primera clase** (macOS y Windows, no sólo Linux). Retrofittear guardas de plataforma,
sustituir `getopt_long_only` y abstraer POSIX en el C++ actual es más trabajo y más frágil
que partir de `nusb` + `clap`. Incluso en ese caso: **después del oráculo, no antes.**

Nota sobre reescritura parcial: la capa PSI/MPEG-TS (~1.100 LOC, pura, con salida binaria
determinista) es el único bloque reescribible hoy con verificación completa por golden
files y sin hardware. Aun así no lo haría: convivir con cargo + CMake y una frontera FFI
en un proyecto de 9.4k LOC cuesta más de lo que aporta.

---

## 8. Anexo — comandos de reproducción

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
