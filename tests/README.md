# Casos de prueba del receptor

Estos vectores siguen exactamente el encuadre y CRC del emisor. Enviar los bytes en hexadecimal mediante un script/herramienta serial a 115200 8N1, o construirlos con el mismo `makeFrame` del emisor. Una trama de prueba con paridad inválida debe llevar un CRC serial válido.

| Caso | Palabra / entrada | Resultado esperado |
|---|---|---|
| Trama y paridad válidas | `02 01 01 00 00 00 00 80 0B B1 03` (`0x80000000`) | Trama aceptada; paridad impar válida; respuesta `02 01 81 00 40 50 03`. |
| Paridad ARINC incorrecta | `02 01 01 01 00 00 00 00 37 D1 03` (`0x00000000`) | CRC y trama válidos, paridad impar incorrecta; se confirma la recepción serial y el LCD muestra `P:ERR`. |
| CRC serial incorrecto | Cambiar un byte del primer vector sin recalcular el CRC | Trama rechazada; sin ACK ni palabra encolada. |
| Datos incompletos | Enviar `02 01 01 00 00` y dejar pasar más de 250 ms antes del siguiente byte | Se descarta la trama parcial; no hay ACK ni palabra encolada. |
| Recuperación | Después de una trama incompleta o CRC erróneo, enviar una trama completa válida | El inicio posterior se detecta; se acepta la trama nueva y se devuelve ACK. |
| Salidas para `0x80000000` | Observar el procesamiento de los 32 bits en orden 1→32 | Bits 1-31: pin 13 LOW / pin 12 HIGH. Bit 32: pin 13 HIGH / pin 12 LOW. En cada bit solo un pin está HIGH; cada estado dura 100 ms. |
| LCD con palabra antigua válida | Enviar el primer vector de esta tabla a un LCD1602 paralelo | Muestra `Etiqueta ARINC` y `0` como dato crudo; luego alterna con etiqueta/SDI/SSM/paridad. |
| LCD con trama extendida | Enviar `makeFrame(0x80000053, 2, { instrument: "Indicador", value: 12.5, unit: "km/h" })` | La pantalla principal muestra `Indicador` y `12.5 km/h`; luego muestra etiqueta/SDI/SSM/paridad. |
| LCD con trama extendida de paridad incorrecta | Construir una trama extendida con palabra `0x00000000` y metadatos de prueba | Muestra nombre y valor/unidad enviados; la página de estado indica `PAR:ERR`. |
| Etiqueta presentada en octal | Enviar `02 01 01 02 53 00 00 80 63 35 03` (`0x80000053`) | El LCD muestra `LBL:123`, que corresponde al byte de etiqueta `0x53` expresado en octal bajo el orden predeterminado del emisor. |
| Datos de demostración | Enviar desde un perfil precargado de la aplicación | El LCD muestra nombre y valor configurados en la app; los perfiles precargados son ficticios, no mediciones de equipos reales. |

El primer vector tiene un único bit en `1` (el bit de paridad 32), por lo que cumple paridad impar. El segundo tiene cero bits en `1`, por lo que no cumple paridad impar. Los CRC serial de ambos vectores sí son válidos.

## Prueba con la aplicación

La aplicación genera palabras con paridad impar y envía la palabra junto con el nombre, valor y unidad del envío. Conecta el Uno, cierra el Monitor Serie, habilita el modo hardware y ejecuta un envío. El LCD presenta esos metadatos proporcionados por la aplicación, no una decodificación autónoma de escala. El estado de envío/ACK solo demuestra recepción serial. Para comprobar los pines, conecta LEDs con resistencias y observa la secuencia completa; cada palabra tarda aproximadamente 3,2 s con el ritmo visual actual.
