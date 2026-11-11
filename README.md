# RFID escolar — asistencia y tarjetas

> Proyecto escolar realizado completamente con **inteligencia artificial y prompt engineering**.

La versión **3.3.0** utiliza un único programa para ESP32 y Arduino IDE:
- **Asistencia:** http://192.168.4.1/
- **Tarjetas:** http://192.168.4.1/admin

Los datos se conservan en LittleFS, sin base de datos externa. No se necesita PlatformIO, ArduinoJson, generación previa ni Internet para las páginas.

## Personas y tarjetas

El grupo es fijo; no existen altas, bajas ni edición de personas o clases.

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
4. Descargar el repositorio y abrir [lector/lector.ino](lector/lector.ino). Deben conservarse [WebPage.h](lector/WebPage.h) y [CooperativeServer.h](lector/CooperativeServer.h) dentro de esa carpeta.
5. Seleccionar **ESP32 Dev Module**, elegir el puerto y subir el programa. Si la carga queda en Connecting, mantener BOOT hasta que comience.
6. Abrir el Monitor serie a **115200 baudios**, reiniciar y comprobar `firmware 3.3.0`.
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

## Asistencia

1. Volver a **/**. Si LittleFS no está disponible, la inicialización requiere escribir `INICIALIZAR` y pulsar su botón. Puede borrar datos previos; no se ejecuta automáticamente ni se permite con un almacenamiento montado.
2. Pulsar **Sincronizar hora del teléfono**. Los registros utilizan hora de Uruguay, UTC−3.
3. Presentar la tarjeta de Ana Martínez para abrir Ingeniería o la de Martín Cabrera para abrir Economía.
4. Presentar las tarjetas de los tres estudiantes de esa clase. Cada asistencia se guarda antes de confirmarse.
5. Consultar los presentes y la cuenta regresiva. A los diez minutos la sesión se cierra automáticamente.
6. Descargar las sesiones mediante **Descargar registros**, en formato NDJSON.

No se admiten tarjetas desconocidas ni estudiantes de otra clase. Los duplicados no agregan otro presente y otro docente no reemplaza una sesión abierta. La hora no se modifica durante la sesión.

La escritura y el diagnóstico pausan la admisión de tarjetas, pero no el reloj ni el cierre. La tarjeta utilizada en administración no marca asistencia hasta retirarse y presentarse nuevamente.

Los registros mantienen el formato binario anterior de **84 bytes**, checksum y guardado inmediato con archivo temporal y renombrado. Si falla el cierre, queda pendiente y se reintenta sin admitir nuevos presentes. Un reinicio conserva los presentes y cierra la sesión como `DEVICE_RESTART`; después se vuelve a sincronizar la hora. Un archivo dañado se informa sin borrarlo ni formatear.

## Diagnóstico

Ambas páginas muestran ESP32, RC522, tarjeta, etapas y los últimos **20 intentos** del encendido.
- **Probar tarjeta sin escribir:** espera 30 segundos y lee el bloque 4 sin registrar asistencia.
- **Cancelar espera:** cancela una operación pendiente.
- **Reiniciar RC522:** recupera el dispositivo sin cerrar la clase; requiere cancelar primero cualquier operación pendiente.
- **Copiar diagnóstico:** copia el informe o presenta un cuadro seleccionable si el navegador bloquea el portapapeles por HTTP.

`A buffer is not big enough` es un estado de MFRC522; no significa que falte RAM. El informe distingue ATQA (buffer de 2 bytes y FIFO observado), SELECT y READ (18 bytes para datos y CRC). Una colisión en detección continúa hacia la anticolisión de la biblioteca; una selección no válida impide escribir. La versión de chip `92` por sí sola no demuestra comunicación válida con la tarjeta.

## Código y comprobaciones

- `lector.ino`: dispositivo, tarjetas, asistencia y API.
- `WebPage.h`: interfaz monocromática, sin recursos externos.
- `CooperativeServer.h`: envío HTTP que sigue comprobando el vencimiento durante descargas.
- [tests/README.md](tests/README.md): pruebas de firmware y Chromium.

Lectura y escritura utilizan una sola función de atención y una sola limpieza de sesión. La consulta de tarjeta se limita a una cada 250 ms; preparar una operación permite consultar inmediatamente. La selección vuelve a comprobar su plazo antes de iniciar una escritura. Los buffers de lectura se reciben por referencia a un arreglo de 16 bytes para evitar pasar accidentalmente uno menor.

La página consulta personas una vez y el historial solo por revisión. El polling no se solapa y se pausa al ocultar la pestaña. Los GET no modifican los intentos. El servidor mantiene el cierre durante exportaciones, aunque no atiende otras solicitudes ni tarjetas dentro del mismo handler síncrono.

Se retiraron el sketch auxiliar duplicado y los informes y scripts históricos de rendimiento. Las pruebas de regresión y la fixture de registros anteriores se conservan.

La versión 3.3.0 pasó 163 comprobaciones automatizadas y compiló para ESP32 Dev Module con núcleo 3.3.8 y MFRC522 1.4.12: 1061897 bytes de programa y 51924 bytes de variables globales. Estas cifras no miden el heap ni la fragmentación durante el uso.

**La lectura y escritura física reportadas como fallidas todavía requieren confirmación con el equipo. Las pruebas simuladas y la compilación no demuestran por sí solas que ese fallo esté resuelto.**
