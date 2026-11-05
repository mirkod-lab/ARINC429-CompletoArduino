#include <Arduino.h>
#include <LiquidCrystal.h>
#include <string.h>

// Protocolo compatible con services/serial.js de ARINC429-Completo.
static const uint8_t MARCA_INICIO = 0x02;
static const uint8_t MARCA_FIN = 0x03;
static const uint8_t VERSION_PROTOCOLO = 0x01;
static const uint8_t COMANDO_PALABRA = 0x01;
static const uint8_t COMANDO_PALABRA_CON_METADATOS = 0x02;
static const uint8_t COMANDO_ACK = 0x81;
static const size_t TAMANO_TRAMA_LEGACY = 11;
static const size_t TAMANO_TRAMA_METADATOS = 70;
static const size_t TAMANO_TRAMA_MAXIMA = TAMANO_TRAMA_METADATOS;
static const size_t TAMANO_ACK = 7;
static const uint8_t MAX_INSTRUMENTO = 24;
static const uint8_t MAX_UNIDAD = 12;
static const uint8_t MAX_VALOR = 20;

// Pines de salida: reflejan un bit lógico, no una señal eléctrica ARINC 429.
static const uint8_t PIN_BIT_UNO = 13;
static const uint8_t PIN_BIT_CERO = 12;

// LCD HD44780 paralelo 16x2: RS, E, D4, D5, D6, D7.
static const uint8_t LCD_RS = 7;
static const uint8_t LCD_E = 6;
static const uint8_t LCD_D4 = 5;
static const uint8_t LCD_D5 = 4;
static const uint8_t LCD_D6 = 3;
static const uint8_t LCD_D7 = 2;
static LiquidCrystal lcd(LCD_RS, LCD_E, LCD_D4, LCD_D5, LCD_D6, LCD_D7);

// Ritmo visual configurable. No representa la velocidad ARINC 429.
static const uint16_t DURACION_BIT_MS = 100;
static const uint16_t TIEMPO_ENTRE_BYTES_MS = 250;
static const uint8_t CAPACIDAD_COLA = 4;

// Activar solo para probar con el Monitor Serie, sin la aplicación conectada.
#define HABILITAR_DIAGNOSTICO_SERIAL 0

struct PalabraDecodificada {
  uint32_t palabra;
  uint8_t etiquetaCruda;
  uint8_t sdi;
  uint32_t datosCrudos;
  uint8_t ssm;
  bool paridadImpar;
  bool tieneMetadatos;
  char instrumento[MAX_INSTRUMENTO + 1];
  char valor[MAX_VALOR + 1];
  char unidad[MAX_UNIDAD + 1];
};

struct TramaRecibida {
  uint8_t secuencia;
  uint32_t palabra;
  bool tieneMetadatos;
  char instrumento[MAX_INSTRUMENTO + 1];
  char valor[MAX_VALOR + 1];
  char unidad[MAX_UNIDAD + 1];
};

static uint8_t trama[TAMANO_TRAMA_MAXIMA];
static size_t bytesRecibidos = 0;
static size_t tamanoTramaEsperado = 0;
static uint32_t ultimoByteMs = 0;

static uint32_t colaPalabras[CAPACIDAD_COLA];
static uint8_t colaCabeza = 0;
static uint8_t colaCola = 0;
static uint8_t palabrasEnCola = 0;
static bool salidaActiva = false;
static uint32_t palabraEnSalida = 0;
static uint8_t bitEnSalida = 0;
static uint32_t proximoBitMs = 0;
static PalabraDecodificada ultimaPalabra;
static bool hayPalabraValida = false;
static bool paginaDatosLCD = false;
static bool lcdNecesitaActualizar = false;
static uint32_t proximaPaginaLCDMs = 0;
static const uint16_t INTERVALO_PAGINA_DATOS_MS = 6500;
static const uint16_t INTERVALO_PAGINA_ESTADO_MS = 1800;
static uint8_t desplazamientoNombreLCD = 0;
static uint8_t desplazamientoValorLCD = 0;
static uint8_t maxDesplazamientoNombreLCD = 0;
static uint8_t maxDesplazamientoValorLCD = 0;
static uint32_t proximoDesplazamientoLCDMs = 0;
static char textoValorUnidad[MAX_VALOR + MAX_UNIDAD + 2];

uint16_t calcularCrc16Modbus(const uint8_t *datos, size_t longitud) {
  uint16_t crc = 0xffff;
  for (size_t i = 0; i < longitud; ++i) {
    crc ^= datos[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 1) ? (crc >> 1) ^ 0xa001 : crc >> 1;
    }
  }
  return crc;
}

bool tieneParidadImpar(uint32_t palabra) {
  uint8_t cantidadUnos = 0;
  for (uint8_t bit = 0; bit < 32; ++bit) {
    cantidadUnos += (palabra >> bit) & 1UL;
  }
  return (cantidadUnos & 1) == 1;
}

bool copiarCampoTexto(const uint8_t *origen, uint8_t longitud, char *destino,
                      uint8_t maximo, bool obligatorio) {
  if (longitud > maximo || (obligatorio && longitud == 0)) return false;
  for (uint8_t i = 0; i < longitud; ++i) {
    if (origen[i] < 0x20 || origen[i] > 0x7e) return false;
    destino[i] = static_cast<char>(origen[i]);
  }
  destino[longitud] = '\0';
  return true;
}

// Valida el CRC y reconstruye la palabra; acepta el protocolo anterior sin metadatos.
bool validarTrama(const uint8_t *datos, size_t longitud, TramaRecibida &resultado) {
  if (datos[0] != MARCA_INICIO || datos[1] != VERSION_PROTOCOLO) return false;
  resultado.tieneMetadatos = false;
  resultado.secuencia = datos[3];

  if (datos[2] == COMANDO_PALABRA) {
    if (longitud != TAMANO_TRAMA_LEGACY || datos[10] != MARCA_FIN) return false;
    const uint16_t crcRecibido =
        datos[8] | (static_cast<uint16_t>(datos[9]) << 8);
    if (calcularCrc16Modbus(&datos[1], 7) != crcRecibido) return false;
  } else if (datos[2] == COMANDO_PALABRA_CON_METADATOS) {
    if (longitud != TAMANO_TRAMA_METADATOS || datos[69] != MARCA_FIN) return false;
    const uint16_t crcRecibido =
        datos[67] | (static_cast<uint16_t>(datos[68]) << 8);
    if (calcularCrc16Modbus(&datos[1], 66) != crcRecibido) return false;

    if (!copiarCampoTexto(&datos[9], datos[8], resultado.instrumento,
                          MAX_INSTRUMENTO, true) ||
        !copiarCampoTexto(&datos[47], datos[46], resultado.valor,
                          MAX_VALOR, true) ||
        !copiarCampoTexto(&datos[34], datos[33], resultado.unidad,
                          MAX_UNIDAD, false)) {
      return false;
    }
    resultado.tieneMetadatos = true;
  } else {
    return false;
  }

  resultado.palabra = static_cast<uint32_t>(datos[4]) |
                      (static_cast<uint32_t>(datos[5]) << 8) |
                      (static_cast<uint32_t>(datos[6]) << 16) |
                      (static_cast<uint32_t>(datos[7]) << 24);
  return true;
}

void decodificarPalabra(uint32_t palabra, PalabraDecodificada &resultado) {
  resultado.palabra = palabra;
  resultado.etiquetaCruda = palabra & 0xff;
  resultado.sdi = (palabra >> 8) & 0x03;
  resultado.datosCrudos = (palabra >> 10) & 0x7ffffUL;
  resultado.ssm = (palabra >> 29) & 0x03;
  resultado.paridadImpar = tieneParidadImpar(palabra);
}

void mostrarEstado(const PalabraDecodificada &resultado) {
#if HABILITAR_DIAGNOSTICO_SERIAL
  Serial.print(F("Palabra 0x"));
  if (resultado.palabra < 0x10000000UL) Serial.print('0');
  if (resultado.palabra < 0x01000000UL) Serial.print('0');
  if (resultado.palabra < 0x00100000UL) Serial.print('0');
  if (resultado.palabra < 0x00010000UL) Serial.print('0');
  if (resultado.palabra < 0x00001000UL) Serial.print('0');
  if (resultado.palabra < 0x00000100UL) Serial.print('0');
  if (resultado.palabra < 0x00000010UL) Serial.print('0');
  Serial.print(resultado.palabra, HEX);
  Serial.print(F(" | etiqueta cruda 0x"));
  if (resultado.etiquetaCruda < 0x10) Serial.print('0');
  Serial.print(resultado.etiquetaCruda, HEX);
  Serial.print(F(" | SDI "));
  Serial.print(resultado.sdi);
  Serial.print(F(" | datos crudos "));
  Serial.print(resultado.datosCrudos);
  Serial.print(F(" | SSM "));
  Serial.print(resultado.ssm);
  Serial.print(F(" | paridad "));
  Serial.println(resultado.paridadImpar ? F("IMPAR OK") : F("ERROR"));
  if (resultado.tieneMetadatos) {
    Serial.print(resultado.instrumento);
    Serial.print(F(": "));
    Serial.print(resultado.valor);
    Serial.print(' ');
    Serial.println(resultado.unidad);
  }
#else
  (void)resultado;
#endif
}

void imprimirVentanaLCD(uint8_t fila, const char *texto, uint8_t longitud,
                        uint8_t desplazamiento) {
  lcd.setCursor(0, fila);
  for (uint8_t i = 0; i < 16; ++i) {
    const uint8_t indice = desplazamiento + i;
    lcd.print(indice < longitud ? texto[indice] : ' ');
  }
}

void mostrarPalabraEnLCD(const PalabraDecodificada &resultado) {
  if (!paginaDatosLCD) {
    char nombreGenerico[] = "Etiqueta ARINC";
    char valorCrudo[11];
    const char *nombre = resultado.tieneMetadatos ? resultado.instrumento : nombreGenerico;
    const char *valor = resultado.valor;
    const char *unidad = resultado.unidad;

    if (!resultado.tieneMetadatos) {
      ultoa(resultado.datosCrudos, valorCrudo, 10);
      valor = valorCrudo;
      unidad = "";
    }

    strcpy(textoValorUnidad, valor);
    if (unidad[0] != '\0') {
      strcat(textoValorUnidad, " ");
      strcat(textoValorUnidad, unidad);
    }
    const uint8_t nombreLongitud = strlen(nombre);
    const uint8_t valorLongitud = strlen(textoValorUnidad);
    maxDesplazamientoNombreLCD = nombreLongitud > 16 ? nombreLongitud - 16 : 0;
    maxDesplazamientoValorLCD = valorLongitud > 16 ? valorLongitud - 16 : 0;
    imprimirVentanaLCD(0, nombre, nombreLongitud, desplazamientoNombreLCD);
    imprimirVentanaLCD(1, textoValorUnidad, valorLongitud, desplazamientoValorLCD);
  } else {
    lcd.setCursor(0, 0);
    lcd.print(F("LBL:"));
    lcd.print((resultado.etiquetaCruda >> 6) & 0x03);
    lcd.print((resultado.etiquetaCruda >> 3) & 0x07);
    lcd.print(resultado.etiquetaCruda & 0x07);
    lcd.print(F(" SDI:"));
    lcd.print(resultado.sdi);
    lcd.print(F("   "));

    lcd.setCursor(0, 1);
    lcd.print(F("SSM:"));
    lcd.print(resultado.ssm);
    lcd.print(F(" PAR:"));
    lcd.print(resultado.paridadImpar ? F("OK") : F("ERR"));
    uint8_t caracteres = 10 + (resultado.paridadImpar ? 2 : 3);
    while (caracteres < 16) {
      lcd.print(' ');
      ++caracteres;
    }
  }
  lcdNecesitaActualizar = false;
}

void actualizarLCD() {
  if (!hayPalabraValida) return;
  const uint32_t ahora = millis();
  if (static_cast<int32_t>(ahora - proximaPaginaLCDMs) >= 0) {
    paginaDatosLCD = !paginaDatosLCD;
    desplazamientoNombreLCD = 0;
    desplazamientoValorLCD = 0;
    proximaPaginaLCDMs = ahora + (paginaDatosLCD
                                      ? INTERVALO_PAGINA_ESTADO_MS
                                      : INTERVALO_PAGINA_DATOS_MS);
    lcdNecesitaActualizar = true;
  }
  if (static_cast<int32_t>(ahora - proximoDesplazamientoLCDMs) >= 0) {
    if (!paginaDatosLCD) {
      desplazamientoNombreLCD =
          desplazamientoNombreLCD >= maxDesplazamientoNombreLCD
              ? 0
              : desplazamientoNombreLCD + 1;
      desplazamientoValorLCD =
          desplazamientoValorLCD >= maxDesplazamientoValorLCD
              ? 0
              : desplazamientoValorLCD + 1;
      lcdNecesitaActualizar = true;
    }
    proximoDesplazamientoLCDMs = ahora + 350;
  }
  if (lcdNecesitaActualizar) mostrarPalabraEnLCD(ultimaPalabra);
}

void mostrarError(const __FlashStringHelper *mensaje) {
#if HABILITAR_DIAGNOSTICO_SERIAL
  Serial.println(mensaje);
#else
  (void)mensaje;
#endif
  lcd.setCursor(0, 0);
  lcd.print(F("ERROR RECEPCION "));
  lcd.setCursor(0, 1);
  lcd.print(F("Trama no valida "));
}

void enviarAck(uint8_t secuencia) {
  uint8_t ack[TAMANO_ACK] = {
      MARCA_INICIO, VERSION_PROTOCOLO, COMANDO_ACK, secuencia, 0, 0, MARCA_FIN};
  const uint16_t crc = calcularCrc16Modbus(&ack[1], 3);
  ack[4] = crc & 0xff;
  ack[5] = crc >> 8;
  Serial.write(ack, TAMANO_ACK);
}

bool encolarPalabra(uint32_t palabra) {
  if (palabrasEnCola == CAPACIDAD_COLA) return false;
  colaPalabras[colaCola] = palabra;
  colaCola = (colaCola + 1) % CAPACIDAD_COLA;
  ++palabrasEnCola;
  return true;
}

void escribirBitEnPines(uint8_t bit) {
  digitalWrite(PIN_BIT_UNO, bit ? HIGH : LOW);
  digitalWrite(PIN_BIT_CERO, bit ? LOW : HIGH);
}

void procesarSalidas() {
  const uint32_t ahora = millis();

  if (!salidaActiva) {
    if (palabrasEnCola == 0) return;
    palabraEnSalida = colaPalabras[colaCabeza];
    colaCabeza = (colaCabeza + 1) % CAPACIDAD_COLA;
    --palabrasEnCola;
    salidaActiva = true;
    bitEnSalida = 0;
    escribirBitEnPines(palabraEnSalida & 1UL);
    proximoBitMs = ahora + DURACION_BIT_MS;
    return;
  }

  if (static_cast<int32_t>(ahora - proximoBitMs) < 0) return;

  ++bitEnSalida;
  if (bitEnSalida == 32) {
    salidaActiva = false;
    return;
  }

  escribirBitEnPines((palabraEnSalida >> bitEnSalida) & 1UL);
  proximoBitMs += DURACION_BIT_MS;
}

void reiniciarTrama() {
  bytesRecibidos = 0;
  tamanoTramaEsperado = 0;
}

void actualizarTamanoTramaEsperado() {
  if (bytesRecibidos < 3) return;
  if (trama[2] == COMANDO_PALABRA) {
    tamanoTramaEsperado = TAMANO_TRAMA_LEGACY;
  } else if (trama[2] == COMANDO_PALABRA_CON_METADATOS) {
    tamanoTramaEsperado = TAMANO_TRAMA_METADATOS;
  } else {
    mostrarError(F("Comando serial desconocido."));
    reiniciarTrama();
  }
}

void procesarTramaCompleta() {
  TramaRecibida recibida;
  if (!validarTrama(trama, tamanoTramaEsperado, recibida)) {
    mostrarError(F("Trama serial invalida: encuadre, version, comando o CRC."));
    reiniciarTrama();
    return;
  }

  if (!encolarPalabra(recibida.palabra)) {
    mostrarError(F("Cola de salida llena; trama no confirmada."));
    reiniciarTrama();
    return;
  }

  PalabraDecodificada resultado;
  decodificarPalabra(recibida.palabra, resultado);
  resultado.tieneMetadatos = recibida.tieneMetadatos;
  if (recibida.tieneMetadatos) {
    strcpy(resultado.instrumento, recibida.instrumento);
    strcpy(resultado.valor, recibida.valor);
    strcpy(resultado.unidad, recibida.unidad);
  }
  ultimaPalabra = resultado;

  // El ACK confirma la trama serial, no la paridad ARINC ni una salida eléctrica.
  enviarAck(recibida.secuencia);
  hayPalabraValida = true;
  paginaDatosLCD = false;
  proximaPaginaLCDMs = millis() + INTERVALO_PAGINA_DATOS_MS;
  desplazamientoNombreLCD = 0;
  desplazamientoValorLCD = 0;
  maxDesplazamientoNombreLCD = 0;
  maxDesplazamientoValorLCD = 0;
  proximoDesplazamientoLCDMs = millis() + 350;
  lcdNecesitaActualizar = true;
  mostrarPalabraEnLCD(ultimaPalabra);
  mostrarEstado(ultimaPalabra);
  reiniciarTrama();
}

void recibirByte(uint8_t byteRecibido) {
  const uint32_t ahora = millis();
  if (bytesRecibidos > 0 &&
      static_cast<uint32_t>(ahora - ultimoByteMs) > TIEMPO_ENTRE_BYTES_MS) {
    mostrarError(F("Trama incompleta: tiempo entre bytes agotado."));
    reiniciarTrama();
  }

  if (bytesRecibidos == 0) {
    if (byteRecibido != MARCA_INICIO) return;
    trama[bytesRecibidos++] = byteRecibido;
    ultimoByteMs = ahora;
    return;
  }

  if (bytesRecibidos < TAMANO_TRAMA_MAXIMA) {
    trama[bytesRecibidos++] = byteRecibido;
    ultimoByteMs = ahora;
  }

  if (bytesRecibidos == 3) actualizarTamanoTramaEsperado();
  if (tamanoTramaEsperado != 0 && bytesRecibidos == tamanoTramaEsperado) {
    procesarTramaCompleta();
  }
}

void descartarTramaIncompletaExpirada() {
  if (bytesRecibidos == 0) return;
  if (static_cast<uint32_t>(millis() - ultimoByteMs) <= TIEMPO_ENTRE_BYTES_MS) return;
  mostrarError(F("Trama incompleta descartada por tiempo entre bytes agotado."));
  reiniciarTrama();
}

void setup() {
  pinMode(PIN_BIT_UNO, OUTPUT);
  pinMode(PIN_BIT_CERO, OUTPUT);
  digitalWrite(PIN_BIT_UNO, LOW);
  digitalWrite(PIN_BIT_CERO, LOW);
  lcd.begin(16, 2);
  lcd.setCursor(0, 0);
  lcd.print(F("Receptor ARINC"));
  lcd.setCursor(0, 1);
  lcd.print(F("Esperando trama"));
  Serial.begin(115200);
}

void loop() {
  while (Serial.available() > 0) {
    recibirByte(static_cast<uint8_t>(Serial.read()));
  }
  descartarTramaIncompletaExpirada();
  actualizarLCD();
  procesarSalidas();
}
