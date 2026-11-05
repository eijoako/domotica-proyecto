# Optimización 3.1.0: resultados y límites

> Archivo histórico de la revisión 3.1.0 con dos sketches. La versión 3.2.0 los reúne en `lector/lector.ino`; sus instrucciones y pruebas actuales están en los README. Las cifras siguientes no se presentan como mediciones de esa versión unificada.

La revisión mantiene dos sketches independientes para Arduino IDE, sin PlatformIO ni generación previa. La interfaz continúa funcionando sin Internet. Los cambios se concentran en la sincronización HTTP, las cadenas temporales, los estados y las descargas del lector; no sustituyen el protocolo RFID ni el almacenamiento de asistencia.

## Referencia y método

La referencia anterior a los cambios es el commit `9e4434c66f8eaa7c138803b3320551efd3c127bc`, firmware 3.0.0. La primera medición se conserva en [baseline-9e4434c.json](baseline-9e4434c.json).

La comparación reproducible de ambas versiones está en [serialization-results.json](serialization-results.json). Se realizó el 29 de octubre de 2026 en x86_64, con GCC 14.2.0, C++17, `-O2 -DNDEBUG`, cien iteraciones de calentamiento y cinco muestras de diez mil serializaciones por escenario. Cada resultado temporal de la tabla es la mediana de esas cinco muestras; el archivo conserva todas las muestras, incluidas las más lentas.

El script [benchmark.cjs](benchmark.cjs) obtiene el sketch anterior desde Git y compila ambas versiones con los mismos adaptadores actuales. Cronometra solamente la expresión que produce el cuerpo de `/api/status`, consumiendo su longitud mediante una variable `volatile`. No incluye transporte ni copias del servidor de pruebas. La medición inicial sí incluía una copia del servidor simulado: sus tiempos quedan archivados, pero no se mezclan con los de esta comparación.

```sh
node tests/benchmark.cjs
```

Se requiere Node.js, `g++` e historial Git con el commit de referencia. Son herramientas opcionales de desarrollo; no son requisitos para utilizar los sketches. Los ejecutables se generan en una carpeta temporal.

Los escenarios comienzan sin sesión de asistencia y utilizan cero intentos, veinte intentos breves o veinte intentos extensos. Cada intento extenso genera 35 líneas de diagnóstico, pasando por los límites originales del log. Se mide el cuerpo JSON UTF-8 real, no una estimación de su estructura.

## Tamaño y coste de la respuesta frecuente

| Sketch | Historial | Bytes antes | Bytes después | Serialización antes, µs | Después, µs |
|---|---|---:|---:|---:|---:|
| Lector | Vacío | 1350 | 611 | 10,94 | 5,16 |
| Lector | 20 breves | 5624 | 577 | 49,88 | 5,32 |
| Lector | 20 extensos | 42604 | 3758 | 246,05 | 15,52 |
| Escritor | Vacío | 1179 | 440 | 8,93 | 4,00 |
| Escritor | 20 breves | 5453 | 406 | 47,47 | 3,71 |
| Escritor | 20 extensos | 42433 | 3587 | 208,50 | 14,25 |

La respuesta frecuente conserva el diagnóstico actual, por lo que su tamaño depende del último log, además de la sesión. No tiene un tamaño fijo ni se ha recortado su contenido adicionalmente para producir estas cifras. Las revisiones, los tiempos, el identificador de arranque y los datos de una sesión también pueden modificar el tamaño.

En estos escenarios, el catálogo separado ocupa **800 bytes** y se solicita una vez por carga de página. El historial separado ocupa **90 bytes** vacío, **4402 bytes** con veinte intentos breves y **38202 bytes** con veinte extensos. Se descarga al cambiar `historyRevision` o `bootId`, no en cada consulta del estado.

Como comparación del tráfico de cuerpos JSON, veinte consultas del lector con el historial extenso anterior transfieren `20 × 42604 = 852080` bytes. Cinco consultas nuevas del mismo estado, ya sincronizado y en reposo, transfieren `5 × 3758 = 18790` bytes. Es un modelo de unos diez segundos con tiempos de respuesta despreciados: antes se consultaba cada 500 ms y ahora el reposo espera dos segundos. No es una captura de tráfico físico y no incluye cabeceras HTTP, framing chunked, TCP, retransmisiones ni la carga inicial.

La primera carga ahora usa tres solicitudes. En el escenario extenso del lector suma `3758 + 800 + 38202 = 42760` bytes de cuerpos, frente a 42604 antes. Si el historial cambia continuamente, sus snapshots completos también se transfieren continuamente. La mejora se concentra en evitar reconstruir y repetir datos que no cambiaron, no en prometer que toda interacción transfiera menos bytes.

## Memoria y estados

El historial continúa siendo un anillo de veinte intentos. Al comenzar uno se añade una entrada `EN_CURSO`; sus detalles actuales se consultan en el estado y su snapshot se captura al terminar. Las consultas GET no copian ni modifican las entradas.

El historial y el informe se envían por entrada, sin construir una cadena que contenga los veinte logs. Se reservan capacidades para las serializaciones pequeñas y para cada entrada. Continúan existiendo cadenas temporales por entrada: no se trata de un serializador sin asignaciones. El log actual conserva su límite anterior de 3500 bytes y el recorte por línea después de 3000 al superarlo; cada snapshot terminado conserva hasta 1600 bytes. Los límites son de bytes UTF-8, no de caracteres.

Las operaciones incompatibles se representan mediante `OperationState`: `IDLE`, `WAIT_DIAGNOSTIC`, `WAIT_WRITE` y `WRITING`. La asistencia tiene su propio `AttendanceState`: `CLOSED`, `OPEN` y `CLOSE_PENDING`. Los campos booleanos de compatibilidad de las APIs se derivan de esos estados; no son fuentes independientes de verdad. La cuenta regresiva puede cambiar sin incrementar la revisión ni reconstruir el resto del DOM.

La compilación para `esp32:esp32:esp32` con núcleo 3.3.8 y MFRC522 1.4.12 produjo:

| Sketch | Programa 3.0.0, bytes | Programa 3.1.0, bytes | Globales antes, bytes | Globales después, bytes |
|---|---:|---:|---:|---:|
| Lector | 1050921 | 1058317 | 51884 | 51924 |
| Escritor | 978324 | 983920 | 50280 | 50312 |

El programa y los globales aumentaron ligeramente. Por tanto, no se afirma una reducción de flash ni de RAM estática. Evitar el temporal del historial reduce trabajo y asignaciones potenciales de las consultas, pero **el pico de heap y la fragmentación en una ESP32 física no se midieron**. Las pruebas de cinco mil intentos verifican límites de datos y ausencia de errores bajo sanitizadores en escritorio; no sustituyen esa medición.

## Sincronización, persistencia y exportación

Cada navegador mantiene su revisión; no existe un cursor global que un cliente pueda consumir en perjuicio de otro. Un snapshot sustituye la copia local completa. Esto recupera correctamente los veinte intentos disponibles después de perder más de veinte eventos, sin duplicarlos. `bootId` invalida la copia tras un reinicio, incluso si las revisiones vuelven a valores anteriores. Si el arranque cambia entre estado e historial, se descarta esa sincronización y se repite inmediatamente.

El polling es secuencial, con un solo ciclo en ejecución: 500 ms durante operaciones o sesiones y dos segundos en reposo, contados después de completar la sincronización. Se pausa en pestañas ocultas y vuelve inmediatamente al mostrar la pestaña o recuperar conexión. Personas, presentes e historial no se reconstruyen cuando sus datos no cambian. El refresco del panel no controla el temporizador de asistencia.

El lector evalúa el vencimiento antes y después de atender HTTP y entre escrituras TCP no bloqueantes de hasta 512 bytes. El adaptador local [CooperativeServer.h](../lector/CooperativeServer.h) incluye el framing chunked en ese camino: la implementación original de `WebServer` podía bloquear incluso al enviar su terminador. Se corta una escritura sin progreso durante 250 ms o que alcanza dos segundos; son límites por escritura, no un límite total de la descarga. Una interrupción de descarga no elimina registros y permite volver a solicitarla.

No se llama a `handleClient()` recursivamente. El servidor sigue siendo síncrono: otras solicitudes y lecturas RFID esperan hasta terminar o abortar la respuesta. La evaluación cooperativa del vencimiento continúa durante las esperas de envío, pero no puede interrumpir una operación de LittleFS. No se promete tiempo real estricto ante bloqueos del almacenamiento.

El formato persistido conserva versión, checksum y tamaño de **84 bytes**. Cada asistencia aceptada sigue guardándose inmediatamente mediante archivo temporal y renombrado; no se agrupan pendientes en RAM. El cierre que falla queda pendiente y se reintenta, sin admitir nuevos presentes. Se mantiene la recuperación tras reinicio y no se formatea automáticamente. Durante una exportación, si el cierre reemplaza el archivo de la sesión actual, se utiliza el registro actualizado en lugar de exportar el contenido antiguo de un handle abierto.

## Verificación y pendientes físicos

La batería completa terminó con **92 comprobaciones aprobadas y cero fallidas**: noventa de firmware con AddressSanitizer/UndefinedBehaviorSanitizer y dos de Chromium, cada una con varios escenarios. Se conservaron las regresiones de recuperación del RC522 y se añadieron consultas inmutables, dos clientes, pérdida de más de veinte intentos, cinco mil intentos, revisiones, reinicios, estados, una fixture del formato anterior, exportación de 25 sesiones y errores de persistencia. El transporte simulado comprueba escrituras parciales, EAGAIN, EINTR, errores, límites y cierre durante una conexión lenta. Chromium comprueba también reconexión, pestañas ocultas, DOM estable y pantalla de 390 × 844.

Ambos sketches compilaron independientemente con Arduino CLI 1.5.1, ESP32 3.3.8 y MFRC522 1.4.12. El entorno utilizó los paquetes ya instalados: los avisos de descarga de índices y descubrimiento de puertos no impidieron la compilación. Las instrucciones opcionales de pruebas están en [README.md](README.md).

No hubo ESP32, RC522 ni tarjetas físicas conectadas. Siguen pendientes la medición de heap mínimo, mayor bloque libre y fragmentación, los tiempos reales de red/LittleFS, y la escritura y verificación física de las ocho tarjetas. Las pruebas simuladas verifican que se escriba solo el bloque 4 y se comparen sus dieciséis bytes; **no demuestran que una escritura física funcione**.
