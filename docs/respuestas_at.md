Etiquetas: [7000G] solo SIM7000G (manual AT V1.06) · [7600G] solo SIM7600G (manual AT V3.00) · [ambos] los dos módems. «Sin probar» = aún no ejecutado en la 7600G.



**Arranque y configuración básica**



[ambos] AT	Pregunta si el módem contesta (responde OK)	Saber si ya está encendido antes de pulsar PWRKEY, y comprobar que contesta tras el pulso. En la 7600G contesta entre 13,5 y 18 s después del pulso de PWRKEY (medido en 9441F4; el manual da 13–14 s)



[ambos] ATE0	Apaga el eco de los comandos	Que en la respuesta solo salga lo que contesta el módem. No se guarda: hay que mandarlo en cada

encendido



[ambos] AT+CMEE=1	Errores como +CME ERROR: <número>	Saber qué falló, no solo ERROR. No se guarda



[ambos] AT+CGMR	Versión del firmware del módem	Saber qué versión corre, porque la sintaxis de algunos comandos cambia entre versiones. En la 7600G sin probar



[ambos] AT+CGSN	IMEI del módem	Identificar el módem. En la 7600G sin probar



[7000G] AT+IPR?	Velocidad de la UART (0 = autobaud)	Saber si la velocidad es fija



[7600G] AT+IPR? / AT+IPREX?	IPR es la velocidad actual y se pierde al reiniciar; IPREX es la que se guarda (manual V3.00, 2.2.12 y 19.2.15)	Saber si la velocidad es fija. Sin probar



[ambos] AT+CSCLK?	Modo sueño por pin DTR (0 = desactivado)	Saber si el módem puede dormirse solo. Importa para el consumo solar. En la 7600G: sueño de la UART con DTR alto (manual V3.00, 19.2.17). Sin probar



[ambos] AT+CFUN?	Funcionalidad (1 = completa, radio encendida)	Comprobar que la radio está activa. En la 7600G sin probar



[7000G] AT+CPOWD=1	Apagado normal	Apagar el módem entre ciclos. No contesta OK: su única respuesta es NORMAL POWER DOWN



[7600G] AT+CPOF	Apagado normal. Contesta OK y el módem se apaga después (manual V3.00, 3.2.12)	Apagar el módem entre ciclos. El firmware espera 15 s y comprueba que ya no contesta a AT. STATUS (GPIO34) baja a 0 hacia los 16 s



**SIM y tecnologías**



[ambos] AT+CPIN?	Estado de la SIM (READY = lista, sin PIN)	Comprobar que hay SIM válida antes de registrar. No basta el OK, hay que ver READY. En la 7600G, justo después de encender puede dar +CME ERROR: 14 (SIM ocupada): el firmware reintenta hasta 15 s



[7000G] AT+CCID	ICCID, el número de serie de la SIM	Comprobar qué SIM es (operador, país)



[7600G] AT+CICCID	ICCID de la SIM; responde +ICCID: <número> (manual V3.00, 3.2.3)	Comprobar qué SIM es. AT+CCID es del SIM7000. Sin probar



[7000G] AT+CNMP?	Modos permitidos (2 = automático GSM/LTE)	Saber por qué cae a GSM



[7600G] AT+CNMP?	Modos permitidos: 2 automático, 13 solo GSM, 14 solo WCDMA, 38 solo LTE, 39 GSM+WCDMA+LTE, 51 GSM+LTE. Se guarda al reiniciar (4.2.11)	Saber o forzar la tecnología. Sin probar: en el banco registra en LTE sin tocarlo



[7000G] AT+CMNB?	Dentro de LTE: Cat-M1, NB-IoT o ambos (3)	Ídem



[7000G] AT+CBANDCFG?	Bandas habilitadas por tecnología	Saber si las bandas de Claro/Perú están cubiertas



[7600G] AT+CNBP?	Bandas habilitadas, como máscara de bits de 64 bits (4.2.12)	Equivale a CBANDCFG; no hay CMNB porque la 7600G no es Cat-M/NB-IoT. Sin probar



**Red y señal**



[ambos] AT+CEREG?	Registro en LTE (<stat>: 1 casa, 5 roaming, 3 denegado, 0 sin registro y sin buscar)	Esperar el registro. El firmware da por bueno 1 o 5. En la 7600G registra en LTE (CEREG 1)



[ambos] AT+CGREG?	Igual, para GSM/GPRS	Es donde acaba registrando en Perú (CGREG 1). En la 7600G también da 1 estando en LTE



[ambos] AT+COPS?	Operador y tecnología (3 = GSM EGPRS)	Mostrar el operador en pantalla y en el log. En la 7600G: +COPS: 0,0,"Claro Claro",7 (7 = LTE)



[ambos] AT+CPSI?	Información de la celda: sistema, MCC-MNC, celda, canal, nivel	Ver la tecnología (GSM, Cat-M, NB-IoT) y la señal en dBm. En la 7600G: +CPSI: LTE,Online,716-10,...,EUTRAN-BAND28,... (también visto en BAND2)



[ambos] AT+CSQ	Calidad de señal. dBm = −114 + 2 × rssi (rssi 2–30)	Medir cobertura. Igual en la 7600G. Medido en el banco: 24–31



**Datos y HTTP**



[7000G] AT+CNCFG=1,"apn","user","pass",1	Configura el contexto de datos 1 (APN, usuario, clave, PAP)	Preparar la conexión de datos. En

V1.06 va todo en un comando



[7000G] AT+CNACT=1	Activa la conexión de datos. Contesta OK enseguida y la IP llega después	Dar conexión de datos al módem



[7000G] AT+CNACT?	Estado (+CNACT: 1,"ip", 1 = activa)	Ver si ya está activa y esperar a que lo esté



[7000G] AT+SHDISC	Cierra la conexión HTTP	Limpiar antes de un envío y cerrar siempre al terminar



[7000G] AT+SHCONF="URL",... / "BODYLEN",1024 / "HEADERLEN",350	Guarda dominio y tamaños máximos	Configurar el HTTP antes de conectar



[7000G] AT+SHCONN	Abre la conexión con el servidor	Conectar con el servidor



[7000G] AT+SHAHEAD="Content-Type","application/json"	Guarda una cabecera	Que el servidor sepa que mandamos JSON



[7000G] AT+SHBODEXT=<len>,<ms>	Anuncia el cuerpo (los bytes van crudos por la UART)	Cargar el JSON. El firmware no manda Ctrl-Z



[7000G] AT+SHREQ="ruta",3	Lanza el POST. Responde OK y luego +SHREQ: "POST",<código>,<len>	Enviar los datos y leer el código HTTP



[7600G] AT+CGDCONT?	Lista los contextos: +CGDCONT: 1,"IP","claro.pe",... (8.2.4)	Ver si el APN ya está. Se guarda en el módem: claro.pe sigue tras apagar



[7600G] AT+CGDCONT=1,"IP","apn"	Define el contexto 1 con el APN	Solo se manda si el APN guardado no es el correcto. Sustituye a CNCFG



[7600G] AT+CGAUTH=1,1,"clave","usuario"	Autenticación PAP del contexto 1. La clave va antes que el usuario (8.2.15)	Usuario y clave del APN de Claro



[7600G] AT+CGPADDR=1	IP del contexto 1 (+CGPADDR: 1,10.13.14.222); 0.0.0.0 = sin IP (8.2.12)	Saber si ya hay datos. En LTE la IP ya está al registrarse. Sustituye a CNACT?



[7600G] AT+CGACT=1,1	Activa el contexto 1 (8.2.3)	Solo si no hay IP. En LTE no hizo falta; sin probar en 2G/3G. Sustituye a CNACT=1



[7600G] AT+HTTPTERM	Cierra el servicio HTTP (13.2.2)	Limpiar antes del envío (si no estaba abierto da ERROR y se ignora) y cerrar siempre al terminar. Sustituye a SHDISC



[7600G] AT+HTTPINIT	Abre el servicio HTTP (hasta 120 s, 13.2.1)	Sustituye a SHCONN. Dio ERROR tras reiniciar el ESP32 con una petición a medias en el módem



[7600G] AT+HTTPPARA="URL","http://dominio/ruta"	Guarda la URL completa (13.2.3)	Sustituye a SHCONF "URL" y a la ruta de SHREQ



[7600G] AT+HTTPPARA="CONTENT","application/json"	Guarda la cabecera Content-Type	Que el servidor sepa que mandamos JSON. Sustituye a SHAHEAD



[7600G] AT+HTTPPARA="CONNECTTO",40 / "RESPTO",30	Límites de conexión y de respuesta del servidor, en segundos	Decisión propia. Con servidor inalcanzable el fallo llegó a los 40,03 s (+HTTPACTION: 1,714,0)



[7600G] AT+HTTPDATA=<len>,<tiempo>	Anuncia el cuerpo: contesta DOWNLOAD, se mandan los bytes crudos y contesta OK (13.2.7)	Cargar el JSON. Sustituye a SHBODEXT. El manual da <tiempo> en segundos (10–65535), pero su ejemplo usa 10000: unidad sin aclarar



[7600G] AT+HTTPACTION=1	Lanza el POST. Responde OK y luego +HTTPACTION: 1,<código>,<len> (13.2.4)	Enviar los datos y leer el código. Sustituye a SHREQ. Medido: 200 en 0,7–2 s. Los 6xx y 7xx son errores del módem, no códigos HTTP (714 = no conecta con el servidor)



**Hora (NTP)**



[7000G] AT+CLTS?	Si el reloj del módem se actualiza con la red (NITZ)	Saber de dónde sale la hora de CCLK. Vale 1 en tu módem



[ambos] AT+CCLK?	Reloj del módem, "yy/MM/dd,hh:mm:ss±zz", resolución 1 s	Leer la hora tras CNTP y medir la fase del cambio de segundo. En la 7600G el formato es el mismo; la zona va en cuartos de hora. Sin probar



[7000G] AT+CNTP?	Configuración de NTP (servidor, zona, cid, modo)	Ver qué servidor usa. Mostró ...,0,1,2: cid 1, modo 2



[7000G] AT+CNTP="srv",0	Fija servidor y zona (0 = UTC)	CCLK queda en UTC



[7000G] AT+CNTP	Pide la hora. OK y luego +CNTP: <código>\[,"hora"]: 1 ok, 61 error de red, 62 DNS, 63 conexión, 64 error de respuesta, 65 timeout	Sincronizar. Salió 61 sin SAPBR, 64 con google.com y 1 con el pool



[7000G] AT+SAPBR=3,1,"APN"/"USER"/"PWD",...	Configura el portador 1	CNTP usa este portador, no el de CNACT



[7000G] AT+SAPBR=1,1	Abre el portador (hasta 85 s). Da +CME ERROR: 3 si ya estaba abierto	Dejar CNTP operativo



[7000G] AT+SAPBR=2,1	Consulta (+SAPBR: 1,<estado>,"ip"; 1 = abierto, 3 = cerrado)	Comprobar si está abierto



[7000G] AT+SAPBR=0,1	Lo cierra (hasta 65 s)	Cronometrar la apertura desde cero



[7600G] AT+CTZU?	Si la hora y la zona se actualizan con la red (NITZ). Vale 1 de fábrica y se guarda (4.2.17)	Equivale a CLTS. Por eso UTC = CCLK − zona × 15 min. Sin probar



[7600G] AT+CNTP="srv",0	Fija servidor y zona (0 = UTC). Solo dos parámetros: no hay cid ni modo (15.2.1)	Sin probar



[7600G] AT+CNTP	Pide la hora. OK y luego +CNTP: <código>, sin la hora: 0 ok, 1 desconocido, 2 parámetro mal, 3 fecha mal calculada, 4 red, 5 zona, 6 timeout (15.2.2)	El éxito es 0, no 1 como en el SIM7000. La hora se lee después con CCLK. No existe SAPBR: si CNTP necesita los datos abiertos está sin verificar. Sin probar





**GNSS**



[7000G] AT+SGPIO=0,4,1,1	Fija un GPIO del módem. Parámetros: operación 0 (escribir), GPIO 4 del módem (pin 48, no el GPIO4 del ESP32), función 1 (salida), nivel 1 (alto). No se guarda (NO\_SAVE): hay que repetirlo en cada encendido.	Alimenta la antena GNSS activa. Sin él no había fix. Con él aparecieron satélites a 24–43 dBHz y llegó el fix. Que sea ese GPIO es una hipótesis que el resultado confirma. Apágalo (nivel 0) cuando apagues el GNSS.



[7000G] AT+CGNSPWR=1 / =0	Enciende o apaga el receptor GNSS.	El GNSS arranca apagado. Se apaga al terminar para ahorrar energía.



[7000G] AT+CGNSURC=n	Emite un URC +UGNSINF: cada n fixes. =1 es uno por segundo y =0 lo desactiva.	Da muestras de hora UTC a 1 Hz sin sondear.



[7000G] AT+CGNSINF	Devuelve el estado GNSS una vez. Campos: 1 run status, 2 fix status, 3 UTC yyyyMMddhhmmss.sss, 4 latitud, 5 longitud, 6 altitud, 9 fix mode, 11–13 HDOP/PDOP/VDOP, 15 satélites a la vista, 16 GPS usados, 17 GLONASS usados, 19 C/N0 máximo.	Comprobar si hay fix y de dónde sale la hora UTC de referencia.



[7000G] +UGNSINF:	No se envía: es el URC con los mismos campos que +CGNSINF.	Es lo que lee leer\_fix\_gnss.



[7000G] AT+CGNSTST=1 / =0	Manda las sentencias NMEA crudas por el puerto AT. El estado persiste aunque se reinicie el ESP32.	Para diagnosticar (ver satélites en GSV). Déjalo en 0: inunda la UART y provocó el watchdog.



[7000G] AT+CGNSNMEA=<máscara>	Elige qué sentencias NMEA salen.	Reducir el volumen si usas CGNSTST. Lo vimos en la documentación, no lo usamos.



[7000G] AT+CGNSCOLD / WARM / HOT	Arranque en frío, templado o en caliente del GNSS.	Cold es el más lento, no acelera el fix. Solo sirve para forzar un arranque limpio.



[7000G] AT+CGNSXTRA	A-GPS: descarga datos de efemérides.	Acelera el primer fix. No lo hemos usado.



[7600G] AT+CVAUXS=1 / =0	Enciende o apaga el LDO VDD_AUX del módem: pin 44, 2,85 V, máx. 150 mA (manual 19.2.2, donde se llama VREG_AUX1; HD 3.15.3)	En la T-SIM7600X ese pin (red VDD_3V) alimenta la antena GNSS activa a través de R43 y FB3. Equivale a SGPIO. Viene encendido de fábrica; se apaga al terminar



[7600G] AT+CGPS=1,1 / =0	Arranca (modo autónomo) o para la sesión GNSS. Al parar, +CGPS: 0 llega más tarde (17.2.1)	Equivale a CGNSPWR. =0 con el GNSS ya apagado da OK (medido). Tras parar, el manual pide esperar 2–30 s antes de arrancar otra vez. El +CGPS: 0 llegó unos 40 s después



[7600G] AT+CGPS?	Estado: +CGPS: <on_off>,<modo>	Saber si está encendido. No lo usamos



[7600G] AT+CGPSINFO=n / =0	URC +CGPSINFO: cada n segundos; =0 lo desactiva (17.2.2)	Equivale a CGNSURC. Con 1 da un aviso por segundo



[7600G] +CGPSINFO:	No se envía: es el URC. Campos: latitud ddmm.mmmmmm, N/S, longitud dddmm.mmmmmm, E/W, fecha ddmmyy, hora UTC hhmmss.s, altitud, velocidad (nudos), rumbo. Sin fix van todos vacíos (,,,,,,,,)	Es lo que lee modem_gnss_esperar_fix. A diferencia de +UGNSINF no trae estado de fix ni satélites



[7600G] AT+CGPSINFO	Devuelve la posición una vez	Equivale a CGNSINF. No lo usamos



[7600G] AT+CGNSSINFO	Como CGPSINFO, pero con los satélites GPS, GLONASS y BeiDou usados y PDOP/HDOP/VDOP (17.2.21)	Diagnosticar la antena. No lo usamos



[7600G] AT+CGPSINFOCFG=1,31 / =0	Manda NMEA por el puerto AT (lo usa el ejemplo GPSDebug de LilyGo)	Equivale a CGNSTST. Para ver satélites; dejar en 0. No lo usamos



[7600G] AT+CGPSCOLD / AT+CGPSHOT	Arranque en frío o en caliente; antes hay que parar con CGPS=0 (17.2.3 y 17.2.4)	No lo usamos



[7600G] AT+CGPSXE / AT+CGPSXD	A-GPS XTRA: descarga efemérides por HTTP (17.2.13 y 17.2.14)	La nota de GNSS de SIMCom dice que reduce el primer fix 18–30 s. No lo usamos; puede interesar por los 81 s medidos



**RESPUESTA DE GPS EXPLICACIÓN DE LO QUE SALE COMO RESPUESTA (7000G):**

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



(7000G) EJEMPLO DE PRIMERA SINCRONIZACIÓN: --- CNTP #1: codigo 1, OK a los 20 ms, +CNTP a los 920 ms

+CNTP: 1,"2026/10/19,22:23:25"

fase de CCLK respecto a la 1.a sincronizacion: +0 ms

CCLK - UTC del GNSS: -330 ms (incluye +L)

codigo 1: la sincronización salió bien.

OK a 20 ms: el módem aceptó la orden.

+CNTP a 920 ms: la respuesta del servidor tardó 0,92 s en ir y volver. Es lo que tarda la red móvil.

+CNTP: 1,"…22:23:25": la hora que dio el servidor, en segundos enteros.

fase: cuándo cae el cambio de segundo del reloj del módem, comparado con la primera sincronización. No es un error de hora, solo una referencia interna.

CCLK - UTC del GNSS: -330 ms: este es el dato que importa. El reloj del módem va 330 ms por detrás de la hora real.



**RESPUESTA DE GPS EN LA 7600G (log del 10/11/2026, 9441F4):**

AT+CVAUXS=1 alimenta la antena activa.

AT+CGPS=1,1 arranca el receptor en modo autónomo.

AT+CGPSINFO=1 hace que el módem avise cada segundo.



Mientras no hay fix, los avisos llegan vacíos (+CGPSINFO: ,,,,,,,,) y el firmware no los imprime. Solo se ve el resumen al final: sin fix tras 90000 ms (90 avisos). 90 avisos en 90 s quiere decir que el módem avisa bien y lo que falta es señal.



GNSS: DS3231 - GNSS = -18018 s es la diferencia entre el DS3231 y la hora del GPS antes de corregir. −18000 s son las 5 h de Perú (el DS3231 estaba en hora local). Los −18 s restantes son deriva del DS3231 o que el módem da hora GPS en vez de UTC (la hora GPS va 18 s por delante): sin verificar, se comprobará con CNTP.

GNSS: est 0, 81177 ms: 0 es fix. Tardó 81 s desde CVAUXS=1. En los dos intentos anteriores no llegó en 90 s.

Al final, CGPSINFO=0, CGPS=0 y CVAUXS=0 paran los avisos, el receptor y la antena.

