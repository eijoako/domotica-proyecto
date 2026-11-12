# RFID escolar — asistencia y tarjetas

> Proyecto escolar realizado completamente con **inteligencia artificial y prompt engineering**.

La versión **4.0.0** utiliza un único programa para ESP32 y Arduino IDE:
- **Asistencia:** http://192.168.4.1/
- **Tarjetas:** http://192.168.4.1/admin

La sesión y sus presentes existen únicamente en RAM. Se pierden al reiniciar. No hay sistema de archivos ni base de datos. No se necesita PlatformIO, ArduinoJson, generación previa ni Internet para las páginas.

## Personas y tarjetas

Las personas, clases y Card ID se editan en [People.h](lector/People.h). No existen altas, bajas ni edición desde la web. Cada fila contiene ID interno, nombre, Card ID, índice de clase (0 para Ingeniería, 1 para Economía) y si es docente. Para agregar o modificar una persona se edita esa estructura y se vuelve a cargar el firmware. Los límites y el total de estudiantes de cada clase se calculan desde los arreglos; no se modifican contadores manualmente. Los ID deben ser únicos, y cada Card ID debe tener de 1 a 16 caracteres ASCII alfanuméricos, guion o guion bajo.

| Persona | ID interno | Clase | Card ID |
|---|---|---|---|
| Lucía Fernández | ALU_01 | Ingeniería | A7F3C901 |
| Martina Silva | ALU_02 | Ingeniería | A7F3C902 |
| Mateo Rodríguez | ALU_03 | Ingeniería | A7F3C903 |
| Valentina Pereira | ALU_04 | Economía | A7F3C904 |
| Santiago López | ALU_05 | Economía | A7F3C905 |
| Nicolás González | ALU_06 | Economía | A7F3C906 |
| Ana Martínez | PROF_01 | Ingeniería | B8D4E101 |
| Martín Cabrera | PROF_02 | Economía | B8D4E102 |

La tarjeta almacena únicamente el Card ID, completado con ceros hasta 16 bytes. No contiene nombres ni clases. El UID se utiliza para diagnóstico y para evitar releer una tarjeta apoyada, no como identidad de una persona.

## Conexión y carga

Se utilizan una ESP32 Dev Module, un RC522 y tarjetas MIFARE Classic 1K de **13,56 MHz**. El cableado se realiza con la placa apagada:

| RC522 | ESP32 |
|---|---|
| SDA / SS | GPIO 5 |
| SCK | GPIO 18 |
| MOSI | GPIO 23 |
| MISO | GPIO 19 |
| RST | GPIO 22 |
| GND | GND |
| 3.3V | 3.3V |

El RC522 utiliza **3,3 V**. IRQ queda sin conectar.

1. Instalar Arduino IDE 2.x.
2. Agregar en Preferencias la URL del Gestor de placas: `https://espressif.github.io/arduino-esp32/package_esp32_index.json`.
3. Instalar **esp32 by Espressif Systems** y **MFRC522 de Miguel Balboa 1.4.12**.
4. Descargar el repositorio y abrir [lector/lector.ino](lector/lector.ino). Deben conservarse [WebPage.h](lector/WebPage.h) [People.h](lector/People.h) y [CooperativeServer.h](lector/CooperativeServer.h) dentro de esa carpeta.
5. Seleccionar **ESP32 Dev Module**, elegir el puerto y subir el programa. Si la carga queda en Connecting, mantener BOOT hasta que comience.
6. Abrir el Monitor serie a **115200 baudios**, reiniciar y comprobar `firmware 4.0.0`.
7. Conectar el teléfono a **RFID-ESP32**, contraseña **cambiar-esta-clave**, y abrir http://192.168.4.1/. La contraseña está definida en `WIFI_PASSWORD`.
8. Recargar la página después de actualizar el firmware. El teléfono debe permanecer en la red aunque indique que no tiene Internet.

## Configuración de tarjetas

1. Abrir **/admin** y elegir una de las ocho personas.
2. Revisar el Card ID mostrado, marcar la confirmación y pulsar **Preparar escritura**.
3. Acercar una sola tarjeta y mantenerla quieta.
4. Aceptar el resultado solo si aparece **ESCRITURA_OK**, con coincidencia de los 16 bytes.
5. Retirar la tarjeta durante al menos 400 ms antes de presentarla para asistencia. Repetir el procedimiento para las otras personas.

Se autentica el sector 1 con Key A predeterminada `FF FF FF FF FF FF`, utilizando su bloque 7 como en el código de referencia. Esto **no escribe el bloque 7**: únicamente se escribe el bloque de datos **4**. No se cambian claves, permisos, tráileres ni bloque 0.

La espera dura 60 segundos. Los errores previos a la escritura permiten reintentar dentro del mismo plazo o cancelar. Una escritura enviada cuya confirmación falla no se repite automáticamente: su contenido debe verificarse mediante **Probar tarjeta sin escribir**.

## Credencial administrativa

En `lector.ino` se configuran `ADMIN_USER` (inicialmente `admin`) y `ADMIN_PASSWORD` (inicialmente `cambiar-clave-admin`). Debe elegirse una clave propia, distinta de `WIFI_PASSWORD`, y volver a cargar el firmware.

Al abrir **/admin** el navegador solicita esas credenciales mediante HTTP Basic. El servidor las exige también para escribir, preparar un diagnóstico, cancelar y reiniciar el RC522, incluso si la solicitud se realiza directamente sin los botones. La asistencia y la consulta de estado son públicas. Las credenciales se usan en HTTP sin cifrado: es una protección sencilla para una red local controlada, no para exponer el dispositivo en Internet. Para cambiar de usuario puede cerrarse la sesión del navegador o utilizar una ventana privada.

Las acciones web vencen a los ocho segundos, incluida la recepción de su respuesta. Si no se pudo confirmar una escritura, debe consultarse el estado y el contenido de la tarjeta antes de reintentar.

## Asistencia

1. Abrir **/**, sin sincronizar fecha ni hora.
2. Presentar la tarjeta de Ana Martínez para Ingeniería o la de Martín Cabrera para Economía.
3. Presentar las tarjetas de los estudiantes de esa clase y comprobar la lista de presentes.
4. A los diez minutos la sesión se cierra. El tiempo se mide por duración transcurrida, incluso cuando `millis()` se desborda.
5. Una nueva tarjeta de docente después del cierre abre una sesión nueva y reemplaza la anterior. Reiniciar la ESP32 borra la sesión y los presentes.

Se rechazan tarjetas desconocidas, estudiantes de otra clase y duplicados. Otro docente no reemplaza una sesión abierta. No se guardan ni se muestran horas de entrada, apertura o cierre. No existe exportación ni historial persistente.

La escritura y el diagnóstico pausan la admisión de tarjetas, pero no el temporizador de la clase. La tarjeta administrativa debe retirarse y presentarse de nuevo para asistencia.

## Límites de seguridad de las tarjetas

El Card ID es un identificador copiable. Las tarjetas con la clave predeterminada pueden copiarse o reescribirse; dos tarjetas con el mismo Card ID representan a la misma persona para esta aplicación. Ni el UID, ni la contraseña de la web, ni la verificación de 16 bytes garantizan autenticidad o protección contra clonación. La verificación solo confirma lo leído inmediatamente después de escribir.

## Diagnóstico

Ambas páginas muestran ESP32, RC522, tarjeta, etapas y los últimos **20 intentos** del encendido.
- **Probar tarjeta sin escribir:** espera 30 segundos y lee el bloque 4 sin registrar asistencia.
- **Cancelar espera:** cancela una operación pendiente.
- **Reiniciar RC522:** recupera el dispositivo sin cerrar la clase; requiere cancelar primero cualquier operación pendiente.
- **Copiar diagnóstico:** copia el informe o presenta un cuadro seleccionable si el navegador bloquea el portapapeles por HTTP.

`A buffer is not big enough` es un estado de MFRC522; no significa que falte RAM. El informe distingue ATQA (buffer de 2 bytes y FIFO observado), SELECT y READ (18 bytes para datos y CRC). Una colisión en detección continúa hacia la anticolisión de la biblioteca; una selección no válida impide escribir. La versión de chip `92` por sí sola no demuestra comunicación válida con la tarjeta.

## Código y comprobaciones

- `lector.ino`: dispositivo, tarjetas, asistencia y API.
- `People.h`: personas, clases y Card ID editables.
- `WebPage.h`: interfaz monocromática, sin recursos externos.
- `CooperativeServer.h`: envío HTTP que sigue comprobando el vencimiento durante descargas.
- [tests/README.md](tests/README.md): pruebas de firmware y Chromium.

Lectura y escritura utilizan una sola función de atención y una sola limpieza de sesión. La consulta de tarjeta se limita a una cada 250 ms; preparar una operación permite consultar inmediatamente. La selección vuelve a comprobar su plazo antes de iniciar una escritura. Los buffers de lectura se reciben por referencia a un arreglo de 16 bytes para evitar pasar accidentalmente uno menor.

La página consulta personas una vez y el historial solo por revisión. El polling no se solapa y se pausa al ocultar la pestaña. Los GET no modifican los intentos. El servidor mantiene el temporizador durante respuestas HTTP, aunque no atiende otras solicitudes ni tarjetas dentro del mismo handler síncrono.

Se retiraron las operaciones de almacenamiento, inicialización, formateo, recuperación y exportación, sus rutas y sus pruebas exclusivas. Se conservaron las regresiones de tarjetas y navegador.

La compilación ESP32 y las pruebas se documentan en tests/README.md. Las pruebas simuladas no demuestran que la comunicación física con un RC522 particular funcione; esa comprobación sigue pendiente.
