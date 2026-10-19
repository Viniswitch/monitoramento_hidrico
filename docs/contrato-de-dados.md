# Contrato de dados e de serviços

Painel de Monitoramento Hídrico · Pousada cliente (nome e localização omitidos neste repositório)

Status: rascunho 1.0 · 19/10/2026 · Responsável: Vinicius (hardware e software)

Este documento é o acordo entre o firmware dos nós, o gateway e o painel. Define o que cada parte envia e recebe, em que formato e unidade, e onde cada dado é guardado. O simulador, os testes e o firmware devem seguir o que está aqui. Qualquer mudança passa primeiro por este documento.

## 1. Arquitetura e decisões gerais

```
 ESP32 reserva ─┐
 ESP32 principal ├─ Wi-Fi / MQTT ─► Mosquitto (serviço do Windows) ─► PROGRAMA (.exe, PC da recepção)
 ESP32 saida   ─┘                                                      ├─ ingestão   (assina os tópicos)
                                                                       ├─ regras     (cálculos e alertas)
                                                                       ├─ banco      (SQLite)
                                                                       ├─ serviços   (fronteira interna, seção 8)
                                                                       └─ painel     (telas e popup)
```

| Decisão | Valor |
|---|---|
| Forma de entrega | Um programa executável para Windows no computador da recepção, com o Mosquitto instalado como serviço. O painel não é uma página web |
| Onde rodam os cálculos | No programa (gateway). Os nós enviam só a grandeza física medida |
| Horário da leitura | Carimbado pelo programa na chegada da mensagem; gravado em UTC, exibido no horário de Recife |
| Dados hipotéticos | Ficam no arquivo de configuração do programa e podem ser corrigidos sem mexer na lógica |
| Expansão futura | A fronteira de serviços (seção 8) permite expor as mesmas operações por uma API de rede, para outro computador ou celular, sem reescrever regras nem banco |

## 2. Dados exibidos e sua origem

| Dado | Unidade | Origem |
|---|---|---|
| Distância sensor→água, de cada sistema | m | Medido (ESP32) |
| Condutividade da mistura (Principal) | μS/cm | Medido (ESP32 do Principal) |
| Temperatura da mistura (Principal) | °C | Medido (ESP32 do Principal) |
| Nível em % e em litros | % e L | Calculado a partir da distância e da configuração |
| Situação (dentro ou fora do limite) | texto | Calculado |
| "Leitura indisponível" (sonda fora da água) | indicador | Calculado: nível do Principal abaixo da altura da sonda |
| Hora da última leitura | data e hora | Carimbo do programa |
| Estado do ponto (enviando dados ou sem comunicação) | estado | Calculado: aviso do broker e tempo sem leitura |
| Alertas e seu status (novo ou visto) | texto e data e hora | Gerado e gravado pelo programa |
| Histórico de até 90 dias | série | Banco |
| Limites e senha de administrador | % e μS/cm | Configuração do usuário (banco) |

Fronteira de responsabilidades: o ESP32 converte o sinal do sensor em unidade física (tensão em μS/cm, eco em metros). O programa converte a unidade física em significado para o negócio (metros em % e litros, μS/cm em dentro ou fora do limite).

## 3. Tópicos MQTT

Prefixo `hidrico`. Os nós são `reserva`, `principal` e `saida` (um por sistema do painel).

| Tópico | Quem publica | QoS | Retida | Finalidade |
|---|---|---|---|---|
| `hidrico/<no>/telemetria` | ESP32, a cada intervalo | 0 | Não | Leituras periódicas |
| `hidrico/<no>/status` | ESP32 (`online`) e broker via LWT (`offline`) | 1 | Sim | Estado de conexão do nó |

O programa assina `hidrico/+/telemetria` e `hidrico/+/status`.

- **Telemetria não retida:** como o programa carimba o horário na chegada, uma leitura retida seria entregue de novo ao reiniciar e pareceria recente.
- **Status retido:** ao iniciar, o programa conhece na hora o estado de cada nó.
- **LWT:** o nó registra no broker, ao conectar, a mensagem `offline` para o tópico de status. Se o nó sumir sem avisar, o broker a publica.
- **Desligamento planejado:** a desconexão limpa descarta o testamento (no `mosquitto_sub` do Windows, até o Ctrl+C o dispara). Por isso, antes de desconectar de propósito (por exemplo, para reiniciar), o nó publica `offline` (retido) por conta própria. A queda inesperada continua coberta pelo testamento.
- **Identificador de cliente MQTT:** único por nó; usar o próprio nome do nó.
- **Keepalive:** a definir (exemplo: 30 s). O broker considera o nó perdido após cerca de 1,5 vez esse valor.

### Detecção de nó fora do ar (duas camadas)

| Camada | Detecta | Limitação |
|---|---|---|
| LWT (broker) | Nó que perdeu a conexão | Pode não disparar se o próprio broker reiniciar |
| Tempo sem telemetria (programa) | Nó conectado que parou de medir ou de enviar | Depende do intervalo e do tempo limite |

Um ponto aparece como "sem comunicação" se o LWT disparou **ou** se passou do tempo limite sem telemetria. Durante a queda, o nó não guarda leituras: o histórico mostra uma lacuna, sem valores inventados.

## 4. Mensagens

### 4.1 Telemetria (JSON)

| Campo | Tipo | Quem envia | Observação |
|---|---|---|---|
| `versao` | inteiro | Todos | Versão do contrato (começa em 1) |
| `distancia_m` | número ou `null` | Os três nós | Distância do sensor até a superfície da água |
| `condutividade_us_cm` | número ou `null` | Só `principal` | Valor medido, sem compensação |
| `temperatura_c` | número ou `null` | Só `principal` | Usada na compensação da condutividade |

Regras de formato:

- Nomes em minúsculas, sem acento, com `_`; a unidade faz parte do nome.
- Ponto decimal, nunca vírgula.
- Casas decimais fixas: distância com 3, condutividade com 0, temperatura com 1.
- Sem campo de horário e sem campo com o nome do nó (o nome já está no tópico).
- Falha de sensor é representada por `null`. Não se usam números mágicos.
- O nó mede várias vezes por intervalo (cerca de 5) e envia a mediana.
- Intervalo de envio: 1 leitura por minuto.
- O nó não trata "sonda fora da água": envia o valor lido, e o programa descarta a leitura ao ver, pelo nível, que a sonda está seca.

Exemplos:

```json
// hidrico/principal/telemetria, normal
{ "versao": 1, "distancia_m": 0.512, "condutividade_us_cm": 780, "temperatura_c": 27.1 }

// hidrico/reserva/telemetria, normal
{ "versao": 1, "distancia_m": 1.204 }

// hidrico/principal/telemetria, sensor de condutividade falhou
{ "versao": 1, "distancia_m": 0.512, "condutividade_us_cm": null, "temperatura_c": 27.1 }
```

### 4.2 Status

Texto simples: `online` ou `offline`.

### 4.3 Tratamento pela ingestão

Nenhuma mensagem inválida pode derrubar o programa: ele descarta o que não entende e registra o motivo em log.

| Situação | Ação |
|---|---|
| Tópico de nó desconhecido | Descarta e registra |
| Texto que não é JSON válido | Descarta e registra |
| `versao` não suportada | Descarta e registra |
| Campo esperado ausente | Descarta e registra |
| Valor `null` | Grava como "sensor com falha"; não calcula nível nem condutividade |
| Valor fora da faixa física plausível | Trata como inválido (grava `null`) e registra |
| Mensagem válida | Carimba o horário, grava e avalia os limites |

## 5. Banco de dados (SQLite)

### 5.1 Onde mora cada tipo de dado

| Tipo | Exemplos | Lugar |
|---|---|---|
| Medições e eventos | Leituras, alertas, estado dos nós | Banco |
| Configuração do usuário (editável na tela) | Limites de alerta, senha de administrador | Banco |
| Parâmetros da instalação | Dimensões, altura da sonda, coeficiente de temperatura, intervalos, faixas plausíveis, retenção | Arquivo de configuração (seção 6) |

### 5.2 Tabelas

**nos**: `id` (texto, chave: `reserva`, `principal`, `saida`), `status` (`online`, `offline` ou `desconhecido`), `status_em` (UTC).

**leituras**: `id` (chave automática), `no` (chave estrangeira para `nos`), `recebida_em` (UTC), `distancia_m`, `condutividade_us_cm`, `temperatura_c` (reais, aceitam nulo).

**alertas**: `id`, `tipo` (`nivel` ou `condutividade`), `sistema`, `valor`, `limite` (o limite vigente na hora do disparo), `iniciado_em`, `encerrado_em` (nulo enquanto a condição persistir), `visto_em` (nulo enquanto não marcado).

| Situação do alerta | `encerrado_em` | `visto_em` |
|---|---|---|
| Ativo e novo | vazio | vazio |
| Ativo e visto | vazio | preenchido |
| Encerrado e não visto | preenchido | vazio |

**limites** (chave e valor): `nivel_minimo_reserva_pct` = 10, `nivel_minimo_principal_pct` = 10, `nivel_minimo_saida_pct` = 10, `condutividade_max_us_cm` = 1000.

**admin** (uma linha): `senha_hash`. Guarda só o hash da senha.

### 5.3 Modelagem

- **Só dado bruto nas leituras.** Percentual, litros e condutividade compensada são calculados na consulta. Assim, corrigir uma dimensão da configuração vale também para o histórico.
- **Datas em UTC, como texto ISO 8601** (`2026-10-19T14:32:05Z`). A conversão para o horário de Recife é só na exibição.
- **Um alerta aberto por sistema e tipo.** Enquanto a condição persistir, novas leituras não criam novos alertas.
- **Retenção de 90 dias.** Uma tarefa diária apaga leituras mais antigas.
- **Índice em `leituras(no, recebida_em)`.** Sem ele, as consultas de histórico percorreriam a tabela inteira.
- **Modo WAL** (`journal_mode=WAL`) para que a ingestão grave enquanto o painel lê, e `PRAGMA foreign_keys=ON` a cada conexão.

## 6. Arquivo de configuração do programa

Valores marcados como **hipotéticos** devem ser substituídos pelos reais quando forem medidos ou confirmados. Cada um deve ter essa marcação em comentário.

| Parâmetro | Valor |
|---|---|
| Intervalo de envio dos nós | 60 s (decidido) |
| Tempo limite sem telemetria | 180 s (decidido, três intervalos) |
| Retenção do histórico | 90 dias (requisito) |
| Capacidade, altura de instalação do sensor e altura máxima da água, por sistema | Hipotéticos |
| Altura da sonda de condutividade | Hipotética |
| Coeficiente de compensação de temperatura e temperatura de referência | A confirmar (referência usual de 25 °C; coeficiente em torno de 2% por °C) |
| Faixa física plausível de cada medição | Conforme o sensor escolhido; hipotética |
| Faixa de valores aceitos para os limites | A definir |

## 7. Telas atendidas

| Tela | Dados |
|---|---|
| T-01, T-02, T-02a a T-02c | Última leitura de cada nó, limites, alertas abertos |
| T-03 | Última leitura do Principal (a distância também define "leitura indisponível") |
| T-04 e P-01 | Alertas por `iniciado_em`; novos têm `visto_em` vazio |
| T-05 | Leituras de um nó entre duas datas, até 90 dias |
| T-06, T-06a, T-06b | Tabelas `limites` e `admin` |
| T-07 | `nos.status` e tempo desde a última leitura |

## 8. Fronteira de serviços (programa interno)

O painel não acessa o banco nem o MQTT diretamente: ele chama as operações abaixo, e só elas. Isso mantém a interface separada das regras e permite, no futuro, expor as mesmas operações por uma API de rede sem reescrever o resto.

| Operação | Tela | Retorno e observações |
|---|---|---|
| `obter_resumo()` | T-01 | Para cada sistema: nível em % e L, situação e hora da última leitura; condutividade, temperatura e situação; alertas ativos |
| `obter_nivel(sistema)` | T-02, T-02a a T-02c | Nível em % e L, limite mínimo e hora da última leitura |
| `obter_condutividade()` | T-03 | Valor compensado e temperatura, ou "indisponível"; limite; situação; hora |
| `listar_alertas()` | T-04 | Alertas por data decrescente, com status e se ainda estão ativos |
| `marcar_alerta_visto(id)` | T-04, P-01 | Preenche `visto_em` |
| `obter_historico(grandeza, sistema, de, ate)` | T-05 | Série até 90 dias; fora do período informa que não há dados |
| `obter_limites()` | T-06 | Limites vigentes |
| `alterar_limites(senha, valores)` | T-06a, T-06b | Erros: senha incorreta; valor inválido |
| `obter_estado_pontos()` | T-07 | Estado de cada nó e hora da última leitura |
| Evento `novo_alerta` | P-01 | O programa avisa a interface quando uma ocorrência nova é criada |

**Regra do popup:** a interface exibe o popup quando existe um alerta novo (`visto_em` vazio) que esta instância do programa ainda não exibiu. O controle de "já exibido" fica na própria instância, não no banco.

**Programa sempre em execução:** o programa deve iniciar junto com o Windows e, ao fechar a janela, minimizar para a bandeja em vez de encerrar, para a coleta de dados nunca parar. Sem programa em execução, não há medição nem popup.

## 9. Pontos em aberto

| Ponto | Situação |
|---|---|
| Dimensões reais das caixas e altura da sonda | Hipóteses até serem medidas |
| Interligação das caixas (vasos comunicantes) | Suposição, a confirmar com o proprietário |
| Sistema operacional e especificações do computador da recepção | A confirmar |
| Coeficiente de compensação de temperatura e faixa válida dos limites | A definir |
| "Marcar como visto" também apaga o ícone do menu? | Hoje não: o ícone segue a condição |
| Nó sem comunicação deve gerar popup? | Fora dos requisitos atuais |
| Oscilação do nível em torno de 10% (alertas repetidos) | Avaliar histerese |
| Texto do RF-04 no Acordo de Projeto (cita Telegram e WhatsApp) | Propor ao professor "popup no computador" |
