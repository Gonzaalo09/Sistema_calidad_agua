# Nodo sensor ESP32 — Comunicación Modbus RTU (TFG)

Firmware ESP-IDF para un ESP32 que actúa como nodo maestro Modbus RTU sobre
RS-485, consultando periódicamente varios sensores agrícolas/ambientales por
UART y volcando las lecturas por consola.

TODO: descripción general del sistema completo del TFG (más allá de este
nodo firmware) y su papel dentro de la arquitectura global.

## Arquitectura

### `main/`

`main.c` (`app_main`) configura el UART2 en modo RS-485 half-duplex por
software (9600 baudios, 8N1, sin control de flujo, pines TX=17 / RX=16,
buffer de evento/RX de 256 bytes) y lanza dos tareas de FreeRTOS:

- `polling` (prioridad 5, stack 4096) — componente `tareas` (`tarea_polling`)
- `transmision` (prioridad 3, stack 4096) — componente `tareas` (`tarea_transmision`)

### `components/modbus`

Construcción de tramas Modbus RTU (función 0x03, "leer holding registers"),
cálculo de CRC16 (polinomio Modbus) y decodificación de los datos de
respuesta a `float`, soportando dos formatos (`TipoDato`):

- `entero16` — entero de 16 bits big-endian (1 registro)
- `float32_CDAB` — float IEEE754 de 32 bits en orden de palabra CDAB
  (word-swapped), 2 registros

### `components/sensores`

Define `TABLA_SENSORES[]`, la tabla estática con la descripción de cada
sensor Modbus consultado (dirección de esclavo, registro inicial, número de
registros, tipo de dato, y nombres de las magnitudes que produce). Según el
código actual:

| Sensor | Slave ID | Registro inicial | Nº registros | Tipo | Magnitudes |
|---|---|---|---|---|---|
| NPK (JXBS-3001-NPK-RS) | 1 | 0x001E | 3 | `entero16` | Nitrógeno, Fósforo, Potasio |
| OD (oxígeno disuelto) | 3 | 0x0001 | 4 | `float32_CDAB` | OD_medida, temperatura |
| pH | 5 | 0x0001 | 4 | `float32_CDAB` | Ph, temperatura |

TODO: modelo/referencia exacta de los sensores de OD y pH (el código solo
los identifica como "OD sensor" y "Ph Sensor").

### `components/tareas`

- `tarea_polling` — cada `PERIODO_MUESTREO_MS` (4000 ms) recorre
  `TABLA_SENSORES`, envía la petición Modbus de cada sensor por UART2,
  espera la respuesta (timeout 1 s vía `uart_queue`), valida longitud y
  CRC, decodifica los valores y los publica en una cola FreeRTOS
  (`cola_lecturas`) como `lectura_sensor`.
- `tarea_transmision` — consume `cola_lecturas` y saca cada lectura por
  consola (`printf`) con su nombre, valor y timestamp (o un aviso de
  lectura fallida si la trama no fue válida).

### `components/I2C_bus` y `components/RTC`

Componentes creados pero **sin implementar todavía** (los `.c` están
vacíos, solo con el include guard). Pensados para un futuro bus I2C y un
RTC (aparentemente un DS3231, a juzgar por el nombre de archivo esperado en
`RTC/CMakeLists.txt`).

> **TODO / pendiente de corregir:** `components/I2C_bus/CMakeLists.txt`
> referencia `SRCS "I2C_bus.c"` (falta el prefijo `source/`, el archivo
> real es `source/I2C_bus.c`), y `components/RTC/CMakeLists.txt` referencia
> `SRCS "rtc_ds3231.c"`, que no existe (el archivo real es
> `source/RTC.c`). Mientras ningún otro componente los requiera esto no
> rompe el build actual, pero hay que corregirlo antes de implementarlos.

## Compilación

Proyecto estándar ESP-IDF (`idf.py` envuelve CMake/Ninja). Ejecutar desde un
entorno ESP-IDF (p. ej. "ESP-IDF PowerShell", o tras cargar
`export.ps1`/`export.bat`):

```
idf.py build
```

TODO: versión de ESP-IDF a usar — hay indicios de que el proyecto se edita
desde más de una máquina y no está claro que ambas tengan instalada la
misma versión; confirmar y documentar aquí la versión objetivo.

## Estado por fases

- [x] Fase 1 — Comunicación Modbus RTU por UART con un sensor NPK
      (petición fija, CRC16, parseo de N/P/K)
- [x] Fase 2 — Generalización a tabla de sensores (`TABLA_SENSORES`),
      arquitectura por tareas FreeRTOS (`polling` / `transmisión`) separada
      en componentes (`modbus`, `sensores`, `tareas`)
- [ ] Fase 3 — Bus I2C y RTC (componentes `I2C_bus` y `RTC` creados, sin
      implementar)
- [ ] TODO: siguientes fases (almacenamiento, transmisión remota, etc.) —
      pendiente de definir
