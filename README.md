# RFID escolar — asistencia y tarjetas

> Proyecto escolar realizado completamente con **inteligencia artificial y prompt engineering**. Su código y documentación se generaron a partir de requisitos para una demostración de aula.

La versión **3.0.0** mantiene dos programas independientes para **Arduino IDE**:

- [lector/lector.ino](lector/lector.ino): asistencia para un grupo fijo y diagnóstico sin escritura.
- [escritor/escritor.ino](escritor/escritor.ino): selección de una persona, escritura del bloque 4 y verificación.

Cada sketch se abre desde su propia carpeta y se carga por separado. La carga reemplaza al otro programa en la ESP32; no modifica por sí sola las tarjetas ni borra los registros de LittleFS. No se requiere PlatformIO, ArduinoJson ni Internet para las páginas.

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
6. Abrir el archivo `.ino` deseado desde su carpeta. No deben reunirse ambos sketches en una misma carpeta.
7. Seleccionar **ESP32 Dev Module**, elegir el puerto USB y pulsar **Subir**.
8. Si queda en `Connecting…`, mantener pulsado **BOOT** hasta comenzar la carga y después soltarlo.
9. Abrir el Monitor serie a **115200 baudios** y pulsar **EN/RESET**. Deben aparecer el nombre del programa y `firmware 3.0.0`.

Si el chip deja de responder durante la carga, se recomienda cerrar el Monitor serie, comprobar el cable de datos, reducir `Upload Speed` a 115200 y probar la carga con el RC522 desconectado. El lector se vuelve a conectar con la placa apagada.

## 4. Acceso a las páginas

| Programa cargado | Red Wi-Fi |
|---|---|
| Lector | RFID-Lector |
| Escritor | RFID-Escritor |

La contraseña inicial es **`cambiar-esta-clave`**. Se configura en `WIFI_PASSWORD` dentro de cada sketch y debe tener entre 8 y 63 caracteres.

Después de conectar el teléfono a la red correspondiente, se abre **http://192.168.4.1**. El teléfono debe permanecer en esa red aunque avise que no tiene Internet. Ya no se utiliza `/admin`.

Las páginas usan únicamente negro y blanco, tipografía monoespaciada y resultados textuales: `[OK]`, `[ERROR]`, `[ESPERANDO]` y `[CERRADO]`. La versión y el nombre del programa permiten comprobar qué firmware está cargado.

## 5. Preparación de las ocho tarjetas

1. Cargar el escritor y abrir su página.
2. Elegir una persona en **Persona**. La pantalla mostrará el Card ID fijo correspondiente; no existe un campo libre para inventar otros IDs.
3. Marcar la confirmación de sobrescritura y pulsar **Preparar escritura**.
4. Presentar una sola tarjeta y mantenerla quieta.
5. Revisar el diagnóstico: detección, selección, autenticación, lectura previa, escritura y verificación.
6. Aceptar la operación únicamente si aparece **ESCRITURA_OK** y el ID leído coincide con el solicitado.
7. Retirar la tarjeta y repetir el procedimiento con las demás personas. Cada operación requiere confirmación.

La escritura modifica exclusivamente el **bloque de datos 4 del sector 1**, con Key A predeterminada `FF FF FF FF FF FF`. Nunca modifica el bloque 0, los tráileres, las claves ni los permisos.

Siempre se comparan los 16 bytes después de escribir. Si la confirmación o la lectura fallan, no se anuncia éxito; debe comprobarse la tarjeta mediante el lector.

## 6. Almacenamiento y asistencia

1. Cargar el lector y abrir su página.
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

Ambos programas muestran por separado la respuesta de la ESP32, el estado del RC522 y la última tarjeta identificada.

- **Probar tarjeta sin escribir:** espera una tarjeta durante 30 segundos, muestra tipo, UID técnico, autenticación y contenido del bloque 4. No escribe ni registra asistencia. La tarjeta debe retirarse antes de volver a utilizarla en la asistencia, incluso si se cancela la prueba.
- **Reiniciar RC522:** recupera el lector sin reiniciar la ESP32, borrar los intentos o cerrar la clase. Se rechaza mientras haya una operación pendiente.
- **Cancelar espera:** abandona una escritura o un diagnóstico todavía pendientes. No cambia una operación que ya terminó.
- **Copiar diagnóstico:** obtiene un informe con versión, programa, estado del RC522 y últimos intentos. Si el navegador bloquea el portapapeles por HTTP, muestra un cuadro seleccionable para copiar manualmente.
- **Últimos 20 intentos:** conserva resultados y errores durante el encendido. Cada intento se puede desplegar. Al reiniciar la ESP32 se pierde este historial de diagnóstico en RAM, pero no las asistencias guardadas.

La espera de escritura es de 60 segundos. El diagnóstico del Monitor serie contiene los mismos pasos que la web.

| Resultado | Comprobación |
|---|---|
| ERROR_LECTOR / registro 00 o FF | Alimentación a 3,3 V y conexiones SPI. |
| ESPERANDO_TARJETA | Una tarjeta compatible sobre la antena, sin otras tarjetas cerca. |
| ERROR_DETECCION / ERROR_SELECCION | Mantener una sola tarjeta quieta y reintentar. El lector recupera la sesión de radio tras el fallo. |
| TIPO_NO_COMPATIBLE | Utilizar MIFARE Classic 1K. |
| ERROR_AUTENTICACION | Puede haber otra clave o un problema de radio. No se intentan cambiar las claves. |
| ERROR_LECTURA | Revisar el error exacto; una lectura incompleta se rechaza. |
| ERROR_ESCRITURA / ERROR_VERIFICACION | Comprobar el contenido antes de usar la tarjeta; no se considera éxito. |

## 8. Comprobación de la demostración

La prueba física debe incluir: configurar las ocho tarjetas, abrir ambas clases, registrar a sus tres estudiantes, rechazar una tarjeta de otra clase, repetir un estudiante, esperar el cierre y reiniciar para comprobar la conservación de datos.

Antes de utilizar una tarjeta escrita, puede cargarse el lector y usarse **Probar tarjeta sin escribir** para verificar su ID de forma independiente.

Las pruebas automatizadas están en [tests/README.md](tests/README.md). Incluyen simulaciones del firmware, fallos de persistencia y pruebas de la página real en Chromium. **No equivalen a comprobar la radio ni la escritura con una ESP32 y un RC522 físicos.** El uso normal solo requiere Arduino IDE; las herramientas de pruebas son opcionales para desarrollo.

La revisión 3.0.0 pasó **71 comprobaciones automatizadas**. Ambos programas se verificaron por separado para ESP32 Dev Module con núcleo ESP32 3.3.8, MFRC522 1.4.12 y Arduino CLI 1.5.1. La comprobación física de las ocho tarjetas sigue pendiente en el liceo.
