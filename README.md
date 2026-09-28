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
| 5. Transmisión remota (SIM7600G) | Pendiente (después de la 6) |
| 6. Almacenamiento en microSD | En curso: pasos 1–3 de 6 validados |
| 7. Gestión de energía (deep sleep, solar) | Pendiente |
| 8. Sincronización temporal (GNSS/NTP) | Pendiente |

Última validación (28/09/2026): cada ciclo de medida viaja completo de
`tarea_polling` a `tarea_transmision`, con los sensores que fallan
incluidos; cada ciclo lleva un número `seq` que sobrevive a reinicios y un
ID de nodo sacado del chip.

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
| MicroSD (en montaje) | Módulo "Micro SD Card Adapter" por SPI, alimentado a 5 V. Tarjeta SanDisk Ultra 16 GB en FAT32 |

## Arquitectura

```
app_main ── UART2, estado_sistema, config_nodo (NVS), bus I2C, RTC, OLED ── crea las tareas

tarea_polling (5) ──cola_lecturas (1 elemento por ciclo)──> tarea_transmision (3)
      │
      ├── RTC (DS3231) ──────┐
      ▼                      ├── I2C_bus
   estado_sistema            │
      ▲                      │
      └── tarea_pantalla (2) ─┴── pantalla_oled ── u8g2

tarea_rtc (2, depuracion)
```

`cola_lecturas` tiene un único consumidor. Cada elemento es un ciclo
completo de medida: la hora, el número `seq` y, por cada sensor, su estado
y sus valores (o ninguno si falló). Los demás lectores usan
`estado_sistema`, una tabla compartida protegida con mutex en la que cada
fuente publica su último valor, su estado y cuándo funcionó por última vez.

### Componentes

| Componente | Contenido |
|---|---|
| `modbus` | Protocolo puro: CRC16, construcción de tramas (0x03) y decodificación según el tipo de dato |
| `sensores` | `TABLA_SENSORES` y el descriptor de cada sensor |
| `config_nodo` | ID del nodo (sacado de la MAC del chip) y número de ciclo `seq`, guardado en la memoria interna (NVS) |
| `I2C_bus` | Dueño único del bus I2C; cada chip registra su propio dispositivo |
| `RTC` | Driver propio del DS3231 en tres capas, siempre en UTC |
| `pantalla_oled` | Driver del SSD1306 sobre U8g2: callbacks de I2C y esperas, y primitivas de dibujo por líneas |
| `u8g2` | Librería de terceros (olikraus, BSD), copiada al repo para poder compilar sin internet |
| `estado_sistema` | Tabla de estado por fuente, con mutex |
| `tareas` | `tarea_polling`, `tarea_transmision`, `tarea_rtc` y `tarea_pantalla`, y el tipo `ciclo` de la cola |

### Añadir o modificar un sensor

Solo se toca `sensores.c`: se añade una fila a `TABLA_SENSORES`, se
recompila y se graba. El número de sensores se calcula en compilación y un
`_Static_assert` corta el build si se supera el máximo.

A partir de la fase 6 (microSD y trama), cada magnitud se identificará en
los archivos y en los datos transmitidos por su nombre de columna,
`nombre_fuente` + magnitud (por ejemplo `NPK_Nitrogeno` u
`OD_temperatura`). De ahí salen estas reglas para quien mantenga el nodo:

- **Si se cambia el `nombre_fuente` de un sensor que ya existe, para la
  base de datos será un sensor distinto**: el histórico queda guardado con
  el nombre antiguo y los datos nuevos empiezan con el nombre nuevo.
- Reordenar las filas de la tabla, en cambio, no afecta a nada.
- `nombre_fuente` tiene que ser único en la tabla y corto (hasta 4
  caracteres, para que quepa en la pantalla).
- Dos sensores pueden tener magnitudes con el mismo nombre (por ejemplo
  `temperatura`): el prefijo del sensor las distingue.
- Al añadir, quitar o renombrar un sensor, el nodo empieza solo un archivo
  nuevo en la microSD, porque cambian las columnas.

### Tarjeta microSD

Diseño de la fase 6, pendiente de implementar.

- Usar una tarjeta **microSDHC de 32 GB como máximo**. La de desarrollo es
  una SanDisk Ultra de 16 GB.
- **Formatear la tarjeta en FAT32 en un ordenador antes de meterla en el
  nodo.** Las tarjetas de 64 GB o más vienen de fábrica en exFAT, y el nodo
  no las lee. En Windows: clic derecho sobre la tarjeta → Formatear →
  sistema de archivos FAT32. También sirve el programa *SD Memory Card
  Formatter* de la SD Association.
- **El nodo nunca formatea la tarjeta por su cuenta.** Si no puede leerla,
  solo lo avisa en pantalla (`SD: SIN FORMATO` / `formatear FAT32 en PC`).
  Así, una tarjeta dañada por un corte de corriente no se borra, y lo que
  tenga se puede intentar recuperar en un ordenador.

## Preparar un nodo antes de llevarlo al campo

1. **Poner en hora el RTC** (si es un RTC nuevo o se ha quedado sin pila):
   ver "Primera puesta en hora del RTC" en Compilación.
2. **Borrar la memoria del ESP32 y grabar el firmware definitivo**, para
   que el contador de ciclos `seq` empiece en 1 y no arrastre los números
   de las pruebas:
   ```
   idf.py -p COM4 erase-flash
   idf.py -p COM4 flash
   ```
   Esto no borra la hora (la guarda el RTC con su pila) ni cambia el ID del
   nodo (viene del chip).
3. **Apuntar el ID del nodo** en una etiqueta pegada a la caja. Sale al
   arrancar, en el log (`ID del nodo: 125010`) y, más adelante, en la
   pantalla. Son 6 letras y números sacados del propio chip: no se
   configura y no se repite entre nodos. Si se cambia la placa de un nodo,
   la placa nueva trae un ID nuevo y hay que actualizar la etiqueta.
4. **Formatear la tarjeta microSD** en FAT32 (ver arriba).

**Una vez desplegado, no volver a usar `erase-flash` en ese nodo**: el
contador `seq` volvería a 1 y el servidor recibiría números repetidos.
Para actualizar el firmware basta con `idf.py -p COM4 flash`, que no borra
el contador.

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

Tras añadir un componente nuevo, ejecutar antes `idf.py reconfigure`.

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
- El NPK tarda hasta 10 s en estabilizarse al alimentarlo: la primera
  lectura después de conectarlo no es fiable.
- GPIO16/17 están reservados a la PSRAM en el WROVER-E: hay que reasignar
  UART2 al migrar al LilyGo.