# Nodo sensor solar ESP32 — Modbus RS-485, red móvil, LoRa y hora sincronizada

Firmware ESP-IDF para un nodo sensor de campo. Consulta sensores
ambientales por Modbus RTU sobre RS-485, sella cada medida con la hora de
un RTC DS3231, la guarda en una microSD, la envía por red móvil a un
servidor y muestra en una pantalla OLED los últimos valores y el estado de
cada componente en lenguaje llano.

TFG de Ingeniería de Telecomunicaciones (UPM, rama electrónica), dentro de
un proyecto de cooperación internacional con despliegue en Perú. El nodo lo
operará personal sin formación para depurar firmware: el requisito que
guía el diseño es que **cualquier fallo se pueda identificar sin conectar
un ordenador**, mirando la pantalla del nodo o los datos transmitidos.

## Qué funciona hoy

Medida, pantalla de diagnóstico, microSD y envío por red móvil (un nodo
con SIM). Pendiente: sincronización de la hora entre nodos, enlace LoRa
y gestión de energía (deep sleep, solar).

## Despliegue previsto (en desarrollo, no implementado)

Seis nodos LilyGo T-SIM7600G en dos grupos de tres, uno a cada lado de
una montaña. Entre los dos lados no hay visión directa, así que los
grupos no se comunican entre sí.

```
Lado A                                   Lado B
nodo ──LoRa──┐                           ┌──LoRa── nodo
             gateway A (SIM)   gateway B (SIM)
nodo ──LoRa──┘      │                │   └──LoRa── nodo
                    └── red móvil ───┴──> servidor
```

- Un **gateway** por grupo, el único con SIM. Los nodos le envían sus
  medidas por LoRa; si uno no le llega, las reenvía el otro nodo.
- El papel (gateway o nodo) se fija al preparar el nodo, según tenga SIM,
  y sale en pantalla.
- Todos llevan microSD. El gateway guarda también lo de sus nodos y lo
  sube al servidor.
- Hora: el gateway la toma de una referencia UTC y sus nodos la copian en
  cada envío. Entre envíos, cada nodo sigue con su DS3231.

## Hardware

### Placas

| Etapa | Placa | Módulo | Módem |
|---|---|---|---|
| **Actual** | **LilyGo T-SIM7000G** | ESP32-WROVER-B (4 MB flash, 8 MB PSRAM) | SIM7000G (LTE-M, NB-IoT, GPRS/EDGE, GNSS) |
| Destino final | LilyGo T-SIM7600G | ESP32-WROVER-E | SIM7600G |

**El firmware actual, incluida la parte del módem (SIM), está hecho para
la T-SIM7000G.** No compila tal cual para la T-SIM7600G.
Cuando el nodo funcione y esté comprobado en campo, se separará lo que
depende de la placa y del módem para que pasar a la T-SIM7600G sea más
sencillo. Hasta entonces, cambiar de placa exige revisar a mano los pines
de `main.c` y `tarjeta_sd.c` y reescribir la parte del módem.

### Elementos

| Elemento | Detalle |
|---|---|
| RS-485 | Módulo RS485-TTL con auto-dirección, alimentado a 3,3 V. UART2 a 9600 8N1, **TX=GPIO19, RX=GPIO18** (16 y 17 están ocupados por la PSRAM del WROVER). Los pines `TX`/`RX` del header son la consola (UART0): no conectar ahí el RS-485 |
| Módem (T-SIM7000G) | SIM7000G por UART1 a 115200: TX del ESP32=GPIO27, RX=GPIO26; encendido (PWRKEY) por GPIO4. No conectar nada a los pines 4 y 25 del header. Hace falta SIM con datos y antena conectada |
| Bus I2C | I2C_NUM_0, SDA=GPIO21, SCL=GPIO22 (igual en las dos placas) |
| RTC | DS3231 (0x68) con pila CR2032, 3,3 V. EEPROM 24C32 del módulo en 0x57 |
| OLED | SSD1306 128×64 de 0,96" (0x3C), 3,3 V |
| Sensores | NPK (JXCT JXBS-3001-NPK-RS, mg/kg), oxígeno disuelto (REMOND, mg/L) y pH (REMOND). 12 V comunes de una fuente externa, GND común con la placa |
| MicroSD | Slot de la placa: CS=GPIO13, SCK=GPIO14, MOSI=GPIO15, MISO=GPIO2. El módulo externo no se usa. Los pines `SCLK`/`MISO`/`MOSI`/`CS` del header (18/19/23/5) **no** son el slot |

### Alimentación de la T-SIM7000G

- Se alimenta por USB-C (5 V), por la batería 18650 del portapilas o por
  el pin VBAT. El pin VBUS del header es la misma línea que el USB-C: solo
  tiene 5 V con el USB conectado. **Con batería sola no hay 5 V en ningún
  pin**: OLED, RTC y módulo RS-485 van al pin 3V3 de la placa.
- El conector solar (panel de 5–6 V) **solo carga la batería** (máx.
  500 mA). Sin batería, el panel no enciende la placa.
- Al conectar la batería por primera vez, pulsar el botón PWR para
  activarla.
- La placa puede reiniciarse al conectar o desconectar el USB con la
  batería puesta. Es normal (no hay conmutación sin cortes).
- Los sensores siguen a 12 V con su propia fuente. GND común con la placa.
- No conectar ni desconectar módulos de la alimentación de 3V3 con el nodo
  encendido: al quitar el RTC en caliente la placa se reinició por caída de
  tensión.

## Arquitectura

```
app_main ── UART2, estado_sistema, config_nodo (NVS), bus I2C, RTC, OLED, SD, módem ── crea las tareas

tarea_polling (5) ──cola_lecturas (1 elemento por ciclo)──> tarea_transmision (3) ──┬── modem ── servidor (HTTP)
      │                                                          │                  └── tarjeta_sd
      ├── RTC (DS3231) ──────┐                                   │ (estado de la red y de la SD)
      ▼                      ├── I2C_bus                         ▼
   estado_sistema  <─────────┼───────────────────────────────────┘
      ▲                      │
      └── tarea_pantalla (2) ─┴── pantalla_oled ── u8g2

tarea_rtc (2, depuracion)
```

`cola_lecturas` tiene un único consumidor. Cada elemento es un ciclo
completo de medida: la hora, el número `seq` y, por cada sensor, su estado
y sus valores (o ninguno si falló). Los demás lectores usan
`estado_sistema`, una tabla compartida protegida con mutex en la que cada
fuente publica su último valor, su estado y cuándo funcionó por última vez,
más un registro aparte con los datos de la red móvil.

### Componentes

| Componente | Contenido |
|---|---|
| `modbus` | Protocolo puro: CRC16, construcción de tramas (0x03) y decodificación según el tipo de dato |
| `sensores` | `TABLA_SENSORES` y el descriptor de cada sensor |
| `config_nodo` | ID del nodo (sacado de la MAC del chip) y número de ciclo `seq`, guardado en la memoria interna (NVS) |
| `tarjeta_sd` | Tarjeta microSD por SPI con sistema de archivos FAT: montar, desmontar, escribir. Nunca formatea |
| `modem` | SIM7000G por comandos AT sobre UART1: encender, apagar, enviar un comando, comprobar la SIM, abrir la conexión de datos, hacer un POST HTTP (devuelve el código del servidor o en qué paso falló) y leer los datos de la red (operador, tecnología, señal, IP) |
| `I2C_bus` | Dueño único del bus I2C; cada chip registra su propio dispositivo |
| `RTC` | Driver propio del DS3231 en tres capas, siempre en UTC |
| `pantalla_oled` | Driver del SSD1306 sobre U8g2: callbacks de I2C y esperas, y primitivas de dibujo por líneas |
| `u8g2` | Librería de terceros (olikraus, BSD), copiada al repo para poder compilar sin internet |
| `estado_sistema` | Tabla de estado por fuente, con mutex, y registro de la red |
| `tareas` | `tarea_polling`, `tarea_transmision`, `tarea_rtc` y `tarea_pantalla`, el tipo `ciclo` de la cola, y `registro_csv` (formato de la línea CSV, de la trama JSON y códigos de `tx`) |

### Añadir o modificar un sensor

Solo se toca `sensores.c`: se añade una fila a `TABLA_SENSORES`, se
recompila y se graba. El número de sensores se calcula en compilación y un
`_Static_assert` corta el build si se supera el máximo.

Cada magnitud se identifica en los archivos de la microSD y en los datos
transmitidos por su nombre de columna, `nombre_fuente` + magnitud (por
ejemplo `NPK_Nitrogeno` u `OD_temperatura`). De ahí salen estas reglas
para quien mantenga el nodo:

- **Si se cambia el `nombre_fuente` de un sensor que ya existe, para la
  base de datos será un sensor distinto**: el histórico queda guardado con
  el nombre antiguo y los datos nuevos empiezan con el nombre nuevo.
- Reordenar las filas de la tabla, en cambio, no afecta a nada.
- `nombre_fuente` tiene que ser único en la tabla, corto (hasta 4
  caracteres, para que quepa en la pantalla) y **sin guion bajo `_`**:
  el servidor separa el sensor de la magnitud por el primer `_` del
  nombre de columna (`OD_OD_medida` → sensor `OD`, magnitud
  `OD_medida`). Los nombres de magnitud (`nombres_datos`) sí pueden
  llevarlo.
- Dos sensores pueden tener magnitudes con el mismo nombre (por ejemplo
  `temperatura`): el prefijo del sensor las distingue.
- Al añadir, quitar o renombrar un sensor, el nodo empieza solo un archivo
  nuevo en la microSD (`_2`, `_3`…), porque cambian las columnas. Si se
  vuelve a la configuración anterior, sigue escribiendo en el archivo
  original.

### Tarjeta microSD

#### Archivos de datos

Un archivo por día, en una carpeta por año, con el ID del nodo delante
para que no se mezclen tarjetas de nodos distintos:

```
/2026/125010_2026-09-29.csv      día normal
/2026/125010_2026-09-29_2.csv    mismo día, después de cambiar los sensores
/125010_SINHORA.csv              medidas tomadas con el RTC sin hora
```

El día es el del país de despliegue: el archivo cambia a las 00:00 locales.
Cada línea es un ciclo de medida. Columnas, en orden:

| Columna | Contenido |
|---|---|
| `formato` | Versión del formato del archivo (hoy `1`) |
| `fecha` | Fecha y hora local legibles. Vacía si el RTC no tenía hora |
| `ts` | La misma hora en segundos desde 1970 (0 sin hora) |
| `nodo` | ID del nodo |
| `seq` | Número de ciclo: nunca se repite en un nodo. Un hueco es un ciclo que no llegó a la tarjeta |
| `rtc` | Código de estado del RTC |
| `<sensor>_est` | Código de estado del sensor, y a continuación una columna por magnitud (`NPK_Nitrogeno`…). Si el sensor falló, sus valores quedan vacíos |
| `tx` | Resultado del envío de ese ciclo: código HTTP del servidor (`200` = guardado) o un código propio del nodo menor que 100. Ver "Transmisión de datos" |

Abrir en Excel: **Datos → Desde texto/CSV**, delimitador **punto y coma**.
Los decimales van con **punto**: si Excel está en español y convierte
`17.298` en `17298` o lo deja como texto, hay que indicar el punto como
separador decimal (en "Transformar datos", cambiar el tipo de esas columnas
usando la configuración regional Inglés (Estados Unidos)).

#### Uso de la tarjeta

- Usar una tarjeta **microSDHC de 32 GB como máximo**. Probada con una
  SanDisk Ultra de 16 GB.
- **Formatear la tarjeta en FAT32 en un ordenador antes de meterla en el
  nodo**, aunque sea nueva. Las tarjetas de 64 GB o más vienen de fábrica en exFAT, y el
  nodo no las lee. En Windows: clic derecho sobre la tarjeta → Formatear →
  sistema de archivos FAT32. También sirve el programa *SD Memory Card
  Formatter* de la SD Association.
- **El nodo nunca formatea la tarjeta por su cuenta.** Si no puede leerla,
  solo lo avisa en pantalla (`SD SIN FORMATO` / `formatear FAT32 en PC`).
  Así, una tarjeta dañada por un corte de corriente no se borra, y lo que
  tenga se puede intentar recuperar en un ordenador.
- Sacar y meter la tarjeta **con el nodo sin alimentación**. Si se saca en
  marcha, el nodo lo detecta y la vuelve a montar al meterla, pero se
  arriesga a cortar una escritura a medias.
- **T-SIM7000G: sacar la tarjeta antes de grabar el firmware.** Con la
  tarjeta puesta, el ordenador puede no conseguir grabar la placa.

## Transmisión de datos

En cada ciclo, `tarea_transmision` construye la trama, la envía, lee el
estado de la red para la pantalla y después guarda la línea en la microSD
con el resultado en la columna `tx`. Un solo intento por ciclo: lo que
falle queda en la tarjeta con su `tx`.

1. Trama: un objeto JSON plano con las mismas claves que las columnas del
   CSV, salvo `fecha` y `tx`. Un sensor que falla, o un valor que no es un
   número, va como `null`.
2. Se comprueba que la SIM está lista (`AT+CPIN?`).
3. Se comprueba el registro en la red (`AT+CEREG?` para LTE y `AT+CGREG?`
   para GSM). Sin red no se intenta la conexión de datos.
4. Se activa la conexión de datos (APN de la SIM) si no lo estaba.
5. POST HTTP al servidor (`DOMINIO_SERVIDOR` y `RUTA_MEDIDAS` en
   `tarea_transmision.c`). Sin seguridad por ahora: HTTP sin `s` y sin clave.

El módem se enciende al arrancar y **se queda encendido y registrado entre
ciclos** (el comportamiento definitivo, con deep sleep, está pendiente).

### Valor de `tx`

| `tx` | Significado | En pantalla | ¿Se reenviará más adelante? |
|---|---|---|---|
| 0 | No intentado | — | — |
| 200 | Guardado en el servidor (o ya estaba) | `ENVÍO OK hh:mm` | No |
| 400 | El servidor rechaza la trama: falta `nodo`, `seq`, `ts` o `rtc` | `TRAMA RECHAZADA` / `avisar al tecnico` | No |
| 403 | Dispositivo desactivado en la plataforma | `NODO DESACTIVADO` / `avisar al tecnico` | No |
| 409 | Mismo `(nodo, seq)` con otro contenido: va a incidencias | `SEQ DUPLICADO` / `avisar al tecnico` | No |
| 500 | Error del servidor o de su base de datos | `ERROR SERVIDOR` | Sí |
| 1 | La SIM no está lista | `SIN SIM` / `revisar SIM` | Sí |
| 2 | Sin red (ni LTE ni GSM registrados) | `SIN RED` / `revisar antena` | Sí |
| 3 | No se pudo activar la conexión de datos | `SIN DATOS` / `revisar saldo de SIM` | Sí |
| 4 | El módem no contesta | `MODEM NO CONTESTA` / `avisar al tecnico` | Sí |
| 5 | No se pudo conectar con el servidor | `SIN SERVIDOR` | Sí |
| 6 | El módem devolvió error durante el envío | `ERROR MODEM` / `avisar al tecnico` | Sí |
| 7 | No llegó la respuesta del servidor a tiempo | `SIN RESPUESTA` | Sí |
| 8 | La trama no cabe en el buffer | `TRAMA NO CABE` / `avisar al tecnico` | No |

Estos valores viajan en la tarjeta: no se cambian ni se reutilizan; los
nuevos se añaden con un número nuevo. El reenvío desde la tarjeta todavía
no existe.

### Tiempos y consumo

- Registrarse en la red puede tardar hasta 2 min después de encender el
  módem. Sin red, los ciclos siguen guardándose en la microSD; cuando
  vuelve la cobertura, el ciclo siguiente se envía sin reiniciar el nodo
  (lo que falló antes queda solo en la tarjeta).
- Donde no hay LTE-M, el módem usa GSM/GPRS: un envío tarda 2–3 s, pero
  pide picos de 2 A (0,6 A en LTE-M) y con batería baja puede reiniciar
  la placa.

## Conectar la placa al ordenador (una vez por PC)

El ESP32 no habla USB directamente: entre el USB-C y el ESP32 hay un chip
puente USB-serie. Windows necesita el driver de ese chip para crear un
puerto COM, y sin puerto COM no se puede grabar ni ver el log.

| Revisión de la T-SIM7000G | Chip puente | Driver |
|---|---|---|
| V1.6 (serigrafía "20-4-15") | CP2104 (Silicon Labs) | *CP210x Universal Windows Driver*, de la web de Silicon Labs |
| Revisiones posteriores | CH9102 (WCH) | *CH343SER*, de la web de WCH |

1. Conectar la placa con un cable **USB-A a USB-C** que transmita datos.
   Muchos cables solo cargan: la placa enciende (LED verde) pero el PC no
   la ve. LilyGo desaconseja los cables USB-C a USB-C.
2. Abrir el **Administrador de dispositivos** de Windows y desplegar
   **Puertos (COM y LPT)**.
   - Si aparece "Silicon Labs CP210x USB to UART Bridge (COMx)" o
     "USB-Enhanced-SERIAL CH9102 (COMx)", el driver ya está: ir al
     paso 4.
   - Si no aparece, desplegar **Otros dispositivos**. Si la placa está
     ahí con un triángulo amarillo, falta el driver: paso 3.
   - Si no aparece en ningún sitio, desenchufar y enchufar mirando qué
     entrada aparece y desaparece. Si no cambia nada, el problema es el
     cable o el puerto USB, no el driver.
3. Instalar el driver de la tabla: descomprimirlo, clic derecho sobre el
   archivo `.inf` → **Instalar**, y volver a conectar la placa.
4. Apuntar el número de COM. Es el que se pone en `-p COMx` en los
   comandos y en `.vscode/settings.json` (o con el botón de puerto de la
   extensión ESP-IDF de VS Code).

El número de COM lo asigna Windows y puede cambiar de un PC a otro, o si
se conecta la placa en otro puerto USB.

## Preparar un nodo antes de llevarlo al campo

En los comandos, `COMx` es el puerto de la placa (ver "Conectar la placa
al ordenador").

1. **Poner en hora el RTC** (si es un RTC nuevo o se ha quedado sin pila):
   ver "Primera puesta en hora del RTC" en Compilación.
2. **Sacar la microSD** (T-SIM7000G), **borrar la memoria del ESP32 y
   grabar el firmware definitivo** (con `FORZAR_HORA 0`), para que el
   contador de ciclos `seq` empiece en 1 y no arrastre los números de las
   pruebas ni datos del firmware de fábrica:
   ```
   idf.py -p COMx erase-flash
   idf.py -p COMx flash
   ```
   Esto no borra la hora (la guarda el RTC con su pila) ni cambia el ID del
   nodo (viene del chip).
3. **Apuntar el ID del nodo** en una etiqueta pegada a la caja. Sale al
   arrancar, en el log (`ID del nodo: 125010`) y en la primera línea de la
   pantalla. Son 6 letras y números sacados del propio chip: no se
   configura y no se repite entre nodos. Si se cambia la placa de un nodo,
   la placa nueva trae un ID nuevo y hay que actualizar la etiqueta.
4. **Formatear la tarjeta microSD** en FAT32 (ver arriba) y volver a
   meterla.
5. **Dar de alta el nodo en la plataforma del servidor** con su ID. Si no,
   el servidor responde 403 (`NODO DESACTIVADO` en pantalla).
6. Si el nodo va a **otro país que no sea Perú**, hacer antes los cambios
   de "Desplegar en otro país".

**Una vez desplegado, no volver a usar `erase-flash` en ese nodo**: el
contador `seq` volvería a 1 y el servidor recibiría números repetidos.
Para actualizar el firmware basta con `idf.py -p COMx flash`, que no borra
el contador.

## Desplegar en otro país

El firmware está preparado para Perú. Para instalar nodos en otro país hay
que revisar todo lo de esta tabla **antes** de grabarlos. Si se añade al
proyecto algo más que dependa del país, se apunta aquí.

| Qué | Dónde se cambia | Cómo |
|---|---|---|
| Desfase horario del país | Firmware: `OFFSET_PAIS_S` en `tarea_rtc.h` (componente `tareas`) | Horas respecto a UTC × 3600, con signo. Perú (UTC−5): `(-5 * 3600)`. Ejemplo, Bolivia (UTC−4): `(-4 * 3600)` |
| El mismo desfase en el servidor | Servidor: campo `desfase_s` del nodo en la tabla de nodos | El mismo valor en segundos (Perú: `-18000`). Si no coincide con el del firmware, el servidor calculará mal la hora de todas las medidas de ese nodo |
| Puesta en hora del RTC | PC con el que se compila con `FORZAR_HORA 1` | Toma la hora del PC y le resta `OFFSET_PAIS_S`: el reloj del PC tiene que estar en la hora del país de destino al compilar |
| Red móvil | Tarjeta SIM y firmware: `APN_RED`, `USUARIO_APN` y `CLAVE_APN` en `tarea_transmision.c` (hoy Claro Perú: `claro.pe`, `claro`, `claro`) | SIM de un operador local, con su APN, usuario y contraseña. Comprobar qué tecnología hay en la zona y que el módem de la placa admite sus bandas: SIM7000G Cat-M1 en B1/B2/B3/B4/B5/B8/B12/B13/B18/B19/B20/B26/B28/B39 (SIMCom, Hardware Design V1.07), además de GSM/GPRS; SIM7600G, sin verificar. En Perú, en los sitios probados, solo había 2G |
| Servidor de datos | Firmware: `DOMINIO_SERVIDOR` y `RUTA_MEDIDAS` en `tarea_transmision.c` | Dominio (`http://dominio`, máx. 64 caracteres) y ruta de la API que recibe las medidas. El nodo debe estar dado de alta y activo en esa plataforma, o el servidor responde 403 |
| Idioma de la pantalla | Firmware: `estado_sistema.c` (`estado_sistema_texto`, `estado_sistema_accion`) y `tarea_pantalla.c` | Los mensajes están en español. Cada acción cabe en 23 caracteres. La fuente admite acentos y ñ (Latin-1), no otros alfabetos |

**Limitación: solo países sin horario de verano.** El desfase es fijo. En
un país que cambia la hora dos veces al año (por ejemplo España o la mayor parte de Chile),
las medidas quedarían con una hora de error durante la mitad del año. Para
esos países hace falta un cambio de diseño en el firmware, todavía no
hecho.

Después de cambiar el firmware, grabar con `idf.py -p COMx flash`. Si el
nodo es nuevo, seguir "Preparar un nodo antes de llevarlo al campo"
completo.

## Diagnóstico en pantalla

Dos páginas que rotan cada 5 s, repintadas cada segundo. La primera línea
de las dos es el reloj (día/mes y hora con segundos) y el ID del nodo: si
los segundos se quedan parados, el firmware está colgado.

```
16/10 16:41:10 ID:AC3D04     16/10 16:41:15 ID:AC3D04

NPK  84.0 117.0 235.0        Claro GSM -60dBm
OD   8.6 27.0 !16:20         IP 100.74.207.3
  OD: revisar cable/12V      ENVÍO OK 16:41
pH   7.7 27.1

                             SD OK, guardado 16:41
```

Página 1: un sensor averiado conserva sus últimos valores buenos, marcados
con la hora a la que se tomaron, y añade debajo la acción concreta a
realizar.

Página 2: operador, tecnología y señal de la red móvil; dirección IP (`IP
--` sin conexión de datos); resultado del último envío y, si hay algo que
se pueda arreglar en el sitio, la acción debajo (ver la tabla de `tx`).
Si un envío falla, aparece la hora del último envío correcto, por ejemplo:

```
SIN RED !16:20
  revisar antena
```

La línea de la SD dice la hora del último guardado correcto. Si la tarjeta
falla, muestra el fallo con esa hora y debajo qué hacer, por ejemplo:

```
SD NO DETECTADA !16:20
  meter o cambiar SD
```

`!--:--` significa que no se ha guardado (o enviado) nada desde que se
encendió el nodo. Sin hora en el RTC, la primera línea es
`-- SIN HORA -- ID:AC3D04`.

## Convención de tiempo

El RTC y cualquier referencia externa (GNSS, NTP) trabajan siempre en UTC.
El timestamp de cada medida es UTC + `OFFSET_PAIS_S`, es decir, la hora
local del país de despliegue, como `uint32_t` en segundos desde 1970.
Perú: −5 h, sin horario de verano. Para otro país, ver "Desplegar en
otro país".

## Compilación

Proyecto estándar ESP-IDF v6.1 (target `esp32`, el mismo en las dos
placas). Desde un entorno ESP-IDF:

```
idf.py build
idf.py -p COMx flash monitor
```

Tras añadir un componente nuevo, ejecutar antes `idf.py reconfigure`.

Abrir el monitor reinicia la placa. Para ver el log sin reiniciarla:
`idf.py -p COMx monitor --no-reset`.

### Configuración del proyecto (menuconfig)

La configuración de ESP-IDF está en dos archivos del repositorio:
`sdkconfig` (completa, generada por menuconfig) y `sdkconfig.defaults`
(solo las opciones que el nodo cambia respecto a los valores por
defecto). Quien clona el proyecto ya tiene la configuración y no
necesita abrir menuconfig. Al cambiar una opción en menuconfig, ejecutar
después `idf.py save-defconfig` y guardar los dos archivos juntos. Al
pasar a otra versión de ESP-IDF, borrar `sdkconfig` y compilar: se
regenera a partir de `sdkconfig.defaults`. Esta tabla dice qué opciones
necesita el nodo y por qué.

menuconfig se abre en el ordenador, **sin la placa conectada**: edita
`sdkconfig`, y los cambios llegan a la placa al compilar y grabar.
Se abre con `idf.py menuconfig` (o el icono de engranaje "SDK
Configuration Editor" de la extensión ESP-IDF). Se navega con las flechas,
`S` guarda y `Q` sale. Después, `idf.py build`.

| Opción (ruta en menuconfig) | Valor | Por qué |
|---|---|---|
| Serial flasher config → Flash size | **4 MB** | Es la flash real del ESP32-WROVER-B. Con 2 MB el firmware funciona, pero ESP-IDF avisa al arrancar y solo usa la mitad |
| Partition Table | **No cambiar** | Si se mueve la partición `nvs`, el nodo pierde el contador `seq` |
| Component config → Hardware Settings → Chip revision → Minimum Supported ESP32 Revision | **Rev v0.0** (por defecto) | Hay T-SIM7000G con chips de revisión antigua (v1.0) y moderna (v3.1). Con el mínimo en v0.0 el firmware arranca en todas; con un mínimo mayor que el del chip (p. ej. v3.0 en una placa v1.0), el bootloader se niega a arrancar (`Image requires chip rev >= v3.0`) |
| Component config → ESP PSRAM → Support for external, SPI-connected RAM | **Desactivada** (por defecto) | El firmware no necesita la RAM externa. Pendiente de decisión definitiva |
| Component config → FAT Filesystem support → Long filename support | **Activada** (cualquier modo salvo "No long filenames") | Los archivos de la SD tienen nombres largos (`125010_2026-09-29.csv`). Sin esto no se pueden crear |

### Primera puesta en hora del RTC

`FORZAR_HORA 1` en `main.c` pone el RTC a la hora a la que se **compiló**
`main.c` (no a la hora del arranque), y lo vuelve a hacer en cada
reinicio, incluido el que provoca abrir el monitor. Procedimiento:

1. Sacar la microSD y poner `FORZAR_HORA 1` (cambiar el valor obliga a
   recompilar `main.c` con la hora actual).
2. `idf.py -p COMx flash`: compila y graba seguido, así que el error es
   solo lo que tardan el enlazado y la grabación (decenas de segundos).
3. Sin reiniciar entre medias, poner `FORZAR_HORA 0` y volver a grabar.
   Desde ahí la hora se conserva en cada reinicio.

El PC que compila debe estar en la zona horaria del país de despliegue.
La puesta en hora automática (GNSS o red móvil) está en desarrollo.

Si la T-SIM7000G no entra en modo de grabación (y la microSD ya está
fuera): unir IO0 con GND con un cable, pulsar RST, soltarlo, quitar el
cable y grabar.

## Limitaciones conocidas

- El timestamp marca el **inicio del ciclo de muestreo**, no el instante
  exacto de cada medida: con tres sensores y timeouts de 1 s, la
  desviación puede llegar a 3 s. Irrelevante para el muestreo previsto,
  pero es lo primero a revisar al implementar la sincronización.
- El RTC no sigue contando cuando se le quita la alimentación de la placa:
  al volver, continúa desde la hora a la que se quitó (en estudio:
  pila o configuración del chip). Si el nodo se reinicia en ese estado,
  vuelve a la hora de compilación.
- El electrodo de oxígeno disuelto necesita 4 h de polarización tras
  cualquier corte de alimentación antes de dar medidas fiables.
- El NPK tarda hasta 10 s en estabilizarse al alimentarlo: la primera
  lectura después de conectarlo no es fiable.
- Transmisión: sin seguridad (HTTP sin clave ni cifrado), el módem se
  queda encendido entre ciclos y el reenvío de lo que falle no está hecho.
- El módem se registra en GSM donde no hay LTE-M (todos los sitios
  probados), con picos de 2 A.
- El desfase horario es fijo: el nodo no sirve tal cual en países con
  horario de verano (ver "Desplegar en otro país").
