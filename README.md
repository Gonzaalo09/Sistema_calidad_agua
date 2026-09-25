# Nodo sensor ESP32 — Modbus RTU + RTC + OLED

Firmware ESP-IDF para un nodo sensor de campo. Consulta sensores
ambientales por Modbus RTU sobre RS-485, sella cada medida con la hora de
un RTC DS3231 y muestra el estado del sistema en una pantalla OLED.

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
| 4b. Tabla de estado y pantallas de diagnóstico | En curso |
| 5. Almacenamiento en microSD | Pendiente |
| 6. Transmisión remota (SIM7600G) | Pendiente |
| 7. Gestión de energía (deep sleep, solar) | Pendiente |
| 8. Sincronización temporal (GNSS/NTP) | Pendiente |

Última validación (25/09/2026): NPK, OD y pH leyendo junto al RTC y al
OLED en el mismo bus I2C, con el timestamp de cada medida en hora local de
Perú.

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
app_main ── UART2, bus I2C, RTC, OLED ── crea las tareas y retorna

tarea_polling (5) ──cola_lecturas──> tarea_transmision (3)
      │
      ▼
tarea_rtc (2) ──────── RTC (DS3231) ────┐
                                        ├── I2C_bus
tarea_display (2) ─── pantalla_oled ────┘
         │
         └── estado_sistema ←── publican polling, rtc y main
```

`cola_lecturas` tiene un único consumidor. Los demás lectores (pantalla,
trama) usan `estado_sistema`, una tabla compartida protegida con mutex en
la que cada fuente publica su último valor, su estado y cuándo funcionó
por última vez.

### Componentes

| Componente | Contenido |
|---|---|
| `modbus` | Protocolo puro: CRC16, construcción de tramas (0x03) y decodificación según el tipo de dato |
| `sensores` | `TABLA_SENSORES`, el descriptor de cada sensor y el tipo `lectura_sensor` |
| `I2C_bus` | Dueño único del bus I2C; cada chip registra su propio dispositivo |
| `RTC` | Driver propio del DS3231 en tres capas, siempre en UTC |
| `pantalla_oled` | Driver del SSD1306 sobre U8g2, con las callbacks de I2C y esperas |
| `u8g2` | Librería de terceros (olikraus, BSD), copiada al repo para poder compilar sin internet |
| `tareas` | `tarea_polling`, `tarea_transmision` y `tarea_rtc` |
| `estado_sistema` | Tabla de estado por fuente, con mutex (en curso) |

Para añadir un sensor solo se toca `sensores.c`: el número de sensores se
calcula en compilación y un `_Static_assert` corta el build si se supera
el máximo.

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

- Un fallo de sensor todavía no deja rastro visible: se resuelve con
  `estado_sistema` (fase 4b).
- Una medida válida puede salir con timestamp 0 si el RTC falla.
- En el montaje sobre protoboard aparece un dispositivo I2C sin explicar en
  0x5F y timeouts intermitentes en direcciones vacías. Pendiente de
  comprobar sobre PCB.
- GPIO16/17 están reservados a la PSRAM en el WROVER-E: hay que reasignar
  UART2 al migrar al LilyGo.
