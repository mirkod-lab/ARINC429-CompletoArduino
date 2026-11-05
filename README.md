# Receptor serial ARINC429-Completo para Arduino Uno

Este firmware recibe las palabras que envía la aplicación del proyecto vecino `ARINC429-Completo`. El repositorio de este receptor estaba vacío; el protocolo se contrastó con `services/serial.js`, `services/arinc429.js` y `arduino/firmware.ino` del emisor disponible localmente.

## Compatibilidad comprobada en el código fuente

La aplicación abre el puerto a 115200 baudios, 8 bits, sin paridad y 1 bit de parada (8N1). Cada envío nuevo usa una trama fija de 70 bytes:

```text
02 | 01 | 02 | secuencia | palabra[4 LE] | longitud + instrumento[24] |
longitud + unidad[12] | longitud + valor[20] | CRC bajo | CRC alto | 03
```

- `02`: inicio; `03`: fin.
- `01`: versión del protocolo; comando `02`: palabra con metadatos de presentación.
- La palabra se reconstruye como `b0 | b1<<8 | b2<<16 | b3<<24` (little-endian), igual que en el emisor.
- Cada texto lleva una longitud y espacio fijo rellenado con cero; máximos: instrumento 24, unidad 12 y valor 20 caracteres ASCII. Se quitan tildes y se adaptan símbolos no ASCII al transmitir. Son el nombre, valor y unidad guardados por la aplicación para ese envío.
- CRC16-Modbus inicial `0xFFFF`, calculado sobre versión, comando, secuencia y todo el contenido de la trama; el CRC se transmite little-endian.
- El receptor también acepta la trama antigua de palabra sola (11 bytes, comando `01`) para compatibilidad. Si llega una trama antigua, no hay metadatos para mostrar.
- El receptor responde con el ACK de 7 bytes que espera la aplicación. El ACK solo confirma que la trama serial pasó la validación y entró en la cola de salida; no confirma paridad ARINC válida ni ninguna transmisión eléctrica.

El receptor también comprueba paridad impar sobre los 32 bits. Una palabra con paridad incorrecta sigue recibiendo ACK si su trama serial y CRC son válidos. En una trama extendida, el LCD presenta el nombre y valor/unidad enviados por la aplicación durante 6,5 s, desplazando textos largos; luego muestra etiqueta octal, SDI, SSM y paridad durante 1,8 s.

## Limitaciones importantes

- El firmware está preparado para un **LCD 16x2 compatible con HD44780 en modo paralelo de 4 bits**, como un LCD1602 con conector de 16 pines. No está configurado para módulos I2C. Usa la biblioteca `LiquidCrystal` incluida en el entorno Arduino.
- El nombre, valor y unidad en la pantalla son los metadatos guardados por la aplicación para el envío; el receptor verifica además el CRC serial y la paridad de la palabra, pero no deriva independientemente la escala a partir de esos metadatos.
- Los perfiles de instrumentos precargados por la aplicación son explícitamente ficticios. La pantalla muestra el nombre/valor de demostración tal como están configurados, pero no son mediciones reales de instrumentos.
- El generador del emisor permite distinto orden de bits de etiqueta (`labelBitOrder`) y no incluye esa opción en la trama transmitida. La etiqueta se muestra en octal según la configuración predeterminada del emisor (`reverse`); si el emisor se configura con `normal`, el receptor no puede detectar ese cambio automáticamente.
- Los pines 13/12 representan cada bit lógico de la palabra, en orden de bit 1 a bit 32, a 100 ms por estado. 100 ms es un ritmo visual configurable, no una capacidad máxima del Uno ni una temporización ARINC 429. La salida no es una interfaz diferencial ARINC 429 y no genera niveles de ±10 V.
- El servicio serial actual del emisor abre y cierra el COM en cada envío. En un Uno, la apertura del puerto USB puede activar el auto-reset por DTR y hacer que se pierda una trama durante el reinicio del bootloader. No se pudo comprobar este comportamiento sin placa; si ocurre, el arreglo requiere mantener el puerto abierto desde la aplicación o resolver el auto-reset de forma controlada.

## Conexiones

1. Conecta el Uno al ordenador por USB; la aplicación usa el puerto serie USB a 115200 8N1.
2. LCD1602 paralelo: `VSS` a GND, `VDD` a 5 V, `RW` a GND, `RS` al pin 7, `E` al pin 6, `D4` al pin 5, `D5` al pin 4, `D6` al pin 3 y `D7` al pin 2. Deja `D0`-`D3` sin conectar. Conecta `VO` al cursor de un potenciómetro de contraste (extremos a 5 V y GND). Conecta la luz de fondo según el módulo; si no trae resistencia, añade una adecuada.
3. Para observar las salidas, conecta un LED con su resistencia limitadora apropiada desde cada pin de salida a GND (o usa entradas lógicas de alta impedancia compatibles con 5 V). Comparte GND con el circuito lógico de prueba.
4. Bit `1`: pin 13 HIGH y pin 12 LOW. Bit `0`: pin 13 LOW y pin 12 HIGH. En ambos casos solo uno de los dos pines está HIGH.
5. No conectes estos pines a un bus ARINC 429, a ±10 V ni a una etapa diferencial. Para una interfaz eléctrica ARINC haría falta hardware de línea separado y especificado.

## Selección, compilación y carga

1. Abre `arduino/receptor/receptor.ino` en Arduino IDE.
2. Selecciona **Arduino Uno** en **Herramientas → Placa → Arduino AVR Boards → Arduino Uno** y el puerto COM de la placa.
3. Verifica y carga el programa. No hace falta instalar bibliotecas adicionales; usa `Arduino.h`, `LiquidCrystal` y el núcleo de Arduino.
4. Cierra el Monitor Serie antes de usar la aplicación, porque el puerto COM solo puede estar abierto por un programa a la vez. La aplicación debe estar configurada para modo hardware y 115200 baudios.
5. Para usar diagnóstico serial manual, cambia `HABILITAR_DIAGNOSTICO_SERIAL` a `1`, carga el firmware y abre el Monitor Serie a 115200. Desconecta/cierra la aplicación durante esta prueba: los mensajes de diagnóstico no forman parte del protocolo ACK.

## Pruebas preparadas

Los vectores con trama completa, casos de paridad, datos incompletos, recuperación, LCD y salidas están en [`tests/README.md`](./tests/README.md). No se realizaron pruebas físicas; la placa, el circuito de LEDs, el LCD y la comunicación real deben probarse por separado.
