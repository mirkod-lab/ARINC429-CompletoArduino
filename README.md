# Receptor serial ARINC429-Completo para Arduino Uno

Este firmware recibe las palabras que envía la aplicación del proyecto vecino `ARINC429-Completo`. El repositorio de este receptor estaba vacío; el protocolo se contrastó con `services/serial.js`, `services/arinc429.js` y `arduino/firmware.ino` del emisor disponible localmente.

## Compatibilidad comprobada en el código fuente

La aplicación abre el puerto a 115200 baudios, 8 bits, sin paridad y 1 bit de parada (8N1). Cada palabra se envía en una trama fija de 11 bytes:

```text
02 | 01 | 01 | secuencia | palabra[0] | palabra[1] | palabra[2] | palabra[3] | CRC bajo | CRC alto | 03
```

- `02`: inicio; `03`: fin.
- `01`: versión del protocolo y comando de palabra.
- La palabra se reconstruye como `b0 | b1<<8 | b2<<16 | b3<<24` (little-endian), igual que en el emisor.
- CRC16-Modbus inicial `0xFFFF`, calculado sobre versión, comando, secuencia y los cuatro bytes de palabra; el CRC se transmite little-endian.
- El receptor responde con el ACK de 7 bytes que espera la aplicación. El ACK solo confirma que la trama serial pasó la validación y entró en la cola de salida; no confirma paridad ARINC válida ni ninguna transmisión eléctrica.

El receptor también comprueba paridad impar sobre los 32 bits. Una palabra con paridad incorrecta sigue recibiendo ACK si su trama serial y CRC son válidos, para no confundir errores de contenido con pérdida del protocolo serial. El estado se conserva para mostrarlo cuando se conecte un LCD.

## Limitaciones importantes

- **No hay LCD especificado ni conectado** (se informó que todavía no se dispone de uno). Por eso el firmware no incluye ni asume una biblioteca, interfaz, dirección I2C, dimensiones o cableado de LCD. La función `mostrarEstado` es el punto de integración; el diagnóstico por Monitor Serie está desactivado por defecto para no contaminar los ACK. Activarlo solo con la aplicación desconectada. Sin LCD o diagnóstico serial, el estado no es visible para el usuario.
- La aplicación manda únicamente la palabra de 32 bits. No transmite el nombre del instrumento, sus unidades ni sus parámetros de escala. El receptor puede extraer los campos crudos (etiqueta, SDI, datos, SSM y paridad), pero **no puede determinar un instrumento ni convertir el valor a unidades** sin un mapeo auténtico.
- Los perfiles de instrumentos precargados por la aplicación son explícitamente ficticios. No se copian al firmware ni se usan para asignar etiquetas, escala o unidades. Cuando haya configuraciones reales, se necesita definir el mapeo con sus hojas de datos; alternativamente, habría que ampliar el protocolo para transmitir identificador/metadatos.
- El generador del emisor permite distinto orden de bits de etiqueta (`labelBitOrder`). Aquí se muestra la etiqueta como byte crudo, sin interpretarla como una etiqueta ARINC oficial.
- Los pines 13/12 representan cada bit lógico de la palabra, en orden de bit 1 a bit 32, a 100 ms por estado. 100 ms es un ritmo visual configurable, no una capacidad máxima del Uno ni una temporización ARINC 429. La salida no es una interfaz diferencial ARINC 429 y no genera niveles de ±10 V.
- El servicio serial actual del emisor abre y cierra el COM en cada envío. En un Uno, la apertura del puerto USB puede activar el auto-reset por DTR y hacer que se pierda una trama durante el reinicio del bootloader. No se pudo comprobar este comportamiento sin placa; si ocurre, el arreglo requiere mantener el puerto abierto desde la aplicación o resolver el auto-reset de forma controlada.

## Conexión segura

1. Conecta el Uno al ordenador por USB; la aplicación usa el puerto serie USB a 115200 8N1.
2. Para observar pines 13 y 12, conecta un LED con su resistencia limitadora apropiada desde cada pin a GND (o usa entradas lógicas de alta impedancia compatibles con 5 V). Comparte GND solo con el circuito lógico de prueba.
3. Bit `1`: pin 13 HIGH y pin 12 LOW. Bit `0`: pin 13 LOW y pin 12 HIGH.
4. No conectes estos pines a un bus ARINC 429, a ±10 V ni a una etapa diferencial. Para una interfaz eléctrica ARINC haría falta hardware de línea separado y especificado.
5. No se puede indicar cableado de LCD hasta conocer su modelo e interfaz.

## Selección, compilación y carga

1. Abre `arduino/receptor/receptor.ino` en Arduino IDE.
2. Selecciona **Arduino Uno** en **Herramientas → Placa → Arduino AVR Boards → Arduino Uno** y el puerto COM de la placa.
3. Verifica y carga el programa. No hace falta instalar bibliotecas adicionales; usa `Arduino.h` y el núcleo de Arduino.
4. Cierra el Monitor Serie antes de usar la aplicación, porque el puerto COM solo puede estar abierto por un programa a la vez. La aplicación debe estar configurada para modo hardware y 115200 baudios.
5. Para usar diagnóstico serial manual, cambia `HABILITAR_DIAGNOSTICO_SERIAL` a `1`, carga el firmware y abre el Monitor Serie a 115200. Desconecta/cierra la aplicación durante esta prueba: los mensajes de diagnóstico no forman parte del protocolo ACK.

## Pruebas preparadas

Los vectores con trama completa, casos de paridad, datos incompletos, recuperación y salidas están en [`tests/README.md`](./tests/README.md). No se realizaron pruebas físicas; la placa, el circuito de LEDs, el LCD y la comunicación real deben probarse por separado.
