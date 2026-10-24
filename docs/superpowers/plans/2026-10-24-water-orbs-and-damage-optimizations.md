# Paquete 1: Optimizaciones de Agua, Orbes y Límite de Daño Flotante

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminar las caídas de FPS a ~39-40 en niveles acuáticos (*Sunken Tombs*, *Damp Caverns*), durante la pelea contra el jefe medusa (*Wandering Vagrant*), y cuando caen hordas de monedas/orbes o se desatan cadenas de daño en multitudes, acelerando el motor C *Butterscotch*.

**Architecture & Components:**
1. **`butterscotch/src/sw_renderer.c`**:
   - `swDrawRectangle`: Implementar fast-path con precomputación constante y mezcla RGB565 paralela de 32 bits (5-bit y 8-bit constante), reduciendo de 25 instrucciones por píxel a ~3-4 ciclos.
   - `swDrawCircleColor`: Implementar fast-path para círculos diminutos ($r \le 6$) con spans horizontales discretos sin `sqrtf` ni punto medio iterativo.
2. **`butterscotch/src/vm_builtins.c`**:
   - Registrar `instance_nearest_var_yoyo` como función nativa en C (`builtin_instance_nearest_var_yoyo`).
   - Si hay 1 solo jugador activo (`oP`), retornar de inmediato su `id` en 1 ciclo.
   - Si hay múltiples instancias, iterar en C de forma continua sin asignar estructuras de heap `ds_priority`.
3. **`butterscotch/src/runner.c` / `runner.h`**:
   - Identificar el índice de objeto de `oEfDamage` durante la carga.
   - En `builtin_instance_create`, limitar las instancias concurrentes de `oEfDamage` a 20 reutilizando la más antigua cuando se exceda el tope.
4. **`tests/test_water_and_small_elements.py`**:
   - Suite de pruebas unitarias verificando la exactitud del blending de rectángulos, los círculos diminutos, el builtin de `instance_nearest_var_yoyo` y el límite de `oEfDamage`.

---

### Task 1: Pruebas Unitarias para Optimizaciones de Paquete 1

- [ ] **Step 1: Crear archivo de pruebas `tests/test_water_and_small_elements.py`**
  Escribir pruebas que verifiquen:
  1. `swDrawRectangle` con alpha implementa fast-path optimizado en `sw_renderer.c`.
  2. `swDrawCircleColor` implementa fast-path para radios pequeños ($r \le 6$).
  3. `instance_nearest_var_yoyo` está registrado como builtin nativo en `vm_builtins.c`.
  4. Límite de instancias para `oEfDamage` en `runner.c` / `vm_builtins.c`.
- [ ] **Step 2: Ejecutar la prueba para verificar que falle (TDD)**
  `python -m unittest tests/test_water_and_small_elements.py`

---

### Task 2: Implementar Aceleración de Rectángulos Alfa en `sw_renderer.c` (Propuesta A)

- [ ] **Step 1: Modificar `swDrawRectangle` en `butterscotch/src/sw_renderer.c`**
  Precomputar los términos de origen constantes:
  `a5 = (a + 4) >> 3; inv_a5 = 32 - a5;`
  `const_rb = (c565 & 0xF81F) * a5;`
  `const_g  = (c565 & 0x07E0) * a5;`
  Procesar cada fila utilizando la mezcla empaquetada de 32 bits de 2 multiplicaciones por píxel.
- [ ] **Step 2: Verificar la compilación local y ejecutar pruebas de regresión**
  Correr tests unitarios de renderizado.

---

### Task 3: Implementar Fast-Path para Círculos Pequeños en `sw_renderer.c` (Propuesta C)

- [ ] **Step 1: Modificar `swDrawCircleColor` en `butterscotch/src/sw_renderer.c`**
  Para círculos rellenos con radio $r \le 6$:
  Utilizar una tabla de offsets horizontales precalculados para cada scanline $y \in [-r, r]$.
  Evitar `sqrtf` y división flotante en cada fila.
- [ ] **Step 2: Verificar con pruebas unitarias**
  Comprobar que los círculos se renderizan idénticos visualmente.

---

### Task 4: Implementar `builtin_instance_nearest_var_yoyo` en `vm_builtins.c` (Propuesta B)

- [ ] **Step 1: Escribir `builtin_instance_nearest_var_yoyo` en `butterscotch/src/vm_builtins.c`**
  Recibir `(xx, yy, objIndex, varName, intendedVal)`.
  Obtener las instancias de `objIndex`.
  Si solo hay 1 instancia, comprobar su variable y retornar su `instanceId`.
  Si hay más de una, buscar linealmente el de menor distancia sin crear colas `ds_priority`.
- [ ] **Step 2: Registrar `instance_nearest_var_yoyo` en `VM_registerAllBuiltins`**
  Permitir que la VM despache directamente a la función C nativa.

---

### Task 5: Implementar Limitador de Daño Flotante `oEfDamage` (Nivel 4)

- [ ] **Step 1: Cachear `damageObjIndex` en `Runner` (`runner.c` / `runner.h`)**
  En `Runner_init`, buscar `oEfDamage` en `dataWin->objt`.
- [ ] **Step 2: En `builtin_instance_create`, limitar instancias simultáneas**
  Si `objectIndex == runner->damageObjIndex` y `arrlen(instancesByExactObject[objectIndex]) >= 20`:
  Reutilizar la instancia más antigua en lugar de asignar una nueva en el heap.
- [ ] **Step 3: Ejecutar pruebas unitarias**
  Comprobar que todas las pruebas pasen en verde.

---

### Task 6: Compilación Cruzada ARM, Empaquetado y Verificación Final

- [ ] **Step 1: Compilar binario ARM en contenedor Docker `determined_moore`**
  `make -C butterscotch PLATFORM=miyoo-mini -j4`
- [ ] **Step 2: Verificar tamaño de binario y empaquetar distribución**
  Asegurar que el binario despojado sea < 2.5 MB y el zip < 5 MB.
- [ ] **Step 3: Ejecutar suite completa de tests de regresión (36+ tests)**
  Verificar 100% de tests pasando en verde.
- [ ] **Step 4: Actualizar grafo de Trail**
  Ejecutar `trail build`.
