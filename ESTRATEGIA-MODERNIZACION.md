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
./gchd           -> "Unable to find a supported device.", exit 1  (degrada limpio,
                    sin señal: no hay crash al no encontrar hardware)
```

**Es decir: el proyecto está a un `#include` de compilar y arrancar hoy.** Esto es la
mejor noticia del análisis y cambia el perfil de riesgo de todo el plan.

### 2.2. Portabilidad y salto de estándar: mejor de lo esperado

| Configuración | Resultado |
|---|---|
| GCC 13 + C++11 | OK |
| GCC 13 + C++17 / C++20 / C++23 | **OK, 0 errores** |
| Estándar adoptado | **C++20** (lo exige `std::span`, ver §2.4) |
| Clang 18 + C++17 | **OK, 0 errores** |

No hay deuda que bloquee el salto de estándar. Hay **una** trampa: `utility.cpp` usa
`std::ptr_fun` y `std::not1`, **eliminados en C++17**. Compila sólo porque libstdc++ los
mantiene por compatibilidad; con libc++ estricto o `_GLIBCXX_USE_DEPRECATED=0` rompe.
Son 2 funciones de `trim()`, reemplazo trivial por lambda.

### 2.3. Auditoría de warnings y defectos reales

El `CMakeLists.txt` raíz silencia tres categorías
(`-Wno-unused-parameter -Wno-reorder -Wno-missing-field-initializers`).
Retirándolas, el árbol emite **60 warnings de bajo impacto**: 27 `unused-parameter`,
27 `reorder` (orden de inicialización de miembros), 4 `deprecated-declarations`
(el `ptr_fun` de §2.2) y 2 `unused-but-set-variable`.

**Aviso metodológico.** Una primera pasada de esta auditoría se hizo con
`-fsyntax-only`, que **no activa el optimizador y por tanto no ejecuta los análisis de
flujo de datos** de GCC/Clang. Esa pasada concluyó que no había nada más que ruido
cosmético, y era una conclusión incorrecta. Recompilando en `RelWithDebInfo` con GCC 13
y Clang 18 aparecen **defectos reales**:

El inventario final, tras cerrar la Fase 1, son **20 defectos**, todos corregidos. Los
10 primeros salieron de compilar con optimización; los 10 restantes los destapó el propio
proceso de escribir los tests y de calibrar el análisis estático.

| # | Defecto | Ubicación | Severidad |
|---|---|---|---|
| 1 | Desreferencia de `shared_ptr` nulo. **SIGSEGV reproducido**, ruta viva | `gchd/psi_pmt.cpp:111` | **crítica** |
| 2 | `fopen` sin comprobar: firmware ilegible → `fseek(nullptr)` | `gchd/commands.cpp:526` | **crítica** |
| 3 | `std::modf` con los nombres invertidos: todo nivel H.264 con decimal se degrada | `gchd/settings.cpp:538` | **cierta** |
| 4 | `state` leído sin inicializar: decide si cargar el firmware | `gchd/configure.cpp:44` | **cierta** |
| 5 | 3 registros sin inicializar; `sendEnableState()` los **envía al dispositivo** | `gchd.hpp:52-53,217` | **cierta** |
| 6 | `struct sigaction` sin inicializar: `sa_mask` y `sa_flags` indefinidos | `fifo.cpp:24` | **cierta** |
| 7 | `unpackInternal` no avanza el iterador (2 clases) → parseo roto | `gchd/psi_descriptors.cpp:114,134` | **cierta** |
| 8 | `PSI_UnknownDescriptor` sin `packInternal`: pierde su carga al reempaquetar | `gchd/psi_descriptors.hpp` | **cierta** |
| 9 | `ftell`/`fseek` sin comprobar: carga cero bytes de firmware en silencio | `gchd/commands.cpp:530` | alta |
| 10 | Retorno de `fread()` ignorado al cargar firmware | `gchd/commands.cpp:548` | alta |
| 11 | Destructor no virtual en la jerarquía polimórfica (11 clases) | `gchd/psi_data.hpp` | alta |
| 12 | Constructores por defecto AVC: 21 miembros sin inicializar | `gchd/psi_descriptors.hpp:122,164` | alta |
| 13 | Retorno de `write()` ignorado: pérdida silenciosa de transport stream | `fifo.cpp`, `socket.cpp` | media |
| 14 | Constructor que ignora su parámetro `currentIndicator` | `gchd/psi_syntax.hpp:31` | media |
| 15 | `PSI_Descriptor()` deja `tag_` sin inicializar | `gchd/psi_descriptors.hpp:35` | media |
| 16 | `autodetectResolution` sin inicializar | `gchd/configure_composite.cpp:26` | media |
| 17 | `bitRateMode` sin inicializar | `gchd/transcoder.cpp:328` | media |
| 18 | `ret` sin inicializar | `main.cpp:719` | media |
| 19 | `ES_info_length` sin validar contra el tamaño disponible | `gchd/psi_pmt.cpp:85` | media |
| 20 | Autoasignaciones `x = trim(x)` | `utility.cpp:99,118` | baja |

**Tres hallazgos quedan documentados y SIN tocar, a propósito:**

- `configure.cpp:1304` lee `status` y acto seguido calcula la máscara sobre `value`, no
  sobre `status`, mientras que la estructura paralela siete líneas arriba sí usa
  `status & 0x10`. Huele a copia-pega mal terminada, pero **cambiarlo altera los bytes
  que van al dispositivo** y no hay forma de saber cuál de las dos variantes es la
  correcta sin el aparato. Marcado en el código para resolverlo con el oráculo de la
  Fase 2, que es exactamente para esto.
- `main.cpp:230`: la opción `-P/-pid` se parsea y se guarda, pero `Process::createPid()`
  **no se llama en ningún sitio**. La opción documentada en la ayuda no hace nada, y el
  cerrojo que evita dos instancias simultáneas es código muerto. Conectarlo cambia el
  comportamiento en ejecución: es decisión de producto.
- El `switch` sobre los magic numbers del dispositivo en `configure.cpp:131` no tiene
  `default`. Añadir uno que lance podría romper un dispositivo que hoy funciona.
- Menor: `--help` sale por `stderr`; debería ir a `stdout`.

Tres merecen detalle:

**1. Desreferencia de puntero nulo, no "posible" sino segura** (`psi_pmt.cpp:111`):

```cpp
auto descriptor = std::shared_ptr<PSI_ParseDescriptor>();  // shared_ptr VACÍO
descriptor->unpack(inputData, offset, endOfDescriptors-offset);   // nullptr-> UB
descriptors_.push_back( descriptor->getParsedDescriptor() );
```

Falta `std::make_shared`. GCC lo reporta como `'this' pointer is null`. Cualquier
ejecución que entre en ese bucle desreferencia un nulo. Atenuante: el driver **genera**
tablas MPEG-TS, no las parsea, así que la ruta puede estar muerta en la práctica — pero
está en el árbol, compilada, lista para dispararse si alguien usa el parser.

**2. Todo nivel H.264 con decimal se aplica mal** (`settings.cpp:538`). `std::modf(x, &p)`
devuelve la parte **fraccionaria** y escribe la **entera** en `p`. El código asigna el
retorno a una variable llamada `integerF` y la salida a otra llamada `fractionF`, con los
nombres al revés:

```cpp
integerF = std::modf(value, &fractionF);       // integerF recibe la FRACCION
unsigned integer  = (unsigned) integerF;        // ...y se usa como entero
unsigned fraction = (int) round(fractionF/.1);  // ...y la parte entera como decimal
return integer*10 + fraction;
```

Para niveles enteros el resultado coincide **por casualidad** (4.0 -> 40). Para cualquier
nivel con decimal se degrada en silencio al `.0`: **3.1 -> 30, 4.1 -> 40, 2.2 -> 20**.
Los niveles 2.1, 2.2 y 3.1 están en la lista de válidos de `setH264Level`, así que se
aceptan sin queja y se aplican mal. Verificado con un programa mínimo aparte.

**3. Decisión de cargar firmware sobre memoria sin inicializar** (`configure.cpp`):
`state` se declara sin valor y sólo se asigna dentro de las ramas
`GameCaptureHD` / `GameCaptureHDNew`. Si `deviceType_` no es ninguna de las dos, el
`if (state == 0x0000)` que decide **si se carga el firmware** lee basura de pila. Clang
lo reporta como `used uninitialized whenever 'if' condition is false`.

**Conclusión revisada.** El veredicto de fondo no cambia — no hay gestión manual de
memoria, no es un rescate de código legacy, y el repo era recuperable. Pero **no estaba
"notablemente sano"**: tenía 20 defectos, dos de ellos capaces de provocar un segfault
(uno reproducido), tres en la ruta de carga de firmware y uno que **enviaba memoria sin
inicializar al dispositivo**. Nada de esto requiere hardware para encontrarse: lo
encontró una CI con optimización activada, 55 tests y análisis estático calibrado. Es la
medida más clara del valor de la Fase 1 — y de lo que cuesta diez años sin CI.

**Implicación para el pipeline:** la CI debe compilar en `RelWithDebInfo` o equivalente.
Un job que compile sin optimizar da verde y oculta exactamente esta clase de defectos.

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
- **macOS compila, pero sigue sin estar verificado funcionalmente.** Hallazgo de la
  Fase 0: lo único que impedía compilar en macOS era el include mal escrito
  `<libusb-1.0/libusb.h>` en lugar de `<libusb.h>`. Arreglado eso, **el árbol compila
  limpio con Clang en macOS-latest** y los tests pasan, aun sin una sola guarda
  `__APPLE__` en todo el árbol y usando `getopt_long_only`, `sigaction`, `flock` y
  `unistd.h` (las BSD, y por tanto macOS, proveen `getopt_long_only`). Esto hace el
  soporte macOS mucho más plausible de lo que parecía, pero **compilar no es funcionar**:
  queda sin verificar porque requiere dispositivo. El job de macOS en CI es informativo
  justo por eso.
- **Upstream muerto.** `tolga9009/elgato-gchd` también paró en 2016. No hay a quién
  mandar PRs ni de quién traer fixes: **este fork es tuyo para siempre.** No existe la
  opción "esperar a que upstream lo arregle".
- **Versión hardcodeada** `"0.2.0"` en `process.cpp:21`, sin relación con tags de git.

---

## 5. Plan por fases

Cada fase es entregable por sí sola y deja el repo en mejor estado que la anterior.
El orden no es negociable: **la red de seguridad va antes del refactor.**

### Fase 0 — Desbloquear y congelar línea base · ~1 día · sin hardware · ✅ HECHA
1. ✅ `#include <cstdint>` en `src/gchd/settings.cpp` → el build vuelve a funcionar.
2. ✅ `ptr_fun`/`not1` → lambdas en `utility.cpp` (adelantado desde la Fase 1: era
   requisito para que el job de macOS compilara, porque libc++ sí elimina esas APIs
   en C++17; además la lambda castea a `unsigned char`, que es el uso correcto de
   `std::isspace`).
3. ✅ CMake moderno: mínimo `3.16`, `target_compile_features(cxx_std_17)`,
   `PkgConfig::LIBUSB` como target importado (aporta también los include dirs: el
   árbol anterior sólo funcionaba por tener `/usr/include/libusb-1.0` en la ruta por
   defecto, y fallaba con libusb en otro prefijo, p.ej. Homebrew), link explícito a
   `stdc++` eliminado, `AUX_SOURCE_DIRECTORY` sustituido por lista explícita de
   fuentes, warnings centralizados en un target `INTERFACE`.
4. ✅ GitHub Actions: matriz `ubuntu × {gcc, clang}` bloqueante + `macos` informativo
   (`continue-on-error`), con smoke test del binario y reporte de warnings al job
   summary. Compila en `RelWithDebInfo` a propósito (ver §2.3).
5. ✅ Decisión sobre `src/gui/`: **se conserva.** El equipo quiere que la GUI acabe
   funcionando, así que no se elimina. Pasa a ser un **workstream aparte**, fuera del
   alcance de las Fases 1-4: hoy son 47 LOC de stub de Qt Designer y rehacerla es un
   proyecto propio, que además depende de que el driver esté verificado primero.
   Sigue sin construirse y documentada en el CMake raíz.

**Resultado:** compila con GCC 13 y Clang 18, 0 errores, inmune a CMake 4, CI en verde.
**Subproducto:** la CI con optimización activada destapó los 8 defectos de §2.3.

### Fase 1 — Red de seguridad · ~1-2 semanas · sin hardware · ✅ HECHA

**1. Separación de `gchd_core`.** Librería estática con los 9 archivos que no tocan
hardware (`utility`, `settings` y las 7 clases PSI), para que los tests enlacen sin
libusb ni dispositivo. Si alguien introduce ahí una dependencia de hardware, el target
de tests deja de compilar: ese es el aviso.

**2. 55 tests, 321 asserts, en verde** (Catch2 v3.9.1 vía `FetchContent`), con GCC 13 y
Clang 18, en `RelWithDebInfo` y en `Debug` con ASan+UBSan.

- `utility`: orden de bytes big-endian, truncado por límite, ida y vuelta con valores
  frontera, `findFraction`, `split`, trims, los 7 casos documentados de
  `splitIPAddressAndPort`, y la cota nueva de `span`.
- `settings`: `convertResolution` en ambos sentidos y su inversa, defaults de
  `InputSettings`, aritmética mbps↔kbps de bitrate constante y variable,
  `getRealMaxBitRateKbps` por modo, niveles y perfiles H.264.
- `psi`: PAT, PMT, SIT y los seis tipos de descriptor — byte a byte contra
  ISO/IEC 13818-1, validación de rangos de PID, ida y vuelta pack/unpack, y el test de
  contrato descrito abajo.

**3. El test de contrato tamaño/empaquetado cumplió su función.** Verifica que `pack()`
avanza el iterador **exactamente** `calculateSize()` bytes. Como `PSI_Data::bytes()`
dimensiona el vector con `calculateSize()` y `pack()` escribe avanzando un puntero, una
desincronización de un byte es un desbordamiento de heap. No es teoría: **disparó** y
destapó que `PSI_UnknownDescriptor` declaraba 2+N bytes y sólo escribía 2, perdiendo su
carga en silencio (defecto 8).

**4. Migración a `std::span` completada**, los 36 puntos de llamada. Diseño clave: **el
span acota, no trunca.** El truncado deliberado se sigue expresando con `limit`, de modo
que los bytes producidos son idénticos; lo que cambia es que un buffer corto lanza
`std::out_of_range` en vez de corromper memoria. La equivalencia byte a byte del nuevo
`byteify` se verificó aparte, exhaustivamente sobre todos los valores y límites — esa es
la garantía para la capa de hardware, donde no hay tests. Y la prueba de que la clase de
bug está cerrada es que **un puntero crudo ya no compila** como argumento.

**5. Los 20 defectos de §2.3, corregidos**, cada uno con test de regresión donde la capa
es testeable. Tres quedan documentados y sin tocar a propósito, por la regla de no
cambiar lo que no se puede verificar (ver §2.3).

**6. Warnings a cero y `-Werror`.** Retiradas las tres supresiones heredadas. De los 60
warnings quedaban 49 tras los arreglos; ahora son **0 con `-Wall -Wextra -Werror`** en
GCC y Clang. Dos variables muertas se convirtieron en validación real en vez de
borrarse.

**7. Análisis estático en CI, bloqueante.** `clang-tidy` con un conjunto calibrado
contra este árbol y `cppcheck`. La calibración importa: el conjunto completo daba **453
avisos** (158 de `avoid-endl`, 140 de `const-correctness`), y un gate con 453 avisos no
lo atiende nadie. El conjunto bloqueante es `clang-analyzer-*` y `bugprone-*` sin las
categorías de estilo; los pocos casos que no se tocan llevan `NOLINT` **con su
justificación escrita en el código**, no silenciados a ciegas. Resultado: **0 y 0**.

**Resultado:** ~2.700 LOC bajo test, 0 warnings con `-Werror`, análisis estático limpio
y bloqueante, sanitizers en CI, y 20 defectos corregidos. La CI son 4 jobs: Linux
gcc/clang, ASan+UBSan y análisis estático bloqueantes, macOS informativo.

**Pendiente, deliberadamente fuera:** la GUI (workstream aparte, decisión del equipo) y
los tres hallazgos que requieren el oráculo de la Fase 2 para resolverse.

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
| 0 — Desbloquear | ~1 día | no | mínimo | ✅ |
| 1 — Red de seguridad | 1-2 semanas | no | bajo | ✅ |
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
   que usa este driver. Eliminaría `libusb` + `pkg-config` + la dependencia de POSIX.
   **Atenuante importante, medido en la Fase 0:** este argumento era más fuerte sobre el
   papel que en la realidad. Tras corregir un include de una línea, el árbol en C++
   **ya compila y pasa los tests en macOS**, así que la ventaja de `nusb` se reduce a
   Windows y a no depender de una librería de C, no a "hacer posible macOS".
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

## 8. Anexo — comandos

```bash
# dependencias (Ubuntu/Debian)
sudo apt-get install -y libusb-1.0-0-dev pkg-config cmake clang-tidy cppcheck

# build y tests, como en CI
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DGCHD_WERROR=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure

# sanitizers
cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Debug -DGCHD_SANITIZE=ON
cmake --build build-san --parallel && ctest --test-dir build-san

# analisis estatico (el conjunto bloqueante esta en .clang-tidy)
cmake -S . -B build-tidy -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DGCHD_BUILD_TESTS=OFF
clang-tidy -p build-tidy $(find src -name '*.cpp' -not -path 'src/gui/*')
cppcheck --enable=warning,portability --inline-suppr --std=c++20 \
         --language=c++ --quiet --error-exitcode=1 -I src \
         $(find src -name '*.cpp' -not -path 'src/gui/*')
```

**Opciones de CMake:** `GCHD_BUILD_TESTS` (ON), `GCHD_WERROR` (OFF),
`GCHD_SANITIZE` (OFF).

**Para reproducir el estado original** y ver de dónde se partió:

```bash
git checkout e5bc6b9        # ultimo commit de 2016
mkdir build && cd build && cmake .. && make   # 12 errores en settings.cpp
```
