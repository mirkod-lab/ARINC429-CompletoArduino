# Casos de prueba del receptor

Estos vectores siguen exactamente el encuadre y CRC del emisor. Enviar los bytes en hexadecimal mediante un script/herramienta serial a 115200 8N1, o construirlos con el mismo `makeFrame` del emisor. Una trama de prueba con paridad inválida debe llevar un CRC serial válido.

| Caso | Palabra / entrada | Resultado esperado |
|---|---|---|
| Trama y paridad válidas | `02 01 01 00 00 00 00 80 0B B1 03` (`0x80000000`) | Trama aceptada; paridad impar válida; respuesta `02 01 81 00 40 50 03`. |
| Paridad ARINC incorrecta | `02 01 01 01 00 00 00 00 37 D1 03` (`0x00000000`) | CRC y trama válidos, paridad impar incorrecta; se confirma la recepción serial y se almacena el último estado para mostrarlo al integrar una pantalla. |
| CRC serial incorrecto | Cambiar un byte del primer vector sin recalcular el CRC | Trama rechazada; sin ACK ni palabra encolada. |
| Datos incompletos | Enviar `02 01 01 00 00` y dejar pasar más de 250 ms antes del siguiente byte | Se descarta la trama parcial; no hay ACK ni palabra encolada. |
| Recuperación | Después de una trama incompleta o CRC erróneo, enviar una trama completa válida | El inicio posterior se detecta; se acepta la trama nueva y se devuelve ACK. |
| Salidas para `0x80000000` | Observar el procesamiento de los 32 bits en orden 1→32 | Bits 1-31: pin 13 LOW / pin 12 HIGH. Bit 32: pin 13 HIGH / pin 12 LOW. Cada estado dura 100 ms. |
| Instrumento conocido / valor con unidades | No hay un perfil auténtico ni metadatos de instrumento en la trama | **Pendiente**: no se debe declarar un instrumento o valor físico hasta recibir la configuración real y el LCD. |

El primer vector tiene un único bit en `1` (el bit de paridad 32), por lo que cumple paridad impar. El segundo tiene cero bits en `1`, por lo que no cumple paridad impar. Los CRC serial de ambos vectores sí son válidos.

## Prueba con la aplicación

La aplicación genera palabras con paridad impar y envía la trama con secuencia dinámica. Conecta el Uno, cierra el Monitor Serie, habilita el modo hardware y envía una palabra desde la aplicación. El estado de envío/ACK solo demuestra recepción serial. Para comprobar los pines, conecta LEDs con resistencias y observa la secuencia completa; cada palabra tarda aproximadamente 3,2 s con el ritmo visual actual.
