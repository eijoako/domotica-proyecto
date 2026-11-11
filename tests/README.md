# Pruebas de regresión

Estas herramientas son opcionales para desarrollo. El uso del programa solo requiere Arduino IDE y MFRC522.

## Ejecución

La lógica se comprueba con Node.js 18 o posterior, g++ con C++17 y sanitizadores:

```sh
node tests/verify.cjs --native-only
```

Para ejecutar también las páginas en Chromium se utiliza Playwright:

```sh
npm install --prefix /tmp/rfid-browser-tests playwright@1.62.1
/tmp/rfid-browser-tests/node_modules/.bin/playwright install chromium
NODE_PATH=/tmp/rfid-browser-tests/node_modules node tests/verify.cjs
```

El script utiliza Chromium del sistema si está disponible. Su ruta puede indicarse mediante `CHROMIUM_PATH`. Los ejecutables y las capturas se guardan en una carpeta temporal que se informa al terminar.

## Cobertura

El controlador incluye el `lector.ino` real, con sus handlers, estados, serialización, lectura y escritura. Las instancias llamadas `lector` y `escritor` ejecutan el mismo programa; sus nombres distinguen escenarios.

Arduino, RC522, Wi-Fi y LittleFS se sustituyen por adaptadores controlados. La batería conserva:
- Escritura y sobrescritura de las ocho personas, verificación completa de 16 bytes y protección de todos los bloques salvo el 4.
- Selección, colisiones, ausencia de tarjeta, errores de buffer, autenticación y confirmaciones de escritura inciertas.
- Reintentos previos a escribir, cancelación, plazo original y prohibición de repetir automáticamente una escritura enviada.
- Una sola limpieza de sesión por resultado y rechazo de una escritura cuando la selección cruza el vencimiento.
- Ambas clases, duplicados, estudiantes de otra clase, segundo docente, cierre y reinicio.
- Persistencia inmediata, fallos de escritura y renombrado, registros dañados, montaje sin formateo y una fixture independiente del formato anterior de 84 bytes.
- Historial de veinte intentos, cinco mil intentos con datos acotados, GET inmutables, revisiones y dos clientes.
- Exportaciones durante una sesión, conexiones lentas, envío parcial y ausencia de recursión HTTP.
- Navegación entre asistencia y administración, interfaz de teléfono, dos colores, copia del informe y sincronización del navegador.

El adaptador reconoce autenticación por dirección del sector 1; la aplicación utiliza el bloque 7, igual que el código de referencia. La escritura simulada sigue limitada al bloque 4.

## Resultado 3.3.0

**163 comprobaciones aprobadas, cero fallidas:** 161 de firmware y dos pruebas de navegador con varios escenarios. Sin errores detectados por AddressSanitizer ni UndefinedBehaviorSanitizer.

Compilación aprobada para `esp32:esp32:esp32`, Arduino CLI 1.5.1, núcleo ESP32 3.3.8 y MFRC522 1.4.12: 1061897 bytes de programa y 51924 bytes de variables globales.

La extracción de la página a `WebPage.h` no añade un paso de generación. Se conservan los adaptadores, el controlador y la fixture porque verifican comportamiento necesario. Se retiraron los benchmarks, resultados JSON e informe histórico de rendimiento que ya no se usaban.

**Los adaptadores no ejecutan la radio física ni el transporte SPI real de MFRC522. Estas pruebas no demuestran que el fallo de lectura comunicado esté resuelto en el equipo.** La compilación ESP32 se comprueba por separado; el heap y la fragmentación física quedan pendientes.
