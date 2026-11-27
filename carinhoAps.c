// ============================================================================
// PROJETO APS - ROBO AUTOMATO DE RESGATE COM RECONHECIMENTO DE CORES
// Placa: Arduino UNO R3
// ============================================================================

// --- PINOS DA PONTE H (L298N) ---
#define PINO_ENA 3   // PWM Velocidade Motor A (Esquerdo)
#define PINO_IN1 4   // Direcao Motor A
#define PINO_IN2 5   // Direcao Motor A
#define PINO_IN3 12  // Direcao Motor B
#define PINO_IN4 10  // Direcao Motor B
#define PINO_ENB 11  // PWM Velocidade Motor B (Direito)

// --- PINOS DOS SENSORES INFRAVERMELHO (SEGUIDOR DE LINHA) ---
#define PINO_IR_ESQ 2   // Sensor Infravermelho 1
#define PINO_IR_DIR A3  // Sensor Infravermelho 2

// --- PINOS DO SENSOR ULTRASSONICO (HC-SR04) ---
#define PINO_TRIG 8
#define PINO_ECHO 9

// --- PINOS DO SENSOR DE COR RGB (TCS230 / TCS3200) ---
#define PINO_S0  6
#define PINO_S1  7
#define PINO_S2  A1
#define PINO_S3  A2
#define PINO_OUT A0

// --- PINOS DOS LEDS OBRIGATORIOS DO MANUAL ---
#define PINO_LED_VERMELHO A4  // Pisca ao achar vitima (Amarelo) e acende na parada
#define PINO_LED_VERDE    A5  // Acende quando autorizado a iniciar (Verde)

// ============================================================================
// CONFIGURACOES DE VELOCIDADE E SENSORES
// ============================================================================
const int VEL_RETA  = 130; // Velocidade em linha reta (0 a 255)
const int VEL_CURVA = 160; // Velocidade de correcao nas curvas
const int DISTANCIA_MINIMA_CM = 12; // Parada de seguranca contra colisao

// Na maioria dos modulos IR, a linha BRANCA reflete a luz e gera nivel LOW (0),
// enquanto o fundo escuro absorve a luz e gera nivel HIGH (1).
// Se o seu sensor funcionar invertido, troque LOW por HIGH abaixo:
const int SOBRE_LINHA_BRANCA = LOW;

// ============================================================================
// CALIBRACAO DE REFERENCIA DO TCS230 (EM MICROSEGUNDOS)
// Valores tipicos para escala de 20% (S0 = HIGH, S1 = LOW).
// O algoritmo normalizado compensa variacoes, mas voce pode ajustar pelo Serial.
// ============================================================================
float pulsoBranco[3] = {18.0, 19.0, 16.0}; // R, G, B lidos sobre o branco puro
float pulsoPreto[3]  = {190.0, 200.0, 175.0}; // R, G, B lidos sobre o preto

// Enumeracao das cores reconhecidas
enum CorDetectada {
  COR_NENHUMA = 0,
  COR_VERDE,
  COR_AMARELA,
  COR_VERMELHA
};

// Estados de operacao do veiculo segundo o manual da APS:
// 0 = Aguardando sinal VERDE (em inercia)
// 1 = Em percurso / busca ativa na linha branca
// 2 = Percurso finalizado (encontrou VERMELHO)
int estadoRobo = 0;

// Controle de confirmacao de cor (evita falsos positivos na borda da linha)
CorDetectada ultimaCorLida = COR_NENHUMA;
int contadorConfirmacaoCor = 0;
const int LEITURAS_PARA_CONFIRMAR = 2;

// Tempo de bloqueio apos resgate (para sair de cima da fita amarela apos 5s)
unsigned long tempoUltimoResgate = 0;
const unsigned long INTERVALO_IGNORAR_AMARELO = 2500; // 2,5s apos retomar marcha

// ============================================================================
// SETUP INICIAL
// ============================================================================
void setup() {
  Serial.begin(9600);

  // Configura pinos da Ponte H
  pinMode(PINO_ENA, OUTPUT);
  pinMode(PINO_IN1, OUTPUT);
  pinMode(PINO_IN2, OUTPUT);
  pinMode(PINO_IN3, OUTPUT);
  pinMode(PINO_IN4, OUTPUT);
  pinMode(PINO_ENB, OUTPUT);

  // Configura pinos dos Sensores Infravermelhos
  pinMode(PINO_IR_ESQ, INPUT);
  pinMode(PINO_IR_DIR, INPUT);

  // Configura pinos do Sensor Ultrassonico
  pinMode(PINO_TRIG, OUTPUT);
  pinMode(PINO_ECHO, INPUT);

  // Configura pinos do Sensor RGB TCS230
  pinMode(PINO_S0, OUTPUT);
  pinMode(PINO_S1, OUTPUT);
  pinMode(PINO_S2, OUTPUT);
  pinMode(PINO_S3, OUTPUT);
  pinMode(PINO_OUT, INPUT);

  // Escala de frequencia do TCS230 em 20% (ideal para o Arduino UNO)
  digitalWrite(PINO_S0, HIGH);
  digitalWrite(PINO_S1, LOW);

  // Configura LEDs
  pinMode(PINO_LED_VERMELHO, OUTPUT);
  pinMode(PINO_LED_VERDE, OUTPUT);

  // Estado inicial: Veiculo em inercia aguardando a cor VERDE
  pararMotores();
  digitalWrite(PINO_LED_VERMELHO, HIGH); // Indica aguardando autorizacao
  digitalWrite(PINO_LED_VERDE, LOW);

  Serial.println("Sistema iniciado. Aguardando cor VERDE para partir...");
}

// ============================================================================
// LOOP PRINCIPAL
// ============================================================================
void loop() {
  // 1. Se o percurso ja terminou (encontrou VERMELHO), permanece parado
  if (estadoRobo == 2) {
    pararMotores();
    return;
  }

  // 2. Realiza a leitura multiescala de cor (Do tom mais claro ao mais escuro)
  CorDetectada corAtual = identificarCorMultitons();

  // Filtro de confirmacao: exige leituras consecutivas iguais para validar
  if (corAtual != COR_NENHUMA && corAtual == ultimaCorLida) {
    contadorConfirmacaoCor++;
  } else {
    ultimaCorLida = corAtual;
    contadorConfirmacaoCor = (corAtual != COR_NENHUMA) ? 1 : 0;
  }

  CorDetectada corConfirmada = (contadorConfirmacaoCor >= LEITURAS_PARA_CONFIRMAR)
                               ? corAtual : COR_NENHUMA;

  // --------------------------------------------------------------------------
  // ESTADO 0: EM INERCIA AGUARDANDO AUTORIZACAO (COR VERDE)
  // --------------------------------------------------------------------------
  if (estadoRobo == 0) {
    pararMotores();
    if (corConfirmada == COR_VERDE) {
      Serial.println(">>> COR VERDE DETECTADA: Autorizado a iniciar percurso!");
      digitalWrite(PINO_LED_VERMELHO, LOW);
      digitalWrite(PINO_LED_VERDE, HIGH);
      estadoRobo = 1;
      delay(300); // Breve pausa antes de partir
    }
    return;
  }

  // --------------------------------------------------------------------------
  // ESTADO 1: EM MOVIMENTO DENTRO DO PERIMETRO DE CONTAMINACAO
  // --------------------------------------------------------------------------
  if (estadoRobo == 1) {
    // Regra E.c: Ao encontrar a cor VERMELHA, permanece totalmente parado
    if (corConfirmada == COR_VERMELHA) {
      Serial.println(">>> COR VERMELHA DETECTADA: Fim do percurso! Parada total.");
      pararMotores();
      digitalWrite(PINO_LED_VERDE, LOW);
      digitalWrite(PINO_LED_VERMELHO, HIGH);
      estadoRobo = 2;
      return;
    }

    // Regra E.b: Ao encontrar a cor AMARELA (vitima localizada),
    // para o veiculo por 5 segundos e pisca o LED vermelho
    if (corConfirmada == COR_AMARELA &&
        (millis() - tempoUltimoResgate > INTERVALO_IGNORAR_AMARELO)) {
      Serial.println(">>> COR AMARELA DETECTADA: Vitima localizada! Resgatando (5s)...");
      pararMotores();

      // Pisca o LED vermelho durante exatos 5 segundos (10 ciclos de 500ms)
      for (int i = 0; i < 10; i++) {
        digitalWrite(PINO_LED_VERMELHO, HIGH);
        delay(250);
        digitalWrite(PINO_LED_VERMELHO, LOW);
        delay(250);
      }

      tempoUltimoResgate = millis();
      contadorConfirmacaoCor = 0;
      Serial.println(">>> Retomando percurso...");
    }

    // Verificacao de seguranca com o sensor Ultrassonico HC-SR04
    long distancia = lerDistanciaUltrassonico();
    if (distancia > 0 && distancia < DISTANCIA_MINIMA_CM) {
      pararMotores();
      return;
    }

    // Seguidor de linha branca (19mm) com suporte a cruzamentos de 90 graus
    seguirLinhaBranca();
  }
}

// ============================================================================
// ALGORITMO DE RECONHECIMENTO DE CORES (TONS CLAROS ATE ESCUROS)
// ============================================================================
float lerPulsoFiltro(bool s2, bool s3) {
  digitalWrite(PINO_S2, s2);
  digitalWrite(PINO_S3, s3);
  delayMicroseconds(200); // Estabilizacao do fotodiodo

  // Media de 3 amostras para eliminar ruido de iluminacao
  unsigned long soma = 0;
  int leiturasValidas = 0;
  for (int i = 0; i < 3; i++) {
    unsigned long p = pulseIn(PINO_OUT, LOW, 25000);
    if (p > 0) {
      soma += p;
      leiturasValidas++;
    }
  }
  if (leiturasValidas == 0) return 999.0;
  return (float)soma / leiturasValidas;
}

CorDetectada identificarCorMultitons() {
  // 1. Leitura dos periodos brutos (menor pulso = maior intensidade de luz)
  float pR = lerPulsoFiltro(LOW, LOW);   // Filtro Vermelho
  float pG = lerPulsoFiltro(HIGH, HIGH); // Filtro Verde
  float pB = lerPulsoFiltro(LOW, HIGH);  // Filtro Azul
  float pC = lerPulsoFiltro(HIGH, LOW);  // Sem Filtro (Luminosidade Geral)

  // Se nao houve retorno ou esta escuro demais (fora de alcance)
  if (pR >= 450.0 && pG >= 450.0 && pB >= 450.0) {
    return COR_NENHUMA;
  }

  // 2. Converte o pulso em Intensidade Linear Relativa (inverso do periodo)
  // Multiplicamos pelos fatores de equilibrio tipicos dos fotodiodos do TCS230
  // para compensar a sensibilidade natural de cada filtro em qualquer brilho.
  float iR = (1000.0 / pR) * 1.00;
  float iG = (1000.0 / pG) * 1.08; // Compensa a menor resposta nativa do filtro verde
  float iB = (1000.0 / pB) * 0.92; // Compensa a maior resposta nativa do filtro azul

  // 3. Encontra o canal dominante (Max), o canal secundario (Min) e o Contraste (Delta)
  float iMax = max(iR, max(iG, iB));
  float iMin = min(iR, min(iG, iB));
  float delta = iMax - iMin;
  float saturacao = (iMax > 0) ? (delta / iMax) : 0.0;

  // 4. FILTRO ACROMATICO: Descarta Linha Branca, Pista Preta e Cinza.
  // No branco, cinza ou preto, R, G e B tem intensidades muito proximas,
  // resultando em uma saturacao baixa (< 0.17).
  // (Se um tom de cor pastel muito claro nao for lido, reduza 0.17 para 0.13)
  if (saturacao < 0.17) {
    return COR_NENHUMA;
  }

  // 5. NORMALIZACAO CROMATICA INTRA-AMOSTRA (0.00 a 1.00)
  // Isola a Matiz (Hue) da cor independentemente de ser um tom muito claro
  // (alta refletancia) ou muito escuro (baixa refletancia):
  float rNorm = (iR - iMin) / delta;
  float gNorm = (iG - iMin) / delta;
  float bNorm = (iB - iMin) / delta;

  // Razoes diretas de seguranca entre os canais
  float razaoRG = iR / iG;
  float razaoRB = iR / iB;
  float razaoGB = iG / iB;

  // --------------------------------------------------------------------------
  // CLASSIFICACAO DE CORES (DO MAIS CLARO AO MAIS ESCURO)
  // --------------------------------------------------------------------------

  // A) RECONHECIMENTO DO AMARELO (Claro, Canario, Ouro, Mostarda, Escuro):
  // Na fisica optica do TCS230, o Amarelo reflete simultaneamente Vermelho (R) e
  // Verde (G) em alta proporcao, enquanto absorve o Azul (bNorm proximo de 0).
  if (bNorm < 0.25 && razaoRB > 1.25 && razaoGB > 1.18) {
    // No amarelo (mesmo escuro ou claro), R e G sao fortes e proximos entre si:
    if (rNorm >= 0.55 && gNorm >= 0.38 && razaoRG > 0.72 && razaoRG < 1.85) {
      return COR_AMARELA;
    }
  }

  // B) RECONHECIMENTO DO VERMELHO (Rosa/Claro, Vivo, Escuro, Vinho/Bordo):
  // O canal Vermelho e o dominante absoluto (rNorm == 1.0) e supera o Verde e o Azul.
  if (rNorm > 0.85 && gNorm < 0.38 && bNorm < 0.45 && razaoRG >= 1.35) {
    return COR_VERMELHA;
  }

  // C) RECONHECIMENTO DO VERDE (Verde-Agua/Claro, Limao, Bandeira, Musgo/Escuro):
  // O canal Verde e o dominante (gNorm == 1.0) ou supera claramente o Vermelho e o Azul.
  if (gNorm > 0.80 && rNorm < 0.55 && bNorm < 0.60 && iG > iR * 1.08) {
    return COR_VERDE;
  }

  return COR_NENHUMA;
}

// ============================================================================
// LOGICA DO SEGUIDOR DE LINHA BRANCA (COM CRUZAMENTOS DE 90 GRAUS)
// ============================================================================
void seguirLinhaBranca() {
  int leituraEsq = digitalRead(PINO_IR_ESQ);
  int leituraDir = digitalRead(PINO_IR_DIR);

  bool esqNaLinha = (leituraEsq == SOBRE_LINHA_BRANCA);
  bool dirNaLinha = (leituraDir == SOBRE_LINHA_BRANCA);

  // Caso 1: Ambos os sensores fora da linha branca (robo centralizado sobre a linha)
  // OU ambos sobre a linha (cruzamento de 90 graus previsto no item B do manual) -> Segue reto
  if ((!esqNaLinha && !dirNaLinha) || (esqNaLinha && dirNaLinha)) {
    moverFrente(VEL_RETA, VEL_RETA);
  }
  // Caso 2: Sensor esquerdo tocou a linha branca -> Corrige virando para a esquerda
  else if (esqNaLinha && !dirNaLinha) {
    virarEsquerda(VEL_CURVA);
  }
  // Caso 3: Sensor direito tocou a linha branca -> Corrige virando para a direita
  else if (!esqNaLinha && dirNaLinha) {
    virarDireita(VEL_CURVA);
  }
}

// ============================================================================
// FUNCOES DE CONTROLE DOS MOTORES (PONTE H L298N)
// ============================================================================
void moverFrente(int velEsq, int velDir) {
  digitalWrite(PINO_IN1, HIGH);
  digitalWrite(PINO_IN2, LOW);
  analogWrite(PINO_ENA, velEsq);

  digitalWrite(PINO_IN3, HIGH);
  digitalWrite(PINO_IN4, LOW);
  analogWrite(PINO_ENB, velDir);
}

void virarEsquerda(int velocidade) {
  // Roda esquerda recua ou para, roda direita avanca
  digitalWrite(PINO_IN1, LOW);
  digitalWrite(PINO_IN2, HIGH);
  analogWrite(PINO_ENA, velocidade / 2);

  digitalWrite(PINO_IN3, HIGH);
  digitalWrite(PINO_IN4, LOW);
  analogWrite(PINO_ENB, velocidade);
}

void virarDireita(int velocidade) {
  // Roda esquerda avanca, roda direita recua ou para
  digitalWrite(PINO_IN1, HIGH);
  digitalWrite(PINO_IN2, LOW);
  analogWrite(PINO_ENA, velocidade);

  digitalWrite(PINO_IN3, LOW);
  digitalWrite(PINO_IN4, HIGH);
  analogWrite(PINO_ENB, velocidade / 2);
}

void pararMotores() {
  digitalWrite(PINO_IN1, LOW);
  digitalWrite(PINO_IN2, LOW);
  analogWrite(PINO_ENA, 0);

  digitalWrite(PINO_IN3, LOW);
  digitalWrite(PINO_IN4, LOW);
  analogWrite(PINO_ENB, 0);
}

// ============================================================================
// LEITURA DO SENSOR ULTRASSONICO HC-SR04
// ============================================================================
long lerDistanciaUltrassonico() {
  digitalWrite(PINO_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PINO_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PINO_TRIG, LOW);

  long duracao = pulseIn(PINO_ECHO, HIGH, 15000); // Timeout de 15ms (~2,5m)
  
  if (duracao == 0) {
    Serial.println("Distancia: Fora de alcance");
    return 999;
  }

  long distanciaCm = duracao * 0.034 / 2;

  Serial.print("Distancia: ");
  Serial.print(distanciaCm);
  Serial.println(" cm");

  return distanciaCm;
}