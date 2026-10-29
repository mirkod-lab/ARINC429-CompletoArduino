#include <Arduino.h>

// Protocolo compatible con services/serial.js de ARINC429-Completo.
static const uint8_t MARCA_INICIO = 0x02;
static const uint8_t MARCA_FIN = 0x03;
static const uint8_t VERSION_PROTOCOLO = 0x01;
static const uint8_t COMANDO_PALABRA = 0x01;
static const uint8_t COMANDO_ACK = 0x81;
static const size_t TAMANO_TRAMA = 11;
static const size_t TAMANO_ACK = 7;

// Pines de salida: reflejan un bit lógico, no una señal eléctrica ARINC 429.
static const uint8_t PIN_BIT_UNO = 13;
static const uint8_t PIN_BIT_CERO = 12;

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
};

static uint8_t trama[TAMANO_TRAMA];
static size_t bytesRecibidos = 0;
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

// Comprueba el encuadre y CRC serial antes de reconstruir la palabra little-endian.
bool validarTrama(const uint8_t *datos, uint32_t &palabra) {
  if (datos[0] != MARCA_INICIO || datos[10] != MARCA_FIN ||
      datos[1] != VERSION_PROTOCOLO || datos[2] != COMANDO_PALABRA) {
    return false;
  }

  const uint16_t crcRecibido =
      datos[8] | (static_cast<uint16_t>(datos[9]) << 8);
  if (calcularCrc16Modbus(&datos[1], 7) != crcRecibido) {
    return false;
  }

  palabra = static_cast<uint32_t>(datos[4]) |
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
  Serial.println(F("Instrumento/valor: sin mapeo real configurado."));
#else
  (void)resultado;
#endif
}

void mostrarError(const __FlashStringHelper *mensaje) {
#if HABILITAR_DIAGNOSTICO_SERIAL
  Serial.println(mensaje);
#else
  (void)mensaje;
#endif
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
}

// Tras una trama malformada, conserva cualquier inicio 0x02 ya recibido.
void recuperarSufijoDeTrama() {
  size_t inicio = 1;
  while (inicio < TAMANO_TRAMA && trama[inicio] != MARCA_INICIO) ++inicio;
  if (inicio == TAMANO_TRAMA) {
    reiniciarTrama();
    return;
  }

  bytesRecibidos = TAMANO_TRAMA - inicio;
  for (size_t i = 0; i < bytesRecibidos; ++i) {
    trama[i] = trama[inicio + i];
  }
}

void procesarTramaCompleta() {
  uint32_t palabra = 0;
  if (!validarTrama(trama, palabra)) {
    mostrarError(F("Trama serial invalida: encuadre, version, comando o CRC."));
    recuperarSufijoDeTrama();
    return;
  }

  if (!encolarPalabra(palabra)) {
    mostrarError(F("Cola de salida llena; trama no confirmada."));
    reiniciarTrama();
    return;
  }

  PalabraDecodificada resultado;
  decodificarPalabra(palabra, resultado);
  ultimaPalabra = resultado;
  mostrarEstado(ultimaPalabra);

  // El ACK confirma la trama serial, no la paridad ARINC ni una salida eléctrica.
  enviarAck(trama[3]);
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

  if (bytesRecibidos < TAMANO_TRAMA) {
    trama[bytesRecibidos++] = byteRecibido;
    ultimoByteMs = ahora;
  }

  if (bytesRecibidos == TAMANO_TRAMA) procesarTramaCompleta();
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
  Serial.begin(115200);
}

void loop() {
  while (Serial.available() > 0) {
    recibirByte(static_cast<uint8_t>(Serial.read()));
  }
  descartarTramaIncompletaExpirada();
  procesarSalidas();
}
