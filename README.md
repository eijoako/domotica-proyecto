# Asistencia RFID — ESP32 + RC522

Prototipo local y sin Internet para registrar asistencia con una placa ESP32, un lector RC522 y tarjetas MIFARE Classic 1K. No requiere PlatformIO: está preparado para abrirse y cargarse con **Arduino IDE**.

Una tarjeta de profesor abre una clase durante diez minutos. Durante ese período, cada tarjeta de alumno activa se registra una sola vez. El ESP32 guarda los datos en su memoria LittleFS, incluso si se reinicia.

> Este es un proyecto demostrativo para el liceo, no un sistema de seguridad ni de asistencia para toda una institución.

## 1. Qué necesitas en el liceo

- Una ESP32 Dev Module y cable USB de datos.
- Un RC522 y cables Dupont.
- Tarjetas MIFARE Classic 1K preparadas por el programa de administración de tarjetas.
- Una computadora donde puedas abrir Arduino IDE. No hace falta instalar PlatformIO.

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

## 2. Instalar y preparar Arduino IDE

1. Instala o abre **Arduino IDE 2.x** en la computadora del liceo.
2. Abre `Archivo > Preferencias`.
3. En **URLs adicionales de gestores de tarjetas**, agrega esta dirección:

   ```text
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```

   Si ya hay otra dirección, sepárala con una coma.
4. Ve a `Herramientas > Placa > Gestor de placas`; busca **esp32** e instala **esp32 by Espressif Systems**.
5. Ve a `Herramientas > Gestionar bibliotecas` e instala estas dos bibliotecas:

   - **MFRC522** de Miguel Balboa.
   - **ArduinoJson** de Benoit Blanchon.

`WiFi`, `WebServer`, `SPI` y `LittleFS` se instalan junto con el paquete de la placa ESP32; no hay que buscarlas por separado.

## 3. Abrir el proyecto

1. Copia toda esta carpeta `domotica-proyecto` a la computadora del liceo. No copies únicamente el archivo `.ino`: también se necesita la carpeta `src`.
2. Abre `domotica-proyecto.ino` con Arduino IDE. El archivo principal debe conservar ese nombre y permanecer junto a la carpeta `src`.
3. Abre `src/config.h` y cambia al menos esta línea antes de una demostración pública:

   ```cpp
   constexpr char AP_PASSWORD[] = "cambiar-esta-clave";
   ```

   Elige una contraseña de ocho caracteres o más. También puedes cambiar `AP_SSID` si quieres otro nombre para la red Wi-Fi.

## 4. Seleccionar la placa y cargar el programa

1. Conecta la ESP32 por USB.
2. En `Herramientas > Placa`, elige **ESP32 Arduino > ESP32 Dev Module**.
3. En `Herramientas > Puerto`, selecciona el puerto que apareció al conectar la placa.
4. Pulsa el botón **Subir** (la flecha hacia la derecha).
5. Si el IDE queda en “Connecting…”, mantén pulsado el botón **BOOT** de la ESP32 hasta que comience la carga y luego suéltalo.
6. Cuando termine, abre `Herramientas > Monitor serie`, selecciona **115200 baudios** y pulsa el botón **EN/RESET** de la placa. Debe aparecer una línea parecida a esta:

   ```text
   AP: Asistencia-RFID | IP: 192.168.4.1 | storage: OK
   ```

Si aparece `storage: ERROR`, no realices una demostración: revisa que el paquete ESP32 esté actualizado y vuelve a cargar el programa.

## 5. Preparar las tarjetas

Este programa de asistencia **lee** un `cardId` que ya debe existir en la tarjeta. No usa el UID de fábrica como identidad.

Usa el programa separado de administración de tarjetas para escribir el `cardId` en el bloque de datos 4 (sector 1) y para asignarlo a la persona correspondiente. Nunca escribas el bloque 0, los tráileres de sector, las claves ni los bits de acceso.

Al iniciarse por primera vez, el programa crea estos datos de ejemplo en LittleFS:

- Profesor: `T1A2B3C4`, clase `3EMS-ECO-A`.
- Alumno: `A7F3C912`.

Para una prueba real, actualiza las asignaciones mediante el programa de administración. Una misma tarjeta activa no puede estar asignada a dos personas.

## 6. Usar la asistencia, de principio a fin

1. En tu teléfono, busca la red Wi-Fi creada por la ESP32 (por defecto, `Asistencia-RFID`) y escribe la contraseña que configuraste.
2. Abre el navegador y visita `http://192.168.4.1`.
3. Pulsa **“Sincronizar hora del teléfono”**. Esto da fecha y hora al ESP32 sin necesitar Internet; es obligatorio para guardar el historial diario correctamente.
4. La pantalla mostrará **CLASE CERRADA**. Acerca una tarjeta de profesor registrada al RC522.
5. La pantalla mostrará la clase abierta, el profesor, los presentes y una cuenta regresiva de diez minutos.
6. Acerca las tarjetas de alumnos registrados. Cada lectura válida mostrará `Alumno registrado: Nombre` y se guardará inmediatamente en la memoria.
7. Si un alumno pasa dos veces la tarjeta, aparecerá `Ya estaba registrado` y no se duplica el registro. Una tarjeta desconocida muestra `Tarjeta no registrada`.
8. A los diez minutos, la clase se cierra sola. La información ya registrada se conserva; no se borra ni hace falta volver a pasar la tarjeta del profesor.
9. Los archivos se guardan internamente como `/attendances/AAAA-MM-DD.json`. Para recuperar o editar esos archivos desde una computadora, puedes añadir una herramienta de exportación en una siguiente versión o usar un cargador de LittleFS compatible.

## 7. Prueba corta y solución de problemas

Para no esperar diez minutos durante una prueba, cambia temporalmente en `src/config.h`:

```cpp
constexpr uint32_t SESSION_DURATION_MS = 10UL * 60UL * 1000UL;
```

Por ejemplo, usa `30UL * 1000UL` para treinta segundos. Vuelve a dejar diez minutos antes de utilizarlo de verdad.

- **No detecta tarjetas:** revisa los cables y que el RC522 use 3.3 V.
- **No abre la clase:** sincroniza la hora y verifica que sea una tarjeta de profesor asignada.
- **“Tarjeta no registrada”:** la tarjeta no tiene el `cardId` correcto en el bloque 4, o no está asignada como activa.
- **No aparece la red Wi-Fi:** reinicia la ESP32 y mira el Monitor serie.

## Límites del prototipo

La clave por defecto de MIFARE Classic (`FF FF FF FF FF FF`) solo es aceptable para la demostración. Una tarjeta demuestra posesión, no que el alumno sea necesariamente quien está presente. No almacenes cédulas, nombres u otros datos personales en la tarjeta.
