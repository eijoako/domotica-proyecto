# Pruebas de regresión

Estas pruebas son opcionales para desarrollo. No se requieren Node.js, Chromium ni un compilador de escritorio para abrir los sketches y subirlos con Arduino IDE.

## Qué se ejecuta

El controlador compila e incluye los archivos `.ino` reales. Los handlers HTTP, las validaciones, las operaciones sobre el bloque 4 y el estado de cada programa son los del firmware.

Las dependencias de Arduino, Wi-Fi y RC522 se sustituyen por adaptadores de prueba. El modelo de tarjeta distingue los estados IDLE, READY, ACTIVE y HALTED; exige selección y autenticación antes de leer o escribir y comprueba que no se modifiquen otros bloques. Se inyectan errores, pérdidas de conexión y respuestas incompletas.

Las pruebas con Chromium cargan las páginas reales desde un servidor HTTP local que dirige las solicitudes a esos mismos handlers. Comprueban los botones, la escritura y sobrescritura simuladas, cancelación, errores, vencimiento, pérdida de conexión del navegador y lectura independiente. También comprueban la disposición en pantalla de teléfono y guardan capturas.

Esto **no emula la radio ni demuestra una escritura física**. La biblioteca MFRC522 se sustituye en estas pruebas; su compatibilidad se comprueba además compilando ambos programas contra la biblioteca real para ESP32.

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

## Resultado de la revisión 2.0.1

- 44 comprobaciones de firmware y una prueba de navegador con varios escenarios: **45 aprobadas, 0 fallidas**.
- Sin errores detectados por los sanitizadores de memoria y comportamiento indefinido.
- Ambos sketches compilados para `esp32:esp32:esp32`, con Arduino CLI 1.5.1, núcleo ESP32 3.3.8 y MFRC522 1.4.12.
- Lector: 960336 bytes de programa y 47200 bytes de variables globales.
- Escritor: 965632 bytes de programa y 47208 bytes de variables globales.
- Pendiente: la comprobación con la ESP32, el RC522 y las tarjetas reales.

La batería detectó y cubre regresiones para el reintento después de una selección fallida, la relectura tras reconectar el RC522, la conservación del último resultado ante pedidos inválidos y el rechazo explícito de lecturas incompletas.
