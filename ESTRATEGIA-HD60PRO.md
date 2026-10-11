# Estrategia para la Elgato Game Capture HD60 Pro

**Fecha:** 2026-10-11
**Nota:** este documento trata de **otro código base**, no de este repositorio.
Vive aquí porque es la continuación de la misma evaluación: ver §4.0 de
`ESTRATEGIA-MODERNIZACION.md`, donde el gate go/no-go de elgato-gchd se resuelve en
no-go porque el hardware disponible es una HD60 Pro.

**Veredicto corto:** viable, y con un perfil de riesgo **mucho mejor** que elgato-gchd,
por una razón que lo cambia todo: **tenéis el hardware**. La validación es posible desde
el primer día.

---

## 1. El objeto: `Wolffyx/hd60pro-linux-driver` (módulo `mz0380`)

Módulo de kernel out-of-tree, GPL-2.0, que expone la tarjeta como dispositivo **V4L2**.
Es el único proyecto que existe para este hardware. Medido sobre el clon, no leído del
README:

| Dato | Valor |
|---|---|
| LOC | **21.039** (18.157 `.c` + 2.882 `.h`) |
| Archivos fuente | 30+, bien descompuestos: `pci`, `mailbox`, `dma`, `mst3367`, `video`, `vb2`, `controls`, `proc-debug` |
| Commits | 158 — 1 en abril, 21 en julio, **120 en agosto**, 16 en septiembre de 2026 |
| Último commit | **2026-09-27** (hace 2 semanas) |
| Autores | **1** (`wolffyx`) |
| Licencia | GPL-2.0-or-later. `MODULE_LICENSE("GPL")`, versión `0.1.0` |
| CI | **ninguna** (no hay `.github/`) |
| Tests automatizados | **ninguno**. 65 scripts de prueba manual con hardware en `scripts/` |
| `module_param` | **148** |
| Logging | **186 `pr_info`**, 50 `pr_warn`, 28 `pr_err` · solo 3 `dev_dbg` |
| Kernels validados | 6.18 LTS y 7.2, con clang/lld y `W=1` (según su README) |
| Issues abiertos | 2, **ambos del tipo "no es un issue, funciona"** |

Hardware: Elgato Game Capture HD60 Pro = **YUAN MZ0380**. PCI `12ab:0380` / `12ab:0381`,
subsistemas `1cfa:0003/0005/0006/0010` y `12ab:05cf`.

**Confirmación independiente.** Los dos issues (4 de octubre de 2026) son de usuarios
distintos del autor; uno dice haberlo probado a fondo y que funciona. Son n=2, pero
responden la pregunta más barata y más importante: el driver **funciona para alguien que
no es quien lo escribió**.

---

## 2. Corrección: el "wedge del mailbox" no es el bloqueador que parecía

En la evaluación anterior lo califiqué de bloqueador para cualquier uso serio. **Eso era
leer el README, no el código. Es incorrecto.**

El mecanismo real, leído en el árbol:

- El wedge va por **spawns de encoder**. Cada `SET_VIC` forca un proceso `tinyvenc5`
  nuevo en el SoC ARM de la tarjeta. Tras unos **8-18 spawns la tarjeta se atasca para
  siempre**, y **ningún reset PCIe lo arregla** (experimento M52): solo quitar
  alimentación del slot.
- `encoder_spawns++` aparece en **un único sitio** del driver:
  `src/mz0380-dma-stream.c:428`, en el arranque de stream. No hay otra fuente de spawns.
- Y la arquitectura actual es `persistent_h264 = true`, declarado `0444` — **solo
  lectura: no es un flag, es el diseño.** Su comentario lo dice literalmente:

  > *"The real no-power-cycle lifecycle. SET_VIC owns a scarce card-side process,
  > whereas STREAMOFF only means the current userspace consumer went away. Keep the
  > process, DMA mappings, completion FIFO and enc_stat ACK path alive until module
  > removal; a later STREAMON attaches without sending any card command."*

**Conclusión:** el presupuesto de 8-18 se consume **por carga de módulo**, no por sesión
de captura. Cargas el módulo una vez al arrancar y puedes abrir y cerrar OBS sin límite.
El techo muerde en dos escenarios: **durante el desarrollo** (donde se recarga el módulo
constantemente) y en **cambios de modo** que fuercen reconfiguración.

Sigue siendo un defecto de firmware de la tarjeta que hay que respetar y presupuestar,
pero no impide el uso normal. El driver incluso avisa por log al llegar al spawn 8.

Los parámetros `setvic_once` y `stop_on_streamoff` son experimentos de la época
(M155/M156) que `persistent_h264` ya dejó atrás, y por eso vienen desactivados.

---

## 3. El activo que de verdad importa

`RE_FINDINGS.md` son **15.429 líneas, 608 secciones y 227 experimentos numerados**
(M0–M227). Y no es un cuaderno de notas: el mapa de registros está extraído del **driver
de Windows `e60MZ0380.X64.SYS`**, con direcciones de funciones decompiladas
(`FUN_140285074` = `MZ0380_SEND_COMMAND`, `FUN_1402762d4` = `MZ0380_DownloadFirmware`),
y cruzado contra hardware real — hay secciones marcadas `VERIFIED ON HARDWARE`.

Además `MZ0380_SDK_CONTROL_PATH.md` mina el **SDK de YUAN** (AMESDK), con código de
ejemplo y PDFs de documentación del fabricante, porque la tarjeta es un **OEM de YUAN**,
no un diseño propio de Elgato.

**Esto es incomparablemente mejor que elgato-gchd**, donde todo salía de sniffing USB a
ciegas y sin documentación. Aquí hay mapa de registros, protocolo de mailbox documentado,
opcodes confirmados y un SDK de fabricante como referencia.

Y mitiga en buena parte el riesgo de autor único: si mañana desaparece, el conocimiento
no se va con él.

---

## 4. La brecha real no es técnica, es de producto

El driver **funciona**. Lo que no es, es un producto. Las tres métricas que lo dicen:

**148 `module_param`.** Un driver V4L2 de producción tiene entre 2 y 5. Cada uno de esos
148 es un experimento fosilizado: `mst_b0_late`, `aic_every_frame`, `buf_pair_swap`,
`dma_iova_offset`, `op6_kick_ms`... Para quien no haya vivido los 227 experimentos, la
superficie de configuración es inabordable.

**186 `pr_info` contra 3 `dev_dbg`.** Un driver de investigación narra todo lo que hace.
Un driver de producción calla salvo que algo vaya mal, y usa `dev_*` ligado al
dispositivo, no `pr_*` global. Mainline rechazaría esto de entrada.

**Sin CI y sin tests automatizados.** Hay 65 scripts de prueba, pero todos exigen la
tarjeta y una persona mirando. Nada que corra en un pipeline.

Carencias funcionales declaradas: **audio no implementado** (`enable_audio=1` solo
registra un andamiaje ALSA inerte), **solo 1920x1080**, sin recuperación automática de
HPD, y **sin envío a mainline**.

---

## 5. Plan por fases

La diferencia estructural con el plan de elgato-gchd: allí la Fase 2 era un gate
imposible por falta de hardware. **Aquí la validación está disponible desde el día 1**,
así que el orden natural se invierte: se valida primero y se planifica después.

### Fase 0 — Validar en vuestro hardware · 1-2 días · **hazlo antes de cualquier plan**

No invirtáis ni una hora de planificación antes de esto:

1. Comprobar en la BIOS/kernel que hay un dominio **IOMMU traductor**. El driver
   **no funciona** con `iommu=off` ni `iommu=pt`, y esto es un requisito de arranque,
   no un detalle.
2. Clonar, compilar contra vuestro kernel (`make KVER=<ver>`), cargar el módulo.
   Con Secure Boot activo hará falta firmar el módulo o enrolar la clave.
3. Capturar 1080p H.264 con OBS. Correr `scripts/mz0380-compliance.sh`
   (`v4l2-compliance`) y comparar con el 148/148 que afirma el README.
4. **Medir el wedge vosotros mismos**: contar spawns con `scripts/mz0380-spawns.sh`,
   ver a los cuántos se atasca en vuestra unidad y confirmar que un ciclo
   STREAMON/STREAMOFF con `persistent_h264` **no** consume presupuesto.

Salida: un sí o un no sobre vuestra máquina, vuestro kernel y vuestra tarjeta. Todo lo
demás depende de esto.

### Fase 1 — Red de seguridad · 1-2 semanas · sin hardware salvo la validación final

1. **CI multi-kernel.** El `Makefile` ya trae `all-kernels` y `make KVER=<ver>`, así que
   es barato: matriz de kernels con headers, build con `W=1`, y `-Werror` si el árbol lo
   aguanta. Esto es lo que hoy falta por completo.
2. **`checkpatch.pl` como gate informativo**, no bloqueante todavía. Da la medida real de
   la distancia a mainline sin frenar el desarrollo.
3. Fijar un **kernel de referencia** y documentarlo. Hoy "6.18 LTS y 7.2" es una
   afirmación del README sin nada que la verifique de forma continua.
4. Integrar `v4l2-compliance` en el pipeline, en la parte que no necesite tarjeta, y como
   job manual en una máquina con hardware si tenéis dónde.
5. Arreglar el README, que **se contradice** en dos puntos (DKMS listado a la vez como
   ausente y como documentado; captura en crudo a la vez funcionando y no anunciada).
   Es la primera contribución fácil y de valor real.

### Fase 2 — Reducir las 148 perillas · 2-3 semanas · **el mayor retorno del proyecto**

Clasificar cada `module_param` en tres cubos y actuar:

- **Resuelto:** el experimento tiene ganador. Fijar el valor en el código y borrar el
  parámetro.
- **Diagnóstico:** útil para depurar, no para configurar. Mover a `debugfs` o a las
  entradas `/proc` que ya existen.
- **Configuración real del usuario:** se queda. Deberían ser 3-5, no 148.

Con hardware, cada decisión es verificable una por una. Sin esta fase el driver no es
mantenible por nadie que no sea su autor, y ninguna de las fases siguientes es realista.

### Fase 3 — Cerrar las carencias funcionales · requiere hardware y más RE

- **Audio.** Hoy es un andamiaje ALSA inerte; falta el DMA de PCM. El SDK de YUAN
  (`AMESDK`, con el handle `MZ0380 PCI, Analog WaveIn` ya identificado en
  `MZ0380_SDK_CONTROL_PATH.md`) es la pista más directa.
- **Resoluciones distintas de 1080p.** Mismo camino: el perfil `res/MZ0380.txt` del SDK
  lista resoluciones, frame rates y bitrates.
- **Recuperación automática de HPD.**

### Fase 4 — Logging y mainline · opcional y caro · decisión estratégica

`pr_*` → `dev_*`, bajar los 186 `info` a `dev_dbg`, y limpiar lo que marque checkpatch.
Solo entonces tiene sentido plantear el envío a mainline.

**La decisión real:** mainline es mucho trabajo y revisión pública exigente, pero es la
**única** forma de que el driver sobreviva sin vosotros ni sin su autor actual. Un módulo
out-of-tree se rompe en cada versión del kernel y alguien tiene que estar ahí para
arreglarlo. Si la tarjeta es infraestructura para el equipo a varios años, mainline es
la respuesta correcta aunque sea la cara.

---

## 6. Riesgos y due diligence

**1. Autor único (bus factor 1).** 158 commits, todos de la misma persona. Mitigado por
los 15.429 líneas de findings documentados, pero sigue siendo una persona. Decisión a
tomar: **contribuir upstream o mantener un fork propio.** Recomiendo contribuir primero
—el autor está activo, con 120 commits en agosto y el último hace dos semanas— y
bifurcar solo si deja de responder. Empezad con el arreglo del README: es la forma más
barata de medir su receptividad.

**2. Procedencia del trabajo de ingeniería inversa.** *Esto es due diligence obligatoria
para una consultora, antes de invertir o distribuir.* El mapa de registros viene de
decompilar el driver de Windows y de minar el SDK de YUAN. El autor **documenta
explícitamente qué consultó y qué no** (sección *Provenance* del README, referenciada
desde la cabecera del `Makefile`), lo cual es lo responsable y es más de lo que hacen
muchos proyectos así. Pero quien lleve legal en vuestra empresa debería mirarlo, y con
más razón si hay intención de mainline o de redistribuir el módulo a clientes.

**3. Requisito de IOMMU traductor.** Comprobadlo antes de nada: `iommu=off` y `iommu=pt`
no sirven. Es un requisito de configuración del host, y en servidores o con determinadas
BIOS puede no ser trivial.

**4. Secure Boot.** Con enforcement activo hay que firmar el módulo o enrolar la clave.
Operacionalmente relevante si esto va a máquinas gestionadas.

**5. Ancho de banda PCIe.** Un fork del proyecto (`lancer1977`) reporta que la tarjeta
negocia **PCIe x1 Gen1**, lo que limita el 1080p60 en crudo. Para H.264 comprimido —el
camino principal— no debería importar, pero conviene saberlo antes de prometer captura
sin comprimir.

**6. No pude compilarlo en este entorno.** El sandbox de esta sesión bloquea construir
código externo, y el kernel del contenedor (6.18 Firecracker) no tiene headers. Todo lo
de este documento sale de **leer el código y medir el árbol**, no de ejecutarlo. La
compilación es parte de la Fase 0, en vuestra máquina.

---

## 7. Qué transfiere del trabajo hecho en elgato-gchd

**No el código: el método.** No hay una línea reutilizable —otro bus, otro lenguaje,
kernel en vez de espacio de usuario— pero sí todo lo que resultó valioso:

- **CI que compila con optimización activada.** En elgato-gchd, auditar con
  `-fsyntax-only` escondió 8 defectos reales que solo aparecieron al activar el
  optimizador. En un módulo de kernel el equivalente es construir con `W=1` y varios
  kernels, de forma continua.
- **`set -o pipefail` explícito** en los pasos de CI que usan tuberías. Sin él, el
  estado de salida es el del último comando y un build roto pasa como verde.
- **Análisis estático calibrado, no en bruto.** 453 avisos no los atiende nadie; un
  conjunto afinado con supresiones justificadas en el código, sí. En kernel el
  equivalente es `checkpatch` con un perfil decidido, más `sparse`.
- **La regla de no cambiar lo que no se puede verificar.** En elgato-gchd era una
  restricción permanente. **Aquí deja de serlo**, y eso es precisamente lo que hace que
  este proyecto merezca la inversión y el otro no.

---

## 8. Recomendación

**Haced la Fase 0 esta semana.** Son 1-2 días y responde la única pregunta que importa:
¿funciona en vuestra máquina, con vuestro kernel y vuestra tarjeta? Dos usuarios ajenos
al autor dicen que sí; confirmadlo vosotros.

Si sale bien, este proyecto tiene lo que a elgato-gchd le faltaba: **un camino claro,
verificable y con el hardware en la mano**. Y la Fase 2 —colapsar 148 parámetros en 5—
es trabajo de ingeniería acotado, de alto valor y sin incógnitas de investigación.

Si sale mal, el coste de haberlo averiguado es de dos días.
