# Lector y escritor RFID — ESP32 + RC522

> Proyecto escolar realizado completamente con **inteligencia artificial y prompt engineering**. El código y la documentación se desarrollaron a partir de requisitos para una demostración de aula.

Este repositorio contiene **dos programas independientes para Arduino IDE**:

- **`lector/lector.ino`** consulta la tarjeta y muestra su contenido; no escribe.
- **`escritor/escritor.ino`** permite ingresar un ID desde una página web, escribirlo y verificarlo.

Ambos muestran el diagnóstico en la web y en el Monitor serie. Se carga **uno por vez** en la ESP32. Cargar un programa reemplaza al otro en la placa, pero no modifica por sí solo las tarjetas.

El firmware anterior de asistencia sigue recuperable en el historial de Git, hasta el commit `3a6b8dc`. Esta versión de diagnóstico no incluye clases, estudiantes, sesiones ni LittleFS.

## 1. Materiales y conexiones

Se necesitan una **ESP32 Dev Module**, un RC522, tarjetas **MIFARE Classic 1K de 13,56 MHz**, cables y un cable USB de datos. Las tarjetas de 125 kHz no funcionan con el RC522.

Las conexiones se realizan con la placa apagada:

| RC522 | ESP32 |
|---|---|
| SDA / SS | GPIO 5 |
| SCK | GPIO 18 |
| MOSI | GPIO 23 |
| MISO | GPIO 19 |
| RST | GPIO 22 |
| GND | GND |
| 3.3V | 3.3V |

El RC522 debe alimentarse con **3,3 V, nunca con 5 V**.

## 2. Preparación de Arduino IDE

1. Instalar o abrir **Arduino IDE 2.x**.
2. En `Archivo > Preferencias > URLs adicionales de gestores de tarjetas`, agregar:

   ```text
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```

3. En el Gestor de placas, instalar **esp32 by Espressif Systems**.
4. En el Administrador de bibliotecas, instalar **MFRC522**, de Miguel Balboa, versión **1.4.12**.
5. Descargar el repositorio con `Code > Download ZIP` en GitHub y descomprimirlo antes de abrir el sketch.

`WiFi`, `WebServer` y `SPI` vienen con el soporte de ESP32. **No se necesitan PlatformIO, ArduinoJson ni complementos para subir archivos.**

## 3. Primera comprobación con el lector

1. Abrir **`lector/lector.ino`**. Debe permanecer en la carpeta `lector`; no deben juntarse los dos sketches en una misma carpeta.
2. Elegir `Herramientas > Placa > ESP32 Arduino > ESP32 Dev Module`.
3. Conectar la ESP32 y seleccionar el puerto USB correspondiente.
4. Pulsar **Subir**. Si queda en `Connecting…`, mantener pulsado **BOOT** hasta que comience la carga y después soltarlo.
5. Abrir el **Monitor serie a 115200 baudios** y pulsar **EN/RESET**. Debe aparecer `RFID-Lector | firmware 2.0.1`, el diagnóstico del RC522 y su dirección IP.
6. Conectar el teléfono a **`RFID-Lector`**, con la contraseña **`cambiar-esta-clave`**. Si avisa que no hay Internet, debe mantenerse la conexión a esa red.
7. Abrir **`http://192.168.4.1`**.
8. Presentar una sola tarjeta sobre la antena del RC522. La web y el Monitor serie mostrarán UID técnico, tipo y contenido del bloque 4.

Si no hay un ID válido, igualmente se muestra el bloque en hexadecimal. El UID es solo información técnica: **no se utiliza como identificador interno**.

Una tarjeta apoyada no genera lecturas repetidas. Para releerla, debe retirarse durante al menos medio segundo y acercarse nuevamente.

## 4. Escritura de un ID

1. Abrir **`escritor/escritor.ino`** y cargarlo en la misma ESP32.
2. Comprobar en el Monitor serie que aparezca `RFID-Escritor | firmware 2.0.1`.
3. Conectar el teléfono a **`RFID-Escritor`**, con la misma contraseña, y abrir **`http://192.168.4.1`**.
4. Ingresar un ID, por ejemplo **`PRUEBA_01`**. Se admiten entre 1 y 16 letras sin tildes, números, guiones y guiones bajos. Las mayúsculas y minúsculas se conservan.
5. Marcar la confirmación de sobrescritura y pulsar **Preparar escritura**.
6. Presentar una sola tarjeta y mantenerla quieta.
7. Consultar el diagnóstico: detección → selección → autenticación → lectura previa → escritura → verificación.
8. Considerar confirmada la operación solo cuando aparezca **`ESCRITURA_OK`**.
9. Retirar la tarjeta. Para volver a escribir debe prepararse una nueva operación.

El botón **Cancelar** abandona una operación que todavía espera tarjeta. A los 60 segundos la espera se cancela automáticamente. No se escribe al arrancar ni fuera de una operación preparada.

No hace falta crear estudiantes, clases o personas.

## 5. Verificación independiente y sobrescritura

La comprobación completa requiere estos pasos:

1. Con el escritor, guardar `PRUEBA_01` y obtener `ESCRITURA_OK`.
2. Cargar el lector y comprobar que la misma tarjeta devuelva exactamente `PRUEBA_01`.
3. Cargar el escritor y sobrescribirla con `OTRO_02`.
4. Cargar el lector y comprobar `OTRO_02`. En hexadecimal, los bytes posteriores al ID deben ser `00`, sin restos del ID anterior.
5. Reiniciar la ESP32 y repetir la lectura. El ID permanece en la tarjeta.

Este procedimiento verifica el contenido con un programa distinto del escritor. **La validación del hardware requiere completarlo con una ESP32, un RC522 y tarjetas reales.**

## 6. Interpretación de errores

| Mensaje | Significado y comprobación |
|---|---|
| `ERROR_LECTOR` / registro `00` o `FF` | No hay comunicación con el RC522. Deben revisarse alimentación y conexiones SPI y reiniciarse la placa. |
| `ESPERANDO_TARJETA` | El RC522 responde, pero todavía no detecta tarjeta. Debe probarse una sola tarjeta de 13,56 MHz sobre la antena. |
| `ERROR_DETECCION` / `ERROR_SELECCION` | Hubo respuesta, pero no se completó la selección. Debe mantenerse una sola tarjeta quieta y volver a preparar la operación. |
| `TIPO_NO_COMPATIBLE` | Se detectó una tarjeta distinta de MIFARE Classic 1K. No se escribe. |
| `ERROR_AUTENTICACION` | Falló el acceso con Key A predeterminada. Puede haber otra clave o problemas de comunicación. |
| `ERROR_LECTURA` | No pudo leerse el bloque 4. Si era la lectura previa, se abandona la escritura. |
| `ERROR_ESCRITURA` | El lector no confirmó la escritura. Debe comprobarse el contenido con el programa lector. |
| `ERROR_VERIFICACION` | La escritura fue enviada, pero su lectura posterior falló o no coincidió. No se informa éxito. |
| `TIEMPO_AGOTADO` / `CANCELADO` | La espera terminó sin ejecutar una escritura. |

Si persiste un problema, debe compartirse el **diagnóstico completo** de la web o del Monitor serie, desde la versión del programa hasta el último error. La página está en `/`; ya no se usa `/admin`.

Si falla la carga por USB, se recomienda cerrar el Monitor serie, reducir `Upload Speed` a **115200** y comprobar el cable y el puerto.

## 7. Contenido modificado y configuración

El escritor usa exclusivamente el **bloque de datos 4 del sector 1**, autenticado con **Key A `FF FF FF FF FF FF`**. No prueba otras claves, no modifica permisos y no escribe el bloque 0 ni tráileres de sector.

Siempre escribe los 16 bytes del bloque: el ID seguido de ceros. No se guardan nombres ni cédulas. El diagnóstico y la última lectura viven en RAM y se pierden al reiniciar la placa.

La contraseña está en `WIFI_PASSWORD`, al comienzo de cada sketch; debe tener entre 8 y 63 caracteres. Para mantener la misma en ambas redes, debe cambiarse en los dos programas.

La web y el Monitor serie muestran **firmware 2.0.1** y el nombre del programa, para confirmar que se cargó el sketch correcto.

## 8. Verificación del software

Cada sketch debe compilarse por separado para ESP32 Dev Module. Las pruebas de lógica permiten comprobar validaciones, cancelación y errores, pero no demuestran por sí solas la escritura física de una tarjeta.

Esta versión se compiló con **Arduino CLI 1.5.1**, **ESP32 3.3.8**, placa **ESP32 Dev Module** (`esp32:esp32:esp32`) y **MFRC522 1.4.12**:

| Programa | Compilación | Comprobación física |
|---|---|---|
| Lector | Correcta | Pendiente en la placa del liceo |
| Escritor | Correcta | Pendiente en la placa del liceo |

La revisión 2.0.1 pasó **45 comprobaciones automatizadas**, incluidas pruebas del firmware con un modelo de estados de tarjeta y pruebas de las páginas en Chromium con pantalla de teléfono. Se verificaron validaciones, cancelación, vencimiento, sobrescritura con un ID más corto, reconexiones y errores de lectura, autenticación, escritura y verificación.

Las pruebas y sus límites se documentan en [tests/README.md](tests/README.md). Son herramientas opcionales para desarrollo; no se necesitan para cargar los programas con Arduino IDE.
