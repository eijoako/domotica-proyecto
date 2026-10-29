# Pruebas de regresión

Estas pruebas son opcionales para desarrollo. No se requieren Node.js, Chromium ni un compilador de escritorio para abrir los sketches y subirlos con Arduino IDE.

## Qué se ejecuta

El controlador compila e incluye los archivos `.ino` reales. Los handlers HTTP, las validaciones, las operaciones sobre el bloque 4 y el estado de cada programa son los del firmware.

Las dependencias de Arduino, Wi-Fi y RC522 se sustituyen por adaptadores de prueba. El modelo de tarjeta distingue los estados IDLE, READY, ACTIVE y HALTED; exige selección y autenticación antes de leer o escribir y comprueba que no se modifiquen otros bloques. Se inyectan errores, pérdidas de conexión y respuestas incompletas.

LittleFS se sustituye por un almacén en memoria que conserva sus archivos durante un reinicio simulado. Las pruebas ejecutan el código real de serialización, integridad, recuperación, exportación y reglas de asistencia. Se fuerzan escrituras incompletas, fallos de renombrado, archivos dañados y errores de montaje, comprobando que no se formatee automáticamente.

Las pruebas con Chromium cargan las páginas reales desde un servidor HTTP local que dirige las solicitudes a esos mismos handlers. Comprueban el selector de ocho personas, escritura y sobrescritura simuladas, cancelación, diagnóstico sin registro de asistencia, reinicio del RC522, estado de la clase y copia del informe con y sin acceso al portapapeles. También comprueban la pantalla de teléfono, la paleta de dos colores, la selección monocromática de texto y guardan capturas.

Esto **no emula la radio, los cortes de energía ni demuestra una escritura física**. Las bibliotecas MFRC522 y LittleFS se sustituyen en estas pruebas; su compatibilidad se comprueba además compilando ambos programas contra las bibliotecas reales para ESP32.

## Ejecución

En Linux, las pruebas de lógica necesitan Node.js 18 o posterior y `g++` con C++17, AddressSanitizer y UndefinedBehaviorSanitizer:

```sh
node tests/verify.cjs --native-only
```

La batería completa necesita además Playwright y Chromium. Por ejemplo, se pueden instalar las dependencias de prueba fuera del proyecto:

```sh
npm install --prefix /tmp/rfid-browser-tests playwright@1.62.1
/tmp/rfid-browser-tests/node_modules/.bin/playwright install chromium
NODE_PATH=/tmp/rfid-browser-tests/node_modules node tests/verify.cjs
```

Si se utiliza Chromium del sistema, su ruta puede indicarse mediante `CHROMIUM_PATH`. El controlador conserva los ejecutables y las capturas en una carpeta temporal cuya ruta aparece al terminar. No escribe archivos de compilación dentro de las carpetas de los sketches.

## Resultado de la revisión 3.0.0

- 70 comprobaciones de firmware y una prueba de navegador con varios escenarios: **71 aprobadas, 0 fallidas**.
- Sin errores detectados por los sanitizadores de memoria y comportamiento indefinido.
- Ambos sketches compilados para `esp32:esp32:esp32`, con Arduino CLI 1.5.1, núcleo ESP32 3.3.8 y MFRC522 1.4.12.
- Pendiente: la comprobación con la ESP32, el RC522 y las tarjetas reales.

La batería conserva las regresiones de la revisión anterior y añade los IDs fijos, ambas clases completas, docentes que no reemplazan sesiones, duplicados, estudiantes de otra clase, reloj, cierre, conservación de sesiones y recuperación después de fallos de persistencia. Incluye la cancelación de un diagnóstico con una tarjeta apoyada para comprobar que no termine marcándola presente.
