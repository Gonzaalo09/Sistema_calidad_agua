**Arranque y configuración básica**



AT	Pregunta si el módem contesta (responde OK)	Saber si ya está encendido antes de pulsar PWRKEY, y comprobar que contesta tras el pulso



ATE0	Apaga el eco de los comandos	Que en la respuesta solo salga lo que contesta el módem. No se guarda: hay que mandarlo en cada

encendido



AT+CMEE=1	Errores como +CME ERROR: <número>	Saber qué falló, no solo ERROR. No se guarda



AT+CGMR	Versión del firmware del módem	Saber qué versión corre, porque la sintaxis de algunos comandos cambia entre versiones



AT+CGSN	IMEI del módem	Identificar el módem



AT+IPR?	Velocidad de la UART (0 = autobaud)	Saber si la velocidad es fija



AT+CSCLK?	Modo sueño por pin DTR (0 = desactivado)	Saber si el módem puede dormirse solo. Importa para el consumo solar



AT+CFUN?	Funcionalidad (1 = completa, radio encendida)	Comprobar que la radio está activa



AT+CPOWD=1	Apagado normal	Apagar el módem entre ciclos. No contesta OK: su única respuesta es NORMAL POWER DOWN



**SIM y tecnologías**



AT+CPIN?	Estado de la SIM (READY = lista, sin PIN)	Comprobar que hay SIM válida antes de registrar. No basta el OK, hay que ver READY



AT+CCID	ICCID, el número de serie de la SIM	Comprobar qué SIM es (operador, país)



AT+CNMP?	Modos permitidos (2 = automático GSM/LTE)	Saber por qué cae a GSM



AT+CMNB?	Dentro de LTE: Cat-M1, NB-IoT o ambos (3)	Ídem



AT+CBANDCFG?	Bandas habilitadas por tecnología	Saber si las bandas de Claro/Perú están cubiertas



**Red y señal**



AT+CEREG?	Registro en LTE (<stat>: 1 casa, 5 roaming, 3 denegado, 0 sin registro y sin buscar)	Esperar el registro. El firmware da por bueno 1 o 5



AT+CGREG?	Igual, para GSM/GPRS	Es donde acaba registrando en Perú (CGREG 1)



AT+COPS?	Operador y tecnología (3 = GSM EGPRS)	Mostrar el operador en pantalla y en el log



AT+CPSI?	Información de la celda: sistema, MCC-MNC, celda, canal, nivel	Ver la tecnología (GSM, Cat-M, NB-IoT) y la señal en dBm



AT+CSQ	Calidad de señal. dBm = −114 + 2 × rssi (rssi 2–30)	Medir cobertura



**Datos y HTTP**



AT+CNCFG=1,"apn","user","pass",1	Configura el contexto de datos 1 (APN, usuario, clave, PAP)	Preparar la conexión de datos. En

V1.06 va todo en un comando



AT+CNACT=1	Activa la conexión de datos. Contesta OK enseguida y la IP llega después	Dar conexión de datos al módem



AT+CNACT?	Estado (+CNACT: 1,"ip", 1 = activa)	Ver si ya está activa y esperar a que lo esté



AT+SHDISC	Cierra la conexión HTTP	Limpiar antes de un envío y cerrar siempre al terminar



AT+SHCONF="URL",... / "BODYLEN",1024 / "HEADERLEN",350	Guarda dominio y tamaños máximos	Configurar el HTTP antes de conectar



AT+SHCONN	Abre la conexión con el servidor	Conectar con el servidor



AT+SHAHEAD="Content-Type","application/json"	Guarda una cabecera	Que el servidor sepa que mandamos JSON



AT+SHBODEXT=<len>,<ms>	Anuncia el cuerpo (los bytes van crudos por la UART)	Cargar el JSON. El firmware no manda Ctrl-Z



AT+SHREQ="ruta",3	Lanza el POST. Responde OK y luego +SHREQ: "POST",<código>,<len>	Enviar los datos y leer el código HTTP



**Hora (NTP)**



AT+CLTS?	Si el reloj del módem se actualiza con la red (NITZ)	Saber de dónde sale la hora de CCLK. Vale 1 en tu módem



AT+CCLK?	Reloj del módem, "yy/MM/dd,hh:mm:ss±zz", resolución 1 s	Leer la hora tras CNTP y medir la fase del cambio de segundo



AT+CNTP?	Configuración de NTP (servidor, zona, cid, modo)	Ver qué servidor usa. Mostró ...,0,1,2: cid 1, modo 2



AT+CNTP="srv",0	Fija servidor y zona (0 = UTC)	CCLK queda en UTC



AT+CNTP	Pide la hora. OK y luego +CNTP: <código>\[,"hora"]: 1 ok, 61 error de red, 62 DNS, 63 conexión, 64 error de respuesta, 65 timeout	Sincronizar. Salió 61 sin SAPBR, 64 con google.com y 1 con el pool



AT+SAPBR=3,1,"APN"/"USER"/"PWD",...	Configura el portador 1	CNTP usa este portador, no el de CNACT



AT+SAPBR=1,1	Abre el portador (hasta 85 s). Da +CME ERROR: 3 si ya estaba abierto	Dejar CNTP operativo



AT+SAPBR=2,1	Consulta (+SAPBR: 1,<estado>,"ip"; 1 = abierto, 3 = cerrado)	Comprobar si está abierto



AT+SAPBR=0,1	Lo cierra (hasta 65 s)	Cronometrar la apertura desde cero





**GNSS**



AT+SGPIO=0,4,1,1	Fija un GPIO del módem. Parámetros: operación 0 (escribir), GPIO 4 del módem (pin 48, no el GPIO4 del ESP32), función 1 (salida), nivel 1 (alto). No se guarda (NO\_SAVE): hay que repetirlo en cada encendido.	Alimenta la antena GNSS activa. Sin él no había fix. Con él aparecieron satélites a 24–43 dBHz y llegó el fix. Que sea ese GPIO es una hipótesis que el resultado confirma. Apágalo (nivel 0) cuando apagues el GNSS.



AT+CGNSPWR=1 / =0	Enciende o apaga el receptor GNSS.	El GNSS arranca apagado. Se apaga al terminar para ahorrar energía.



AT+CGNSURC=n	Emite un URC +UGNSINF: cada n fixes. =1 es uno por segundo y =0 lo desactiva.	Da muestras de hora UTC a 1 Hz sin sondear.



AT+CGNSINF	Devuelve el estado GNSS una vez. Campos: 1 run status, 2 fix status, 3 UTC yyyyMMddhhmmss.sss, 4 latitud, 5 longitud, 6 altitud, 9 fix mode, 11–13 HDOP/PDOP/VDOP, 15 satélites a la vista, 16 GPS usados, 17 GLONASS usados, 19 C/N0 máximo.	Comprobar si hay fix y de dónde sale la hora UTC de referencia.



+UGNSINF:	No se envía: es el URC con los mismos campos que +CGNSINF.	Es lo que lee leer\_fix\_gnss.



AT+CGNSTST=1 / =0	Manda las sentencias NMEA crudas por el puerto AT. El estado persiste aunque se reinicie el ESP32.	Para diagnosticar (ver satélites en GSV). Déjalo en 0: inunda la UART y provocó el watchdog.



AT+CGNSNMEA=<máscara>	Elige qué sentencias NMEA salen.	Reducir el volumen si usas CGNSTST. Lo vimos en la documentación, no lo usamos.



AT+CGNSCOLD / WARM / HOT	Arranque en frío, templado o en caliente del GNSS.	Cold es el más lento, no acelera el fix. Solo sirve para forzar un arranque limpio.



AT+CGNSXTRA	A-GPS: descarga datos de efemérides.	Acelera el primer fix. No lo hemos usado.



**RESPUESTA DE GPS EXPLICACIÓN DE LO QUE SALE COMO RESPUESTA:**

ATE0, AT+CMEE=1 y registrado a los 40 ms solo preparan el módem y confirman que está en la red. No hay nada que interpretar aquí.



AT+CGNSTST=0 apaga el flujo de texto NMEA que causó el cuelgue.

AT+SGPIO=0,4,1,1 alimenta la antena activa.

AT+CGNSPWR=1 enciende el receptor GPS.

AT+CGNSURC=1 hace que el módem avise cada segundo con la hora GPS.



**Luego vienen 10 muestras de GNSS**: muestra GNSS 1: UTC 1792448595000 ms, llegada 590 ms, diferencia 1792448594410 ms

muestra GNSS 2: UTC 1792448596000 ms, llegada 1590 ms, diferencia 1792448594410 ms ....



UTC es la hora real que dice el GPS, en milisegundos desde 1970.

llegada es lo que marcaba el cronómetro del ESP32 cuando llegó ese aviso.

diferencia es UTC menos llegada.



Esa diferencia es el puente entre los dos relojes: cuando mi cronómetro marca X, la hora real es X + 1 792 448 594 410 ms. Sale idéntica en las diez muestras porque el aviso llega exactamente cada 1000 ms. Así el puente es estable y se puede usar.



Luego comprobamos que el GPS es valido: +CGNSINF: 1,1,20261019222324.000,<latitud>,<longitud>,… indica fix válido, la hora 22:23:24 y tu posición. En el mismo mensaje hay 19 satélites a la vista, 8 GPS y 2 GLONASS usados, y un HDOP de 1,0.

Luego abrimos los datos para sincronizar con NTP:
AT+SAPBR=… abre la conexión de datos del módem. Sin ella, AT+CNTP falla con código 61.

AT+CGMR, AT+CPSI?, AT+COPS?, AT+CLTS? y AT+CNTP? solo informan: versión, red, operador, si la hora de red está activa y el servidor configurado.

AT+CCLK? da 22:23:23+00. Es la hora del módem antes de sincronizar.



EJEMPLO DE PRIMERA SINCRONIZACIÓN: --- CNTP #1: codigo 1, OK a los 20 ms, +CNTP a los 920 ms

+CNTP: 1,"2026/10/19,22:23:25"

fase de CCLK respecto a la 1.a sincronizacion: +0 ms

CCLK - UTC del GNSS: -330 ms (incluye +L)

codigo 1: la sincronización salió bien.

OK a 20 ms: el módem aceptó la orden.

+CNTP a 920 ms: la respuesta del servidor tardó 0,92 s en ir y volver. Es lo que tarda la red móvil.

+CNTP: 1,"…22:23:25": la hora que dio el servidor, en segundos enteros.

fase: cuándo cae el cambio de segundo del reloj del módem, comparado con la primera sincronización. No es un error de hora, solo una referencia interna.

CCLK - UTC del GNSS: -330 ms: este es el dato que importa. El reloj del módem va 330 ms por detrás de la hora real.

