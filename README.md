# RFID escolar — asistencia y tarjetas

> Proyecto escolar realizado completamente con **inteligencia artificial y prompt engineering**. Su código y documentación se generaron a partir de requisitos para una demostración de aula.

La versión **3.2.2** reúne asistencia, escritura y diagnóstico en un único programa para **Arduino IDE**: [lector/lector.ino](lector/lector.ino).

La asistencia está en **http://192.168.4.1/** y la configuración de tarjetas en **http://192.168.4.1/admin**. No es necesario cambiar de firmware para pasar de una función a otra. El antiguo sketch `escritor/escritor.ino` se retiró para mantener una sola implementación.

Los registros siguen guardándose en LittleFS con el mismo formato. **Todavía no se incorpora una base de datos**. No se requiere PlatformIO, ArduinoJson ni Internet para las páginas.

## 1. Grupo fijo

No existen altas, bajas ni edición desde la interfaz o la API. Son exactamente estas ocho personas y dos clases:

| Persona | ID interno | Clase | Card ID en la tarjeta |
|---|---|---|---|
| Lucía Fernández | ALU_01 | Ingeniería | A7F3C901 |
| Martina Silva | ALU_02 | Ingeniería | A7F3C902 |
| Mateo Rodríguez | ALU_03 | Ingeniería | A7F3C903 |
| Valentina Pereira | ALU_04 | Economía | A7F3C904 |
| Santiago López | ALU_05 | Economía | A7F3C905 |
| Nicolás González | ALU_06 | Economía | A7F3C906 |
| Ana Martínez (docente) | PROF_01 | Ingeniería | B8D4E101 |
| Martín Cabrera (docente) | PROF_02 | Economía | B8D4E102 |

Los `cardId` son identificadores opacos y fijos. La tarjeta solo contiene ese valor, seguido de ceros hasta completar 16 bytes: **no contiene el nombre, la clase ni la cédula**. El UID se muestra como dato técnico, pero no se utiliza para identificar a una persona.

## 2. Materiales y conexiones

Se necesitan una ESP32 Dev Module, un RC522, tarjetas **MIFARE Classic 1K de 13,56 MHz**, cables Dupont y un cable USB de datos. Las tarjetas de 125 kHz no son compatibles.

Con la placa apagada, el cableado es:

| RC522 | ESP32 |
|---|---|
| SDA / SS | GPIO 5 |
| SCK | GPIO 18 |
| MOSI | GPIO 23 |
| MISO | GPIO 19 |
| RST | GPIO 22 |
| GND | GND |
| 3.3V | 3.3V |

El RC522 utiliza **3,3 V, nunca 5 V**.

## 3. Preparación y carga con Arduino IDE

1. Instalar o abrir Arduino IDE 2.x.
2. En `Archivo > Preferencias > URLs adicionales de gestores de tarjetas`, agregar:

   ```text
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```

3. Instalar **esp32 by Espressif Systems** desde el Gestor de placas.
4. Instalar **MFRC522 de Miguel Balboa, versión 1.4.12**, desde el Administrador de bibliotecas. WiFi, WebServer, SPI y LittleFS vienen con el núcleo ESP32.
5. Descargar el ZIP de GitHub y descomprimirlo.
6. Abrir `lector/lector.ino` y conservar `CooperativeServer.h` junto al `.ino`; no requiere instalación ni generación previa. Solo se carga este programa.
7. Seleccionar **ESP32 Dev Module**, elegir el puerto USB y pulsar **Subir**.
8. Si queda en `Connecting…`, mantener pulsado **BOOT** hasta comenzar la carga y después soltarlo.
9. Abrir el Monitor serie a **115200 baudios** y pulsar **EN/RESET**. Deben aparecer `RFID-ESP32` y `firmware 3.2.2`. Tras actualizar el firmware, debe recargarse también la página del teléfono.

Si el chip deja de responder durante la carga, se recomienda cerrar el Monitor serie, comprobar el cable de datos, reducir `Upload Speed` a 115200 y probar la carga con el RC522 desconectado. El lector se vuelve a conectar con la placa apagada.

## 4. Acceso a las páginas

La red Wi-Fi es **`RFID-ESP32`** y la contraseña inicial es **`cambiar-esta-clave`**. Se configura en `WIFI_PASSWORD` dentro del sketch y debe tener entre 8 y 63 caracteres.

Después de conectar el teléfono, se abre **http://192.168.4.1** para asistencia, o **http://192.168.4.1/admin** para configurar tarjetas. Ambas páginas tienen enlaces de navegación. El teléfono debe permanecer en esa red aunque avise que no tiene Internet.

`/admin` separa las pantallas; no añade un inicio de sesión ni permisos por usuario. Los equipos conectados a la red pueden acceder a la escritura, que sigue exigiendo confirmación explícita.

Las páginas usan únicamente negro y blanco, tipografía monoespaciada y resultados textuales: `[OK]`, `[ERROR]`, `[ESPERANDO]` y `[CERRADO]`. La versión y el nombre del programa permiten comprobar qué firmware está cargado.

## 5. Preparación de las ocho tarjetas

1. Cargar el programa único y abrir **http://192.168.4.1/admin**.
2. Elegir una persona en **Persona**. La pantalla mostrará el Card ID fijo correspondiente; no existe un campo libre para inventar otros IDs.
3. Marcar la confirmación de sobrescritura y pulsar **Preparar escritura**.
4. Presentar una sola tarjeta y mantenerla quieta.
5. Revisar el diagnóstico: detección, selección, autenticación, lectura previa, escritura y verificación.
6. Aceptar la operación únicamente si aparece **ESCRITURA_OK** y el ID leído coincide con el solicitado.
7. Retirar la tarjeta y repetir el procedimiento con las demás personas. Cada operación requiere confirmación.

La escritura modifica exclusivamente el **bloque de datos 4 del sector 1**, con Key A predeterminada `FF FF FF FF FF FF`. Nunca modifica el bloque 0, los tráileres, las claves ni los permisos.

Siempre se comparan los 16 bytes después de escribir. Si la confirmación o la lectura fallan, no se anuncia éxito; debe comprobarse la tarjeta mediante el lector.

Durante una escritura o diagnóstico se pausa el registro automático de tarjetas, pero el tiempo de la clase sigue corriendo y su cierre se guarda al vencer. La tarjeta utilizada en administración debe retirarse durante al menos 400 ms antes de volver a presentarla para asistencia. Esto también se aplica después de cancelar o fallar una escritura. Dos clientes no pueden reemplazar una operación pendiente.

## 6. Almacenamiento y asistencia

1. Abrir **http://192.168.4.1/** con el mismo programa cargado.
2. Revisar el estado de LittleFS. Si una placa nueva no tiene el sistema de archivos preparado, aparecerá **Inicializar almacenamiento**.
3. La inicialización es explícita: se debe escribir `INICIALIZAR` y pulsar el botón. **Puede borrar datos previos y no debe utilizarse para intentar recuperar registros dañados.** No se ejecuta automáticamente. Si el almacenamiento está montado correctamente, el servidor rechaza el formateo.
4. Pulsar **Sincronizar hora del teléfono** antes de abrir una clase. Los registros utilizan la hora de Uruguay, UTC−3. No se permite cambiar la hora durante una sesión.
5. Pasar la tarjeta de Ana Martínez para abrir Ingeniería, o la de Martín Cabrera para abrir Economía.
6. Pasar las tarjetas de los tres estudiantes de esa clase. La lista muestra los presentes y sus fechas y horas.
7. Una tarjeta desconocida, un estudiante de otra clase o un segundo docente no reemplazan ni alteran la sesión. Un estudiante repetido no genera otro registro.
8. A los diez minutos, la ventana se cierra automáticamente y se conservan los presentes. Si falla el guardado del cierre, se impiden nuevas asistencias y se reintenta cada cinco segundos.
9. El enlace **Descargar registros** exporta todas las sesiones como `asistencias.ndjson`: un objeto JSON por línea.

Cada registro aceptado se guarda antes de confirmarlo en pantalla. Los archivos internos están en `/sessions/`, usan un formato binario versionado con comprobación de integridad y se reemplazan mediante un archivo temporal y renombrado de LittleFS. No se borra el archivo anterior antes de renombrar.

Si se reinicia la ESP32 durante una clase, los registros aceptados se conservan, la sesión queda cerrada como `DEVICE_RESTART` y se vuelve a pedir sincronización de hora. El cierre por reinicio no inventa una hora: `closedAt` queda vacío. Si un archivo está dañado, el sistema informa el error y no lo borra ni formatea la memoria.

## 7. Diagnóstico desde la web

Ambas páginas muestran por separado la respuesta de la ESP32, el estado del RC522 y la última tarjeta identificada. Comparten el estado y el historial de un único RC522.

- **Probar tarjeta sin escribir:** espera una tarjeta durante 30 segundos, muestra tipo, UID técnico, autenticación y contenido del bloque 4. No escribe ni registra asistencia. La tarjeta debe retirarse antes de volver a utilizarla en la asistencia, incluso si se cancela la prueba.
- **Reiniciar RC522:** recupera el lector sin reiniciar la ESP32, borrar los intentos o cerrar la clase. Se rechaza mientras haya una operación pendiente.
- **Cancelar espera:** abandona una escritura o un diagnóstico todavía pendientes. No cambia una operación que ya terminó.
- **Copiar diagnóstico:** obtiene un informe con versión, programa, estado del RC522 y últimos intentos. Si el navegador bloquea el portapapeles por HTTP, muestra un cuadro seleccionable para copiar manualmente.
- **Últimos 20 intentos:** conserva resultados y errores durante el encendido. Cada intento se puede desplegar. Al reiniciar la ESP32 se pierde este historial de diagnóstico en RAM, pero no las asistencias guardadas.

La espera de escritura es de 60 segundos. El diagnóstico del Monitor serie contiene los mismos pasos que la web.

La versión 3.2.2 simplifica la búsqueda: prepara los registros como `PICC_IsNewCardPresent()` de MFRC522, consulta REQA y usa WUPA si la tarjeta está detenida y REQA agota su espera. Una colisión durante esa detección permite continuar hacia `PICC_Select()`, que implementa la anticolisión y valida el UID/SAK. La versión anterior abortaba antes de esa selección.

Se retiraron los reinicios periódicos del campo de radio por ausencia de respuesta y los reinicios al preparar cada operación. Se conserva la recuperación breve después de un error real de detección o selección, y el botón explícito **Reiniciar RC522**. El tiempo agotado de una consulta no reinicia la radio.

Un fallo de detección o selección mantiene la escritura o el diagnóstico esperando hasta su límite original, sin necesitar otra pulsación. Los errores permanecen en el log y el mensaje de espera no reemplaza el último error. Si el comando de escritura ya fue enviado y su resultado es incierto, no se repite automáticamente.

El mensaje `A buffer is not big enough` viene de MFRC522 (`STATUS_NO_ROOM`), no demuestra que se haya agotado la RAM de la ESP32. Ahora se distingue entre detección ATQA (buffer de 2 bytes y contador FIFO observado), selección/anticolisión y lectura del bloque 4 (buffer de 18 bytes). No se agrandaron buffers para ocultar una respuesta de longitud inesperada. Una versión `92` confirma respuesta del chip, pero no demuestra una comunicación válida con la tarjeta. La causa física del caso comunicado todavía no se ha reproducido en este entorno.

| Resultado | Comprobación |
|---|---|
| ERROR_LECTOR / registro 00 o FF | Alimentación a 3,3 V y conexiones SPI. |
| ESPERANDO_TARJETA | Una tarjeta compatible sobre la antena, sin otras tarjetas cerca. |
| ERROR_DETECCION / ERROR_SELECCION | Mantener una sola tarjeta quieta y reintentar. El lector recupera la sesión de radio tras el fallo. |
| TIPO_NO_COMPATIBLE | Utilizar MIFARE Classic 1K. |
| ERROR_AUTENTICACION | Puede haber otra clave o un problema de radio. No se intentan cambiar las claves. |
| ERROR_LECTURA | Revisar el error exacto; una lectura incompleta se rechaza. |
| ERROR_ESCRITURA / ERROR_VERIFICACION | Comprobar el contenido antes de usar la tarjeta; no se considera éxito. |

### Prueba mínima opcional

[diagnostico_rc522/diagnostico_rc522.ino](diagnostico_rc522/diagnostico_rc522.ino) es una prueba de solo lectura, sin Wi-Fi, web, asistencia ni LittleFS. Utiliza los mismos pines, biblioteca y clave predeterminada. No escribe tarjetas ni monta el almacenamiento.

1. Abrir ese `.ino` en Arduino IDE, seleccionar la misma placa y cargarlo.
2. Abrir el Monitor serie a **115200 baudios**, reiniciar y presentar una sola tarjeta.
3. Observar REQA/WUPA, SELECT, autenticación y lectura del bloque 4. Si la detección falla, aparecen FIFOLevelReg, ErrorReg y VersionReg para conservar datos concretos del fallo.
4. Retirar la tarjeta antes de repetir. Para volver a la aplicación, cargar `lector/lector.ino`.

Esta prueba permite comparar la comunicación con el programa completo y reducir las variables. Que compile no demuestra que funcione con la radio real; ambos comportamientos deben observarse en el equipo. El bloque mostrado sigue siendo solo el 4.

## 8. Comprobación de la demostración

La prueba física debe incluir: configurar las ocho tarjetas, abrir ambas clases, registrar a sus tres estudiantes, rechazar una tarjeta de otra clase, repetir un estudiante, esperar el cierre y reiniciar para comprobar la conservación de datos.

Antes de utilizar una tarjeta escrita, se puede retirar y volver a presentar mediante **Probar tarjeta sin escribir**, sin cambiar de firmware.

Las pruebas automatizadas están en [tests/README.md](tests/README.md). Incluyen simulaciones del firmware, fallos de persistencia y pruebas de la página real en Chromium. **No equivalen a comprobar la radio ni la escritura con una ESP32 y un RC522 físicos.** El uso normal solo requiere Arduino IDE; las herramientas de pruebas son opcionales para desarrollo.

La revisión 3.2.2 pasó **156 comprobaciones automatizadas**, incluidas ambas páginas en Chromium. Incluye colisiones resueltas y no resueltas, `STATUS_NO_ROOM` en cada fase, reintentos antes de escribir, conservación del plazo original y ausencia de repetición de una escritura incierta. Son pruebas de lógica con radio simulada: no reproducen ni confirman corregida la causa física comunicada. La comprobación en el equipo sigue pendiente.

El sketch unificado compiló para `esp32:esp32:esp32` con Arduino CLI 1.5.1, núcleo ESP32 3.3.8 y MFRC522 1.4.12: **1062201 bytes de programa** y **51932 bytes de variables globales**. La prueba mínima también compiló: 295364 bytes de programa y 22196 bytes de globales. Estas cifras no miden el heap durante el uso ni su fragmentación.

El buffer de lectura mantiene 18 bytes para datos y CRC, antes de copiar los 16 bytes de datos. Se rechazan UID que no tengan 4, 7 o 10 bytes y longitudes de respuesta fuera del buffer. La escritura valida el límite de 16 bytes del ID antes de copiarlo. La inicialización duplicada del RC522 sigue corregida y se conserva una pausa de 50 ms después de cada inicialización real.

## 9. Optimización conservada desde la versión 3.1.0

El panel consulta el estado cada 500 ms cuando hay una operación o sesión activa, y cada dos segundos en reposo. Al ocultar la pestaña se pausa la consulta; al volver o recuperar conexión se sincroniza de inmediato. El funcionamiento del lector y el guardado de asistencia no dependen de que la pestaña esté visible.

Las APIs se separan para evitar transferir información que no cambió:

| Ruta GET | Contenido |
|---|---|
| `/api/status` | Estado actual, cuenta regresiva, `bootId`, `stateRevision` y `historyRevision`. No incluye personas ni el historial completo. |
| `/api/people` | Las ocho personas fijas; se consulta una vez al cargar la página. |
| `/api/attempts` | Snapshot completo de hasta veinte intentos, con `bootId`, revisión y secuencias más antigua y reciente. Se consulta solo al cambiar la revisión o el arranque. |

Cada cliente sustituye su copia del historial por el snapshot recibido. Esto evita duplicados y recupera los intentos disponibles después de perder conexión, aunque hayan ocurrido más de veinte. Un arranque distinto invalida la copia anterior. Las consultas no cambian los intentos almacenados.

El firmware utiliza estados explícitos para las operaciones y para la asistencia. Se mantienen los límites anteriores de texto: log actual de hasta 3500 bytes UTF-8, recortado a una línea completa después de 3000 cuando supera el límite, y hasta 1600 bytes por intento finalizado. Un intento en curso se consulta en el estado actual; su snapshot final se guarda al terminar, sin copiarlo en cada GET.

El historial y el informe se serializan por entrada, en lugar de construir una cadena con las veinte entradas. El lector usa escrituras TCP no bloqueantes para comprobar el vencimiento durante la transmisión. Cierra una conexión sin progreso durante 250 ms o que exceda dos segundos en una escritura. Una descarga interrumpida puede reintentarse y no borra registros.

El servidor sigue siendo síncrono: no procesa otras solicitudes HTTP ni nuevas lecturas RFID dentro de una descarga. El servicio cooperativo mantiene la evaluación del cierre; no puede interrumpir una operación de LittleFS que esté en curso. No realiza llamadas recursivas al manejador HTTP.

El formato binario de 84 bytes y el guardado inmediato de asistencias se conservan. La metodología, los resultados históricos de la versión 3.1.0 y sus límites se documentan en [tests/OPTIMIZATION.md](tests/OPTIMIZATION.md). Sus mediciones no se presentan como nuevas mediciones de la versión unificada.
