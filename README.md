# Nodo sensor ESP32 — Modbus RTU + RTC (TFG)

Firmware ESP-IDF para un nodo sensor de campo: consulta periódicamente
sensores agrícolas/ambientales por Modbus RTU sobre RS-485, sella cada
medida con la hora de un RTC DS3231 y la vuelca por consola. La transmisión
remota (LilyGo T-SIM7600G) está pendiente.

Proyecto de cooperación internacional con despliegue en Perú. El nodo lo
operará personal sin formación para depurar firmware, así que cualquier
fallo debe poder identificarse sin abrir el código (pantalla del nodo y
datos transmitidos).

TODO: descripción del sistema completo del TFG (más allá de este nodo) y
su papel en la arquitectura global.

## Hardware

| Elemento | Detalle |
|---|---|
| MCU (desarrollo) | Placa ESP32-WROOM-32 DevKit de 30 pines |
| MCU (final) | LilyGo T-SIM7600G (ESP32-WROVER-E) — ver Pendientes |
| RS-485 | Módulo RS485-TTL con auto-dirección (VCC, TXD, RXD, GND). UART2, TX=GPIO17, RX=GPIO16, 9600 baudios 8N1 |
| Bus I2C | I2C_NUM_0, SDA=GPIO21, SCL=GPIO22. Pull-ups externas de los módulos; la interna solo como respaldo |
| RTC | Módulo DS3231 (0x68) con pila CR2032, alimentado a 3,3 V. Incluye EEPROM 24C32 (0x57). SQW y 32K sin conectar |
| Sensores | Alimentación 12 V compartida, GND común con el ESP32 y el módulo RS-485 |

Direcciones esperadas en el escaneo I2C: `0x57`, `0x68` (y `0x3C` cuando
se conecte la pantalla).

## Arquitectura

```
app_main ── UART2, bus I2C, RTC ── crea las tareas y retorna

tarea_polling (prio 5) ──cola_lecturas──> tarea_transmision (prio 3)
      │ rtc_timestamp_pais()
      ▼
tarea_rtc (prio 2, depuración) ── RTC (DS3231) ── I2C_bus
```

### `main/`

`app_main`:

1. Configura UART2 (Modbus RTU) e instala el driver con `uart_queue`.
2. `i2c_bus_init()` → `i2c_bus_scan()` (solo bring-up) → `rtc_init()` →
   `rtc_comprobar_oscilador()`. Si `FORZAR_HORA == 1` o el oscilador del
   RTC se ha parado (OSF=1), fija la hora de compilación convertida a UTC
   (`hora_compilacion_a_utc()`, andamiaje provisional hasta tener GNSS/red
   móvil). Si el bus o el RTC fallan, el arranque continúa.
3. Crea las tareas `polling` (5), `transmision` (3) y `rtc_debug` (2),
   stack 4096. `app_main` retorna; no tiene bucle propio.

### `components/modbus`

Construcción de tramas Modbus RTU (función 0x03, leer holding registers),
CRC16 Modbus y decodificación de la respuesta a `float` según `TipoDato`:

- `entero16` — entero de 16 bits big-endian (1 registro)
- `float32_CDAB` — float IEEE-754 de 32 bits con palabras intercambiadas
  (CDAB), 2 registros

### `components/sensores`

`TABLA_SENSORES[]` describe cada sensor (esclavo, registro inicial, nº de
registros, tipo de dato y nombres de las magnitudes). Define también
`lectura_sensor` (nombre, valor, `timestamp` `uint32_t`, `valido`).

| Sensor | Slave ID | Registro inicial | Nº registros | Tipo | Magnitudes |
|---|---|---|---|---|---|
| NPK (JXCT JXBS-3001-NPK-RS) | 1 | 0x001E | 3 | `entero16` | Nitrógeno, Fósforo, Potasio |
| OD (REMOND) | 3 | 0x0001 | 4 | `float32_CDAB` | OD_medida, temperatura |
| pH (REMOND RMD-ISHP105) | 5 | 0x0001 | 4 | `float32_CDAB` | Ph, temperatura |

TODO: referencia exacta del modelo del sensor de OD.

### `components/I2C_bus`

Dueño único del bus I2C (driver maestro nuevo de ESP-IDF, modelo
bus-dispositivo). Cada chip registra su propio dispositivo sobre este bus.

| Función | Descripción |
|---|---|
| `i2c_bus_init()` | Crea el bus (idempotente). Llamar desde `app_main` antes de crear tareas |
| `get_i2c_bus()` | Devuelve el handle del bus, o `NULL` si no existe |
| `i2c_bus_scan()` | Sondea 0x08–0x77 y lista las direcciones que responden; se detiene en el primer timeout |

### `components/RTC` (DS3231)

Driver propio en tres capas: conversión pura (BCD, validación de
calendario, día de la semana, segundos UTC), acceso al chip (lectura y
escritura de registros en una sola transacción I2C) y API pública.
Fuente: datasheet DS3231 (Analog Devices / Maxim).

| Función | Descripción |
|---|---|
| `rtc_init()` | Registra el dispositivo 0x68 a 100 kHz y lo sondea. Si no responde, lo elimina del bus y devuelve error (no aborta el arranque) |
| `rtc_leer_hora(struct tm *)` | Lee 00h–06h en ráfaga y devuelve la hora en UTC, validada |
| `rtc_fijar_hora(const struct tm *)` | Escribe 00h–06h en una transacción, relee y verifica (±2 s) y solo entonces borra OSF. Debe llamarla una única tarea |
| `rtc_comprobar_oscilador()` | Lee el flag OSF (0Fh, bit 7) |
| `rtc_leer_temperatura(float *)` | Lee 11h–12h (resolución 0,25 °C; el chip la actualiza cada 64 s) |

Estados devueltos (`rtc_estado_t`): `RTC_OK`, `RTC_NO_RESPONDE`,
`RTC_OSCILADOR_PARADO`, `RTC_HORA_INCOHERENTE`.

### `components/tareas`

- `tarea_polling` — cada `PERIODO_MUESTREO_MS` (4000 ms) recorre
  `TABLA_SENSORES`, envía la petición de cada sensor por UART2, espera la
  respuesta (timeout 1 s), valida longitud y CRC, decodifica y publica en
  `cola_lecturas`. Lee `rtc_timestamp_pais()` una vez por trama válida y
  asigna ese timestamp a todas sus magnitudes. Si el RTC no responde
  (timestamp 0) avisa por consola pero no descarta la medida.
- `tarea_transmision` — único consumidor de `cola_lecturas`. Imprime cada
  lectura con la fecha y hora del país (`AAAA-MM-DD hh:mm:ss`), o
  `SIN HORA` si el timestamp es 0.
- `tarea_rtc` — tarea de depuración: cada segundo imprime la hora UTC del
  RTC, su temperatura y el timestamp del país. Contiene provisionalmente
  `OFFSET_PAIS_S` y `rtc_timestamp_pais()`, que pasarán a un futuro
  módulo `tiempo`.

## Convención de tiempo

- El RTC y cualquier referencia externa (GNSS, NTP) trabajan siempre en UTC.
- El timestamp de cada medida (`lectura_sensor.timestamp`, `uint32_t`,
  segundos desde 1970) es UTC + `OFFSET_PAIS_S`, es decir, la hora local
  del país de despliegue.
- Perú: `OFFSET_PAIS_S = -5 * 3600` (sin horario de verano). Para
  desplegar en otro país se cambia solo esa constante, en `tarea_rtc.h`.
  Un país con horario de verano necesitaría otro mecanismo.
- No se fija `TZ` en el firmware: `mktime` se usa suponiendo `TZ` sin fijar.

## Compilación

Proyecto estándar ESP-IDF v6.1 (`idf.py` envuelve CMake/Ninja). Desde un
entorno ESP-IDF:

```
idf.py build
idf.py -p COM4 flash monitor
```

Primera puesta en hora del RTC:

1. Compilar y flashear con `FORZAR_HORA 1` (en `main.c`) y comprobar la
   hora en el log.
2. Cambiar a `FORZAR_HORA 0`, compilar y flashear de nuevo, para que los
   siguientes arranques conserven la hora del RTC.

`hora_compilacion_a_utc()` toma `__DATE__`/`__TIME__` y resta
`OFFSET_PAIS_S`, así que el PC que compila debe estar en la zona horaria
del país de despliegue.

## Estado validado (22/09/2026)

Sensores NPK, OD y pH leyendo a la vez que el RTC: la hora UTC avanza
segundo a segundo, la temperatura del DS3231 es coherente (~29 °C) y el
timestamp de cada medida es exactamente UTC − 5 h.

## Pendientes y problemas conocidos

- [ ] Fallos de sensor silenciosos: si una trama falla (timeout, CRC,
      longitud) no se encola nada ni se avisa. Observado con el pH por
      cableado defectuoso. Se resolverá con la tabla `estado_sistema` y
      mensajes por sensor.
- [ ] Una medida válida puede salir con timestamp 0 si el RTC falla (solo
      hay aviso por consola).
- [ ] `i2c_bus_scan()`: el aviso de timeout no indica la dirección; se ha
      visto un timeout puntual al arrancar.
- [ ] Prueba de persistencia del RTC: `FORZAR_HORA 0` + reset (EN) + corte
      de VCC con la pila puesta.
- [ ] Aviso al arrancar: flash detectada de 4 MB y cabecera de 2 MB
      (ajustar Flash size en `idf.py menuconfig`).
- [ ] GPIO16/17 están reservados a la PSRAM en el WROVER-E: reasignar UART2
      al migrar al LilyGo T-SIM7600G.
- [ ] Revisar mayúsculas en nombres de archivo y `#include` (Windows no las
      distingue; fallaría al compilar en Linux/CI).
- [ ] Escritura Modbus 0x06 (reprogramar direcciones desde el firmware).
- [ ] Cuarto sensor (sin identificar).

## Estado por fases

- [x] Fase 1 — Comunicación Modbus RTU con un sensor NPK (petición fija,
      CRC16, parseo de N/P/K)
- [x] Fase 2 — Tabla de sensores (`TABLA_SENSORES`), tareas FreeRTOS
      (`polling` / `transmision`) y componentes (`modbus`, `sensores`,
      `tareas`)
- [x] Fase 3 — Bus I2C y RTC DS3231: driver propio, puesta en hora con la
      hora de compilación y timestamp de cada medida en hora de Perú
      (pendiente la prueba de persistencia)
- [ ] Fase 4 — Pantalla OLED SSD1306 y tabla de estado (`estado_sistema`)
      con diagnóstico por sensor legible por personal no técnico
- [ ] Fase 5 — Transmisión remota (SIM7600G): empaquetado por ciclo y
      códigos de estado en la trama
- [ ] Fase 6 — Sincronización temporal: GNSS/red móvil disciplinando el
      DS3231 (módulo `tiempo`)
- [ ] TODO: almacenamiento local y gestión de energía (deep sleep, solar)
