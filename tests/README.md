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

Arduino, RC522 y Wi-Fi se sustituyen por adaptadores controlados. La batería conserva:
- Escritura y sobrescritura de las ocho personas, verificación completa de 16 bytes y protección de todos los bloques salvo el 4.
- Selección, colisiones, ausencia de tarjeta, errores de buffer, autenticación y confirmaciones de escritura inciertas.
- Reintentos previos a escribir, cancelación, plazo original y prohibición de repetir automáticamente una escritura enviada.
- Una sola limpieza de sesión por resultado y rechazo de una escritura cuando la selección cruza el vencimiento.
- Ambas clases, duplicados, estudiantes de otra clase, segundo docente, cierre y reinicio.
- Sesión en RAM, pérdida completa al reiniciar y rechazo de las rutas eliminadas.
- Historial de veinte intentos, cinco mil intentos con datos acotados, GET inmutables, revisiones y dos clientes.
- Navegación entre asistencia y administración, interfaz de teléfono, dos colores, copia del informe y sincronización del navegador.

## Resultado 4.0.0

159 comprobaciones aprobadas: 157 de firmware con AddressSanitizer/UndefinedBehaviorSanitizer y dos pruebas de Chromium con varios escenarios.

Se adaptaron las regresiones de tarjetas y navegador al estado en RAM. Se eliminaron las pruebas exclusivas de archivos y horarios, su adaptador y la fixture binaria. Las pruebas de administración comprueban rechazo en el servidor de cada operación sin autorización; Chromium comprueba acceso anónimo y credenciales incorrectas frente a credenciales válidas. El puente HTTP de pruebas valida Authorization antes de invocar los handlers reales, mientras que la compilación utiliza WebServer real y su autenticación HTTP Basic.

Se añadieron escenarios de autenticación y lectura previa lentas, reintento antes de escribir, sesión al desbordarse millis(), ausencia de las rutas retiradas y reinicio que pierde presentes. Se conserva la regresión del plazo administrativo al desbordarse millis() y de una escritura incierta que no se repite. Chromium deja una acción sin respuesta y comprueba que el timeout libere la interfaz y pida consultar el resultado antes de reintentar.

Compilación aprobada con Arduino CLI 1.5.1, ESP32 3.3.8, MFRC522 1.4.12 y FQBN esp32:esp32:esp32. Las advertencias del entorno sobre índices de descarga y descubrimiento de puertos no impidieron compilar con los paquetes instalados.

Los adaptadores no ejecutan la radio física ni el transporte SPI real. Quedan pendientes la lectura/escritura con ESP32 y RC522 reales, las ocho tarjetas y la medición física de heap/fragmentación. Las pruebas no garantizan protección frente a clonación.
