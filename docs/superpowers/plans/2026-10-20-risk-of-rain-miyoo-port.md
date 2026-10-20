# Risk of Rain (2013) Miyoo Mini Plus Port Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Desarrollar el port nativo de *Risk of Rain (2013)* para la consola portátil *Miyoo Mini Plus* (OnionOS) con renderizado por software a 60 FPS, relación 4:3 sin bandas negras, controles ergonómicos y consumo de RAM blindado con cero swap en MicroSD.

**Architecture:** Arquitectura híbrida compuesta por: 1) Una herramienta de preprocesamiento de assets en PC basada en `UndertaleModLib` que reduce las hojas de texturas a 16 bits (RGB565/RGBA4444), recomprime el audio y parchea los scripts GML del HUD y rendimiento; 2) Una bifurcación personalizada del runner C++ `Butterscotch` optimizada para el SoC SigmaStar SSD202D con vectorización ARM NEON, decodificación de audio por streaming con Tremor (`libvorbisidec`), bucle de pacing monotónico adaptado a `CONFIG_HZ=100` y blit directo a `/dev/fb0` con rotación invertida de 180°; 3) Un script de arranque `launch.sh` para OnionOS con overclock a 1.8 GHz y gestión limpia de señales.

**Tech Stack:** C++17, C, ARMv7-A NEON, SDL2, `libvorbisidec` (Tremor fixed-point), CMake, Docker (`aemiii91/miyoomini-toolchain`), Python 3.10+ / C# (`UndertaleModLib`), Bash.

**Spec:** `docs/superpowers/specs/2026-10-20-risk-of-rain-miyoo-design.md`

---

## Global Constraints

- **Cero Swap en MicroSD:** Queda estrictamente vetado el uso de swap físico en tarjeta de memoria para evitar desgaste de celdas flash y tirones de I/O.
- **Presupuesto de Memoria RAM:** El consumo total del juego no debe superar los 55 MB en ejecución, garantizando >30 MB libres respecto a los 85 MB de OnionOS.
- **Resolución y Pantalla:** Salida física fija a 640×480 píxeles. El combate corre en 640×360 nativo (1:1) en el centro, y las franjas de 60 px superior e inferior se reservan para HUD activo. Cero barras negras.
- **Pacing de Reloj:** Prohibido el uso de `SDL_Delay` o `usleep` menores a 12 ms debido al tick de 10 ms del planificador del kernel (`CONFIG_HZ=100`). Usar espera monotónica con `sched_yield`.
- **Arquitectura de Procesador:** Binario compilado para ARMv7-A Cortex-A7 Dual-Core con FPU `neon-vfpv4` y ABI `hard` (`-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard`).
- **Compatibilidad de Guardado:** Lectura y escritura íntegra del archivo `Save.ini` en la raíz de instalación del juego.

---

## Review Focus

1. **Underrun en streaming de audio:** Bajo picos extremos de combate donde el rasterizador sature la CPU, Tremor podría quedarse sin muestras PCM; debe garantizarse bombeo periódico del buffer de audio durante el renderizado.
2. **Desbordamiento de textura (OOM):** Si un nivel avanzado carga más atlas de los previstos, la memoria superará los 85 MB; la herramienta de PC debe garantizar que los atlas queden estrictamente por debajo de 28 MB en 16 bits.
3. **Pérdida de sincronización de cuadros (Stutter por tick de 10 ms):** Si un cuadro toma 16.8 ms, una llamada no protegida a sleep colapsará el framerate a 30 FPS inmediatamente.
4. **Fallo en detección de puertos de OnionOS:** Si `Risk of Rain.port` contiene bloques condicionales o rutas dinámicas, el scraper `import.sh` lo renombrará a `.notfound`.
5. **Pantalla congelada al pulsar MENU:** Si el proceso termina abruptamente con `exit(0)` sin desmapear `/dev/fb0` ni restaurar `FBIOPAN_DISPLAY`, la consola quedará con la pantalla en negro requiriendo reinicio forzado.

---

### Task 1: Herramienta de Preprocesamiento de Assets en PC - Conversor de Texturas y Audio

**Files:**
- Create: `tools/optimize_ror_assets.py`
- Create: `tools/requirements.txt`
- Test: `tests/test_asset_optimizer.py`

**Interfaces:**
- Consumes: `data.win` original de Steam y archivos `.ogg` de Chris Christodoulou.
- Produces: `data_mmp.win` con hojas de textura cuantizadas a 16 bits (RGB565 / RGBA4444) y carpeta `audio_mmp/` con streams a 22,050 Hz Q2.

- [ ] **Step 1: Escribir prueba de validación de compresión de texturas y audio**
Escribir `tests/test_asset_optimizer.py` que verifique que una imagen RGBA8888 de 2048×2048 se convierte correctamente a formato 16 bits con una reducción de peso $\ge 50\%$, y que un archivo de audio OGG se resamplea a 22,050 Hz sin artefactos de recorte.

- [ ] **Step 2: Ejecutar prueba y verificar que falla**
Run: `python -m pytest tests/test_asset_optimizer.py -v`  
Expected: FAIL (módulo no existe).

- [ ] **Step 3: Implementar `optimize_ror_assets.py`**
Implementar el script CLI en Python utilizando Pillow / ImageMagick y `ffmpeg` para:
1. Extraer los bloques `TXTR` de `data.win`.
2. Convertir cada página de textura a RGB565 (con canal alfa de 1 bit o RGBA4444 para sprites con semitransparencia).
3. Resamplear los archivos OGG a 22,050 Hz estéreo/mono con calidad constante `-qscale:a 2`.

- [ ] **Step 4: Ejecutar prueba y verificar que pasa**
Run: `python -m pytest tests/test_asset_optimizer.py -v`  
Expected: PASS.

- [ ] **Step 5: Commit**
```bash
git add tools/ tests/
git commit -m "feat(tools): pipeline de cuantizacion de texturas a 16-bit y resampleo de audio"
```

---

### Task 2: Parcheador de Bytecode GML - Reposicionamiento de HUD y Mitigaciones de Rendimiento

**Files:**
- Create: `tools/gml_patcher.py`
- Modify: `tools/optimize_ror_assets.py`
- Test: `tests/test_gml_patcher.py`

**Interfaces:**
- Consumes: Objeto `data.win` de UndertaleModLib.
- Produces: `data_mmp.win` con scripts modificados: `oHUD_Draw` desviado a $y < 60$ e $y > 420$, `oDamageText` acumulativo y diezmado de ticks lejanos en `pEnemy`.

- [ ] **Step 1: Escribir prueba unitaria de inyección de bytecode GML**
Verificar en `tests/test_gml_patcher.py` que el parser localiza el evento Draw de `oHUD` y desplaza las coordenadas verticales de dibujo a las bandas superior e inferior, e inyecta el acumulador de daño en `oDamageText`.

- [ ] **Step 2: Ejecutar prueba y verificar que falla**
Run: `python -m pytest tests/test_gml_patcher.py -v`  
Expected: FAIL.

- [ ] **Step 3: Implementar `gml_patcher.py`**
Crear las rutinas de modificación de bytecode:
1. Desplazar dibujo de barras de vida y habilidades a $y \in [420, 480]$.
2. Desplazar temporizador, barra de dificultad y oro a $y \in [0, 60]$.
3. Inyectar guard de distancia en el evento Step de `pEnemy`: si `point_distance(x, y, oPlayer.x, oPlayer.y) > 960` y `(current_frame % 4 != 0)`, saltar la búsqueda de ruta.
4. Sustituir la instanciación masiva de `oDamageText` por actualización de acumulador en la instancia del enemigo.

- [ ] **Step 4: Ejecutar prueba y verificar que pasa**
Run: `python -m pytest tests/test_gml_patcher.py -v`  
Expected: PASS.

- [ ] **Step 5: Commit**
```bash
git add tools/gml_patcher.py tests/test_gml_patcher.py
git commit -m "feat(gml): parches de bytecode para recolocacion de HUD y mitigacion de CPU"
```

---

### Task 3: Configuración del Entorno de Compilación Cruzada y Scaffolding de Butterscotch

**Files:**
- Create: `docker/Dockerfile.miyoo`
- Create: `docker-compose.yml`
- Create: `CMakeLists.txt`
- Create: `cmake/miyoo-toolchain.cmake`
- Test: `tests/test_toolchain_build.sh`

**Interfaces:**
- Consumes: Repositorio base de `cobaltgit/Butterscotch`.
- Produces: Binario ARMv7 ELF `RiskOfRain` ejecutable en OnionOS.

- [ ] **Step 1: Escribir prueba de validación de compilación cruzada**
Escribir `tests/test_toolchain_build.sh` que verifique que el toolchain GCC emite un binario de prueba con arquitectura `ARMv7-A`, FPU hard-float y enlace con `libSDL2.so`.

- [ ] **Step 2: Ejecutar prueba y verificar que falla (sin Docker/CMake configurado)**
Run: `bash tests/test_toolchain_build.sh`  
Expected: FAIL.

- [ ] **Step 3: Configurar Toolchain CMake y Dockerfile**
1. Configurar `docker/Dockerfile.miyoo` basado en `aemiii91/miyoomini-toolchain:latest`.
2. Crear `cmake/miyoo-toolchain.cmake` con:
   ```cmake
   set(CMAKE_SYSTEM_NAME Linux)
   set(CMAKE_SYSTEM_PROCESSOR arm)
   set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
   set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)
   set(CMAKE_C_FLAGS "-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -O3 -fomit-frame-pointer -ffast-math -pthread")
   set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=c++17")
   ```
3. Configurar `CMakeLists.txt` para estructurar la compilación del runner `Butterscotch`.

- [ ] **Step 4: Ejecutar prueba y verificar compilación exitosa**
Run: `bash tests/test_toolchain_build.sh`  
Expected: PASS con binario ARM emitido correctamente.

- [ ] **Step 5: Commit**
```bash
git add docker/ cmake/ CMakeLists.txt tests/test_toolchain_build.sh
git commit -m "build: configuracion de toolchain ARMv7 NEON y entorno Docker para Miyoo Mini Plus"
```

---

### Task 4: Frame Pacing Monotónico y Blit a Framebuffer con Rotación 180°

**Files:**
- Create: `src/platform/mmp_video.h`
- Create: `src/platform/mmp_video.c`
- Test: `tests/test_frame_pacing.c`

**Interfaces:**
- Consumes: Buffer de imagen en RGB565 / RGBA8888 de la máquina virtual.
- Produces: Escritura física en `/dev/fb0` (640×480) con rotación de 180° y pacing de 16.66 ms sin penalizaciones por `CONFIG_HZ=100`.

- [ ] **Step 1: Escribir prueba del bucle de pacing monotónico**
Crear `tests/test_frame_pacing.c` para medir 120 cuadros a 60 FPS con reloj monotónico (`CLOCK_MONOTONIC`) y verificar que la desviación estándar de frame time sea inferior a 0.8 ms en Linux con tick de 10 ms simulado.

- [ ] **Step 2: Ejecutar prueba y verificar que el bucle estándar falla con jitter**
Run: `gcc tests/test_frame_pacing.c -o test_pacing && ./test_pacing`  
Expected: FAIL (jitter > 5 ms con `usleep` estándar).

- [ ] **Step 3: Implementar `mmp_frame_pacing` y `mmp_video_flip_180`**
1. Implementar `mmp_frame_pacing(uint64_t target_ns)` con algoritmo híbrido: `nanosleep` si el tiempo restante supera los 12 ms, y espera activa con `sched_yield()` en el tramo final.
2. Implementar `mmp_video_flip_180`: mapear `/dev/fb0` vía `mmap` y escribir directamente invirtiendo coordenadas $(639-x, 479-y)$ o programando el blitter `MI_GFX`.

- [ ] **Step 4: Ejecutar prueba y verificar estabilidad de cuadros**
Run: `./test_pacing`  
Expected: PASS (60 FPS estables, desviación < 0.5 ms).

- [ ] **Step 5: Commit**
```bash
git add src/platform/ tests/test_frame_pacing.c
git commit -m "feat(video): frame pacing con reloj monotonico y rotacion fisica de 180 grados a /dev/fb0"
```

---

### Task 5: Subsistema de Audio por Streaming con Tremor (`libvorbisidec`)

**Files:**
- Create: `src/audio/audio_tremor.h`
- Create: `src/audio/audio_tremor.c`
- Test: `tests/test_audio_stream.c`

**Interfaces:**
- Consumes: Archivos `.ogg` optimizados de la carpeta `audio_mmp/`.
- Produces: Ring-buffer estático de 128 KB alimentando el callback de SDL2 Audio en estéreo a 22,050 Hz sin pop de chasquido.

- [ ] **Step 1: Escribir prueba de decodificación Ogg en punto fijo**
Escribir `tests/test_audio_stream.c` para decodificar un buffer Ogg Vorbis mediante las llamadas de `libvorbisidec` (`ov_open_callbacks`, `ov_read`) y confirmar que no se producen saltos en el stream ni asignaciones de memoria dinámicas durante el loop de reproducción.

- [ ] **Step 2: Ejecutar prueba y verificar que falla**
Run: `gcc tests/test_audio_stream.c -lvorbisidec -o test_audio && ./test_audio`  
Expected: FAIL.

- [ ] **Step 3: Implementar `audio_tremor.c`**
1. Integrar el decodificador de punto fijo de Tremor para plataformas ARM sin FPU pesada.
2. Configurar el buffer circular de 128 KB acoplado a `SDL_AudioCallback`.
3. Añadir curva de atenuación rápida de 3 ms (`hh_declick`) para prevenir chasquidos al pausar pistas o cambiar de fase.
4. Implementar bombeo de audio periódico (`audio_pump`) invocado desde el rasterizador para evitar *buffer underruns*.

- [ ] **Step 4: Ejecutar prueba y verificar audio fluido sin underruns**
Run: `./test_audio`  
Expected: PASS.

- [ ] **Step 5: Commit**
```bash
git add src/audio/ tests/test_audio_stream.c
git commit -m "feat(audio): streaming de musica con Tremor libvorbisidec y supresion anti-chasquidos"
```

---

### Task 6: Rasterizador por Software Vectorizado con ARM NEON

**Files:**
- Modify: `src/renderer/SoftwareRenderer.h`
- Modify: `src/renderer/SoftwareRenderer.cpp`
- Test: `tests/test_neon_blitter.c`

**Interfaces:**
- Consumes: Comandos de dibujo de sprites y tiles de la cola de GameMaker.
- Produces: Escritura de píxeles en RGB565 / RGBA4444 con mezcla alfa acelerada en bloques de 8 y 16 píxeles vía registros SIMD de 128 bits (`uint16x8_t`).

- [ ] **Step 1: Escribir prueba de rendimiento del blitter NEON**
Escribir `tests/test_neon_blitter.c` comparando el tiempo de dibujado de 10,000 sprites de 32×32 con alfa entre el bucle escalar en C y la rutina vectorizada NEON.

- [ ] **Step 2: Ejecutar prueba y verificar que la rutina NEON supera a la escalar por al menos 2.5x**
Run: `arm-linux-gnueabihf-gcc -O3 -mfpu=neon-vfpv4 tests/test_neon_blitter.c -o test_neon` (ejecutar en emulador/dispositivo).  
Expected: Rutina NEON completada en < 40% del tiempo escalar.

- [ ] **Step 3: Implementar vectorización NEON en `SoftwareRenderer.cpp`**
1. Sustituir los bucles de span de `draw_sprite_ext` por instrucciones intrínsecas NEON:
   - Carga de píxeles: `vld1_u16`.
   - Cálculo de mezcla alfa entera: `Dest = ((Src * Alpha) + (Dest * (255 - Alpha))) >> 8`.
   - Almacenamiento directo: `vst1_u16`.
2. Añadir descarte rápido de bounding box (`noclip`) para no evaluar sprites situados fuera de la vista de 640×360.

- [ ] **Step 4: Ejecutar prueba de verificación y confirmar consistencia visual**
Verificar que la salida en píxeles de la rutina NEON es idéntica bit a bit a la salida de referencia escalar.  
Expected: PASS (0 discrepancias de color).

- [ ] **Step 5: Commit**
```bash
git add src/renderer/ tests/test_neon_blitter.c
git commit -m "perf(renderer): vectorizacion SIMD NEON para mezcla alfa y descarte rapido de sprites"
```

---

### Task 7: Compatibilidad de VM y Soporte de Opcodes GMS 1.4 en Butterscotch

**Files:**
- Modify: `src/vm/GMLVM.h`
- Modify: `src/vm/GMLVM.cpp`
- Create: `src/vm/RoRSpecialFunctions.cpp`
- Test: `tests/test_ror_opcodes.cpp`

**Interfaces:**
- Consumes: Bytecode de GameMaker Studio 1.4 presente en `data_mmp.win`.
- Produces: Ejecución libre de excepciones de las funciones `ds_grid_*`, `instance_deactivate_region` y manejo de `Save.ini`.

- [ ] **Step 1: Escribir prueba de ejecución de scripts de RoR**
Crear `tests/test_ror_opcodes.cpp` que alimente a la VM con secuencias de bytecode reales de RoR v1.3.0 (generación procedural de mapa con `ds_grid` y desactivación de instancias).

- [ ] **Step 2: Ejecutar prueba y listar opcodes faltantes**
Run: `./test_ror_opcodes`  
Expected: FAIL con advertencias de opcodes no implementados.

- [ ] **Step 3: Implementar funciones GML requeridas en `RoRSpecialFunctions.cpp`**
1. Completar la implementación de funciones de estructuras de datos: `ds_grid_create`, `ds_grid_set_region`, `ds_priority_*`.
2. Implementar `instance_deactivate_region` y `instance_activate_region` con optimización espacial (QuadTree o rejilla regular).
3. Redirigir las operaciones de archivo `ini_open("Save.ini")` hacia la ruta local de la tarjeta SD en `./Save.ini`.

- [ ] **Step 4: Ejecutar prueba y verificar ejecución sin advertencias**
Run: `./test_ror_opcodes`  
Expected: PASS (todas las rutinas de RoR ejecutan limpiamente).

- [ ] **Step 5: Commit**
```bash
git add src/vm/ tests/test_ror_opcodes.cpp
git commit -m "feat(vm): soporte completo de opcodes GMS 1.4 y redireccion de Save.ini"
```

---

### Task 8: Integración en OnionOS, `launch.sh` y Empaquetado BYOD

**Files:**
- Create: `dist/Risk of Rain.port`
- Create: `dist/Games/Risk of Rain/launch.sh`
- Create: `dist/Games/Risk of Rain/properties/control.json`
- Test: `tests/test_launcher_syntax.sh`

**Interfaces:**
- Consumes: Binario compilado `RiskOfRain`, librerías dinámicas y `data_mmp.win`.
- Produces: Paquete de instalación para `/mnt/SDCARD/Roms/PORTS/` con salida limpia y reloj seguro.

- [ ] **Step 1: Escribir prueba estricta de sintaxis de OnionOS para el archivo `.port`**
Verificar en `tests/test_launcher_syntax.sh` que `Risk of Rain.port` contiene exactamente la línea `GameDataFile="data_mmp.win"` sin bloques condicionales (previniendo el bug `.notfound` de `import.sh`).

- [ ] **Step 2: Ejecutar prueba y verificar**
Run: `bash tests/test_launcher_syntax.sh`  
Expected: PASS.

- [ ] **Step 3: Escribir `launch.sh` robusto con traps**
1. Detección de hardware Miyoo Mini Plus (`wlan0` / `grep "Plus" /proc/cpuinfo`).
2. Overclock seguro: `cpuclock 1800`.
3. Aislamiento de audio: `killall -9 audioserver`.
4. Definir manejador de salida limpia:
   ```sh
   cleanup() {
     cpuclock 1200
     /customer/app/audioserver &
     sync
   }
   trap cleanup EXIT INT TERM
   ```
5. Lanzamiento del binario: `./RiskOfRain > ror.log 2>&1`.

- [ ] **Step 4: Verificar ejecución limpia del launcher en entorno simulado**
Probar el ciclo completo de inicio, señal `SIGINT` (simulando pulsar MENU) y ejecución del trap de limpieza.  
Expected: Reloj y audioserver restaurados sin procesos huérfanos.

- [ ] **Step 5: Commit final de empaquetado**
```bash
git add dist/ tests/test_launcher_syntax.sh
git commit -m "release: empaquetado oficial para OnionOS con launch.sh protegido y configuracion de controles"
```
