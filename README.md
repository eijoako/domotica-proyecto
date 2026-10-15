# Asistencia RFID — ESP32 + RC522

> Proyecto escolar realizado completamente con **inteligencia artificial** y **prompt engineering**. El código, la documentación y la estructura inicial fueron generados a partir de requisitos definidos para un prototipo de aula y deben revisarse, comprenderse y probarse antes de su uso.

Este repositorio presenta un prototipo local y sin Internet para registrar asistencia con una placa ESP32, un lector RC522 y tarjetas MIFARE Classic 1K. No requiere PlatformIO: está preparado para abrirse y cargarse con **Arduino IDE**.

Una tarjeta de docente abre una clase durante diez minutos. Durante ese período, cada tarjeta de estudiante activa se registra una sola vez. El ESP32 guarda los datos en su memoria LittleFS, incluso si se reinicia.

Si se reinicia durante una sesión, las lecturas ya guardadas se conservan y la sesión anterior queda marcada como `DEVICE_RESTART`. El equipo vuelve a iniciar sin una clase abierta.

> Este es un proyecto demostrativo para el liceo, no un sistema de seguridad ni de asistencia para toda una institución.

## 1. Materiales necesarios en el liceo

- Una ESP32 Dev Module y cable USB de datos.
- Un RC522 y cables Dupont.
- Tarjetas MIFARE Classic 1K preparadas por el programa de administración de tarjetas.
- Una computadora en la que sea posible abrir Arduino IDE. No hace falta instalar PlatformIO.

El RC522 debe conectarse a **3.3 V, nunca a 5 V**:

| RC522 | ESP32 |
|---|---:|
| SDA / SS | GPIO 5 |
| SCK | GPIO 18 |
| MOSI | GPIO 23 |
| MISO | GPIO 19 |
| RST | GPIO 22 |
| GND | GND |
| 3.3V | 3.3V |

## 2. Instalación y preparación de Arduino IDE

En la computadora del liceo, el público debe realizar los siguientes pasos:

1. Instalar o abrir **Arduino IDE 2.x**.
2. Abrir `Archivo > Preferencias`.
3. En **URLs adicionales de gestores de tarjetas**, agregar esta dirección:

   ```text
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```

   Si ya hay otra dirección, sepárala con una coma.
4. Ir a `Herramientas > Placa > Gestor de placas`, buscar **esp32** e instalar **esp32 by Espressif Systems**.
5. Ir a `Herramientas > Gestionar bibliotecas` e instalar estas dos bibliotecas:

   - **MFRC522** de Miguel Balboa.
   - **ArduinoJson** de Benoit Blanchon.

`WiFi`, `WebServer`, `SPI` y `LittleFS` se instalan junto con el paquete de la placa ESP32; no hay que buscarlas por separado.

## 3. Apertura y configuración del proyecto

1. Copiar toda la carpeta `domotica-proyecto` a la computadora del liceo. No debe copiarse únicamente el archivo `.ino`: también se necesita la carpeta `src`.
2. Abrir `domotica-proyecto.ino` con Arduino IDE. El archivo principal debe conservar ese nombre y permanecer junto a la carpeta `src`.
3. Abrir `src/config.h` y cambiar, al menos, esta línea antes de una demostración pública:

   ```cpp
   constexpr char AP_PASSWORD[] = "cambiar-esta-clave";
   ```

   Se recomienda elegir una contraseña de ocho caracteres o más. También puede cambiarse `AP_SSID` para dar otro nombre a la red Wi-Fi.

## 4. Selección de la placa y carga del programa

1. Conectar la ESP32 por USB.
2. En `Herramientas > Placa`, elegir **ESP32 Arduino > ESP32 Dev Module**.
3. En `Herramientas > Puerto`, seleccionar el puerto que apareció al conectar la placa.
4. Pulsar el botón **Subir** (la flecha hacia la derecha).
5. Si el IDE queda en “Connecting…”, mantener pulsado el botón **BOOT** de la ESP32 hasta que comience la carga y luego soltarlo.
6. Al terminar, abrir `Herramientas > Monitor serie`, seleccionar **115200 baudios** y pulsar el botón **EN/RESET** de la placa. Debe aparecer una línea parecida a esta:

   ```text
   AP: Asistencia-RFID | IP: 192.168.4.1 | storage: OK
   ```

Si aparece `storage: ERROR`, no debe realizarse una demostración. Se recomienda revisar que el paquete ESP32 esté actualizado y volver a cargar el programa.

Ante un fallo de montaje de LittleFS en una placa que ya había funcionado, el programa **no formatea la memoria automáticamente**. De ese modo evita borrar el historial por accidente. En ese caso debe revisarse la placa y recuperarse la información antes de borrar o reformatear la memoria.

## 5. Preparar las tarjetas

El proyecto dispone de una pantalla local de administración en `http://192.168.4.1/admin`. Desde allí se pueden crear, actualizar y borrar clases, docentes y estudiantes; los datos quedan guardados en LittleFS. También permite escribir el `cardId` en una tarjeta, por lo que no es necesario usar un programa separado para la demostración.

La administración no tiene cuentas de usuario: cualquier dispositivo conectado a la red Wi-Fi de la ESP32 puede abrir `/admin`. Por ello, la contraseña de la red debe cambiarse antes de una demostración y la administración debe utilizarse únicamente en un entorno de confianza.

Este programa **lee** un `cardId` guardado en la tarjeta. No usa el UID de fábrica como identidad.

Para configurar una tarjeta desde `/admin`, el público debe seguir este orden:

1. Crear una clase, por ejemplo `3EMS-ECO-A`.
2. Crear un docente o estudiante activo, con un ID propio y un `Card ID` de hasta 16 caracteres. Los IDs admiten letras sin tildes, números, guiones y guiones bajos; los nombres sí admiten tildes y espacios. En el caso de un docente, debe indicarse una clase existente.
3. En la sección **Escribir una tarjeta**, ingresar ese mismo `Card ID` y pulsar **Preparar escritura**.
4. Acercar una tarjeta al RC522. La pantalla mostrará si la escritura se realizó correctamente.

Una tarjeta ya utilizada también puede reescribirse. Tras pulsar **Preparar escritura**, el lector puede despertar la tarjeta aunque haya quedado apoyada desde una lectura anterior. El programa vuelve a leer el bloque 4 y solo confirma la escritura cuando su contenido coincide con el nuevo `Card ID`. Si no se presenta ninguna tarjeta en un minuto, la operación se cancela para que la lectura de asistencia vuelva a funcionar.

Una vez escrita, la tarjeta debe retirarse del lector y acercarse de nuevo para usarla en la asistencia.

El sistema escribe únicamente el bloque de datos 4 (sector 1) usando la clave de demostración predeterminada. Nunca deben escribirse el bloque 0, los tráileres de sector, las claves ni los bits de acceso.

Al iniciarse por primera vez, el programa crea estos datos de ejemplo en LittleFS:

- Profesor: `T1A2B3C4`, clase `3EMS-ECO-A`.
- Alumno: `A7F3C912`.

Para una prueba real, las asignaciones deben actualizarse desde la administración local. Una misma tarjeta activa no puede estar asignada a dos personas.

## 6. Uso de la asistencia, de principio a fin

1. En un teléfono, buscar la red Wi-Fi creada por la ESP32 (por defecto, `Asistencia-RFID`) e ingresar la contraseña configurada.
2. Abrir el navegador y visitar `http://192.168.4.1`.
3. Pulsar **“Sincronizar hora del teléfono”**. Esto proporciona fecha y hora al ESP32 sin necesitar Internet; es obligatorio para guardar el historial diario correctamente. Los archivos diarios usan la hora de Uruguay (UTC−3).
4. La pantalla mostrará **CLASE CERRADA**. Debe acercarse una tarjeta de docente registrada al RC522.
5. La pantalla mostrará la clase abierta, el docente, los presentes y una cuenta regresiva de diez minutos.
6. Deben acercarse las tarjetas de estudiantes registrados. Cada lectura válida mostrará `Alumno registrado: Nombre` y se guardará inmediatamente en la memoria.
7. Si un estudiante pasa dos veces la tarjeta, aparecerá `Ya estaba registrado` y no se duplicará el registro. Una tarjeta desconocida mostrará `Tarjeta no registrada`.
8. A los diez minutos, la clase se cerrará sola. La información ya registrada se conservará; no se borrará ni hará falta volver a pasar la tarjeta del docente.
9. Los archivos se guardarán internamente como `/attendances/AAAA-MM-DD.json`. Para recuperarlos o editarlos desde una computadora, el proyecto podría incorporar una herramienta de exportación en una versión posterior, o utilizarse un cargador de LittleFS compatible.

## 7. Prueba corta y solución de problemas

Para no esperar diez minutos durante una prueba, puede cambiarse temporalmente en `src/config.h`:

```cpp
constexpr uint32_t SESSION_DURATION_MS = 10UL * 60UL * 1000UL;
```

Por ejemplo, puede utilizarse `30UL * 1000UL` para treinta segundos. Deben restaurarse los diez minutos antes de utilizarlo en una demostración real.

- **No detecta tarjetas:** deben revisarse los cables y que el RC522 use 3.3 V.
- **No abre la clase:** debe sincronizarse la hora y verificarse que sea una tarjeta de docente asignada.
- **“Tarjeta no registrada”:** la tarjeta no tiene el `cardId` correcto en el bloque 4, o no está asignada como activa.
- **No aparece la red Wi-Fi:** debe reiniciarse la ESP32 y revisarse el Monitor serie.

## Límites del prototipo

La clave por defecto de MIFARE Classic (`FF FF FF FF FF FF`) solo es aceptable para la demostración. Una tarjeta demuestra posesión, no que el estudiante sea necesariamente quien está presente. No deben almacenarse cédulas, nombres u otros datos personales en la tarjeta.
