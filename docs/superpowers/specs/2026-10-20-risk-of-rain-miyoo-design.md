# Documento de Diseño: Port Nativo de Risk of Rain (2013) para Miyoo Mini Plus (OnionOS)

**Fecha:** 2026-10-20  
**Estado:** Propuesta de Diseño Finalizada (Aprobación Pendiente)  
**Plataforma Objetivo:** Miyoo Mini Plus (SigmaStar SSD202D, Dual-Core Cortex-A7 @ 1.8 GHz Overclock, 128 MB RAM, sin GPU 3D, OnionOS v4.x)  
**Motor Base:** Runner open-source `Butterscotch` (`cobaltgit/Butterscotch`) en C++ con rasterizador por software vectorizado NEON.

---

## 1. Contexto e Intención

Llevar el clásico *Risk of Rain (2013)* a la consola portátil *Miyoo Mini Plus* en una versión fluida, ergonómica y visualmente adaptada a su pantalla de 3.5 pulgadas. La meta es brindar una experiencia monojugador completa a 60 FPS estables, con pantalla 100% aprovechada sin barras negras, controles naturales que no fatiguen las manos, y un consumo de memoria optimizado que garantice estabilidad total dentro del límite de 128 MB de RAM compartida, protegiendo estrictamente la tarjeta MicroSD.

---

## 2. Metas, No-Metas y Anti-Metas

### Metas (Goals)
1. **Pantalla Completa 4:3 sin Bandas Negras:** Aprovechar íntegramente la resolución física de 640×480 píxeles de la pantalla LCD.
2. **Cámara Nativa Completa 1:1 (640×360):** Conservar el campo de visión (FOV) original del juego para mantener intacta la justicia del combate y visibilidad de enemigos a distancia (francotiradores, gusanos de magma y colosos). La acción ocupa el centro (640×360) flanqueada por bandas de interfaz activas de 60 px superior e inferior.
3. **Rendimiento Sólido a 60 FPS:** Rasterizado por software optimizado con ARM NEON y pacing monotónico para evitar las caídas a 30 FPS derivadas del tick de 10 ms del kernel (`CONFIG_HZ=100`).
4. **Presupuesto de RAM Estricto (~20–30 MB en Texturas):** Conversión de atlas a formato 16-bit (RGB565 / RGBA4444) en PC y desalojo de niveles inactivos, manteniendo el juego completo en < 55 MB de RAM (dentro del presupuesto de 80–85 MB libres de OnionOS).
5. **Streaming de Audio Tremor:** Reproducción continua de la banda sonora mediante `libvorbisidec` (punto fijo en enteros) con buffer circular de 128 KB, evitando precargar pistas en memoria.
6. **Mapeo Ergonómico de Controles:** Diseñado para la cruceta y botones frontales/traseros de la MMP para partidas de larga duración sin incomodidad táctil.
7. **Compatibilidad de Guardado:** Lectura y escritura íntegra del archivo `Save.ini` de la versión de Steam v1.3.0 para conservar partidas y desbloqueos.
8. **Herramienta de Preparación de Datos (PC):** Script `tools/optimize_ror_assets.py` (basado en `UndertaleModLib`) que procese el juego original bajo la filosofía BYOD ("Bring Your Own Data").

### Metas Secundarias (Post-v1 / Stretch Goal)
- **Conversor Integrado en la Consola (On-Device):** Una vez estabilizado el port, desarrollar una utilidad ligera nativa para que el usuario simplemente copie la carpeta de Steam a la tarjeta MicroSD y la consola realice la conversión de assets localmente en el primer arranque.

### No-Metas y Anti-Metas (Anti-Goals)
- **PROHIBIDO EL USO DE SWAP EN MICROSD:** Queda terminantemente vetado cualquier archivo o partición de swap físico en la tarjeta de memoria (para evitar desgaste de celdas flash y tirones por latencia de bus I/O). Toda la memoria debe caber en DRAM física.
- **Anti-Meta: Zoom 2x / Recorte de Cámara a 320×180:** Descartado para no perjudicar la visibilidad táctica ni recibir disparos ciegos fuera de pantalla.
- **Anti-Meta: Multijugador en Línea / Red:** El port está enfocado exclusivamente en una experiencia monojugador offline estable y pulida.
- **Anti-Meta: Runtimes basados en aceleración GPU (GMLoader / libyoyo):** Descartados al carecer el SoC SSD202D de soporte OpenGL ES 2.0 por hardware.

---

## 3. Especificaciones y Decisiones de Arquitectura

### 3.1 Interfaz y Composición de Pantalla (640×480)
- **Área Central de Juego (640×360):**
  - Render target nativo a 640×360 píxeles (relación 16:9 intacta, píxel 1:1).
  - Centrado verticalmente exacto entre las líneas $y \in [60, 420)$.
- **Franja Superior HUD (640×60):**
  - Oro, nivel del superviviente, tiempo transcurrido, medidor gráfico de dificultad ("VERY HARD", "INSANE", "I SEE YOU") y barra de progreso de teletransportador.
- **Franja Inferior HUD (640×60):**
  - Barra de vida numérica y porcentual (HP actual / máximo).
  - Habilidades activas con contador de enfriamiento (cooldown en segundos).
  - Tira horizontal de inventario de ítems pasivos coleccionados con auto-ticker/scrolling pasivo cuando se acumulan muchos ítems (despejando el combate para que no se superpongan sobre los enemigos).
- **Menús y Pantallas Fuera de Combate [Decisión del Constructor]:**
  - Los menús principales, pantalla de selección de superviviente y bitácora se dibujan centrados en 640×360 con un marco decorativo temático del juego en las franjas de 60 px para preservar la estética 4:3 continua.
- **Rotación Física de Pantalla:** Escritura directa en coordenadas invertidas de 180° hacia `/dev/fb0` o mediante el acelerador 2D `MI_GFX` de SigmaStar para no gastar ciclos de memoria en un paso posterior.

### 3.2 Distribución de Controles
| Acción | Botón Miyoo Mini Plus | Justificación Ergonómica |
| :--- | :--- | :--- |
| **Moverse / Apuntar** | Cruceta (D-Pad Izq / Der) | Desplazamiento horizontal. |
| **Trepar Cuerdas / Escaleras** | Cruceta (D-Pad Arriba / Abajo) | Subir y bajar elementos del mapa. |
| **Habilidad 1 (Disparo Primario)** | **Botón X** | Fuego rápido continuo con la base del pulgar relajada. |
| **Saltar** | **Botón A** | Posición natural contigua a X para disparar y saltar a la vez. |
| **Habilidad 2 (Ataque Secundario)** | **Botón Y** | Daño concentrado de enfriamiento corto. |
| **Interactuar / Activar / Comprar** | **Botón B** | Cofres, drones, santuarios y teletransportadores. |
| **Habilidad 3 (Utilidad / Dash / Roll)**| **Gatillo L1** | Reacción inmediata con el dedo índice izquierdo. |
| **Habilidad 4 (Especial / Definitiva)** | **Gatillo R1** | Habilidad pesada con el índice derecho. |
| **Ítem de Equipo (Use Item Activable)** | **Gatillo R2** | Cooldown largo (misiles, curación masiva, etc.). |
| **Inspección de Inventario / Bitácora** | **Gatillo L2** | Despliegue de descripciones detalladas de ítems. |
| **Pausa / Opciones** | **START** | Menú del juego. |
| **Estadísticas de la Partida** | **SELECT** | Marcador y resumen del run. |
| **Salida Segura a OnionOS** | **Botón MENU** | Capturado por el launcher con limpieza de memoria y framebuffer. |

### 3.3 Presupuesto de Memoria RAM (Meta: < 55 MB Total)
| Componente | Memoria Original (PC) | Memoria Optimizada (Port MMP) | Técnica de Mitigación |
| :--- | :--- | :--- | :--- |
| **Páginas de Texturas** | 96 MB (RGBA8888 32-bit) | **~24–28 MB** | Atlas a 16-bit (RGB565/RGBA4444) + desalojo de salas inactivas. |
| **Banda Sonora y Efectos** | 35 MB (PCM descomprimido) | **128 KB** (Buffer circular) | Streaming secuencial de disco con `libvorbisidec` (Tremor). |
| **Estructuras de Objetos/VM**| ~15 MB | **~15 MB** | Gestión de memoria compacta en Butterscotch. |
| **Búferes de Pantalla (Framebuffers)**| 4 MB | **2 MB** (Backbuffer + FB) | Doble búfer directo a memoria física `/dev/fb0`. |
| **TOTAL ESTIMADO** | **~150 MB (Crash OOM)** | **~42–46 MB (Estable)** | **Margen de seguridad de >35 MB libres.** |

### 3.4 Mitigación de Carga de CPU en Late-Game (Enjambre de Monstruos)
1. **Fusión Acumulativa de Daño [Decisión del Constructor]:** Inyectar micro-parche GML en el script de daño (`oDamageText`) para acumular los números de daño flotantes por entidad en lugar de instanciar decenas de objetos de texto por segundo.
2. **Diezmado de Ticks Lejanos (Distance Decimation) [Decisión del Constructor]:** Guard condicional en GML para que los enemigos a más de 1.5 pantallas del jugador solo ejecuten la búsqueda de ruta cada 3 o 4 cuadros, preservando la física y evitando caídas al vacío.
3. **Poda de Partículas Cosméticas:** Limitar las partículas simultáneas de sangre, chispas y humo a un máximo estricto de 25 activas.
4. **Pacing de Reloj Monotónico:** Algoritmo híbrido `nanosleep` (>12 ms) + `sched_yield` para sincronización exacta a 60 FPS sin sufrir el tick de 10 ms del kernel (`CONFIG_HZ=100`).
5. **Capa del HUD [Decisión del Constructor]:** El reposicionamiento del HUD hacia las franjas de 60 px se realiza mediante parcheo de scripts GML en el pipeline de PC (`tools/optimize_ror_assets.py`), manteniendo el runner `Butterscotch` desacoplado y limpio como un motor GM genérico.

---

## 4. Estructura del Software y Entorno de Ejecución

### 4.1 Árbol de Directorios del Despliegue en OnionOS
```text
/mnt/SDCARD/Roms/PORTS/
├── Risk of Rain.port                     (Lanzador para el menú de Ports)
└── Games/
    └── Risk of Rain/
        ├── RiskOfRain                   (Binario ejecutable compilado con Butterscotch)
        ├── launch.sh                    (Script de arranque con traps de salida y reloj)
        ├── data_mmp.win                 (Asset procesado por la herramienta de PC)
        ├── Save.ini                     (Archivo de guardado del usuario)
        ├── libs/                        (libSDL2.so, libvorbisidec.so)
        └── properties/                  (Icono del juego, carátula y control.json)
```

### 4.2 Script de Inicio (`launch.sh`)
- Overclock seguro a **1.8 GHz** (`cpuclock 1800` en Miyoo Mini Plus).
- Detención preventiva de servicios de audio en segundo plano: `killall -9 audioserver`.
- Manejador de señal `trap cleanup EXIT INT TERM`:
  - Restablecimiento del reloj a 1.2 GHz (`cpuclock 1200`).
  - Reinicio del servidor de audio del sistema (`/customer/app/audioserver &`).
  - Limpieza de framebuffer mediante `ioctl(fb_fd, FBIOPAN_DISPLAY, &vinfo)`.
  - Ejecución de `sync`.

---

## 5. Qué Queda al Ingeniero Implementador (Builder Tasks)
- Configuración del CMake y compilación cruzada con `aemiii91/miyoomini-toolchain`.
- Implementación y validación de opcodes específicos de GMS 1.4 requeridos por RoR 1.3.0 en el intérprete de Butterscotch (`ds_grid_*`, `instance_deactivate_region`, etc.).
- Vectorización NEON de los blits y alpha blending en `SoftwareRenderer.cpp`.
- Construcción del script de PC `tools/optimize_ror_assets.py` con `UndertaleModLib`.
- Pruebas de regresión con partidas largas hasta el nivel final "Risk of Rain" (USS Contact Light).
