# Prompt de continuación — modernización de elgato-gchd

> Pega todo lo que hay debajo de la línea en una sesión nueva de Claude Code,
> con el repo `eddyv73/elgato-gchd` disponible.

---

Trabajas en `eddyv73/elgato-gchd`: un driver de espacio de usuario en C++ para la
capturadora **Elgato Game Capture HD**. Habla USB directo vía libusb, carga firmware
propietario en el dispositivo y emite MPEG-TS a disco, FIFO o socket UDP.

## Lee esto primero

`ESTRATEGIA-MODERNIZACION.md` en la raíz del repo. Es el plan completo en 8 secciones:
viabilidad verificada, inventario de los 20 defectos corregidos, arquitectura, los
límites reales, el plan por fases, la decisión de lenguaje y el anexo de comandos. **No
re-derives lo que ya está ahí.** La rama de trabajo es `docs/estrategia-modernizacion`.

## Estado actual

Las **Fases 0 y 1 están cerradas** y la CI está verde en 5 jobs:

- Compila con GCC 13 y Clang 18, **C++20**, **0 warnings con `-Wall -Wextra -Werror`**.
- **55 tests / 321 asserts** (Catch2 vía `FetchContent`), verdes también bajo ASan+UBSan.
- `clang-tidy` y `cppcheck` **limpios y bloqueantes** en CI.
- **20 defectos corregidos**, entre ellos un SIGSEGV reproducido, dos rutas que dejaban
  el firmware sin cargar en silencio, y un registro sin inicializar que se enviaba por
  USB al dispositivo.
- `gchd_core` es una librería estática con los 9 archivos que **no** tocan hardware
  (`utility`, `settings`, las 7 clases PSI). Los tests enlazan contra ella sin libusb.

## Verifica antes de tocar nada

```bash
sudo apt-get install -y libusb-1.0-0-dev pkg-config cmake clang-tidy cppcheck
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DGCHD_WERROR=ON
cmake --build build --parallel && ctest --test-dir build --output-on-failure
```

Debe dar 0 warnings y 55/55. Si no, **para y averigua por qué antes de seguir**.

Opciones de CMake: `GCHD_BUILD_TESTS` (ON), `GCHD_WERROR` (OFF), `GCHD_SANITIZE` (OFF).

## Las dos reglas que no se pueden romper en este repo

**1. El activo de este repositorio no es el código: es el conocimiento de ingeniería
inversa.** `src/gchd_hardware.hpp` son 696 LOC de direcciones de registro y secuencias
USB obtenidas sondeando hardware. **Son datos, no lógica: se congelan.** Puedes cambiar
estructura; nunca valores, ni el orden de las secuencias USB, ni lo que se envía al
dispositivo. Un dígito hexadecimal mal copiado no da error de compilación: da un
dispositivo que no arranca, y solo se detecta con el aparato delante.

**2. No hay dispositivo físico, así que la validación funcional es imposible.** La CI
puede probar que compila, que la lógica pura es correcta y (cuando exista el arnés) que
una traza grabada se reproduce igual. **No** puede probar que captura vídeo. Si una
tarea exige verificar comportamiento en hardware, **no la hagas: documéntala**.

Corolarios operativos:

- **Tests antes del arreglo**, siempre que la capa sea testeable. Si no lo es (capa USB),
  arregla por inspección, explica el razonamiento y dilo claramente en el commit.
- **Nunca silencies un warning para que pase la CI.** Si hay que suprimir algo, va con
  `NOLINT` y **la justificación escrita al lado**, en el código.
- **No amplíes el alcance.** Si encuentras un defecto fuera de la tarea, apúntalo en el
  documento y sigue.
- Hay tres hallazgos **documentados y sin tocar a propósito** (ver §2.3 del documento).
  No los "arregles" sin leer por qué están así.

## Trabajo pendiente — Vía A: sin hardware (haz esto ahora)

En orden de valor:

### A1. Poner bajo test el parseo de la línea de comandos

`src/main.cpp` son 751 LOC, en su mayoría parseo de argumentos con `getopt_long_only`, y
es **el mayor bloque de lógica pura que sigue sin red**. Extrae el parseo a una unidad
testeable (que devuelva `InputSettings` + `TranscoderSettings` + formato + destino a
partir de un `argv`), muévela a `gchd_core` y cúbrela con tests: resoluciones, bitrate
en formato `max:avg:min`, niveles H.264, combinaciones inválidas, valores frontera.

Mantén el comportamiento observable idéntico, incluida la salida de error. Es refactor,
no rediseño.

### A2. Montar el arnés de record/replay, sin el dispositivo

Es la **Fase 2** del documento y el desbloqueo de todo lo demás. La parte que necesita
hardware es solo *hacer* la grabación; todo lo anterior se puede construir y testear hoy:

1. Extrae una interfaz `UsbTransport`. La costura ya existe y es limpia:
   `read_config` / `write_config` / `scmd` / `sparam` / `mailRead` / `mailWrite` son el
   cuello de botella por el que pasa **todo** el tráfico USB (ver `src/gchd.hpp`).
2. `LibUsbTransport`: envoltorio fino sobre lo que ya hay. Cambio mecánico y revisable,
   sin alterar ni un byte de lo que se envía.
3. `RecordingTransport` y `ReplayTransport`: **código nuevo**, así que es testeable al
   100% con trazas sintéticas. Define el formato de traza (sugerencia: JSON Lines con
   dirección, `bRequest`, `wValue`, `wIndex`, payload en hex y marca de tiempo) y
   escribe tests que graben y reproduzcan una secuencia inventada, incluyendo los casos
   de divergencia: traza más corta, llamada inesperada, payload distinto.
4. Documenta el procedimiento de grabación en un `docs/` para que la sesión con
   dispositivo sea **un comando**, no una investigación.

Objetivo: cuando aparezca el aparato, grabar la traza real y tener oráculo de
conformidad el mismo día.

### A3. Fase 4 — distribución

- La versión está **hardcodeada** como `"0.2.0"` en `src/process.cpp:20`. Derívala de
  los tags de git.
- Target de instalación para `udev-rules/55-elgato-game-capture.rules` (hoy el usuario
  las copia a mano).
- Endurece `firmware_extract/extract_firmware_windows`: es bash + 7z **sin verificación
  de checksum** ni mensajes de error claros. Añade además la ruta de extracción desde
  macOS, que hoy no existe en el repo.
- Empaquetado `.deb` y pipeline de release con artefactos.

**Restricción legal, no técnica:** el firmware es propietario y **no redistribuible**.
Nunca lo metas en el repo, en una imagen, ni en un artefacto de CI. El paquete debe
documentar el paso manual de extracción.

### A4. Menores

- `--help` sale por `stderr`; debería ir a `stdout`.
- `getopt_long_only` es extensión GNU. Sustituirlo por CLI11 (header-only) hace realista
  el soporte macOS/BSD. Encaja de forma natural con A1.

## Trabajo pendiente — Vía B: solo con el dispositivo físico

No empieces nada de esto sin confirmar que hay una **Game Capture HD de primera
generación** disponible (product IDs `0x0044`, `0x004e`, `0x0051`, `0x005d`; las
HD60 / HD60 S / HD60 Pro **no** están soportadas y usan otra interfaz).

1. Grabar la traza USB real completa: init, carga de firmware, configuración
   HDMI/componente/compuesto y streaming.
2. Resolver el hallazgo marcado en `src/gchd/configure.cpp:1304`, donde la máscara se
   calcula sobre `value` y no sobre el `status` que se acaba de leer, mientras la
   estructura paralela siete líneas arriba sí usa `status`. Está marcado con `NOLINT` y
   un comentario. Se resuelve comparando la secuencia USB real contra la grabación.
3. Fase 3: descomponer la god-class `GCHD` (60+ métodos privados, un `friend class`, y
   mezcla transporte USB, máquina de estados, configuración por entrada y transcoder).
   El propio autor dejó el diagnóstico en `src/gchd.hpp:34`. **Solo con el oráculo de la
   Fase 2 funcionando.**

## Fuera de alcance

- **La GUI.** `src/gui/` son 47 LOC de stub vacío de Qt Designer, sin construir y
  comentado en el CMake raíz. El equipo quiere que acabe funcionando, pero es un
  **workstream aparte** y depende de que el driver esté verificado primero. No la
  elimines ni intentes rehacerla.
- **La opción `-P/-pid`.** Se parsea y se guarda pero `Process::createPid()` no se llama
  nunca: la opción documentada no hace nada y el cerrojo contra dos instancias es código
  muerto. Conectarlo cambia el comportamiento en ejecución, así que es decisión de
  producto. Está marcado en `src/main.cpp:230`. **Pregunta antes de activarlo.**

## Cómo entregar

- Rama `docs/estrategia-modernizacion`. Commits pequeños y temáticos, con cuerpo que
  explique **por qué**, no solo qué. Mira `git log` para el estilo.
- **No abras un pull request** salvo que te lo pidan.
- Antes de cada push: build con `-Werror`, los 55+ tests, sanitizers, clang-tidy y
  cppcheck. Todo en verde. Una push validada vale más que tres especulativas.
- Actualiza `ESTRATEGIA-MODERNIZACION.md` cuando cambie el estado de una fase o
  aparezca un defecto nuevo. El documento es el entregable que lee el equipo.
- Upstream (`tolga9009/elgato-gchd`) está **muerto desde 2016**: no hay a quién mandar
  PRs ni de quién traer fixes. Este fork es propio para siempre.

## Si algo te bloquea

Di qué te bloquea, qué asumiste y qué dejaste sin hacer. No inventes resultados de
hardware que no tienes, y no declares verificado lo que solo compila.
