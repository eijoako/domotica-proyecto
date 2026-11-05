# Pruebas de regresión

Estas pruebas son opcionales para desarrollo. No se requieren Node.js, Chromium ni un compilador de escritorio para abrir los sketches y subirlos con Arduino IDE.

## Qué se ejecuta

El controlador compila e incluye el `.ino` real unificado. Los handlers HTTP, las validaciones y las operaciones sobre el bloque 4 son los del firmware. Las instancias de pruebas llamadas `lector` y `escritor` ejecutan el mismo sketch en procesos independientes; los nombres distinguen los escenarios, no dos programas distintos.

Las dependencias de Arduino, Wi-Fi y RC522 se sustituyen por adaptadores de prueba. El modelo de tarjeta distingue los estados IDLE, READY, ACTIVE y HALTED; exige selección y autenticación antes de leer o escribir y comprueba que no se modifiquen otros bloques. Se inyectan errores, pérdidas de conexión y respuestas incompletas.

LittleFS se sustituye por un almacén en memoria que conserva sus archivos durante un reinicio simulado. Las pruebas ejecutan el código real de serialización, integridad, recuperación, exportación y reglas de asistencia. Se fuerzan escrituras incompletas, fallos de renombrado, archivos dañados y errores de montaje, comprobando que no se formatee automáticamente.

Las pruebas con Chromium cargan las páginas reales desde un servidor HTTP local que dirige las solicitudes a esos mismos handlers. Comprueban el selector de ocho personas, escritura y sobrescritura simuladas, cancelación, diagnóstico sin registro de asistencia, reinicio del RC522, estado de la clase y copia del informe con y sin acceso al portapapeles. También comprueban la pantalla de teléfono, la paleta de dos colores, la selección monocromática de texto y guardan capturas.

Las pruebas de sincronización comprueban dos pestañas, catálogo consultado una sola vez por carga, historial solo por revisión, recuperación después de perder más de veinte intentos, reinicios, pausa de pestañas ocultas, refresco al volver y ausencia de GET de polling solapados. Verifican que el DOM de presentes e historial se conserve mientras sus datos no cambien.

El adaptador TCP del lector también se ejecuta en las pruebas: se simulan escrituras parciales, interrupciones, receptor sin progreso, errores de socket y vencimiento durante la descarga. Se comprueban el framing HTTP chunked, los bloques de hasta 512 bytes, los presupuestos temporales y la ausencia de llamadas recursivas al manejador HTTP. Una fixture independiente de 84 bytes comprueba que siga leyéndose el formato de asistencia anterior.

Esto **no emula la radio, los cortes de energía ni demuestra una escritura física**. Las bibliotecas MFRC522 y LittleFS se sustituyen en estas pruebas; la compilación para ESP32 se comprueba por separado con las bibliotecas reales.

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

## Resultado de la revisión 3.2.0

- 106 comprobaciones de firmware y dos pruebas de navegador con varios escenarios: **108 aprobadas, 0 fallidas**.
- Sin errores detectados por los sanitizadores de memoria y comportamiento indefinido.
- Sketch unificado compilado para `esp32:esp32:esp32` con Arduino CLI 1.5.1, núcleo ESP32 3.3.8 y MFRC522 1.4.12: 1062049 bytes de programa y 51932 de variables globales. Los paquetes se prepararon en una carpeta temporal de desarrollo; no se agregó PlatformIO ni herramientas al flujo del usuario.
- Pendiente: la comprobación con la ESP32, el RC522 y las tarjetas reales.

La batería conserva las regresiones de la revisión anterior y añade los IDs fijos, ambas clases completas, docentes que no reemplazan sesiones, duplicados, estudiantes de otra clase, reloj, cierre, conservación de sesiones y recuperación después de fallos de persistencia. Incluye la cancelación de un diagnóstico con una tarjeta apoyada para comprobar que no termine marcándola presente.

La unificación añade navegación entre `/` y `/admin` sin reinicio, escritura durante una sesión sin registrar la tarjeta administrativa, cancelación con tarjeta apoyada, vencimiento mientras se espera una escritura y conflicto entre dos clientes. Comprueba también una sola inicialización al arrancar, su pausa de estabilización en cada recuperación, UID inválidos y longitudes de bloque excesivas, sin lecturas fuera de límites.

## Mediciones

`node tests/benchmark.cjs` compara la serialización del sketch unificado con el lector del commit `9e4434c`, utilizando las mismas cabeceras de prueba y opciones del compilador. Requiere el historial Git que contiene ese commit; no basta el ZIP sin historial. El script deja los ejecutables en una carpeta temporal e imprime las muestras como JSON.

Los resultados archivados de la revisión 3.1.0 y sus límites se explican en [OPTIMIZATION.md](OPTIMIZATION.md). No se han atribuido esos resultados a la versión unificada ni son mediciones de heap, fragmentación, radio o latencia de una ESP32 física.
