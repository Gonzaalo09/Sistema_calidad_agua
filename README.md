# Nodo sensor ESP32 — Modbus RTU + RTC + OLED

Firmware ESP-IDF para un nodo sensor de campo. Consulta sensores
ambientales por Modbus RTU sobre RS-485, sella cada medida con la hora de
un RTC DS3231 y muestra en una pantalla OLED los últimos valores y el
estado de cada componente en lenguaje llano.

TFG de Ingeniería de Telecomunicaciones (UPM, rama electrónica), dentro de
un proyecto de cooperación internacional con despliegue en Perú. El nodo lo
operará personal sin formación para depurar firmware: el requisito que
guía el diseño es que **cualquier fallo se pueda identificar sin conectar
un ordenador**, mirando la pantalla del nodo o los datos transmitidos.

## Estado

| Fase | Estado |
|---|---|
| 1. Modbus RTU con un sensor | Validada en hardware |
| 2. Tabla de sensores y tareas FreeRTOS | Validada en hardware |
| 3. Bus I2C y RTC DS3231 | Validada en hardware |
| 4a. Bring-up de la pantalla OLED | Validada en hardware |
| 4b. Tabla de estado y pantallas de diagnóstico | Validada en hardware |
| 5. Transmisión remota (SIM7600G) | En curso |
| 6. Almacenamiento en microSD | Pendiente |
| 7. Gestión de energía (deep sleep, solar) | Pendiente |
| 8. Sincronización temporal (GNSS/NTP) | Pendiente |

Última validación (25/09/2026): con un sensor o el RTC desconectados, la
pantalla lo indica con una acción concreta, conserva los últimos valores
buenos con la hora a la que se tomaron, y se recupera sola al reconectar.

## Hardware

| Elemento | Detalle |
|---|---|
| MCU (desarrollo) | ESP32-WROOM-32 DevKit, 30 pines |
| MCU (destino) | LilyGo T-SIM7600G (ESP32-WROVER-E) |
| RS-485 | Módulo RS485-TTL con auto-dirección. UART2, TX=GPIO17, RX=GPIO16, 9600 8N1 |
| Bus I2C | I2C_NUM_0, SDA=GPIO21, SCL=GPIO22 |
| RTC | DS3231 (0x68) con pila CR2032, 3,3 V. EEPROM 24C32 del módulo en 0x57 |
| OLED | SSD1306 128×64 de 0,96" (0x3C), 3,3 V |
| Sensores | NPK (JXCT JXBS-3001-NPK-RS), oxígeno disuelto y pH (REMOND). 12 V comunes, GND común con el ESP32 |

## Arquitectura

```
app_main ── UART2, estado_sistema, bus I2C, RTC, OLED ── crea las tareas

tarea_polling (5) ──cola_lecturas──> tarea_transmision (3)
      │
      ├── RTC (DS3231) ──────┐
      ▼                      ├── I2C_bus
   estado_sistema            │
      ▲                      │
      └── tarea_pantalla (2) ─┴── pantalla_oled ── u8g2

tarea_rtc (2, depuracion)
```

`cola_lecturas` tiene un único consumidor. Los demás lectores usan
`estado_sistema`, una tabla compartida protegida con mutex en la que cada
fuente publica su último valor, su estado y cuándo funcionó por última vez.
La pantalla y, más adelante, la trama transmitida, son dos formas de
mostrar esa misma tabla.

### Componentes

| Componente | Contenido |
|---|---|
| `modbus` | Protocolo puro: CRC16, construcción de tramas (0x03) y decodificación según el tipo de dato |
| `sensores` | `TABLA_SENSORES`, el descriptor de cada sensor y el tipo `lectura_sensor` |
| `I2C_bus` | Dueño único del bus I2C; cada chip registra su propio dispositivo |
| `RTC` | Driver propio del DS3231 en tres capas, siempre en UTC |
| `pantalla_oled` | Driver del SSD1306 sobre U8g2: callbacks de I2C y esperas, y primitivas de dibujo por líneas |
| `u8g2` | Librería de terceros (olikraus, BSD), copiada al repo para poder compilar sin internet |
| `estado_sistema` | Tabla de estado por fuente, con mutex |
| `tareas` | `tarea_polling`, `tarea_transmision`, `tarea_rtc` y `tarea_pantalla` |

Para añadir un sensor solo se toca `sensores.c`: el número de sensores se
calcula en compilación y un `_Static_assert` corta el build si se supera
el máximo.

## Diagnóstico en pantalla

Dos páginas que rotan cada 5 s, repintadas cada segundo. El reloj con
segundos aparece en ambas: si se queda parado, el firmware está colgado.

```
2026-09-25 16:41:10          2026-09-25 16:41:15

NPK  84.0 117.0 235.0        RED: SIN DATOS
OD   8.6 27.0 !16:20
  OD: revisar cable/12V
pH   7.7 27.1
```

Un sensor averiado conserva sus últimos valores buenos, marcados con la
hora a la que se tomaron, y añade debajo la acción concreta a realizar.

## Convención de tiempo

El RTC y cualquier referencia externa (GNSS, NTP) trabajan siempre en UTC.
El timestamp de cada medida es UTC + `OFFSET_PAIS_S`, es decir, la hora
local del país de despliegue, como `uint32_t` en segundos desde 1970.
Perú: −5 h, sin horario de verano. Para desplegar en otro país solo hay
que cambiar esa constante.

## Compilación

Proyecto estándar ESP-IDF v6.1. Desde un entorno ESP-IDF:

```
idf.py build
idf.py -p COM4 flash monitor
```

Primera puesta en hora del RTC: compilar con `FORZAR_HORA 1` en `main.c`,
comprobar la hora en el log, y volver a compilar con `FORZAR_HORA 0` para
que los siguientes arranques conserven la hora. El PC que compila debe
estar en la zona horaria del país de despliegue.

## Limitaciones conocidas

- El timestamp marca el **inicio del ciclo de muestreo**, no el instante
  exacto de cada medida: con tres sensores y timeouts de 1 s, la
  desviación puede llegar a 3 s. Irrelevante para el muestreo previsto,
  pero es lo primero a revisar en la fase de sincronización temporal.
- En el montaje sobre protoboard aparece un dispositivo I2C sin explicar en
  0x5F y timeouts intermitentes en direcciones vacías. Pendiente de
  comprobar sobre PCB.
- El electrodo de oxígeno disuelto necesita 4 h de polarización tras
  cualquier corte de alimentación antes de dar medidas fiables.
- GPIO16/17 están reservados a la PSRAM en el WROVER-E: hay que reasignar
  UART2 al migrar al LilyGo.
